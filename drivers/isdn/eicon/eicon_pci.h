/* $Id$
 *
 * ISDN low-level module for Eicon.Diehl active ISDN-Cards (PCI part).
 *
 * Copyright 1998,99 by Armin Schindler (mac@topmail.de)
 * Copyright 1999    Cytronics & Melware (cytronics-melware@topmail.de)
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
 *
 */

#ifndef eicon_pci_h
#define eicon_pci_h

#ifdef __KERNEL__


#define PCI_VENDOR_EICON        0x1133
#define PCI_DIVA_PRO20          0xe001
#define PCI_DIVA20              0xe002
#define PCI_DIVA_PRO20_U        0xe003
#define PCI_DIVA20_U            0xe004
#define PCI_MAESTRA             0xe010
#define PCI_MAESTRAQ            0xe012
#define PCI_MAESTRAQ_U          0xe013
#define PCI_MAESTRAP            0xe014

#define DIVA_PRO20          1
#define DIVA20              2
#define DIVA_PRO20_U        3
#define DIVA20_U            4
#define MAESTRA             5
#define MAESTRAQ            6
#define MAESTRAQ_U          7
#define MAESTRAP            8

#define TRUE  1
#define FALSE 0

#define DIVAS_SIGNATURE 0x4447


/* MAESTRA BRI PCI */

#define M_RESET		0x10		/* offset of reset register */
#define M_DATA		0x00		/* offset of data register */
#define M_ADDR		0x04		/* offset of address register */
#define M_ADDRH		0x0c		/* offset of high address register */

#define M_DSP_CODE_LEN            0xbf7d0000
#define M_DSP_CODE                0xbf7d0004  /* max 128K DSP-Code */ 
#define M_DSP_CODE_BASE           0xbf7a0000  
#define M_MAX_DSP_CODE_SIZE       0x00050000  /* max 320K DSP-Code (Telindus) */



/* MAESTRA PRI PCI */

#define MP_SHARED_RAM_OFFSET 0x1000  /* offset of shared RAM base in the DRAM memory bar */

#define MP_IRQ_RESET     0xc18       /* offset of interrupt status register in the CONFIG memory bar */
#define MP_IRQ_RESET_VAL 0xfe        /* value to clear an interrupt */

#define MP_PROTOCOL_ADDR 0xa0011000  /* load address of protocol code */
#define MP_DSP_ADDR      0xa03c0000  /* load address of DSP code */
#define MP_MAX_PROTOCOL_CODE_SIZE  0x000a0000   /* max 640K Protocol-Code */
#define MP_DSP_CODE_BASE           0xa03a0000
#define MP_MAX_DSP_CODE_SIZE       0x00060000   /* max 384K DSP-Code */

#define MP_RESET         0x20        /* offset of RESET register in the DEVICES memory bar */

/* RESET register bits */
#define _MP_S2M_RESET    0x10        /* active lo   */
#define _MP_LED2         0x08        /* 1 = on      */
#define _MP_LED1         0x04        /* 1 = on      */
#define _MP_DSP_RESET    0x02        /* active lo   */
#define _MP_RISC_RESET   0x81        /* active hi, bit 7 for compatibility with old boards */


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


/* boot interface structure */
typedef struct {
	__u32 cmd	__attribute__ ((packed));
	__u32 addr	__attribute__ ((packed));
	__u32 len	__attribute__ ((packed));
	__u32 err	__attribute__ ((packed));
	__u32 live	__attribute__ ((packed));
	__u32 reserved[(0x1020>>2)-6] __attribute__ ((packed));
	__u32 signature	__attribute__ ((packed));
	__u8 data[1];    /* real interface description */
} diehl_pci_boot;


#define DL_PARA_IO_TYPE   0
#define DL_PARA_MEM_TYPE  1

typedef struct tag_dsp_download_space
{
  __u16 type;  /* see definitions above to differ union elements */
  union
  {
    struct
    {
      __u32               r3addr;
      __u16               ioADDR;
      __u16               ioADDRH;
      __u16               ioDATA;
      __u16               BadData;  /* in case of verify error */
      __u16               GoodData;
    } io;     /* for io based adapters */
    struct
    {
      __u32               r3addr;
      diehl_pci_boot	  *boot;
      __u32               BadData;  /* in case of verify error */
      __u32               GoodData;
      __u16               timeout;
    } mem;    /* for memory based adapters */
  } dat;
} t_dsp_download_space;


/* Shared memory */
typedef union {
	diehl_pci_boot boot;
} diehl_pci_shmem;

