/* $Id$
 *
 * Low level driver for Eicon DIVA Server ISDN cards.
 * /proc functions
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

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>

#include <linux/isdn_compat.h>

#include "platform.h"
#include "debuglib.h"
#include "dlist.h"
#undef ID_MASK
#undef N_DATA
#include "pc.h"
#include "diva_pci.h"
#include "di_defs.h"
#include "divasync.h"
#include "di.h"
#include "io.h"
#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "diva.h"


EXPORT_NO_SYMBOLS;

extern PISDN_ADAPTER IoAdapters[MAX_ADAPTER];
extern diva_entity_queue_t adapter_queue;
extern void divas_get_version(char *);

/*
** kernel/user space copy functions
*/
static int
xdi_copy_to_user(void* os_handle, void* dst, const void* src, int length)
{
  if (copy_to_user (dst, src, length)) {
    return (-EFAULT);
  }
  return (length);
}

static int
xdi_copy_from_user (void* os_handle, void* dst, const void* src, int length)
{
  if (copy_from_user(dst, src, length)) {
    return (-EFAULT);
  }
  return (length);
}

/*********************************************************
 ** Functions for /proc interface / File operations
 *********************************************************/

static char *divas_proc_name = "divas";
static char *adapter_dir_name = "adapter";
static char *info_proc_name = "info";
static char *grp_opt_proc_name = "group_optimization";
static char *d_l1_down_proc_name = "dynamic_l1_down";

/*
** "divas" entry
*/

extern struct proc_dir_entry *proc_net_isdn_eicon;
static struct proc_dir_entry *divas_proc_entry = NULL;

static ssize_t
divas_read(struct file *file, char *buf, size_t count, loff_t * off)
{
  int len = 0;
  int cadapter;
  char tmpbuf[80];

  if (*off)
      return 0;
  if (off != &file->f_pos)
      return -ESPIPE;

  divas_get_version(tmpbuf);
  if(copy_to_user(buf+len, &tmpbuf, strlen(tmpbuf)))
      return -EFAULT;
  len += strlen(tmpbuf);

  for(cadapter = 0; cadapter < MAX_ADAPTER; cadapter++) {
    if (IoAdapters[cadapter]) {
      sprintf(tmpbuf, "%2d: %-30s Serial:%-10ld IRQ:%2d\n", cadapter + 1,
          IoAdapters[cadapter]->Properties.Name,
          IoAdapters[cadapter]->serialNo,
          IoAdapters[cadapter]->irq_info.irq_nr);
      if((strlen(tmpbuf)+len) > count)
        break;
      if(copy_to_user(buf+len, &tmpbuf, strlen(tmpbuf)))
        return -EFAULT;
      len += strlen(tmpbuf);
    }
  }

  *off += len;
  return(len);
}

static ssize_t
divas_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
  return (-ENODEV);
}                                  

static unsigned int
divas_poll(struct file *file, poll_table * wait)
{
  if (!file->private_data) {
    return (POLLERR);
  }
  return (POLLIN | POLLRDNORM);
}

static int
divas_open(struct inode *inode, struct file *file)
{
#ifdef COMPAT_USE_MODCOUNT_LOCK
  MOD_INC_USE_COUNT;
#endif
  return(0);
}

static int
divas_close(struct inode *inode, struct file *file)
{
  if (file->private_data) {
    diva_xdi_close_adapter (file->private_data, file);
  }
#ifdef COMPAT_USE_MODCOUNT_LOCK
  MOD_DEC_USE_COUNT;
#endif
  return(0);
}

static int
divas_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{
  int ret = -EINVAL;
  diva_xdi_io_cmd xcmd;

  if (copy_from_user((void *)&xcmd, (void *)arg, sizeof(diva_xdi_io_cmd))) {
    return(-ESPIPE);
  }

  if (!file->private_data) {
    file->private_data = diva_xdi_open_adapter(file, xcmd.cmd,
                            xcmd.length, xdi_copy_from_user);
                                               
  }
  if (!file->private_data) {
    return (-ENODEV);
  }

  switch(cmd) {
    case DIVA_XDI_IO_CMD_WRITE_MSG:
      ret = diva_xdi_write(file->private_data, file,
                           xcmd.cmd, xcmd.length, xdi_copy_from_user);
      switch(ret) {
        case -1: /* Message should be removed from rx mailbox first */
          ret = -EBUSY;
          break;
        case -2: /* invalid adapter was specified in this call */
          ret = -ENOMEM;
          break;
        case -3:
          ret = -ENXIO;
          break;
      }
      break;
    case DIVA_XDI_IO_CMD_READ_MSG:
      ret = diva_xdi_read(file->private_data, file,
                          xcmd.cmd, xcmd.length, xdi_copy_to_user);
      switch(ret) {
        case -1: /* RX mailbox is empty */
          ret = -EAGAIN;
          break;
        case -2: /* no memory, mailbox was cleared, last command is failed */
          ret = -ENOMEM;
          break;
        case -3: /* can't copy to user, retry */
          ret = -EFAULT;
          break;
      }
      break;
  }
  DBG_TRC(("ioctl: cmd %d, ret %d", cmd, ret));
  return(ret);
}

