/* $Id$
 *
 * Low level driver for Eicon DIVA Server ISDN cards.
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

#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>

#include <linux/isdn_compat.h>

#include "platform.h"
#undef ID_MASK
#undef N_DATA
#include "pc.h"
#include "di_defs.h"
#include "divasync.h"
#include "diva.h"
#include "diva_pci.h"
#include "di.h"
#include "io.h"
#include "xdi_vers.h"


EXPORT_NO_SYMBOLS;

static char *main_revision = "$Revision$";

int errno = 0;

MODULE_DESCRIPTION( "Kernel driver for Eicon DIVA Server cards");
MODULE_AUTHOR(      "Cytronics & Melware, Eicon Networks");

static char *DRIVERNAME = "Eicon DIVA Server driver (http://www.melware.net)";
static char *DRIVERLNAME = "divas";
static char *DRIVERRELEASE = "1.0beta8";

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern void DIVA_DIDD_Read (void *, int);
extern void diva_os_irq_wrapper (int irq, void* context, struct pt_regs* regs);
extern int create_divas_proc(void);
extern void remove_divas_proc(void);
extern int create_adapters_proc(void);
extern void remove_adapters_proc(void);
extern void diva_get_vserial_number(PISDN_ADAPTER IoAdapter, char *buf);

extern PISDN_ADAPTER IoAdapters[MAX_ADAPTER];

#define MAX_DESCRIPTORS  32

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;

typedef struct _diva_os_thread_dpc {
  struct semaphore divas_sem;
  struct semaphore divas_end;
  int pid;
  atomic_t thread_running;
  diva_os_soft_isr_t* psoft_isr;
  int trapped;
} diva_os_thread_dpc_t;


void no_printf (unsigned char * x ,...)
{
  /* dummy debug function */
}
DIVA_DI_PRINTF dprintf = no_printf;


#include "debuglib.c"

/*********************************************************
 ** little helper functions
 *********************************************************/
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

void
diva_os_sleep (dword mSec)
{
  unsigned long timeout = HZ * mSec / 1000 + 1;

  set_current_state(TASK_UNINTERRUPTIBLE);
  schedule_timeout(timeout);
}

void
diva_os_wait (dword mSec)
{
  mdelay (mSec);
} 

void
diva_log_info(unsigned char * format, ...)
{
  va_list args;
  unsigned char line[160];

  va_start(args, format);
  vsprintf(line, format, args);  
  va_end(args);

  printk(KERN_INFO "%s: %s\n", DRIVERLNAME, line);
}

void
divas_get_version(char *p)
{
  char tmprev[32];

  strcpy(tmprev, main_revision);
  sprintf(p, "%s: %s(%s) %s(%s)\n", DRIVERLNAME, DRIVERRELEASE,
                          getrev(tmprev),
                          diva_xdi_common_code_build,
                          DIVA_BUILD);
}

/*********************************************************
 ** malloc / free
 *********************************************************/

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

/*********************************************************
 ** on card failure we try to start a user script
 *********************************************************/

#define TRAP_PROG  "/usr/sbin/divas_trap.rc"
static int
exec_user_script(void *data)
{ 
  PISDN_ADAPTER IoAdapter = (PISDN_ADAPTER) data;
  static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/usr/sbin:/sbin:/bin:/usr/bin", NULL };
  char *argv[] = { TRAP_PROG, "trap", NULL, NULL };
  char anum[6];
  int i;

  sprintf(anum, "%ld", IoAdapter->ANum);
  argv[2] = anum;

  current->session = 1;
  current->pgrp = 1;
  current->policy = SCHED_OTHER;

  spin_lock_irq(&current->sigmask_lock);
  flush_signals(current);
  spin_unlock_irq(&current->sigmask_lock);
 
  for (i = 0; i < current->files->max_fds; i++ ) {
     if (current->files->fd[i]) close(i);
  }
 
  /* We need all rights */
  current->uid = current->euid = current->fsuid = 0;
  cap_set_full(current->cap_effective);
 
  /* Allow execve args to be in kernel space. */
  set_fs(KERNEL_DS);

  if (execve(TRAP_PROG, argv, envp) < 0) {
      printk(KERN_ERR "%s: couldn't start trap script, errno %d\n",
            DRIVERLNAME, -errno);
  }
  return(0);
}

