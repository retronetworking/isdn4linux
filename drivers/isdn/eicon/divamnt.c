/* $Id$
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * Maint module
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

#include <linux/isdn_compat.h>

#include "platform.h"
#include "di_defs.h"
#include "divasync.h"

#include "main_if.h"
#include "debug_if.h"

EXPORT_NO_SYMBOLS;

static char *main_revision = "$Revision$";

MODULE_DESCRIPTION(             "Maint driver for Eicon DIVA Server cards");
MODULE_AUTHOR(                  "Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE(        "DIVA card driver");

static char *DRIVERNAME = "Eicon DIVA - MAINT module (http://www.melware.net)";
static char *DRIVERLNAME = "diva_mnt";
static char *DRIVERRELEASE = "0.1a";

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern void DIVA_DIDD_Read (void *, int);

#define MAX_DESCRIPTORS  32

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;
static DESCRIPTOR MaintDescriptor = { IDI_DIMAINT, 0, 0, (IDI_CALL) prtComp };

static spinlock_t maint_lock;

static struct sk_buff_head msgq;
static wait_queue_head_t msgwaitq;
static atomic_t opened; 

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
**
*/
void
DI_lock(void)
{
  spin_lock(&maint_lock);
}

void
DI_unlock(void)
{
  spin_unlock(&maint_lock);
}

void add_to_q(int type, char* buf, unsigned int length)
{
  struct sk_buff *skb;
  char *p;

  if(!length)
    return;

  skb = alloc_skb(length + 1, GFP_ATOMIC);
  if(!skb)
    return;

  p = skb_put(skb, length + 1);
  memcpy(p, buf, length);
  p[length] = 10;

  skb_queue_tail(&msgq, skb);
  while (skb_queue_len(&msgq) > 100) {
      skb = skb_dequeue(&msgq);
      dev_kfree_skb(skb);
  }
  wake_up_interruptible(&msgwaitq);
}

/*
** /proc entries
*/

extern struct proc_dir_entry *proc_net_isdn_eicon;
static struct proc_dir_entry *maint_proc_entry = NULL;

static ssize_t
maint_read(struct file *file, char *buf, size_t count, loff_t * off)
{
  int len = 0;
  int cnt = 0;
  struct sk_buff *skb;

  if (off != &file->f_pos)
      return -ESPIPE;

  if (!skb_queue_len(&msgq)) {
    if(file->f_flags & O_NONBLOCK) {
      return (-EAGAIN);
    }
    interruptible_sleep_on(&msgwaitq);
  }

  while((skb = skb_dequeue(&msgq))) {

    if ((skb->len + len) > count)
        cnt = count - len;
    else
        cnt = skb->len;

    copy_to_user(buf, skb->data, cnt);

    len += cnt;
    buf += cnt;

    if (cnt == skb->len) {
        dev_kfree_skb(skb);
    } else {
        skb_pull(skb, cnt);
        skb_queue_head(&msgq, skb);
        *off += len;
        return(len);
    }
  }
  *off += len;
  return(len);
}

static ssize_t
maint_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
  return (-ENODEV);
}                                  

static unsigned int
maint_poll(struct file *file, poll_table * wait)
{
  unsigned int mask = 0;

  poll_wait(file, &msgwaitq, wait);
  mask = POLLOUT | POLLWRNORM;
  if (skb_queue_len(&msgq)) {
    mask |= POLLIN | POLLRDNORM;
  }
  return (mask);
}

static int
maint_open(struct inode *ino, struct file *filep)
{
  if (atomic_read(&opened))
      return(-EBUSY);
  else
    atomic_inc(&opened);

#ifdef COMPAT_USE_MODCOUNT_LOCK
  MOD_INC_USE_COUNT;
#endif
  return(0);
}

static int
maint_close(struct inode *ino, struct file *filep)
{
#ifdef COMPAT_USE_MODCOUNT_LOCK
  MOD_DEC_USE_COUNT;
#endif
  atomic_dec(&opened);
  return(0);
}

static int
maint_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{

  return(0);
}

