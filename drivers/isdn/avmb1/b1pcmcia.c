/*
 * $Id$
 * 
 * Module for AVM B1/M1/M2 PCMCIA-card.
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
#include <asm/io.h>
#include <linux/capi.h>
#include <linux/b1pcmcia.h>
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

static void b1pcmcia_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
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

static void b1pcmcia_remove_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = (avmcard *)(ctrl->driverdata);
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	/* io addrsses managent by CardServices 
	 * release_region(card->port, AVMB1_PORTLEN);
	 */
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static int b1pcmcia_add_card(struct capi_driver *driver,
				unsigned int port,
				unsigned irq,
				enum avmcardtype cardtype)
{
	avmcard *card;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "b1pcmcia: no memory.\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
	switch (cardtype) {
		case avm_m1: sprintf(card->name, "m1-%x", port); break;
		case avm_m2: sprintf(card->name, "m2-%x", port); break;
		default: sprintf(card->name, "b1pcmcia-%x", port); break;
	}
	card->port = port;
	card->irq = irq;
	card->cardtype = cardtype;

	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1pcmcia: NO card at 0x%x (%d)\n",
					card->port, retval);
		kfree(card);
		return -EIO;
	}
	b1_reset(card->port);

	retval = request_irq(card->irq, b1pcmcia_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1pcmcia: unable to get IRQ %d.\n", card->irq);
		kfree(card);
		return -EBUSY;
	}

	card->ctrl = di->attach_ctr(driver, card->name, card);
	if (!card->ctrl) {
		printk(KERN_ERR "b1pcmcia: attach controller failed.\n");
		free_irq(card->irq, card);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;
	return card->ctrl->cnr;
}

/* ------------------------------------------------------------- */

static char *b1pcmcia_procinfo(struct capi_ctr *ctrl)
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

static struct capi_driver b1pcmcia_driver = {
    "b1pcmcia",
    b1_load_firmware,
    b1_reset_ctr,
    b1pcmcia_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    b1_send_message,

    b1pcmcia_procinfo,

    0,
};

/* ------------------------------------------------------------- */

int b1pcmcia_addcard_b1(unsigned int port, unsigned irq)
{
	return b1pcmcia_add_card(&b1pcmcia_driver, port, irq, avm_b1pcmcia);
}

int b1pcmcia_addcard_m1(unsigned int port, unsigned irq)
{
	return b1pcmcia_add_card(&b1pcmcia_driver, port, irq, avm_m1);
}

int b1pcmcia_addcard_m2(unsigned int port, unsigned irq)
{
	return b1pcmcia_add_card(&b1pcmcia_driver, port, irq, avm_m2);
}

int b1pcmcia_delcard(unsigned int port, unsigned irq)
{
	struct capi_ctr *ctrl;
	avmcard *card;

	for (ctrl = b1pcmcia_driver.controller; ctrl; ctrl = ctrl->next) {
		card = (avmcard *)(ctrl->driverdata);
		if (card->port == port && card->irq == irq) {
			b1pcmcia_remove_ctr(ctrl);
			return 0;
		}
	}
	return -ESRCH;
}

EXPORT_SYMBOL(b1pcmcia_addcard_b1);
EXPORT_SYMBOL(b1pcmcia_addcard_m1);
EXPORT_SYMBOL(b1pcmcia_addcard_m2);
EXPORT_SYMBOL(b1pcmcia_delcard);

/* ------------------------------------------------------------- */

#ifdef MODULE
#define b1pcmcia_init init_module
void cleanup_module(void);
#endif

int b1pcmcia_init(void)
{
	char *p;
	char rev[10];

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");

	printk(KERN_INFO "b1pcmcia: revision %s\n", rev);

        di = attach_capi_driver(&b1pcmcia_driver);

	if (!di) {
		printk(KERN_ERR "b1pcmcia: failed to attach capi_driver\n");
		return -EIO;
	}
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
    detach_capi_driver(&b1pcmcia_driver);
}
#endif