static void
start_trap_script(PISDN_ADAPTER IoAdapter)
{
  int pid = -1;

  pid = kernel_thread(exec_user_script, IoAdapter, 0);
  if (pid < 0) {
    printk(KERN_ERR "%s: couldn't start thread for executing trap script, errno %d\n",
          DRIVERLNAME, -pid);
  }
  waitpid(pid, NULL, __WCLONE);
}

static void
diva_run_trap_script(PISDN_ADAPTER IoAdapter, dword ANum)
{
  diva_os_soft_isr_t* pisr = &IoAdapter->isr_soft_isr;
  diva_os_thread_dpc_t* tisr = (diva_os_thread_dpc_t*)pisr->object;

  if(!tisr->trapped) {
    tisr->trapped = 1;
    diva_os_schedule_soft_isr(pisr);
  }
}

/*********************************************************
 ** PCI Bus services  
 *********************************************************/
void
divasa_find_card_by_type (unsigned short device_id, divasa_find_pci_proc_t signal_card_fn, int handle)
{
#define HW_ID_EICON_PCI                 0x1133
 
  unsigned char bus = 0;
  unsigned char func = 0;
	struct pci_dev	*dev = 0;
 
  while ((dev = pci_find_device(HW_ID_EICON_PCI, device_id, dev))) {
    func = (byte)dev->devfn;
		bus  = (dev->bus) ? (byte)dev->bus->number : 0;
    (*signal_card_fn) (handle, bus, func, dev);
	}
} 

unsigned long
divasa_get_pci_irq (unsigned char bus, unsigned char func, void* pci_dev_handle)
{
  unsigned char irq = 0;
  struct pci_dev	*dev = (struct pci_dev*)pci_dev_handle;

	irq = dev->irq;
 
  return ((unsigned long)irq);
} 

unsigned long
divasa_get_pci_bar (unsigned char bus, unsigned char func, int bar, void* pci_dev_handle)
{
  unsigned long ret = 0;
  struct pci_dev	*dev = (struct pci_dev*)pci_dev_handle;

  if (bar < 6) {
    ret = get_pcibase(dev, bar);
  }

  DBG_TRC(("GOT BAR[%d]=%08x", bar, ret));

  {
    unsigned long type = (ret & 0x00000001);
    if (type & PCI_BASE_ADDRESS_SPACE_IO) {
        DBG_TRC(("  I/O"));
        ret &= PCI_BASE_ADDRESS_IO_MASK;
    } else {
        DBG_TRC(("  memory"));
        ret &= PCI_BASE_ADDRESS_MEM_MASK;
    }
    DBG_TRC(("  final=%08x", ret));  
  }

  return(ret);
}

void
PCIwrite (byte bus, byte func, int offset, void* data, int length, void* pci_dev_handle)
{
  struct pci_dev	*dev = (struct pci_dev*)pci_dev_handle;

  switch (length) {
      case 1: /* byte */
          pci_write_config_byte (dev, offset, *(unsigned char*)data);
          break;
      case 2: /* word */
          pci_write_config_word (dev, offset, *(unsigned short*)data);
          break;
      case 4: /* dword */
          pci_write_config_dword (dev, offset, *(unsigned int*)data);
          break;
 
      default: /* buffer */
          if (!(length % 4) && !(length & 0x03)) { /* Copy as dword */ 
            dword* p = (dword*) data;
            length /= 4;
 
            while (length--) {
              pci_write_config_dword (dev, offset, *(unsigned int*)p++);
            }
          } else { /* copy as byte stream */
            byte* p = (byte*)data;
 
            while (length--) {
              pci_write_config_byte (dev, offset, *(unsigned char*)p++);
            }
          }
  } 
}

