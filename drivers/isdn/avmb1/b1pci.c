/*
 * $Id$
 * 
 * Module for AVM B1 PCI-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log$
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <asm/io.h>
#include "compat.h"
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision$";

/* ------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif

#ifndef PCI_DEVICE_ID_AVM_B1
#define PCI_DEVICE_ID_AVM_B1	0x700
#endif

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void b1pci_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "b1_interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "b1_interrupt: reentering interrupt hander (%s)\n", card->name);
		return;
	}

	card->interrupt = 1;

	b1_handle_interrupt(card);

	card->interrupt = 0;
}
/* ------------------------------------------------------------- */

static void b1pci_remove_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static char *b1pci_procinfo(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	if (!card)
		return "";
	sprintf(card->infobuf, "%s %s 0x%x %d",
		card->cardname[0] ? card->cardname : "-",
		card->version[VER_DRIVER] ? card->version[VER_DRIVER] : "-",
		card->port, card->irq
		);
	return card->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pci_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	avmcard *card;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "b1pci: no memory.\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	sprintf(card->name, "b1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1pci;

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "b1pci: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1pci: NO card at 0x%x (%d)\n",
					card->port, retval);
		kfree(card);
		return -EIO;
	}
	b1_reset(card->port);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, b1pci_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1pci: unable to get IRQ %d.\n", card->irq);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	card->ctrl = di->attach_ctr(driver, card->name, card);
	if (!card->ctrl) {
		printk(KERN_ERR "b1pci: attach controller failed.\n");
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;

	return 0;
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pci_driver = {
    "b1pci",
    b1_load_firmware,
    b1_reset_ctr,
    b1pci_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    b1_send_message,

    b1pci_procinfo,

    0 /* no add_card function */,
};

#ifdef MODULE
#define b1pci_init init_module
void cleanup_module(void);
#endif

static int ncards = 0;

int b1pci_init(void)
{
	struct pci_dev *dev = NULL;
	char *p;
	char rev[10];
	int retval;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");

	printk(KERN_INFO "b1pci: revision %s\n", rev);

        di = attach_capi_driver(&b1pci_driver);

	if (!di) {
		printk(KERN_ERR "b1pci: failed to attach capi_driver\n");
		return -EIO;
	}

#ifdef CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "b1pci: no PCI bus present\n");
    		detach_capi_driver(&b1pci_driver);
		return -EIO;
	}

	while ((dev = pci_find_device(PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, dev))) {
		struct capicardparams param;

		param.port = dev->base_address[1] & PCI_BASE_ADDRESS_IO_MASK;
		param.irq = dev->irq;
		printk(KERN_INFO
			"b1pci: PCI BIOS reports AVM-B1 at i/o %#x, irq %d\n",
			param.port, param.irq);
		retval = b1pci_add_card(&b1pci_driver, &param);
		if (retval != 0) {
		        printk(KERN_ERR
			"b1pci: no AVM-B1 at i/o %#x, irq %d detected\n",
			param.port, param.irq);
    			/* detach_capi_driver(&b1pci_driver); */
#ifdef MODULE
			cleanup_module();
#endif
			return retval;
		}
		ncards++;
	}
	printk(KERN_INFO "b1pci: %d B1-PCI card(s) detected\n", ncards);
	return 0;
#else
	printk(KERN_ERR "b1pci: kernel not compiled with PCI.\n");
	return -EIO;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
    detach_capi_driver(&b1pci_driver);
}
#endif
