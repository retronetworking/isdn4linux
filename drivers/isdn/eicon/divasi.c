/* $Id$
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * User Mode IDI Interface 
 *
 * Copyright 2000,2001 by Armin Schindler (mac@melware.de)
 * Copyright 2000,2001 Cytronics & Melware (info@melware.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>

#include <linux/isdn_compat.h>

#include "platform.h"
#include "di_defs.h"
#include "divasync.h"
#include "um_xdi.h"
#include "um_idi.h"

EXPORT_NO_SYMBOLS;

static char *main_revision = "$Revision$";

MODULE_DESCRIPTION(             "User IDI Interface for Eicon ISDN cards");
MODULE_AUTHOR(                  "Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE(        "DIVA card driver");

typedef struct _diva_um_idi_os_context {
	wait_queue_head_t read_wait;
	wait_queue_head_t close_wait;
} diva_um_idi_os_context_t;

static char *DRIVERNAME = "Eicon DIVA - User IDI (http://www.melware.net)";
static char *DRIVERLNAME = "diva_idi";
static char *DRIVERRELEASE = "1.0beta6";

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern void DIVA_DIDD_Read (void *, int);
extern int diva_user_mode_idi_create_adapter (const DESCRIPTOR*, int);
extern void diva_user_mode_idi_remove_adapter (int);

#define MAX_DESCRIPTORS  32

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;

void no_printf (unsigned char * x ,...)
{
  /* dummy debug function */
}
DIVA_DI_PRINTF dprintf = no_printf;

#include "debuglib.c"

/*
**  helper functions
*/
static char *
getrev(const char *revision)
{
  char *rev;
  char *p;
  if ((p = strchr(revision, ':'))) {
     rev = p + 2;
     p = strchr(rev, '$');
     *--p = 0;
  } else rev = "1.0";
  return rev;
}

/*
**  LOCALS
*/
static loff_t um_idi_lseek (struct file *file, loff_t offset, int orig);
static ssize_t um_idi_read (struct file *file, char *buf, size_t count, loff_t *offset);
static ssize_t um_idi_write (struct file *file, const char *buf, size_t count, loff_t *offset);
static unsigned int um_idi_poll (struct file *file, poll_table *wait);
static ssize_t um_idi_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t um_idi_open (struct inode *inode, struct file *file);
static ssize_t um_idi_release (struct inode *inode, struct file *file);
static int remove_entity (void* entity);

typedef struct _udiva_card {
    struct _udiva_card *next;
    int Id;
    DESCRIPTOR  d;
    struct proc_dir_entry *proc_entry;
} udiva_card;

static udiva_card *cards;
static spinlock_t ll_lock;

/*
** malloc
*/
void* diva_os_malloc (unsigned long flags, unsigned long size)
{
  void* ret = NULL;

  if (size) {
      ret = (void*)vmalloc ((unsigned int)size);
  }
  return (ret);
}

void
diva_os_free (unsigned long unused, void* ptr)
{
  if (ptr) {
    vfree (ptr);
  }
}

/*
** card list
*/
static void
add_card_to_list(udiva_card *c)
{
  spin_lock(&ll_lock);
  c->next = cards;
  cards = c;
  spin_unlock(&ll_lock);
}

static udiva_card *
find_card_in_list(DESCRIPTOR *d)
{
  udiva_card *card;

  spin_lock(&ll_lock);
  card = cards;
  while(card) {
    if (card->d.request == d->request) {
      spin_unlock(&ll_lock);
      return(card);
    }
    card = card->next;
  }
  spin_unlock(&ll_lock);
  return((udiva_card *)NULL);
}

static void
remove_card_from_list(udiva_card *c)
{
  udiva_card *list = NULL, *last;

  spin_lock(&ll_lock);
  list = cards;
  last = list;
  while(list) {
    if(list == c) {
      if(cards == c) {
        cards = c->next;
      } else {
        last->next = c->next;
      }
      break;
    }
    last = list;
    list = list->next;
  }
  spin_unlock(&ll_lock);
}