void
PCIread (byte bus, byte func, int offset, void* data, int length, void* pci_dev_handle)
{
  struct pci_dev	*dev = (struct pci_dev*)pci_dev_handle;

  switch (length) {
      case 1: /* byte */
          pci_read_config_byte (dev, offset, (unsigned char*)data);
          break;
      case 2: /* word */
          pci_read_config_word (dev, offset, (unsigned short*)data);
          break;
      case 4: /* dword */
          pci_read_config_dword (dev, offset, (unsigned int*)data);
          break;
 
      default: /* buffer */
          if (!(length % 4) && !(length & 0x03)) { /* Copy as dword */
              dword* p = (dword*)data;
              length /= 4;
 
              while (length--) {
                pci_read_config_dword (dev, offset, (unsigned int*)p++);
              }
          } else { /* copy as byte stream */
              byte* p = (byte*) data;
 
              while (length--) {
                pci_read_config_byte (dev, offset, (unsigned char*)p++);
              }
          }
  }
} 


/*********************************************************
 ** I/O port utilities  
 *********************************************************/

int
diva_os_register_io_port (int on, unsigned long port, unsigned long length, const char* name)
{
  int ret;

  if (on) {
    if ((ret = check_region (port, length)) < 0) {
        DBG_ERR(("A: I/O: can't register port=%08x, error=%d", port, ret))
        return (-1);
    }
    request_region (port, length, name);
  } else {
    release_region (port, length);
  }
  return (0);
}

void *
divasa_remap_pci_bar (unsigned long  bar, unsigned long area_length)
{
  void *ret;

  ret = ret =  (void*)ioremap(bar, area_length);
  DBG_TRC(("remap(%08x)->%08x", bar, ret));
  return (ret);
}

void
divasa_unmap_pci_bar (void* bar)
{
  if (bar) {
    iounmap (bar);
  }
}

/*********************************************************
 ** I/O port access 
 *********************************************************/
byte __inline__
inpp (void* addr) {
  return (inb((dword)addr));
}

word __inline__
inppw (void* addr)
{
  return (inw((dword)addr));
}

void __inline__
inppw_buffer (void* addr, void* P, int length)
{
  insw ((dword)addr, (word*)P, length>>1);
}

void __inline__
outppw_buffer (void* addr, void* P, int length)
{
  outsw ((dword)addr, (word*)P, length>>1);
}

void __inline__
outppw (void* addr, word w)
{
  outw (w, (dword)addr);
}

void __inline__
outpp (void* addr, word p)
{
  outb (p, (dword)addr);
}

/*********************************************************
 ** IRQ request / remove  
 *********************************************************/
int
diva_os_register_irq (void* context, byte irq, const char* name)
{
  int result = request_irq (irq, diva_os_irq_wrapper, SA_INTERRUPT | SA_SHIRQ, name, context);
  return (result);
}

void
diva_os_remove_irq (void* context, byte irq)
{
  free_irq (irq, context);
}

/*********************************************************
 ** Kernel Thread / dpc
 *********************************************************/

static int
diva_os_dpc_proc(void* context)
{
  int i;
  diva_os_thread_dpc_t* psoft_isr = (diva_os_thread_dpc_t*)context;
  diva_os_soft_isr_t* pisr = psoft_isr->psoft_isr;

  atomic_inc(&psoft_isr->thread_running);
  if (atomic_read(&psoft_isr->thread_running) > 1) {
      printk(KERN_WARNING"%s: thread already running\n", DRIVERLNAME);
      return(0);
  }

  printk(KERN_INFO "%s: thread started with pid %d\n", DRIVERLNAME, current->pid);

  exit_mm(current);
  for (i = 0; i < current->files->max_fds; i++ ) {
    if (current->files->fd[i]) close(i);
  }

  /* Set to RealTime */
  current->policy = SCHED_FIFO;
  current->rt_priority = 32;
 
  strcpy(current->comm, pisr->dpc_thread_name);
 
  for(;;) {
    down_interruptible(&psoft_isr->divas_sem);
    if(!(atomic_read(&psoft_isr->thread_running)))
      break;
    if(signal_pending(current)) {
         /* we may want to do something on signals here */
         spin_lock_irq(&current->sigmask_lock);
         flush_signals(current);
         spin_unlock_irq(&current->sigmask_lock);
    } else {
         if(psoft_isr->trapped) {
           start_trap_script(pisr->callback_context);
           psoft_isr->trapped = 0;
         } else {
           (*(pisr->callback))(pisr, pisr->callback_context);
         }
    }
  }
  up(&psoft_isr->divas_end);
  psoft_isr->pid = -1;
  return (0);
} 