static loff_t
divas_lseek(struct file *file, loff_t offset, int orig)
{
  return(-ESPIPE);
}

#ifdef COMPAT_NO_SOFTNET
static struct inode_operations divas_file_inode_ops;
#endif

static struct file_operations divas_fops =
{
  llseek:         divas_lseek,
  read:           divas_read,
  write:          divas_write,
  poll:           divas_poll,
  ioctl:          divas_ioctl,
  open:           divas_open,
  release:        divas_close,
};

int
create_divas_proc(void)
{
  divas_proc_entry = create_proc_entry(divas_proc_name,
                     S_IFREG | S_IRUGO , proc_net_isdn_eicon);
  if (!divas_proc_entry)
    return(0);

#ifdef COMPAT_NO_SOFTNET
  divas_file_inode_ops.default_file_ops = &divas_fops;
  divas_proc_entry->ops = &divas_file_inode_ops;
#else 
  divas_proc_entry->proc_fops = &divas_fops;   
#endif
#ifdef COMPAT_HAS_FILEOP_OWNER
  divas_proc_entry->owner = THIS_MODULE;
#endif

  return(1);
}

void
remove_divas_proc(void)
{
  if (divas_proc_entry) {
    remove_proc_entry(divas_proc_name, proc_net_isdn_eicon);
    divas_proc_entry = NULL;
  }
}

/*
** write group_optimization 
*/
static int
write_grp_opt(struct file *file, const char *buffer, unsigned long count, void *data)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t *)data;
  PISDN_ADAPTER IoAdapter = IoAdapters[a->controller-1];
  
  if ((count == 1) || (count == 2)) {
    switch(buffer[0]) {
        case '0':
          IoAdapter->capi_cfg.cfg_1 &= ~DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON;
          break;
        case '1':
          IoAdapter->capi_cfg.cfg_1 |= DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON;
          break;
        default:
          return(-EINVAL);
    }
    return(count);
  }
  return(-EINVAL);
}

/*
** write dynamic_l1_down
*/
static int
write_d_l1_down(struct file *file, const char *buffer, unsigned long count, void *data)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t *)data;
  PISDN_ADAPTER IoAdapter = IoAdapters[a->controller-1];

  if ((count == 1) || (count == 2)) {
    switch(buffer[0]) {
        case '0':
          IoAdapter->capi_cfg.cfg_1 &= ~DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON;
          break;
        case '1':
          IoAdapter->capi_cfg.cfg_1 |= DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON;
          break;
        default:
          return(-EINVAL);
    }
    return(count);
  }
  return(-EINVAL);
}


/*
** read dynamic_l1_down 
*/
static int
read_d_l1_down(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len = 0;
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t *)data;
  PISDN_ADAPTER IoAdapter = IoAdapters[a->controller-1];

  len += sprintf(page+len, "%s\n",
         (IoAdapter->capi_cfg.cfg_1 & DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON) ?
          "1" : "0");

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

/*
** read group_optimization
*/
static int
read_grp_opt(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len = 0;
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t *)data;
  PISDN_ADAPTER IoAdapter = IoAdapters[a->controller-1];

  len += sprintf(page+len, "%s\n",
         (IoAdapter->capi_cfg.cfg_1 & DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON) ?
          "1" : "0");

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

