/* $Id$
 *
 * ISDN low-level module for Eicon.Diehl active ISDN-Cards.
 *
 * Copyright 1998    by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1998,99 by Armin Schindler (mac@melware.de) 
 * Copyright 1999    Cytronics & Melware (info@melware.de)
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
 * Revision 1.3  1999/01/24 20:14:07  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.2  1999/01/10 18:46:04  armin
 * Bug with wrong values in HLC fixed.
 * Bytes to send are counted and limited now.
 *
 * Revision 1.1  1999/01/01 18:09:41  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */


#ifndef eicon_h
#define eicon_h

#define DIEHL_IOCTL_SETMMIO   0
#define DIEHL_IOCTL_GETMMIO   1
#define DIEHL_IOCTL_SETIRQ    2
#define DIEHL_IOCTL_GETIRQ    3
#define DIEHL_IOCTL_LOADBOOT  4
#define DIEHL_IOCTL_ADDCARD   5
#define DIEHL_IOCTL_GETTYPE   6
#define DIEHL_IOCTL_LOADPCI   7 

#define DIEHL_IOCTL_MANIF    90 

#define DIEHL_IOCTL_FREEIT   97
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

#define MAX_HEADER_LEN 10

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

/* Data for Management interface */
typedef struct {
	int count;
	int pos;
	int length[50];
	unsigned char data[700]; 
} eicon_manifbuf;


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
#include "eicon_isa.h"

/* Macro for delay via schedule() */
#define SLEEP(j) {                     \
  current->state = TASK_INTERRUPTIBLE; \
  schedule_timeout(j);                 \
}

#endif /* KERNEL */


#define DSP_COMBIFILE_FORMAT_IDENTIFICATION_SIZE 48
#define DSP_COMBIFILE_FORMAT_VERSION_BCD    0x0100

#define DSP_FILE_FORMAT_IDENTIFICATION_SIZE 48
#define DSP_FILE_FORMAT_VERSION_BCD         0x0100

typedef struct tag_dsp_combifile_header
{
  char                  format_identification[DSP_COMBIFILE_FORMAT_IDENTIFICATION_SIZE] __attribute__ ((packed));
  __u16                  format_version_bcd             __attribute__ ((packed));
  __u16                  header_size                    __attribute__ ((packed));
  __u16                  combifile_description_size     __attribute__ ((packed));
  __u16                  directory_entries              __attribute__ ((packed));
  __u16                  directory_size                 __attribute__ ((packed));
  __u16                  download_count                 __attribute__ ((packed));
  __u16                  usage_mask_size                __attribute__ ((packed));
} t_dsp_combifile_header;

typedef struct tag_dsp_combifile_directory_entry
{
  __u16                  card_type_number               __attribute__ ((packed));
  __u16                  file_set_number                __attribute__ ((packed));
} t_dsp_combifile_directory_entry;

typedef struct tag_dsp_file_header
{
  char                  format_identification[DSP_FILE_FORMAT_IDENTIFICATION_SIZE] __attribute__ ((packed));
  __u16                 format_version_bcd              __attribute__ ((packed));
  __u16                 download_id                     __attribute__ ((packed));
  __u16                 download_flags                  __attribute__ ((packed));
  __u16                 required_processing_power       __attribute__ ((packed));
  __u16                 interface_channel_count         __attribute__ ((packed));
  __u16                 header_size                     __attribute__ ((packed));
  __u16                 download_description_size       __attribute__ ((packed));
  __u16                 memory_block_table_size         __attribute__ ((packed));
  __u16                 memory_block_count              __attribute__ ((packed));
  __u16                 segment_table_size              __attribute__ ((packed));
  __u16                 segment_count                   __attribute__ ((packed));
  __u16                 symbol_table_size               __attribute__ ((packed));
  __u16                 symbol_count                    __attribute__ ((packed));
  __u16                 total_data_size_dm              __attribute__ ((packed));
  __u16                 data_block_count_dm             __attribute__ ((packed));
  __u16                 total_data_size_pm              __attribute__ ((packed));
  __u16                 data_block_count_pm             __attribute__ ((packed));
} t_dsp_file_header;

typedef struct tag_dsp_memory_block_desc
{
  __u16                 alias_memory_block;
  __u16                 memory_type;
  __u16                 address;
  __u16                 size;             /* DSP words */
} t_dsp_memory_block_desc;

typedef struct tag_dsp_segment_desc
{
  __u16                 memory_block;
  __u16                 attributes;
  __u16                 base;
  __u16                 size;
  __u16                 alignment;        /* ==0 -> no other legal start address than base */
} t_dsp_segment_desc;

typedef struct tag_dsp_symbol_desc
{
  __u16                 symbol_id;
  __u16                 segment;
  __u16                 offset;
  __u16                 size;             /* DSP words */
} t_dsp_symbol_desc;

typedef struct tag_dsp_data_block_header
{
  __u16                 attributes;
  __u16                 segment;
  __u16                 offset;
  __u16                 size;             /* DSP words */
} t_dsp_data_block_header;

