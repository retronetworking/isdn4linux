/* $Id$
 *
 * ISDN low-level module for DIEHL active ISDN-Cards.
 *
 * Copyright 1994,95 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1998    by Armin Schindler (mac@gismo.telekom.de) 
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
 * $Log$
 * Revision 1.2  1998/06/13 10:55:16  armin
 * Added first PCI parts. STILL UNUSABLE
 *
 * Revision 1.1  1998/06/04 10:23:32  fritz
 * First check in. YET UNUSABLE!
 *
 */


#ifndef diehl_h
#define diehl_h

#define DIEHL_IOCTL_SETMMIO   0
#define DIEHL_IOCTL_GETMMIO   1
#define DIEHL_IOCTL_SETIRQ    2
#define DIEHL_IOCTL_GETIRQ    3
#define DIEHL_IOCTL_LOADBOOT  4
#define DIEHL_IOCTL_ADDCARD   5
#define DIEHL_IOCTL_GETTYPE   6
#define DIEHL_IOCTL_LOADPRI   7 

#define DIEHL_IOCTL_TEST     98
#define DIEHL_IOCTL_DEBUGVAR 99

/* Bus types */
#define DIEHL_BUS_ISA          1
#define DIEHL_BUS_MCA          2
#define DIEHL_BUS_PCI          3

/* Constants for describing Card-Type */
#define DIEHL_CTYPE_S            0
#define DIEHL_CTYPE_SX           1
#define DIEHL_CTYPE_SCOM         2
#define DIEHL_CTYPE_QUADRO       3
#define DIEHL_CTYPE_PRI          4
#define DIEHL_CTYPE_MAESTRA      5
#define DIEHL_CTYPE_MAESTRAQ     6
#define DIEHL_CTYPE_MAESTRAQ_U   7
#define DIEHL_CTYPE_MAESTRAP     8
#define DIEHL_CTYPE_MASK         0x0f
#define DIEHL_CTYPE_QUADRO_NR(n) (n<<4)


/* Struct for adding new cards */
typedef struct diehl_cdef {
	int type;      /* Card-Type (DIEHL_CTYPE_...)                 */
        int membase;   /* membase & irq only needed for old ISA cards */
        int irq;
        char id[10];
} diehl_cdef;

#define DIEHL_ISA_BOOT_MEMCHK 1
#define DIEHL_ISA_BOOT_NORMAL 2

/* Struct for downloading protocol via ioctl for ISA cards */
typedef struct {
	/* start-up parameters */
	unsigned char tei;
	unsigned char nt2;
	unsigned char skip1;
	unsigned char WatchDog;
	unsigned char Permanent;
	unsigned char XInterface;
	unsigned char StableL2;
	unsigned char NoOrderCheck;
	unsigned char HandsetType;
	unsigned char skip2;
	unsigned char LowChannel;
	unsigned char ProtVersion;
	unsigned char Crc4;
	unsigned char LoopBack;
	unsigned char oad[32];
	unsigned char osa[32];
	unsigned char spid[32];
	unsigned char boot_opt;
	unsigned long bootstrap_len;
	unsigned long firmware_len;
	unsigned char code[1]; /* Rest (bootstrap- and firmware code) will be allocated */
} diehl_isa_codebuf;

/* Struct for downloading protocol via ioctl for PCI cards */
typedef struct {
        /* start-up parameters */
        unsigned char tei;
        unsigned char nt2;
        unsigned char WatchDog;
        unsigned char Permanent;
        unsigned char XInterface;
        unsigned char StableL2;
        unsigned char NoOrderCheck;
        unsigned char HandsetType;
        unsigned char LowChannel;
        unsigned char ProtVersion;
        unsigned char Crc4;
        unsigned char NoHscx30Mode;  /* switch PRI into No HSCX30 test mode */
        unsigned char Loopback;      /* switch card into Loopback mode */
        struct q931_link_s
        {
          unsigned char oad[32];
          unsigned char osa[32];
          unsigned char spid[32];
        } l[2];
        unsigned long protocol_len;
	unsigned int  dsp_code_num;
        unsigned long dsp_code_len[9];
        unsigned char code[1]; /* Rest (protocol- and dsp code) will be allocated */
} diehl_pci_codebuf;

/* Data for downloading protocol via ioctl */
typedef union {
	diehl_isa_codebuf isa;
	diehl_pci_codebuf pci;
	/* diehl_mca_codebuf mca etc. ... */
} diehl_codebuf;

#ifdef __KERNEL__

/* Kernel includes */
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/ctype.h>

#include <linux/isdnif.h>
#include "diehl_isa.h"
#include "diehl_pci.h"