/*
** proc entry
*/
extern struct proc_dir_entry *proc_net_isdn_eicon;
static struct proc_dir_entry *um_idi_proc_entry = NULL;

static int
um_idi_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len = 0;
  char tmprev[32];

  len += sprintf(page+len, "%s\n", DRIVERNAME);
  len += sprintf(page+len, "name     : %s\n", DRIVERLNAME);
  len += sprintf(page+len, "release  : %s\n", DRIVERRELEASE);
  strcpy(tmprev, main_revision);
  len += sprintf(page+len, "revision : %s\n", getrev(tmprev));
  len += sprintf(page+len, "build    : %s\n", DIVA_BUILD);

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

static int __init
create_um_idi_proc(void)
{
  um_idi_proc_entry = create_proc_entry(DRIVERLNAME,
                     S_IFREG | S_IRUGO | S_IWUSR, proc_net_isdn_eicon);
  if (!um_idi_proc_entry)
    return(0);

  um_idi_proc_entry->read_proc = um_idi_proc_read;

#ifdef COMPAT_HAS_FILEOP_OWNER
  um_idi_proc_entry->owner = THIS_MODULE;
#endif

  return(1);
}

static void
remove_um_idi_proc(void)
{
  if (um_idi_proc_entry) {
    remove_proc_entry(DRIVERLNAME, proc_net_isdn_eicon);
    um_idi_proc_entry = NULL;
  }
}

/*
** proc idi entries
*/
struct file_operations um_idi_fops = {
  llseek:      um_idi_lseek,
  read:        um_idi_read,
  write:       um_idi_write,
  poll:        um_idi_poll,
  ioctl:       um_idi_ioctl,
  open:        um_idi_open,
  release:     um_idi_release,
};

#ifdef COMPAT_NO_SOFTNET
static struct inode_operations um_idi_inode_ops;
#endif

static int
create_idi_proc(udiva_card *card)
{
  char pname[32];
  struct proc_dir_entry *pe, *de;

  sprintf(pname, "adapter%d", card->Id);
  for(de = proc_net_isdn_eicon->subdir; de; de = de->next) {
    if((!memcmp(pname, de->name, de->namelen)) &&
        (de->namelen == strlen(pname))) {
      break;
    }
  }
  if(!de)
    return(0);

  pe = create_proc_entry("idi", S_IFREG | S_IRUGO , de);
  if(!pe)
    return(0);

  card->proc_entry = de;
#ifdef COMPAT_NO_SOFTNET
  um_idi_inode_ops.default_file_ops = &um_idi_fops;
  pe->ops = &um_idi_inode_ops;
#else
  pe->proc_fops = &um_idi_fops;
#endif
#ifdef COMPAT_HAS_FILEOP_OWNER
  pe->owner = THIS_MODULE;
#endif
  pe->data = card;
  return(1);
}

static void
remove_idi_proc(udiva_card *card)
{
  struct proc_dir_entry *pe = card->proc_entry;
  
  if(pe) {
    remove_proc_entry("idi", pe);
    card->proc_entry = NULL;
  }
}

/*
** new card
*/
static void
um_new_card(DESCRIPTOR *d)
{
  int adapter_nr = 0;
  udiva_card *card = NULL;
  IDI_SYNC_REQ sync_req;

  if(!(card = kmalloc(sizeof(udiva_card), GFP_ATOMIC))) {
    DBG_ERR(("cannot get buffer for card"));
    return;
  }
  memcpy(&card->d, d, sizeof(DESCRIPTOR));
  sync_req.xdi_logical_adapter_number.Req = 0;
  sync_req.xdi_logical_adapter_number.Rc = IDI_SYNC_REQ_XDI_GET_LOGICAL_ADAPTER_NUMBER;
  card->d.request((ENTITY *)&sync_req);
  adapter_nr = sync_req.xdi_logical_adapter_number.info.logical_adapter_number;
  card->Id = adapter_nr;
  if(!(diva_user_mode_idi_create_adapter(d, adapter_nr))) {
    if((create_idi_proc(card))) {
      add_card_to_list(card);
      DBG_LOG(("idi proc entry added for card %d", adapter_nr));
    } else {
      diva_user_mode_idi_remove_adapter(adapter_nr);
      DBG_ERR(("could not add idi proc entry for card %d", adapter_nr));
      kfree(card);
    }
  } else {
    DBG_ERR(("could not create user mode idi card %d", adapter_nr));
  }
}

