/* $Id$
 *
 * ISDN low-level module for Eicon active ISDN-Cards.
 * Hardware-specific code for PCI cards.
 *
 * Copyright 1998-2000 by Armin Schindler (mac@melware.de)
 * Copyright 1999,2000 Cytronics & Melware (info@melware.de)
 *
 * Thanks to	Eicon Technology GmbH & Co. oHG for 
 *		documents, informations and hardware. 
 *
 *		Deutsche Telekom AG for S2M support.
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
 * Revision 1.11  2000/01/23 21:21:23  armin
 * Added new trace capability and some updates.
 * DIVA Server BRI now supports data for ISDNLOG.
 *
 * Revision 1.10  1999/08/22 20:26:49  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.9  1999/08/11 21:01:11  keil
 * new PCI codefix
 *
 * Revision 1.8  1999/08/10 16:02:20  calle
 * struct pci_dev changed in 2.3.13. Made the necessary changes.
 *
 * Revision 1.7  1999/06/09 19:31:29  armin
 * Wrong PLX size for request_region() corrected.
 * Added first MCA code from Erik Weber.
 *
 * Revision 1.6  1999/04/01 12:48:37  armin
 * Changed some log outputs.
 *
 * Revision 1.5  1999/03/29 11:19:49  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.4  1999/03/02 12:37:48  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.3  1999/01/24 20:14:24  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.2  1999/01/10 18:46:06  armin
 * Bug with wrong values in HLC fixed.
 * Bytes to send are counted and limited now.
 *
 * Revision 1.1  1999/01/01 18:09:45  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#include <linux/config.h>
#include <linux/pci.h>

#include "eicon.h"
#include "eicon_pci.h"

#undef N_DATA
#include "adapter.h"
#include "uxio.h"

char *eicon_pci_revision = "$Revision$";

#if CONFIG_PCI	         /* intire stuff is only for PCI */
#ifdef CONFIG_ISDN_DRV_EICON_PCI

int eicon_pci_find_card(char *ID)
{
	int pci_cards = 0;
	int ctype = 0;
	char did[20];
	card_t *pCard;
	word wCardIndex;

	pCard = DivasCards;
	for (wCardIndex = 0; wCardIndex < MAX_CARDS; wCardIndex++)
	{
	if ((pCard->hw) && (pCard->hw->in_use))
		{
			switch(pCard->hw->card_type) {
				case DIA_CARD_TYPE_DIVA_SERVER:
					ctype = EICON_CTYPE_MAESTRAP;
					break;
				case DIA_CARD_TYPE_DIVA_SERVER_B:
					ctype = EICON_CTYPE_MAESTRA;
					break;
				case DIA_CARD_TYPE_DIVA_SERVER_Q:
					ctype = EICON_CTYPE_MAESTRAQ;
					break;
				default:
			}
			sprintf(did, "%s%d", (strlen(ID) < 1) ? "eicon":ID, pci_cards);
			if ((!ctype) || (!(eicon_addcard(ctype, 0, pCard->hw->irq, did)))) {
				printk(KERN_ERR "eicon_pci: Card could not be added !\n");
			} else {
				pci_cards++;
				printk(KERN_INFO "%s: DriverID: '%s'\n",eicon_ctype_name[ctype] , did);
			}
		}
		pCard++;
	}
	return pci_cards;
}

void
eicon_pci_init_conf(eicon_card *card)
{
	int j;

	/* initializing some variables */
	card->ReadyInt = 0;

	for(j = 0; j < 256; j++)
		card->IdTable[j] = NULL;

	for(j = 0; j < (card->d->channels + 1); j++) {
		card->bch[j].e.busy = 0;
		card->bch[j].e.D3Id = 0;
		card->bch[j].e.B2Id = 0;
		card->bch[j].e.ref = 0;
		card->bch[j].e.Req = 0;
		card->bch[j].e.complete = 1;
		card->bch[j].fsm_state = EICON_STATE_NULL;
	}
}

#endif
#endif	/* CONFIG_PCI */