/*
** info read
*/
static int
info_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int i = 0;
  int len = 0;
  char *p;
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t*)data;
  PISDN_ADAPTER IoAdapter = IoAdapters[a->controller-1];

  len += sprintf(page+len, "Name        : %s\n", IoAdapter->Properties.Name);
  len += sprintf(page+len, "Serial      : %ld\n", IoAdapter->serialNo);
  len += sprintf(page+len, "IRQ         : %d\n", IoAdapter->irq_info.irq_nr);
  len += sprintf(page+len, "CardIndex   : %d\n", a->CardIndex);
  len += sprintf(page+len, "CardOrdinal : %d\n", a->CardOrdinal);
  len += sprintf(page+len, "Controller  : %d\n", a->controller);
  len += sprintf(page+len, "Bus-Type    : %s\n",
      (a->Bus == DIVAS_XDI_ADAPTER_BUS_ISA) ? "ISA":"PCI");
  len += sprintf(page+len, "Port-Name   : %s\n", a->port_name);
  if (a->Bus == DIVAS_XDI_ADAPTER_BUS_PCI) {
    len += sprintf(page+len, "PCI-bus     : %d\n", a->resources.pci.bus);
    len += sprintf(page+len, "PCI-func    : %d\n", a->resources.pci.func);
    for(i = 0; i < 8; i++) {
      if(a->resources.pci.bar[i]) {
        len += sprintf(page+len, "Mem / I/O %d : 0x%lx / mapped : 0x%lx",
          i, a->resources.pci.bar[i], (dword)a->resources.pci.addr[i]);
        if(a->resources.pci.length[i]) {
          len += sprintf(page+len, " / length : %ld",
               a->resources.pci.length[i]);
        }
        len += sprintf(page+len, "\n");
      }
    }
  }
  if ((!a->xdi_adapter.port) &&
      ((!a->xdi_adapter.ram) || (!a->xdi_adapter.reset) || (!a->xdi_adapter.cfg)))
  {
    if(!IoAdapter->irq_info.irq_nr) {
      p = "slave";
    } else {
      p = "out of service";
    }
  } else if (a->xdi_adapter.trapped) {
     p = "trapped";
  } else if (a->xdi_adapter.Initialized) {
     p = "active";
  } else {
     p = "ready";
  }
  len += sprintf(page+len, "State       : %s\n", p);

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

/*
** adapter proc init/de-init
*/

int
create_adapters_proc(void)
{
  diva_os_xdi_adapter_t* a;
  struct proc_dir_entry *de, *pe;
  char tmp[16];

  a = (diva_os_xdi_adapter_t*)diva_q_get_head(&adapter_queue);
  while (a) {
    sprintf(tmp, "%s%d", adapter_dir_name, a->controller);
    if(!(de = create_proc_entry(tmp, S_IFDIR, proc_net_isdn_eicon)))
          return(0);
    a->proc_adapter_dir = (void *)de;

    if(!(pe = create_proc_entry(info_proc_name, S_IFREG | S_IRUGO, de)))
          return(0);
    a->proc_info = (void *)pe;
    pe->read_proc = info_read;
    pe->data = a; 

    if((pe = create_proc_entry(grp_opt_proc_name, S_IFREG | S_IRUGO | S_IWUSR, de))) {
      a->proc_grp_opt = (void *)pe;
      pe->write_proc = write_grp_opt;
      pe->read_proc = read_grp_opt;
      pe->data = a; 
    }
    if((pe = create_proc_entry(d_l1_down_proc_name, S_IFREG | S_IRUGO | S_IWUSR, de))) {
      a->proc_d_l1_down = (void *)pe;
      pe->write_proc = write_d_l1_down;
      pe->read_proc = read_d_l1_down;
      pe->data = a; 
    }
  
    DBG_TRC(("proc entry %s created", tmp));
    a = (diva_os_xdi_adapter_t*)diva_q_get_next(&a->link);
  }
  return(1);
}

void
remove_adapters_proc(void)
{
  diva_os_xdi_adapter_t* a;
  char tmp[16];

  a = (diva_os_xdi_adapter_t*)diva_q_get_head(&adapter_queue);
  while (a) {
    if(a->proc_adapter_dir) {
      if(a->proc_d_l1_down) {
        remove_proc_entry(d_l1_down_proc_name, (struct proc_dir_entry *)a->proc_adapter_dir);
      }
      if(a->proc_grp_opt) {
        remove_proc_entry(grp_opt_proc_name, (struct proc_dir_entry *)a->proc_adapter_dir);
      }
      if(a->proc_info) {
        remove_proc_entry(info_proc_name, (struct proc_dir_entry *)a->proc_adapter_dir);
      }
      sprintf(tmp, "%s%d", adapter_dir_name, a->controller);
      remove_proc_entry(tmp, proc_net_isdn_eicon);
      DBG_TRC(("proc entry %s%d removed", adapter_dir_name, a->controller));
    }
    a = (diva_os_xdi_adapter_t*)diva_q_get_next(&a->link);
  }
}