#define DIEHL_FLAGS_RUNNING  1 /* Cards driver activated */
#define DIEHL_FLAGS_PVALID   2 /* Cards port is valid    */
#define DIEHL_FLAGS_IVALID   4 /* Cards irq is valid     */
#define DIEHL_FLAGS_MVALID   8 /* Cards membase is valid */
#define DIEHL_FLAGS_LOADED   8 /* Firmware loaded        */

#define DIEHL_BCH            2 /* # of channels per card */

/* D-Channel states */
#define DIEHL_STATE_NULL     0
#define DIEHL_STATE_ICALL    1
#define DIEHL_STATE_OCALL    2
#define DIEHL_STATE_IWAIT    3
#define DIEHL_STATE_OWAIT    4
#define DIEHL_STATE_IBWAIT   5
#define DIEHL_STATE_OBWAIT   6
#define DIEHL_STATE_BWAIT    7
#define DIEHL_STATE_BHWAIT   8
#define DIEHL_STATE_BHWAIT2  9
#define DIEHL_STATE_DHWAIT  10
#define DIEHL_STATE_DHWAIT2 11
#define DIEHL_STATE_BSETUP  12
#define DIEHL_STATE_ACTIVE  13

#define DIEHL_MAX_QUEUED  8000 /* 2 * maxbuff */

#define DIEHL_LOCK_TX 0
#define DIEHL_LOCK_RX 1

typedef struct {
	int dummy;
} diehl_mca_card;

typedef union {
	diehl_isa_card isa;
	diehl_pci_card pci;
	diehl_mca_card mca;
} diehl_hwif;

typedef struct {
	__u8 ret;
	__u8 id;
	__u8 ch;
} diehl_ack;

typedef struct {
	__u8 code;
	__u8 id;
	__u8 ch;
} diehl_req;

typedef struct {
	__u8 ret;
	__u8 id;
	__u8 ch;
	__u8 more;
} diehl_indhdr;

typedef struct {
	unsigned short callref;          /* Call Reference              */
	unsigned short fsm_state;        /* Current D-Channel state     */
	unsigned short eazmask;          /* EAZ-Mask for this Channel   */
	short queued;                    /* User-Data Bytes in TX queue */
	unsigned short plci;
	unsigned short ncci;
	unsigned char  l2prot;           /* Layer 2 protocol            */
	unsigned char  l3prot;           /* Layer 3 protocol            */
} diehl_chan;

typedef struct msn_entry {
	char eaz;
        char msn[16];
        struct msn_entry * next;
} msn_entry;

/*
 * Per card driver data
 */
typedef struct diehl_card {
	diehl_hwif hwif;                 /* Hardware dependant interface     */
        u_char ptype;                    /* Protocol type (1TR6 or Euro)     */
        u_char bus;                      /* Bustype (ISA, MCA, PCI)          */
        u_char type;                     /* Cardtype (DIEHL_CTYPE_...)       */
        struct diehl_card *next;	 /* Pointer to next device struct    */
        int myid;                        /* Driver-Nr. assigned by linklevel */
        unsigned long flags;             /* Statusflags                      */
        unsigned long ilock;             /* Semaphores for IRQ-Routines      */
	struct sk_buff_head rcvq;        /* Receive-Message queue            */
	struct sk_buff_head sndq;        /* Send-Message queue               */
	struct sk_buff_head rackq;       /* Req-Ack-Message queue            */
	struct sk_buff_head sackq;       /* Data-Ack-Message queue           */
	u_char *ack_msg;                 /* Ptr to User Data in User skb     */
	__u16 need_b3ack;                /* Flag: Need ACK for current skb   */
	struct sk_buff *sbuf;            /* skb which is currently sent      */
	struct tq_struct snd_tq;         /* Task struct for xmit bh          */
	struct tq_struct rcv_tq;         /* Task struct for rcv bh           */
	struct tq_struct ack_tq;         /* Task struct for ack bh           */
	msn_entry *msn_list;
	unsigned short msgnum;           /* Message number fur sending       */
	int    nchannels;                /* Number of B-Channels             */
	diehl_chan *bch;                 /* B-Channel status/control         */
	char   status_buf[256];          /* Buffer for status messages       */
	char   *status_buf_read;
	char   *status_buf_write;
	char   *status_buf_end;
        isdn_if interface;               /* Interface to upper layer         */
        char regname[35];                /* Name used for request_region     */
} diehl_card;

extern diehl_card *cards;
extern char *diehl_ctype_name[];

extern __inline__ void diehl_schedule_tx(diehl_card *card)
{
        queue_task(&card->snd_tq, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
}

extern __inline__ void diehl_schedule_rx(diehl_card *card)
{
        queue_task(&card->rcv_tq, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
}

extern __inline__ void diehl_schedule_ack(diehl_card *card)
{
        queue_task(&card->ack_tq, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
}

extern char *diehl_find_eaz(diehl_card *, char);
extern int diehl_addcard(int, int, int, char *);

#endif  /* __KERNEL__ */

#endif	/* diehl_h */