int
diva_os_initialize_soft_isr (diva_os_soft_isr_t* psoft_isr, diva_os_soft_isr_callback_t callback, void*   callback_context)
{
  diva_os_thread_dpc_t* context;
  int name_len;

  context = diva_os_malloc (0, sizeof (*context));

  name_len = MIN(sizeof(current->comm), sizeof(psoft_isr->dpc_thread_name));
  psoft_isr->dpc_thread_name[name_len - 1] = 0;

  psoft_isr->object = context;
  if (!psoft_isr->object) {
    return (-1);
  }
  memset (context, 0x00, sizeof (*context));
  psoft_isr->callback = callback;
  psoft_isr->callback_context = callback_context;
  context->psoft_isr = psoft_isr;
  init_MUTEX_LOCKED(&context->divas_sem);
  init_MUTEX_LOCKED(&context->divas_end);
  context->pid       = -1;

  context->pid = kernel_thread (diva_os_dpc_proc, context, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
  return ((context->pid == (-1)) ? (-1) : 0);  
}

int
diva_os_schedule_soft_isr (diva_os_soft_isr_t* psoft_isr)
{
  diva_os_thread_dpc_t* context = (diva_os_thread_dpc_t*)psoft_isr->object;

  up(&context->divas_sem);
  return (1);
}

int
diva_os_cancel_soft_isr (diva_os_soft_isr_t* psoft_isr)
{
  return (0);
} 
  
void
diva_os_remove_soft_isr (diva_os_soft_isr_t* psoft_isr)
{
  diva_os_thread_dpc_t* context;

  if (psoft_isr->object) {
    context = (diva_os_thread_dpc_t*)psoft_isr->object;
    if (context->pid != -1) {
        atomic_set(&context->thread_running, 0);
        up(&context->divas_sem);
        down_interruptible(&context->divas_end);
    }
    diva_os_free (0, psoft_isr->object);
    psoft_isr->object = 0;
  }
} 

/*********************************************************
 ** DIDD  
 *********************************************************/
void
diva_xdi_didd_register_adapter (int card)
{
  DESCRIPTOR d;
  IDI_SYNC_REQ req;
  char tmpser[16];

  if (card && ((card-1) < MAX_ADAPTER) && IoAdapters[card-1] && Requests[card-1])
  {
    d.type = IoAdapters[card-1]->Properties.DescType;
    d.request  = Requests[card-1];
    d.channels = IoAdapters[card-1]->Properties.Channels;
    d.features = IoAdapters[card-1]->Properties.Features;
    DBG_TRC(("DIDD register A(%d) channels=%d", card, d.channels))
    /* workaround for different Name in structure */
    strncpy(IoAdapters[card-1]->Name, IoAdapters[card-1]->Properties.Name,
            MIN(30, strlen(IoAdapters[card-1]->Properties.Name)));
    req.didd_remove_adapter.e.Req = 0;
    req.didd_add_adapter.e.Rc = IDI_SYNC_REQ_DIDD_ADD_ADAPTER;
    req.didd_add_adapter.info.descriptor = (void *)&d;
    DAdapter.request((ENTITY *)&req);
    if (req.didd_add_adapter.e.Rc != 0xff) {
      DBG_ERR(("DIDD register A(%d) failed !", card))
    } else {
      IoAdapters[card-1]->os_trap_nfy_Fnc = diva_run_trap_script;
      diva_get_vserial_number(IoAdapters[card-1], tmpser);
      printk(KERN_INFO "%s: %s (%s) started\n", DRIVERLNAME,
                  IoAdapters[card-1]->Properties.Name, tmpser);
    }
  }
}

void
diva_xdi_didd_remove_adapter (int card)
{
  IDI_SYNC_REQ req;
  ADAPTER *a = &IoAdapters[card-1]->a;
  char tmpser[16];

  IoAdapters[card-1]->os_trap_nfy_Fnc = NULL;
  DBG_TRC(("DIDD de-register A(%d)", card))
  req.didd_remove_adapter.e.Req = 0;
  req.didd_remove_adapter.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER;
  req.didd_remove_adapter.info.p_request = (IDI_CALL)Requests[card-1];
  DAdapter.request((ENTITY *)&req);
  diva_get_vserial_number(IoAdapters[card-1], tmpser);
  printk(KERN_INFO "%s: %s (%s) stopped\n", DRIVERLNAME,
               IoAdapters[card-1]->Properties.Name, tmpser);
  memset(&(a->IdTable), 0x00, 256);
}

static void
start_dbg(void)
{
  DbgRegister("DIVAS", DRIVERRELEASE, DBG_DEFAULT);
  DBG_LOG(("DIVA ISDNXDI BUILD (%s[%s]-%s-%s)\n",
    DIVA_BUILD, diva_xdi_common_code_build, __DATE__, __TIME__))
}

static void
didd_callback(void *context, DESCRIPTOR* adapter, int removal)
{
  if (adapter->type == IDI_DADAPTER)
  {
    printk(KERN_ERR "%s: Change in DAdapter ? Oops ?.\n", DRIVERLNAME);
    DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
    return;
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
      start_dbg();
    }
  }
  return;
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
    } 
    else if (DIDD_Table[x].type == IDI_DIMAINT)
    {  /* MAINT found */
      memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      start_dbg();
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
}

