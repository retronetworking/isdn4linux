/* $Id$
 *
 * platform.h
 * 
 *
 * Copyright 2000  by Armin Schindler (mac@melware.de)
 * Copyright 2000  Eicon Networks 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#ifndef	__PLATFORM_H__
#define	__PLATFORM_H__

#if !defined(DIVA_BUILD)
#define DIVA_BUILD "local"
#endif

#include <linux/config.h>
#include <linux/version.h>

#include "cardtype.h"

#define DIVA_USER_MODE_CARD_CONFIG 1
#define XDI_USE_XLOG 1
#define	USE_EXTENDED_DEBUGS 1

#define MAX_ADAPTER     32

#define DIVA_ISTREAM 1

#define MEMORY_SPACE_TYPE  0
#define PORT_SPACE_TYPE    1

#include "debuglib.h"

#define dtrc(p) DBG_PRV0(p)
//#define dbug(p) DBG_TRC(p)
//#define dbug(a,p) DBG_PRV1(p)
#define dbug(a,p) p;


#include <linux/string.h>

#ifndef	byte
#define	byte   unsigned char 
#endif

#ifndef	word
#define	word   unsigned short
#endif

#ifndef	dword
#define	dword  unsigned long 
#endif

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif

#ifndef	NULL
#define	NULL	((void *) 0)
#endif

#ifndef	MIN
#define MIN(a,b)	((a)>(b) ? (b) : (a))
#endif

#ifndef	MAX
#define MAX(a,b)	((a)>(b) ? (a) : (b))
#endif

#ifndef	far
#define far
#endif

#ifndef	_pascal
#define _pascal
#endif

#ifndef	_loadds
#define _loadds
#endif

#ifndef	_cdecl
#define _cdecl
#endif

#if !defined(DIM)
#define DIM(array)  (sizeof (array)/sizeof ((array)[0]))
#endif

#define DIVA_INVALID_FILE_HANDLE  ((dword)(-1))

#define DIVAS_CONTAINING_RECORD(address, type, field) \
        ((type *)((char*)(address) - (char*)(&((type *)0)->field)))

extern int sprintf(char *, const char*, ...);

typedef void* LIST_ENTRY;

typedef char    DEVICE_NAME[64];
typedef struct _ISDN_ADAPTER   ISDN_ADAPTER;
typedef struct _ISDN_ADAPTER* PISDN_ADAPTER;

typedef void (* DIVA_DI_PRINTF) (unsigned char *, ...);
extern DIVA_DI_PRINTF dprintf;

typedef struct e_info_s E_INFO ;

typedef char diva_os_dependent_devica_name_t[64];
typedef void* PDEVICE_OBJECT;

struct _diva_os_soft_isr;
struct _diva_os_timer;
struct _ISDN_ADAPTER;

void diva_log_info(unsigned char *, ...);

/*
**  XDI DIDD Interface
*/
void diva_xdi_didd_register_adapter (int card);
void diva_xdi_didd_remove_adapter (int card);

/*
** memory allocation
*/
void* diva_os_malloc (unsigned long flags, unsigned long size);
void    diva_os_free     (unsigned long flags, void* ptr);

/*
** mSeconds waiting
*/
void diva_os_sleep(dword mSec);
void diva_os_wait(dword mSec);

/*
**  PCI Configuration space access
*/
void PCIwrite (byte bus, byte func, int offset, void* data, int length, void* pci_dev_handle);
void PCIread (byte bus, byte func, int offset, void* data, int length, void* pci_dev_handle);

/*
**  I/O Port utilities
*/
int diva_os_register_io_port (int register, unsigned long port, unsigned long length, const char* name);

/*
**  I/O port access abstraction
*/
byte inpp (void*);
word inppw (void*);
void inppw_buffer (void*, void*, int);
void outppw (void*, word);
void outppw_buffer (void* , void*, int);
void outpp (void*, word);

