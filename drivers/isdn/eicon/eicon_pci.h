/* $Id$
 *
 * ISDN low-level module for Eicon active ISDN-Cards (PCI part).
 *
 * Copyright 1998-2000 by Armin Schindler (mac@melware.de)
 * Copyright 1999,2000 Cytronics & Melware (info@melware.de)
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
 * Revision 1.4  2000/01/23 21:21:23  armin
 * Added new trace capability and some updates.
 * DIVA Server BRI now supports data for ISDNLOG.
 *
 * Revision 1.3  1999/03/29 11:19:51  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.2  1999/03/02 12:37:50  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.1  1999/01/01 18:09:46  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#ifndef eicon_pci_h
#define eicon_pci_h

#ifdef __KERNEL__

/*
 * card's description
 */
typedef struct {
	int   		  irq;	    /* IRQ		          */
	int		  channels; /* No. of supported channels  */
        void*             card;
        unsigned char     type;     /* card type                  */
        unsigned char     master;   /* Flag: Card is Quadro 1/4   */
} eicon_pci_card;

extern int eicon_pci_find_card(char *ID);

#endif  /* __KERNEL__ */

#endif	/* eicon_pci_h */
