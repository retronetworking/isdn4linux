/* $Id$
 *
 * ISDN low-level module for DIEHL active ISDN-Cards (PCI part).
 *
 * Copyright 1998 by Armin Schindler (mac@gismo.telekom.de)
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

#ifndef diehl_pci_h
#define diehl_pci_h

#ifdef __KERNEL__

#include "diehl.h"

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


/*----------------------------------------------------------------------------
// MAESTRA PRI PCI */

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

/* Shared memory */
typedef union {
	diehl_pci_boot boot;
} diehl_pci_shmem;


/*
 * card's description
 */
typedef struct {
	int		  ramsize;
	int   		  irq;	    /* IRQ		          */
	unsigned int*	  PCIram;
	unsigned int*	  PCIreg;
	unsigned int*	  PCIcfg;
	int 		  serial;   /* Serial No.		  */
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
        unsigned char     master;   /* Flag: Card ist Quadro 1/4  */
        void*             generic;  /* Ptr to generic card struct */
} diehl_pci_card;


extern int diehl_pci_load(diehl_pci_card *card, diehl_pci_codebuf *cb);
extern void diehl_pci_release(diehl_pci_card *card);
extern void diehl_pci_printpar(diehl_pci_card *card);
extern void diehl_pci_transmit(diehl_pci_card *card);
extern int diehl_pci_find_card(char *ID);

#endif  /* __KERNEL__ */

#endif	/* diehl_pci_h */
