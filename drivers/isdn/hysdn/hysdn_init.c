/* $Id$

 * Linux driver for HYSDN cards, init functions.
 * written by Werner Cornelius (werner@titro.de) for Hypercope GmbH
 *
 * Copyright 1999  by Werner Cornelius (werner@titro.de)
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/malloc.h>
#include <linux/pci.h>

#include "hysdn_defs.h"

static char *hysdn_init_revision = "$Revision$";
int cardmax;			/* number of found cards */
hysdn_card *card_root = NULL;	/* pointer to first card */

/**********************************************/
/* table assigning PCI-sub ids to board types */
/* the last entry contains all 0              */
/**********************************************/
static struct {
	word subid;		/* PCI sub id */
	uchar cardtyp;		/* card type assigned */
} pci_subid_map[] = {

	{
		PCI_SUB_ID_METRO, BD_METRO
	},
	{
		PCI_SUB_ID_CHAMP2, BD_CHAMP2
	},
	{
		PCI_SUB_ID_ERGO, BD_ERGO
	},
	{
		PCI_SUB_ID_OLD_ERGO, BD_ERGO
	},
	{
		0, 0
	}			/* terminating entry */
};

/*********************************************************************/
/* search_cards searches for available cards in the pci config data. */
/* If a card is found, the card structure is allocated and the cards */
/* ressources are reserved. cardmax is incremented.                  */
/*********************************************************************/
static void
search_cards(void)
{
	struct pci_dev *akt_pcidev = NULL;
	hysdn_card *card, *card_last;
	uchar irq;
	int i;

	card_root = NULL;
	card_last = NULL;
	while ((akt_pcidev = pci_find_device(PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_PLX,
					     akt_pcidev)) != NULL) {

		if (!(card = kmalloc(sizeof(hysdn_card), GFP_KERNEL))) {
			printk(KERN_ERR "HYSDN: unable to alloc device mem \n");
			return;
		}
		memset(card, 0, sizeof(hysdn_card));
		card->myid = cardmax;	/* set own id */
		card->bus = akt_pcidev->bus->number;
		card->devfn = akt_pcidev->devfn;	/* slot + function */
		pcibios_read_config_word(card->bus, card->devfn, PCI_SUBSYSTEM_ID, &card->subsysid);
		pcibios_read_config_byte(card->bus, card->devfn, PCI_INTERRUPT_LINE, &irq);
		card->irq = irq;
		card->iobase = get_pcibase(akt_pcidev, PCI_REG_PLX_IO_BASE) & PCI_BASE_ADDRESS_IO_MASK;
		card->plxbase = get_pcibase(akt_pcidev, PCI_REG_PLX_MEM_BASE);
		card->membase = get_pcibase(akt_pcidev, PCI_REG_MEMORY_BASE);
		card->brdtype = BD_NONE;	/* unknown */
		card->debug_flags = DEF_DEB_FLAGS;	/* set default debug */
		card->faxchans = 0;	/* default no fax channels */
		card->bchans = 2;	/* and 2 b-channels */
		for (i = 0; pci_subid_map[i].subid; i++)
			if (pci_subid_map[i].subid == card->subsysid) {
				card->brdtype = pci_subid_map[i].cardtyp;
				break;
			}
		if (card->brdtype != BD_NONE) {
			if (ergo_inithardware(card)) {
				printk(KERN_WARNING "HYSDN: card at io 0x%04x already in use\n", card->iobase);
				kfree(card);
				continue;
			}
		} else {
			printk(KERN_WARNING "HYSDN: unknown card id 0x%04x\n", card->subsysid);
			kfree(card);	/* release mem */
			continue;
		}
		cardmax++;
		card->next = NULL;	/*end of chain */
		if (card_last)
			card_last->next = card;		/* pointer to next card */
		else
			card_root = card;
		card_last = card;	/* new chain end */
	}			/* device found */
}				/* search_cards */

/************************************************************************************/
/* free_resources frees the acquired PCI resources and returns the allocated memory */
/************************************************************************************/
static void
free_resources(void)
{
	hysdn_card *card;

	while (card_root) {
		card = card_root;
		if (card->releasehardware)
			card->releasehardware(card);	/* free all hardware resources */
		card_root = card_root->next;	/* remove card from chain */
		kfree(card);	/* return mem */

	}			/* while card_root */
}				/* free_resources */

/**************************************************************************/
/* stop_cards disables (hardware resets) all cards and disables interrupt */
/**************************************************************************/
static void
stop_cards(void)
{
	hysdn_card *card;

	card = card_root;	/* first in chain */
	while (card) {
		if (card->stopcard)
			card->stopcard(card);
		card = card->next;	/* remove card from chain */
	}			/* while card */
}				/* stop_cards */


/****************************************************************************/
/* The module startup and shutdown code. Only compiled when used as module. */
/* Using the driver as module is always advisable, because the booting      */
/* image becomes smaller and the driver code is only loaded when needed.    */
/* Additionally newer versions may be activated without rebooting.          */
/****************************************************************************/
#ifdef CONFIG_MODULES

/******************************************************/
/* extract revision number from string for log output */
/******************************************************/
char *
hysdn_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}


/****************************************************************************/
/* init_module is called once when the module is loaded to do all necessary */
/* things like autodetect...                                                */
/* If the return value of this function is 0 the init has been successfull  */
/* and the module is added to the list in /proc/modules, otherwise an error */
/* is assumed and the module will not be kept in memory.                    */
/****************************************************************************/
int
init_module(void)
{
	char tmp[50];

	strcpy(tmp, hysdn_init_revision);
	printk(KERN_NOTICE "HYSDN: module Rev: %s loaded\n", hysdn_getrev(tmp));
	strcpy(tmp, hysdn_net_revision);
	printk(KERN_NOTICE "HYSDN: network interface Rev: %s \n", hysdn_getrev(tmp));
	if (!pci_present()) {
		printk(KERN_ERR "HYSDN: no PCI bus present, module not loaded\n");
		return (-1);
	}
	search_cards();
	printk(KERN_INFO "HYSDN: %d card(s) found.\n", cardmax);

	if (hysdn_procconf_init()) {
		free_resources();	/* proc file_sys not created */
		return (-1);
	}
	return (0);		/* no error */
}				/* init_module */


/***********************************************************************/
/* cleanup_module is called when the module is released by the kernel. */
/* The routine is only called if init_module has been successfull and  */
/* the module counter has a value of 0. Otherwise this function will   */
/* not be called. This function must release all resources still allo- */
/* cated as after the return from this function the module code will   */
/* be removed from memory.                                             */
/***********************************************************************/
void
cleanup_module(void)
{

	stop_cards();
	hysdn_procconf_release();
	free_resources();
	printk(KERN_NOTICE "HYSDN: module unloaded\n");
}				/* cleanup_module */

#endif				/* CONFIG_MODULES */