/*
**  IRQ 
*/
typedef struct _diva_os_adapter_irq_info {
        byte irq_nr;
        int  registered;
        char irq_name[24];
} diva_os_adapter_irq_info_t;
int diva_os_register_irq (void* context, byte irq, const char* name);
void diva_os_remove_irq (void* context, byte irq);

/*
**  Spin Lock framework
*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#include <asm/spinlock.h>
#else
#include <linux/spinlock.h>
#endif

typedef long diva_os_spin_lock_magic_t;

#define diva_os_spin_lock_t spinlock_t
static __inline__ int diva_os_initialize_spin_lock(spinlock_t *lock, void * unused)
{
  *lock = (spinlock_t) SPIN_LOCK_UNLOCKED;
  return(0);
}
#define diva_os_destroy_spin_lock(a,b)
#define diva_os_enter_spin_lock(a,b,c) { spin_lock(a); *(b) = 0; }
#define diva_os_leave_spin_lock(a,b,c) { spin_unlock(a); *(b) = 0; }

/*
**  Deffered processing framework
*/
typedef int (*diva_os_isr_callback_t)(struct _ISDN_ADAPTER*);
typedef void (*diva_os_soft_isr_callback_t)(struct _diva_os_soft_isr* psoft_isr, void* context);

typedef struct _diva_os_soft_isr {
  void* object;
  diva_os_soft_isr_callback_t callback;
  void* callback_context;
  char dpc_thread_name[24];
} diva_os_soft_isr_t;

int diva_os_initialize_soft_isr (diva_os_soft_isr_t* psoft_isr, diva_os_soft_isr_callback_t callback, void*   callback_context);
int diva_os_schedule_soft_isr (diva_os_soft_isr_t* psoft_isr);
int diva_os_cancel_soft_isr (diva_os_soft_isr_t* psoft_isr);
void diva_os_remove_soft_isr (diva_os_soft_isr_t* psoft_isr);

/*
**  atomic operation, fake because we use threads
*/
typedef int diva_os_atomic_t;
static diva_os_atomic_t __inline__
diva_os_atomic_increment(diva_os_atomic_t* pv)
{
  *pv += 1;
  return (*pv);
}
static diva_os_atomic_t __inline__
diva_os_atomic_decrement(diva_os_atomic_t* pv)
{
  *pv -= 1;
  return (*pv);
}

/* 
**  CAPI SECTION
*/
#define NO_CORNETN
#define IMPLEMENT_DTMF 1
#define IMPLEMENT_LINE_INTERCONNECT 1
#define IMPLEMENT_ECHO_CANCELLER 1
#define IMPLEMENT_RTP 1
#define IMPLEMENT_T38 1
#define IMPLEMENT_FAX_SUB_SEP_PWD 1
#define IMPLEMENT_V18 1
#define IMPLEMENT_DTMF_TONE 1
#define IMPLEMENT_PIAFS 1
#define IMPLEMENT_FAX_PAPER_FORMATS 1

#if !defined(__i386__)
#define READ_WORD(w) ( ((byte *)(w))[0] + \
                      (((byte *)(w))[1]<<8) )

#define READ_DWORD(w) ( ((byte *)(w))[0] + \
                       (((byte *)(w))[1]<<8) + \
                       (((byte *)(w))[2]<<16) + \
                       (((byte *)(w))[3]<<24) )

#define WRITE_WORD(b,w) { (b)[0]=(byte)(w); \
                          (b)[1]=(byte)((w)>>8); }

#define WRITE_DWORD(b,w) { (b)[0]=(byte)(w); \
                           (b)[1]=(byte)((w)>>8); \
                           (b)[2]=(byte)((w)>>16); \
                           (b)[3]=(byte)((w)>>24); } 
#else
#define READ_WORD(w) (*(word *)(w))
#define READ_DWORD(w) (*(dword *)(w))
#define WRITE_WORD(b,w) { *(word *)(b)=(w); }
#define WRITE_DWORD(b,w) { *(dword *)(b)=(w); }
#endif

#undef ID_MASK
#undef N_DATA

#endif	/* __PLATFORM_H__ */