/*********************************************************
 ** Driver Load / Startup  
 *********************************************************/
static int __init
divas_init(void)
{
  char tmprev[50];
  int ret = 0;

  MOD_INC_USE_COUNT;

  printk(KERN_INFO "%s\n", DRIVERNAME);
  printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE);
  strcpy(tmprev, main_revision);
  printk("%s  Build: %s(%s)\n", getrev(tmprev),
        diva_xdi_common_code_build, DIVA_BUILD);
  printk(KERN_INFO "%s: support for: ", DRIVERLNAME);
#ifdef CONFIG_ISDN_DIVAS_BRIPCI
  printk("BRI/PCI ");
#endif
#ifdef CONFIG_ISDN_DIVAS_4BRIPCI
  printk("4BRI/PCI ");
#endif
#ifdef CONFIG_ISDN_DIVAS_PRIPCI
  printk("PRI/PCI ");
#endif
  printk("\n");

  if(!pci_present()) {
  /* Maybe some day we support BRI-ISA and this is obsolete */
    printk(KERN_ERR "%s: No PCI present !\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }
  if(!connect_didd()) {
    printk(KERN_ERR "%s: failed to connect to DIDD.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }
  if(!create_divas_proc()) {
    remove_divas_proc();
    printk(KERN_ERR "%s: failed to create proc entry.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }
  if(divasa_xdi_driver_entry()) {
    printk(KERN_ERR "%s: No cards found !\n", DRIVERLNAME);
    remove_divas_proc();
    ret = -EIO;
    goto out;
  }
  if(!create_adapters_proc()) {
    printk(KERN_ERR "%s: failed to create adapters proc entries !\n", DRIVERLNAME);
    remove_adapters_proc();
    divasa_xdi_driver_unload();
    remove_divas_proc();
    ret = -EIO;
    goto out;
  }

out:
  MOD_DEC_USE_COUNT;
  return(ret);  
}

/*********************************************************
 ** Driver Unload
 *********************************************************/
static void __exit
divas_exit(void)
{
  remove_adapters_proc();
  disconnect_didd();
  divasa_xdi_driver_unload();
  remove_divas_proc();

  DbgDeregister();
  printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divas_init);
module_exit(divas_exit);

