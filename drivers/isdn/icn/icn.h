/* $Id$
 * ISDN lowlevel-module for the ICN active ISDN-Card.
 *
 * Copyright 1994 by Fritz Elfert (fritz@wuemaus.franken.de)
 *
 * $Log$
 */

#ifndef icn_h

#define ICN_IOCTL_SETMMIO 0
#define ICN_IOCTL_GETMMIO 1
#define ICN_IOCTL_SETPORT 2
#define ICN_IOCTL_GETPORT 3
#define ICN_IOCTL_MAP0    4
#define ICN_IOCTL_MAP1    5

#ifdef __KERNEL__
/* Kernel includes */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/string.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include "isdnif.h"

/* some useful macros for debugging */
#ifdef ICN_DEBUG_PORT
#define OUTB_P(v,p) {printk("icn: outb_p(0x%02x,0x%03x)\n",v,p); outb_p(v,p);}
#else
#define OUTB_P outb_p
#endif

#define ICN_BASEADDR 0x320
#define ICN_PORTLEN (0x08)
#define ICN_MEMADDR 0x0d0000

#define ICN_CFG    (dev->port)
#define ICN_MAPRAM (dev->port+1)
#define ICN_RUN    (dev->port+2)
#define ICN_BANK   (dev->port+3)

#define ISDN_SERVICE_VOICE 1
#define ISDN_SERVICE_AB    1<<1 
#define ISDN_SERVICE_X21   1<<2
#define ISDN_SERVICE_G4    1<<3
#define ISDN_SERVICE_BTX   1<<4
#define ISDN_SERVICE_DFUE  1<<5
#define ISDN_SERVICE_X25   1<<6
#define ISDN_SERVICE_TTX   1<<7
#define ISDN_SERVICE_MIXED 1<<8
#define ISDN_SERVICE_FW    1<<9
#define ISDN_SERVICE_GTEL  1<<10
#define ISDN_SERVICE_BTXN  1<<11
#define ISDN_SERVICE_BTEL  1<<12

#define ICN_FLAGS_CTLOPEN 1
#define ICN_FLAGS_B1ACTIVE 2
#define ICN_FLAGS_B2ACTIVE 4
#define ICN_FLAGS_RBTIMER 8
#define ICN_FLAGS_MODEMONLINE 16

#define ICN_BOOT_TIMEOUT1 100 /* jiffies */
#define ICN_BOOT_TIMEOUT2 1000 /* jiffies */
#define ICN_CHANLOCK_DELAY 10
#define ICN_SEND_TIMEOUT   10

#define ICN_TIMER_BCREAD 20
#define ICN_TIMER_DCREAD 50

#define ICN_CODE_STAGE1 4096
#define ICN_CODE_STAGE2 65536

#define ICN_FRAGSIZE (250)
#define ICN_BCH 2

typedef struct icn_devt *icn_devptr;
typedef struct icn_devt {
  unsigned short   port;                /* Base-port-adress                 */
  union icn_shmt *shmem;               /* Pointer to memory-mapped-buffers */
  unsigned short   bootstate;           /* Boot-State of driver             */
				        /*  0 = Uninitialized               */
                                        /*  1 = prepare to store bootcode   */
                                        /*  2 = loading bootcode            */
                                        /*  3 = wait for bootcode ready     */
                                        /*  4 = start loading protocolcode  */
                                        /*  5 = loading protocolcode        */
                                        /*  6 = wait for protocolcode ready */
                                        /*  7 = protocol running            */
  unsigned short   flags;               /* Statusflags                      */
  unsigned short   timer1;              /* Timeout-counter                  */
  struct timer_list st_timer;           /* Timer for Status-Polls           */
  struct timer_list rb_timer;           /* Timer for B-Channel-Polls        */
  struct wait_queue *st_waitq;          /* Wait-Queue for status-read's     */
  int              myid;
  int              codelen;
  u_char           *codeptr;
  int              channel;             /* Currently mapped Channel         */
  int              chanlock;            /* Semaphore for Channel-Mapping    */
  u_char           rcvbuf[ICN_BCH][4096]; /* B-Channel-Receive-Buffers      */
  int              rcvidx[ICN_BCH];     /* Index for above buffers          */
  isdn_if          interface;           /* Interface to upper layer         */
  int              iptr;                /* Index to imsg-buffer             */
  char             imsg[40];            /* Internal buf for status-parsing  */
  char             msg_buf[1024];       /* Buffer for status-messages       */
  char             *msg_buf_write;
  char             *msg_buf_read;
  char             *msg_buf_end;
} icn_dev;