typedef struct tag_dsp_download_desc      /* be sure to keep native alignment for MAESTRA's */
{
  __u16                 download_id;
  __u16                 download_flags;
  __u16                 required_processing_power;
  __u16                 interface_channel_count;
  __u16                 excess_header_size;
  __u16                 memory_block_count;
  __u16                 segment_count;
  __u16                 symbol_count;
  __u16                 data_block_count_dm;
  __u16                 data_block_count_pm;
  __u8  *            p_excess_header_data               __attribute__ ((packed));
  char  *            p_download_description             __attribute__ ((packed));
  t_dsp_memory_block_desc  *p_memory_block_table        __attribute__ ((packed));
  t_dsp_segment_desc  *p_segment_table                  __attribute__ ((packed));
  t_dsp_symbol_desc  *p_symbol_table                    __attribute__ ((packed));
  __u16 *            p_data_blocks_dm                   __attribute__ ((packed));
  __u16 *            p_data_blocks_pm                   __attribute__ ((packed));
} t_dsp_download_desc;


#ifdef __KERNEL__

typedef struct {
  __u8                  Req;            /* pending request          */
  __u8                  Rc;             /* return code received     */
  __u8                  Ind;            /* indication received      */
  __u8                  ReqCh;          /* channel of current Req   */
  __u8                  RcCh;           /* channel of current Rc    */
  __u8                  IndCh;          /* channel of current Ind   */
  __u8                  D3Id;           /* ID used by this entity   */
  __u8                  B2Id;           /* ID used by this entity   */
  __u8                  GlobalId;       /* reserved field           */
  __u8                  XNum;           /* number of X-buffers      */
  __u8                  RNum;           /* number of R-buffers      */
  struct sk_buff_head   X;              /* X-buffer queue           */
  struct sk_buff_head   R;              /* R-buffer queue           */
  __u8                  RNR;            /* receive not ready flag   */
  __u8                  complete;       /* receive complete status  */
  __u8                  busy;           /* busy flag                */
  __u16                 ref;            /* saved reference          */
} entity;


typedef struct {
	int	       No;		 /* Channel Number	        */
	unsigned short callref;          /* Call Reference              */
	unsigned short fsm_state;        /* Current D-Channel state     */
	unsigned short eazmask;          /* EAZ-Mask for this Channel   */
	unsigned int   queued;           /* User-Data Bytes in TX queue */
	unsigned int   waitq;            /* User-Data Bytes in wait queue */
	unsigned int   waitpq;           /* User-Data Bytes in packet queue */
	unsigned short plci;
	unsigned short ncci;
	unsigned char  l2prot;           /* Layer 2 protocol            */
	unsigned char  l3prot;           /* Layer 3 protocol            */
	entity		e;		 /* Entity  			*/
	char		cpn[32];	 /* remember cpn		*/
	char		oad[32];	 /* remember oad		*/
	unsigned char   cause[2];	 /* Last Cause			*/
	unsigned char	si1;
	unsigned char	si2;
} diehl_chan;

typedef struct {
	diehl_chan *ptr;
} diehl_chan_ptr;

#include "eicon_pci.h"

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
#define DIEHL_STATE_ICALLW  14
#define DIEHL_STATE_LISTEN  15
#define DIEHL_STATE_WMCONN  16

#define EICON_MAX_QUEUED  8000 /* 2 * maxbuff */

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
        int Feature;                     /* Protocol Feature Value           */
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
	unsigned short msgnum;           /* Message number for sending       */
	int    nchannels;                /* Number of B-Channels             */
	diehl_chan *bch;                 /* B-Channel status/control         */
	char   status_buf[256];          /* Buffer for status messages       */
	char   *status_buf_read;
	char   *status_buf_write;
	char   *status_buf_end;
        isdn_if interface;               /* Interface to upper layer         */
        char regname[35];                /* Name used for request_region     */
} diehl_card;

/* -----------------------------------------------------------**
** The PROTOCOL_FEATURE_STRING                                **
** defines capabilities and                                   **
** features of the actual protocol code. It's used as a bit   **
** mask.                                                      **
** The following Bits are defined:                            **
** -----------------------------------------------------------*/
#define PROTCAP_TELINDUS  0x0001  /* Telindus Variant of protocol code   */
#define PROTCAP_MANIF     0x0002  /* Management interface implemented    */
#define PROTCAP_V_42      0x0004  /* V42 implemented                     */
#define PROTCAP_V90D      0x0008  /* V.90D (implies up to 384k DSP code) */
#define PROTCAP_EXTD_FAX  0x0010  /* Extended FAX (ECM, 2D, T6, Polling) */
#define PROTCAP_FREE4     0x0020  /* not used                            */
#define PROTCAP_FREE5     0x0040  /* not used                            */
#define PROTCAP_FREE6     0x0080  /* not used                            */
#define PROTCAP_FREE7     0x0100  /* not used                            */
#define PROTCAP_FREE8     0x0200  /* not used                            */
#define PROTCAP_FREE9     0x0400  /* not used                            */
#define PROTCAP_FREE10    0x0800  /* not used                            */
#define PROTCAP_FREE11    0x1000  /* not used                            */
#define PROTCAP_FREE12    0x2000  /* not used                            */
#define PROTCAP_FREE13    0x4000  /* not used                            */
#define PROTCAP_EXTENSION 0x8000  /* used for future extentions          */

#include "eicon_idi.h"

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
extern ulong DebugVar;

#endif  /* __KERNEL__ */

#endif	/* eicon_h */
