/* $Id$
 *
 * ISDN low-level module for DIEHL active ISDN-Cards.
 *
 * Copyright 1998 by Fritz Elfert (fritz@wuemaus.franken.de)
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
 */

#ifndef diehl_isa_h
#define diehl_isa_h

#ifdef __KERNEL__

#include "diehl.h"

/* Factory defaults for ISA-Cards */
#define DIEHL_ISA_MEMBASE 0xd0000
#define DIEHL_ISA_IRQ     3
/* shmem offset for Quadro parts */
#define DIEHL_ISA_QOFFSET 0x0800

/*
 * Request-buffer
 */
typedef struct {
	__u16 len __attribute__ ((packed));        /* length of data/parameter field         */
	__u8  buf[270];                            /* data/parameter field                   */
} diehl_isa_reqbuf;

/* General communication buffer */
typedef struct {
        __u8   Req;                                /* request register                       */
	__u8   ReqId;                              /* request task/entity identification     */
	__u8   Rc;                                 /* return code register                   */
	__u8   RcId;                               /* return code task/entity identification */
	__u8   Ind;                                /* Indication register                    */
	__u8   IndId;                              /* Indication task/entity identification  */
	__u8   IMask;                              /* Interrupt Mask Flag                    */
	__u8   RNR;                                /* Receiver Not Ready (set by PC)         */
	__u8   XLock;                              /* XBuffer locked Flag                    */
	__u8   Int;                                /* ISDN interrupt                         */
	__u8   ReqCh;                              /* Channel field for layer-3 Requests     */
	__u8   RcCh;                               /* Channel field for layer-3 Returncodes  */
	__u8   IndCh;                              /* Channel field for layer-3 Indications  */
	__u8   MInd;                               /* more data indication field             */
	__u16  MLength;                            /* more data total packet length          */
	__u8   ReadyInt;                           /* request field for ready interrupt      */
	__u8   Reserved[12];                       /* reserved space                         */
	__u8   IfType;                             /* 1 = 16k-Interface                      */
	__u16  Signature __attribute__ ((packed)); /* ISDN adapter Signature                 */
	diehl_isa_reqbuf XBuffer;                            /* Transmit Buffer                        */
	diehl_isa_reqbuf RBuffer;                            /* Receive Buffer                         */
} diehl_isa_com;

/* struct for downloading firmware */
typedef struct {
	__u8  ctrl;
	__u8  card;
	__u8  msize;
	__u8  fill0;
	__u16 ebit __attribute__ ((packed));
	__u32 eloc __attribute__ ((packed));
	__u8  reserved[20];
	__u16 signature __attribute__ ((packed));
	__u8  fill[224];
	__u8  b[256];
} diehl_isa_boot;

/* Shared memory */
typedef union {
	unsigned char  c[0x400];
	diehl_isa_com  com;
	diehl_isa_boot boot;
} diehl_isa_shmem;

/*
 * card's description
 */
typedef struct {
	int               ramsize;
	int               irq;	    /* IRQ                        */
	void*             card;
	diehl_isa_shmem*  shmem;    /* Shared-memory area         */
	unsigned char*    intack;   /* Int-Acknowledge            */
	unsigned char*    stopcpu;  /* Writing here stops CPU     */
	unsigned char*    startcpu; /* Writing here starts CPU    */
	unsigned char     type;     /* card type                  */
	unsigned char     irqprobe; /* Flag: IRQ-probing          */
	unsigned char     mvalid;   /* Flag: Memory is valid      */
	unsigned char     ivalid;   /* Flag: IRQ is valid         */
	unsigned char     master;   /* Flag: Card ist Quadro 1/4  */
	void*             generic;  /* Ptr to generic card struct */
} diehl_isa_card;

/* Offsets for special locations on standard cards */
#define INTACK     0x03fe /* HW-doc says: 0x3ff, sample code and manual say: 0x3fe ??? */
#define STOPCPU    0x0400
#define STARTCPU   0x0401
#define RAMSIZE    0x0400
/* Offsets for special location on PRI card */
#define INTACK_P   0x3ffc
#define STOPCPU_P  0x3ffe
#define STARTCPU_P 0x3fff
#define RAMSIZE_P  0x4000


extern int diehl_isa_load(diehl_isa_card *card, diehl_isa_codebuf *cb);
extern void diehl_isa_release(diehl_isa_card *card);
extern void diehl_isa_printpar(diehl_isa_card *card);
extern void diehl_isa_transmit(diehl_isa_card *card);

#endif  /* __KERNEL__ */

#endif	/* diehl_isa_h */