static loff_t
maint_lseek(struct file *file, loff_t offset, int orig)
{
  return(-ESPIPE);
}

#ifdef COMPAT_NO_SOFTNET
static struct inode_operations maint_file_inode_ops;
#endif

static struct file_operations maint_fops =
{
  llseek:         maint_lseek,
  read:           maint_read,
  write:          maint_write,
  poll:           maint_poll,
  ioctl:          maint_ioctl,
  open:           maint_open,
  release:        maint_close,
};

static int __init
create_maint_proc(void)
{
  maint_proc_entry = create_proc_entry("maint", S_IFREG | S_IRUGO | S_IWUSR, proc_net_isdn_eicon);
  if (!maint_proc_entry)
    return(0);

#ifdef COMPAT_NO_SOFTNET
  maint_file_inode_ops.default_file_ops = &maint_fops;
  maint_proc_entry->ops = &maint_file_inode_ops;
#else 
  maint_proc_entry->proc_fops = &maint_fops;   
#endif
#ifdef COMPAT_HAS_FILEOP_OWNER
  maint_proc_entry->owner = THIS_MODULE;
#endif

  return(1);
}

static void
remove_maint_proc(void)
{
  if (maint_proc_entry) {
    remove_proc_entry("maint", proc_net_isdn_eicon);
    maint_proc_entry = NULL;
  }
}



/*
**  DIDD
*/
static void
*didd_callback(void *context, DESCRIPTOR* adapter, int removal)
{
  if (adapter->type == IDI_DADAPTER)
  {
    printk(KERN_ERR "%s: Change in DAdapter ? Oops ?.\n", DRIVERLNAME);
    return(NULL);
  }

  if (adapter->type == IDI_DIMAINT)
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
      DbgRegister("MAINT", DRIVERRELEASE, DBG_DEFAULT);
    }
  }
  return(NULL);
}

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
      /* Register MAINT (me) */
      req.didd_add_adapter.e.Req = 0;
      req.didd_add_adapter.e.Rc = IDI_SYNC_REQ_DIDD_ADD_ADAPTER;
      req.didd_add_adapter.info.descriptor = (void *)&MaintDescriptor;
      DAdapter.request((ENTITY *)&req);
      if (req.didd_add_adapter.e.Rc != 0xff)
        return(0);
    } 
  }
  return(dadapter);
}

static void __exit
disconnect_didd(void)
{
  IDI_SYNC_REQ req;

  req.didd_notify.e.Req = 0;
  req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
  req.didd_notify.info.handle = notify_handle;
  DAdapter.request((ENTITY *)&req);

  req.didd_remove_adapter.e.Req = 0;
  req.didd_remove_adapter.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER;
  req.didd_remove_adapter.info.p_request = (IDI_CALL)MaintDescriptor.request;
  DAdapter.request((ENTITY *)&req);
}

/*
**  Driver Load
*/
static int __init
maint_init(void)
{
  char tmprev[50];
  int ret = 0;

  MOD_INC_USE_COUNT;

  maint_lock = SPIN_LOCK_UNLOCKED;

  skb_queue_head_init(&msgq);
  init_waitqueue_head(&msgwaitq);

  printk(KERN_INFO "%s\n", DRIVERNAME);
  printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE);
  strcpy(tmprev, main_revision);
  printk("%s  Build: %s\n", getrev(tmprev), DIVA_BUILD);

  DI_init(NULL, 0, 0, 0, 0, 0, 0);

  if(!connect_didd()) {
    printk(KERN_ERR "%s: failed to connect to DIDD.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

  if(!create_maint_proc()) {
    remove_maint_proc();
    printk(KERN_ERR "%s: failed to create proc entry.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

out:
  MOD_DEC_USE_COUNT;
  return(ret);  
}

/*
**  Driver Unload
*/
static void __exit
maint_exit(void)
{

  DI_finit();
  DI_unload();

  disconnect_didd();

  remove_maint_proc();

  DbgDeregister();
  printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(maint_init);
module_exit(maint_exit);