/* type-definitions for accessing the mmap-io-areas */

#define SHM_DCTL_OFFSET (0)      /* Offset to data-controlstructures in shm */
#define SHM_CCTL_OFFSET (0x1d2)  /* Offset to comm-controlstructures in shm */
#define SHM_CBUF_OFFSET (0x200)  /* Offset to comm-buffers in shm           */
#define SHM_DBUF_OFFSET (0x2000) /* Offset to data-buffers in shm           */

typedef struct icn_frag {
  unsigned char length;              /* Bytecount of fragment (max 250)     */
  unsigned char endflag;             /* 0=last frag., 0xff=frag. continued  */
  unsigned char data[ICN_FRAGSIZE]; /* The data                            */
  /* Fill to 256 bytes */
  char          unused[0x100-ICN_FRAGSIZE-2];
} frag_buf;

typedef union icn_shmt {

  struct {
    unsigned char  scns;             /* Index to free SendFrag.             */
    unsigned char  scnr;             /* Index to active SendFrag   READONLY */
    unsigned char  ecns;             /* Index to free RcvFrag.     READONLY */
    unsigned char  ecnr;             /* Index to valid RcvFrag              */
    char           unused[6];
    unsigned short fuell1;           /* Internal Buf Bytecount              */ 
  } data_control;

  struct {
    char          unused[SHM_CCTL_OFFSET];
    unsigned char iopc_i;            /* Read-Ptr Status-Queue      READONLY */
    unsigned char iopc_o;            /* Write-Ptr Status-Queue              */
    unsigned char pcio_i;            /* Write-Ptr Command-Queue             */
    unsigned char pcio_o;            /* Read-Ptr Command Queue     READONLY */
  } comm_control;

  struct {
    char          unused[SHM_CBUF_OFFSET];
    unsigned char pcio_buf[0x100];   /* Ring-Buffer Command-Queue           */
    unsigned char iopc_buf[0x100];   /* Ring-Buffer Status-Queue            */
  } comm_buffers;

  struct {
    char          unused[SHM_DBUF_OFFSET];
    frag_buf receive_buf[0x10];
    frag_buf send_buf[0x10];
  } data_buffers;

} icn_shmem;

static icn_dev *dev = (icn_dev *)0;

/* Utility-Macros */

/* Return true, if there is a free transmit-buffer */
#define sbfree (((dev->shmem->data_control.scns+1) & 0xf) != \
                dev->shmem->data_control.scnr)

/* Switch to next transmit-buffer */
#define sbnext (dev->shmem->data_control.scns = \
               ((dev->shmem->data_control.scns+1) & 0xf))

/* Shortcuts for transmit-buffer-access */
#define sbuf_n dev->shmem->data_control.scns
#define sbuf_d dev->shmem->data_buffers.send_buf[sbuf_n].data
#define sbuf_l dev->shmem->data_buffers.send_buf[sbuf_n].length
#define sbuf_f dev->shmem->data_buffers.send_buf[sbuf_n].endflag

/* Return true, if there is receive-data is available */
#define rbavl  (dev->shmem->data_control.ecnr != \
                dev->shmem->data_control.ecns)

/* Switch to next receive-buffer */
#define rbnext (dev->shmem->data_control.ecnr = \
               ((dev->shmem->data_control.ecnr+1) & 0xf))

/* Shortcuts for receive-buffer-access */
#define rbuf_n dev->shmem->data_control.ecnr
#define rbuf_d dev->shmem->data_buffers.receive_buf[rbuf_n].data
#define rbuf_l dev->shmem->data_buffers.receive_buf[rbuf_n].length
#define rbuf_f dev->shmem->data_buffers.receive_buf[rbuf_n].endflag

/* Shortcuts for command-buffer-access */
#define cmd_o (dev->shmem->comm_control.pcio_o)
#define cmd_i (dev->shmem->comm_control.pcio_i)

/* Return free space in command-buffer */
#define cmd_free ((cmd_i>=cmd_o)?0x100-cmd_i+cmd_o:cmd_o-cmd_i)

/* Shortcuts for message-buffer-access */
#define msg_o (dev->shmem->comm_control.iopc_o)
#define msg_i (dev->shmem->comm_control.iopc_i)

/* Return length of Message, if avail. */
#define msg_avail ((msg_o>msg_i)?0x100-msg_o+msg_i:msg_i-msg_o)

#endif /* __KERNEL__ */
#endif /* icn_h */



















