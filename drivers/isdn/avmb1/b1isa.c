/*
 * $Id$
 * 
 * Module for AVM B1 ISA-card.
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
#include <linux/capi.h>
#include <asm/io.h>
#include "compat.h"
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision$";

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void b1isa_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
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

static void b1isa_remove_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static int b1isa_add_card(struct capi_driver *driver, struct capicardparams *p)
{
	avmcard *card;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "b1isa: no memory.\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	sprintf(card->name, "b1isa-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1isa;

	if (check_region(card->port, AVMB1_PORTLEN)) {
		printk(KERN_WARNING
		       "b1isa: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}
	if (b1_irq_table[card->irq & 0xf] == 0) {
		printk(KERN_WARNING "b1isa: irq %d not valid.\n", card->irq);
		kfree(card);
		return -EINVAL;
	}
	if (   card->port != 0x150 && card->port != 0x250
	    && card->port != 0x300 && card->port != 0x340) {
		printk(KERN_WARNING "b1isa: illegal port 0x%x.\n", card->port);
		kfree(card);
		return -EINVAL;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1isa: NO card at 0x%x (%d)\n",
					card->port, retval);
		kfree(card);
		return -EIO;
	}
	b1_reset(card->port);

	request_region(p->port, AVMB1_PORTLEN, card->name);

	retval = request_irq(card->irq, b1isa_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1isa: unable to get IRQ %d.\n", card->irq);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	card->ctrl = di->attach_ctr(driver, card->name, card);
	if (!card->ctrl) {
		printk(KERN_ERR "b1isa: attach controller failed.\n");
		free_irq(card->irq, card);
		release_region(card->port, AVMB1_PORTLEN);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;
	return 0;
}

static char *b1isa_procinfo(struct capi_ctr *ctrl)
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

static struct capi_driver b1isa_driver = {
    "b1isa",
    b1_load_firmware,
    b1_reset_ctr,
    b1isa_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    b1_send_message,
    b1isa_procinfo,

    b1isa_add_card,
};

#ifdef MODULE
#define b1isa_init init_module
void cleanup_module(void);
#endif

int b1isa_init(void)
{
	char *p;
	char rev[10];

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");

	printk(KERN_INFO "b1isa: revision %s\n", rev);

        di = attach_capi_driver(&b1isa_driver);

	if (!di) {
		printk(KERN_ERR "b1isa: failed to attach capi_driver\n");
		return -EIO;
	}
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
    detach_capi_driver(&b1isa_driver);
}
#endif