typedef struct {
  __u16 length __attribute__ ((packed)); /* length of data/parameter field */
  __u8  P[1];                          /* data/parameter field */
} diehl_pci_PBUFFER;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Req             __attribute__ ((packed));
  __u8  ReqId           __attribute__ ((packed));
  __u8  ReqCh           __attribute__ ((packed));
  __u8  Reserved1       __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  Reserved[8]     __attribute__ ((packed));
  diehl_pci_PBUFFER XBuffer;
} diehl_pci_REQ;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Rc              __attribute__ ((packed));
  __u8  RcId            __attribute__ ((packed));
  __u8  RcCh            __attribute__ ((packed));
  __u8  Reserved1       __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  Reserved2[8]    __attribute__ ((packed));
} diehl_pci_RC;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Ind             __attribute__ ((packed));
  __u8  IndId           __attribute__ ((packed));
  __u8  IndCh           __attribute__ ((packed));
  __u8  MInd            __attribute__ ((packed));
  __u16 MLength         __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  RNR             __attribute__ ((packed));
  __u8  Reserved        __attribute__ ((packed));
  __u32 Ack             __attribute__ ((packed));
  diehl_pci_PBUFFER RBuffer;
} diehl_pci_IND;


typedef struct {
  __u16 NextReq  __attribute__ ((packed));       	/* pointer to next Req Buffer               */
  __u16 NextRc   __attribute__ ((packed));          	/* pointer to next Rc Buffer                */
  __u16 NextInd  __attribute__ ((packed));         	/* pointer to next Ind Buffer               */
  __u8 ReqInput  __attribute__ ((packed));        	/* number of Req Buffers sent               */
  __u8 ReqOutput  __attribute__ ((packed));       	/* number of Req Buffers returned           */
  __u8 ReqReserved  __attribute__ ((packed));     	/* number of Req Buffers reserved           */
  __u8 Int  __attribute__ ((packed));             	/* ISDN-P interrupt                         */
  __u8 XLock  __attribute__ ((packed));           	/* Lock field for arbitration               */
  __u8 RcOutput  __attribute__ ((packed));        	/* number of Rc buffers received            */
  __u8 IndOutput  __attribute__ ((packed));       	/* number of Ind buffers received           */
  __u8 IMask  __attribute__ ((packed));           	/* Interrupt Mask Flag                      */
  __u8 Reserved1[2]  __attribute__ ((packed));    	/* reserved field, do not use               */
  __u8 ReadyInt  __attribute__ ((packed));        	/* request field for ready interrupt        */
  __u8 Reserved2[12]  __attribute__ ((packed));   	/* reserved field, do not use               */
  __u8 InterfaceType  __attribute__ ((packed));   	/* interface type 1=16K interface           */
  __u16 Signature  __attribute__ ((packed));       	/* ISDN-P initialized indication            */
  __u8 B[1];			         		/* buffer space for Req,Ind and Rc          */
} diehl_pci_pr_ram;

typedef union {
	diehl_pci_pr_ram ram;
} diehl_pci_ram;

/*
 * card's description
 */
typedef struct {
	int		  ramsize;
	int   		  irq;	    /* IRQ		          */
	unsigned int      PCIram;
	unsigned int	  PCIreg;
	unsigned int	  PCIcfg;
	long int   	  serial;   /* Serial No.		  */
	int		  channels; /* No. of supported channels  */
        void*             card;
        diehl_pci_shmem*  shmem;    /* Shared-memory area         */
        unsigned char*    intack;   /* Int-Acknowledge            */
        unsigned char*    stopcpu;  /* Writing here stops CPU     */
        unsigned char*    startcpu; /* Writing here starts CPU    */
        unsigned char     type;     /* card type                  */
        unsigned char     irqprobe; /* Flag: IRQ-probing          */
        unsigned char     mvalid;   /* Flag: Memory is valid      */
        unsigned char     ivalid;   /* Flag: IRQ is valid         */
        unsigned char     master;   /* Flag: Card is Quadro 1/4   */
        void*             generic;  /* Ptr to generic card struct */
	diehl_chan* 	  IdTable[256]; /* Table to find entity   */
} diehl_pci_card;



extern int diehl_pci_load_pri(diehl_pci_card *card, diehl_pci_codebuf *cb);
extern int diehl_pci_load_bri(diehl_pci_card *card, diehl_pci_codebuf *cb);
extern void diehl_pci_release(diehl_pci_card *card);
extern void diehl_pci_printpar(diehl_pci_card *card);
extern void diehl_pci_transmit(diehl_pci_card *card);
extern int diehl_pci_find_card(char *ID);
extern void diehl_pci_ack_dispatch(diehl_pci_card *card); 
extern void diehl_pci_rcv_dispatch(diehl_pci_card *card); 

#endif  /* __KERNEL__ */

#endif	/* eicon_pci_h */