/*
** remove card
*/
static void
um_remove_card(DESCRIPTOR *d)
{
  udiva_card *card = NULL;

  if(!(card = find_card_in_list(d))) {
    DBG_ERR(("cannot find card to remove"));
    return;
  }
  diva_user_mode_idi_remove_adapter(card->Id);
  remove_idi_proc(card);
  remove_card_from_list(card);
  DBG_LOG(("idi proc entry removed for card %d", card->Id));
  kfree(card);
}

static void __exit
remove_all_idi_proc(void)
{
  udiva_card *card, *last;

  spin_lock(&ll_lock);
  card = cards;
  cards = NULL;
  spin_unlock(&ll_lock);
  while(card) {
    diva_user_mode_idi_remove_adapter(card->Id);
    remove_idi_proc(card);
    last = card;
    card = card->next;
    kfree(last);
  }
}

/*
** DIDD notify callback
*/
static void *
didd_callback(void *context, DESCRIPTOR* adapter, int removal)
{
  if (adapter->type == IDI_DADAPTER)
  {
    printk(KERN_ERR "%s: Change in DAdapter ? Oops ?.\n", DRIVERLNAME);
    DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
    return(NULL);
  }
  else if (adapter->type == IDI_DIMAINT)
  {
    if (removal)
    {
      memset(&MAdapter, 0, sizeof(MAdapter));
      dprintf = no_printf;
    }
    else
    {
      memcpy(&MAdapter, adapter, sizeof(MAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      DbgRegister("User IDI", DRIVERRELEASE, DBG_DEFAULT);
    }
  }
  else if ((adapter->type > 0) &&
           (adapter->type < 16))
  {  /* IDI Adapter */
    if (removal)
    {
      um_remove_card(adapter);
    }
    else
    {
      um_new_card(adapter);
    }
  }
  return(NULL);
}

/*
** connect DIDD
*/
static int __init
connect_didd(void)
{
  int x = 0;
  int dadapter = 0;
  IDI_SYNC_REQ req;
  DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

  DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

  for (x = 0; x < MAX_DESCRIPTORS; x++)
  {
    if (DIDD_Table[x].type == IDI_DADAPTER)
    {  /* DADAPTER found */
      dadapter = 1;
      memcpy(&DAdapter, &DIDD_Table[x], sizeof(DAdapter));
      req.didd_notify.e.Req = 0;
      req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REGISTER_ADAPTER_NOTIFY;
      req.didd_notify.info.callback = didd_callback;
      req.didd_notify.info.context = 0;
      DAdapter.request((ENTITY *)&req);
      if (req.didd_notify.e.Rc != 0xff)
        return(0);
      notify_handle = req.didd_notify.info.handle;
    }
    else if (DIDD_Table[x].type == IDI_DIMAINT)
    {  /* MAINT found */
      memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      DbgRegister("User IDI", DRIVERRELEASE, DBG_DEFAULT);
    }
    else if ((DIDD_Table[x].type > 0) &&
             (DIDD_Table[x].type < 16))
    {  /* IDI Adapter found */
      um_new_card(&DIDD_Table[x]);
    }
  }
  return(dadapter);
}

/*
**  Disconnect from DIDD
*/
static void __exit
disconnect_didd(void)
{
  IDI_SYNC_REQ req;

  req.didd_notify.e.Req = 0;
  req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
  req.didd_notify.info.handle = notify_handle;
  DAdapter.request((ENTITY *)&req);
}

/*
** Driver Load
*/
static int __init
divasi_init(void)
{
  char tmprev[50];
  int ret = 0;

  MOD_INC_USE_COUNT;

  ll_lock = SPIN_LOCK_UNLOCKED;

  printk(KERN_INFO "%s\n", DRIVERNAME);
  printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE);
  strcpy(tmprev, main_revision);
  printk("%s  Build: %s\n", getrev(tmprev), DIVA_BUILD);

  if (diva_user_mode_idi_init ()) {
    printk(KERN_ERR "%s: init failed.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

   if(!connect_didd()) {
    diva_user_mode_idi_finit ();
    printk(KERN_ERR "%s: failed to connect to DIDD.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

  if(!create_um_idi_proc()) {
    remove_um_idi_proc();
    disconnect_didd();
    diva_user_mode_idi_finit ();
    printk(KERN_ERR "%s: failed to create proc entry.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

out:
  MOD_DEC_USE_COUNT;
  return (ret);
}


/*
** Driver Unload
*/
static void __exit
divasi_exit(void)
{
  diva_user_mode_idi_finit ();
  disconnect_didd();
  remove_all_idi_proc();
  remove_um_idi_proc();

  DbgDeregister();
  printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divasi_init);
module_exit(divasi_exit);


/*
**  FILE OPERATIONS
*/

static loff_t
um_idi_lseek(struct file *file, loff_t offset, int orig)
{
  return (-ESPIPE);
}

static int
divas_um_idi_copy_to_user (void* os_handle, void* dst, const void* src, int length)
{
  memcpy (dst, src, length);
  return (length);
}

static ssize_t
um_idi_read (struct file *file, char *buf, size_t count, loff_t *offset)
{
  return (0);
}


static int
divas_um_idi_copy_from_user (void* os_handle, void* dst, const void* src, int length)
{
  memcpy(dst, src, length);
  return (length);
}

static ssize_t
um_idi_write (struct file *file, const char *buf, size_t count, loff_t *offset)
{
  return (-ENODEV);
}

static unsigned int
um_idi_poll (struct file *file, poll_table *wait)
{
  diva_um_idi_os_context_t* p_os;

  if (!file->private_data) {
    return (POLLERR);
  }

  p_os = (diva_um_idi_os_context_t*)diva_um_id_get_os_context (file->private_data);

  switch (diva_user_mode_idi_ind_ready (file->private_data, file)) {
    case (-1):
      return (POLLERR);

    case 0:
      break;

    default:
      return (POLLIN | POLLRDNORM);
  }

  poll_wait (file, &p_os->read_wait, wait);

  switch (diva_user_mode_idi_ind_ready (file->private_data, file)) {
    case (-1):
      return (POLLERR);

    case 0:
      return (0);
  }

  return (POLLIN | POLLRDNORM);
}

static ssize_t
um_idi_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
  int ret = -EINVAL;
  diva_um_io_cmd xcmd;
  void *data;

  if (copy_from_user((void *)&xcmd, (void *)arg, sizeof(diva_um_io_cmd))) {
    return(-ESPIPE);
  }

  if (!file->private_data) {
    return (-ENODEV);
  }
  if(!(data = diva_os_malloc(0, xcmd.length))) {
    return (-ENOMEM);
  }

  switch(cmd) {
    case DIVA_UM_IDI_IO_CMD_READ:
      ret = diva_um_idi_read (file->private_data,
                              file, data, xcmd.length,
                              divas_um_idi_copy_to_user);
      switch(ret) {
        case   0 : /* no message available */
          ret = (-EAGAIN);
          break;
        case (-1): /* adapter was removed */
          ret  = (-ENODEV);
          break;
        case (-2): /* message_length > length of user buffer */
          ret = (-EFAULT);
          break;
      }
      if(ret > 0) {
        if (copy_to_user(xcmd.data, data, ret)) {
          ret = (-EFAULT);
        }
      }
      break;

    case DIVA_UM_IDI_IO_CMD_WRITE:
      if (copy_from_user(data, xcmd.data, xcmd.length)) {
        diva_os_free (0, data);
        ret = -EFAULT;
        break;
      }
      ret = diva_um_idi_write (file->private_data,
                               file, data, xcmd.length,
                               divas_um_idi_copy_from_user);
      switch (ret) {
        case   0 : /* no space available */
          ret = (-EAGAIN);
          break;
        case (-1): /* adapter was removed */
          ret = (-ENODEV);
          break;
        case (-2): /* length of user buffer > max message_length */
          ret = (-EFAULT);
          break;
      }
      break;
  }
  diva_os_free (0, data);
  DBG_TRC(("ioctl: cmd %d, ret %d", cmd, ret));
  return (ret);
}

static ssize_t
um_idi_open (struct inode *inode, struct file *file)
{
  struct proc_dir_entry * dp = (struct proc_dir_entry *) inode->u.generic_ip;
  udiva_card *card = (udiva_card *) dp->data;
  unsigned int adapter_nr = card->Id;
  diva_um_idi_os_context_t* p_os;
  void* e = divas_um_idi_create_entity ((int)adapter_nr, (void*)file);

  if (!(file->private_data = e)) {
    return (-ENODEV);
  }
  p_os = (diva_um_idi_os_context_t*)diva_um_id_get_os_context(e);
  init_waitqueue_head(&p_os->read_wait);
  init_waitqueue_head(&p_os->close_wait);
#ifdef COMPAT_USE_MODCOUNT_LOCK
  MOD_INC_USE_COUNT;
#endif
  return (0);
}


static ssize_t
um_idi_release (struct inode *inode, struct file *file)
{
  unsigned int adapter_nr = MINOR(inode->i_rdev);
  int ret = 0;

  if (!(file->private_data)) {
    ret = -ENODEV;
    goto out;
  }

  if ((ret = remove_entity (file->private_data))) {
    goto out;
  }

  if (divas_um_idi_delete_entity ((int)adapter_nr, file->private_data)) {
    ret = -ENODEV;
    goto out;
  }

out:
#ifdef COMPAT_USE_MODCOUNT_LOCK
  MOD_DEC_USE_COUNT;
#endif
  return(ret);
}

int
diva_os_get_context_size (void)
{
  return (sizeof(diva_um_idi_os_context_t));
}

void
diva_os_wakeup_read (void* os_context)
{
  diva_um_idi_os_context_t* p_os = (diva_um_idi_os_context_t*)os_context;
  wake_up_interruptible (&p_os->read_wait);
}

void
diva_os_wakeup_close (void* os_context)
{
  diva_um_idi_os_context_t* p_os = (diva_um_idi_os_context_t*)os_context;
  wake_up_interruptible (&p_os->close_wait);
}

/*
**  If application exits without entity removal this function wil remove
**  entity and block until removal is complete
*/
static int
remove_entity (void* entity)
{
  struct task_struct *curtask = current;
  DECLARE_WAITQUEUE(wait, curtask);
  diva_um_idi_os_context_t* p_os;

  if (!divas_um_idi_entity_assigned(entity)) {
    return (0);
  }

  p_os = (diva_um_idi_os_context_t*)diva_um_id_get_os_context(entity);

  add_wait_queue (&p_os->close_wait, &wait);

  while (divas_um_idi_entity_start_remove (entity)) {
    current->state = TASK_INTERRUPTIBLE;
    schedule();
  }

  remove_wait_queue (&p_os->close_wait, &wait);
  add_wait_queue (&p_os->close_wait, &wait);

  while (divas_um_idi_entity_assigned (entity)) {
    current->state = TASK_INTERRUPTIBLE;
    schedule();
  }

  remove_wait_queue (&p_os->close_wait, &wait);
	
  return (0);
}


