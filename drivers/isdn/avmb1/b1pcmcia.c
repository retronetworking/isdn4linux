/*
 * $Id$
 * 
 * Module for AVM B1/M1/M2 PCMCIA-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log$
 * Revision 1.4  1999/08/22 20:26:26  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.3  1999/07/09 15:05:41  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.2  1999/07/05 15:09:51  calle
 * - renamed "appl_release" to "appl_released".
 * - version und profile data now cleared on controller reset
 * - extended /proc interface, to allow driver and controller specific
 *   informations to include by driver hackers.
 *
 * Revision 1.1  1999/07/01 15:26:30  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 *
 */

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
#include <linux/isdn_compat.h>
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
		printk(KERN_WARNING "b1pcmcia: interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "%s: reentering interrupt hander.\n",
			card->name);
		return;
	}

	card->interrupt = 1;

	b1_handle_interrupt(card);

	card->interrupt = 0;
}
/* ------------------------------------------------------------- */

static void b1pcmcia_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
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
	avmctrl_info *cinfo;
	avmcard *card;
	int retval;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_ATOMIC);

	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		return -ENOMEM;
	}
	memset(card, 0, sizeof(avmcard));
        cinfo = (avmctrl_info *) kmalloc(sizeof(avmctrl_info), GFP_ATOMIC);
	if (!cinfo) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		kfree(card);
		return -ENOMEM;
	}
	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
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
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EIO;
	}
	b1_reset(card->port);

	retval = request_irq(card->irq, b1pcmcia_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				driver->name, card->irq);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EBUSY;
	}

	cinfo->capi_ctrl = di->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n",
				driver->name);
		free_irq(card->irq, card);
	        kfree(card->ctrlinfo);
		kfree(card);
		return -EBUSY;
	}

	MOD_INC_USE_COUNT;
	return cinfo->capi_ctrl->cnr;
}

/* ------------------------------------------------------------- */

static char *b1pcmcia_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pcmcia_driver = {
    "b1pcmcia",
    "0.0",
    b1_load_firmware,
    b1_reset_ctr,
    b1pcmcia_remove_ctr,
    b1_register_appl,
    b1_release_appl,
    b1_send_message,

    b1pcmcia_procinfo,
    b1ctl_read_proc,
    0,	/* use standard driver_read_proc */

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
		card = ((avmctrl_info *)(ctrl->driverdata))->card;
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
	struct capi_driver *driver = &b1pcmcia_driver;
	char *p;

	if ((p = strchr(revision, ':'))) {
		strncpy(driver->revision, p + 1, sizeof(driver->revision));
		p = strchr(driver->revision, '$');
		*p = 0;
	}

	printk(KERN_INFO "%s: revision %s\n", driver->name, driver->revision);

        di = attach_capi_driver(driver);

	if (!di) {
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driver->name);
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
