/*
 * $Id$
 * 
 * Module for AVM B1 PCI-card.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * 2001-02-24 enable unloading when not used
 *            move to new pci interface, cleanup
 *            move to new kernelcapi
 *            Kai Germaschewski (kai.germaschewski@gmx.de)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <net/capi/driver.h>
#include <net/capi/kcapi.h>
#include "avmcard.h"
#include <linux/isdn_compat.h>

static char *revision = "$Revision$";
static char *rev;

/* ------------------------------------------------------------- */

static struct pci_device_id b1pci_pci_tbl[] __initdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, b1pci_pci_tbl);
MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;
static struct capi_driver b1pci_driver;
static int b1pci_count;

/* ------------------------------------------------------------- */

static void b1pci_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "b1pci: interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "%s: reentering interrupt hander.\n", card->name);
		return;
	}

	card->interrupt = 1;

	b1_handle_interrupt(card);

	card->interrupt = 0;
}
/* ------------------------------------------------------------- */

static char *b1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	if (b1pci_count++ == 0)
		di = attach_capi_driver(&b1pci_driver);

	retval = -ENODEV;
	if (pci_enable_device(pdev) < 0)
		goto outf_count;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_KERNEL);
	retval = -ENOMEM;
	if (!card)
		goto outf_count;

	memset(card, 0, sizeof(avmcard));
        cinfo = (avmctrl_info *) kmalloc(sizeof(avmctrl_info), GFP_KERNEL);
	if (!cinfo)
		goto outf_card;

	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	if (pci_resource_start(pdev, 2)) { /* B1 PCI V4 */
		card->port = pci_resource_start(pdev, 2);
	} else {
		card->port = pci_resource_start(pdev, 1);
	}
	card->irq = pdev->irq;
	card->cardtype = avm_b1pci;
	sprintf(card->name, "b1pci-%x", card->port);

	retval = -EBUSY;
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING
		       "b1pci: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		goto outf_ctrlinfo;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1pci: NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -EIO;
		goto outf_region;
	}
	b1_reset(card->port);
	b1_getrevision(card);

	retval = request_irq(card->irq, b1pci_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1pci: unable to get IRQ %d.\n",
				card->irq);
		goto outf_region;
	}

	printk(KERN_INFO
	       "b1pci: AVM B1 PCI at i/o %#x, irq %d, revision %d (no dma)\n",
	       card->port, card->irq, card->revision);

	retval = -EBUSY;
	cinfo->capi_ctrl = di->attach_ctr(&b1pci_driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "b1pci: attach controller failed.\n");
		goto outf_irq;
	}
	card->cardnr = cinfo->capi_ctrl->cnr;
	pdev->driver_data = card;
	return 0;

 outf_irq:
	free_irq(card->irq, card);
 outf_region:
	release_region(card->port, AVMB1_PORTLEN);
 outf_ctrlinfo:
	kfree(card->ctrlinfo);
 outf_card:
	kfree(card);
 outf_count:
	if (--b1pci_count == 0)
		detach_capi_driver(&b1pci_driver);
	return retval;
	
}

/* ------------------------------------------------------------- */

static void b1pci_remove(struct pci_dev *pdev)
{
	avmcard *card = pdev->driver_data;
	struct capi_ctr *ctrl = card->ctrlinfo->capi_ctrl;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	kfree(card->ctrlinfo);
	kfree(card);
	if (--b1pci_count == 0)
		detach_capi_driver(&b1pci_driver);
}

/* ------------------------------------------------------------- */

static int b1pci_conf_controller(struct capi_ctr *ctr, int cmd, void *data)
{
	switch (cmd) {
	case KCAPI_CMD_CONTR_AVMB1_LOAD_AND_CONFIG:
		return b1_load_and_config(ctr, data);
	case KCAPI_CMD_CONTR_RESET:
		return b1_reset_ctr(ctr);
	default:
		printk(KERN_DEBUG "b1.c: unknown cmd %#x\n", cmd);
	}
	return -EINVAL;
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pci_driver = {
	name:            "b1pci",
	revision:        "0.0",
	register_appl:   b1_register_appl,
	release_appl:    b1_release_appl,
	send_message:    b1_send_message,
	procinfo:        b1pci_procinfo,
	ctr_read_proc:   b1ctl_read_proc,
	conf_controller: b1pci_conf_controller,
};

static struct pci_driver b1pci_pci_driver = {
	name:		 "b1pci",
	id_table:	 b1pci_pci_tbl,
	probe:		 b1pci_probe,
	remove:		 b1pci_remove,
};

#ifdef CONFIG_ISDN_DRV_AVM_B1PCIV4
/* ------------------------------------------------------------- */

static struct capi_driver_interface *div4;
static struct capi_driver b1pciv4_driver;
static int b1pciv4_count;

/* ------------------------------------------------------------- */

static char *b1pciv4_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pciv4_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	if (b1pciv4_count++ == 0)
		div4 = attach_capi_driver(&b1pciv4_driver);

	retval = -ENODEV;
	if (pci_enable_device(pdev) < 0)
		goto outf_count;

	card = (avmcard *) kmalloc(sizeof(avmcard), GFP_KERNEL);
	retval = -ENOMEM;
	if (!card)
		goto outf_count;

	memset(card, 0, sizeof(avmcard));
	card->dma = (avmcard_dmainfo *) kmalloc(sizeof(avmcard_dmainfo), GFP_KERNEL);
	if (!card->dma)
		goto outf_card;

	memset(card->dma, 0, sizeof(avmcard_dmainfo));
        cinfo = (avmctrl_info *) kmalloc(sizeof(avmctrl_info), GFP_KERNEL);
	if (!cinfo)
		goto outf_dma;

	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	pci_set_master(pdev);
	card->membase = pci_resource_start(pdev, 0);
	card->port = pci_resource_start(pdev, 2);
	card->irq = pdev->irq;
	card->cardtype = avm_b1pci;
	sprintf(card->name, "b1pciv4-%x", card->port);

	retval = -EBUSY;
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING
		       "b1pci: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		goto outf_ctrlinfo;
	}
	card->mbase = ioremap_nocache(card->membase, 64);
	retval = -EIO;
	if (!card->mbase)
		goto outf_region;

	b1dma_reset(card);

	if ((retval = b1pciv4_detect(card))) {
		printk(KERN_NOTICE "b1pci: NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -ENODEV;
		goto outf_unmap;
	}
	b1dma_reset(card);
	b1_getrevision(card);

	retval = request_irq(card->irq, b1dma_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1pci: unable to get IRQ %d.\n",
				card->irq);
		goto outf_unmap;
	}

	printk(KERN_INFO
	       "b1pci: AVM B1 PCI V4 at i/o %#x, irq %d, mem %#lx, revision %d (dma)\n",
	       card->port, card->irq, card->membase, card->revision);

	retval = -EBUSY;
	cinfo->capi_ctrl = div4->attach_ctr(&b1pciv4_driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "b1pci: attach controller failed.\n");
		goto outf_irq;
	}

	card->cardnr = cinfo->capi_ctrl->cnr;
	skb_queue_head_init(&card->dma->send_queue);
	pdev->driver_data = card;
	return 0;
	
 outf_irq:
	free_irq(card->irq, card);
 outf_unmap:
	iounmap(card->mbase);
 outf_region:
	release_region(card->port, AVMB1_PORTLEN);
 outf_ctrlinfo:
	kfree(card->ctrlinfo);
 outf_dma:
	kfree(card->dma);
 outf_card:
	kfree(card);
 outf_count:
	if (--b1pciv4_count == 0)
		detach_capi_driver(&b1pciv4_driver);
	return retval;
}

static void b1pciv4_remove(struct pci_dev *pdev)
{
	avmcard *card = pdev->driver_data;
	struct capi_ctr *ctrl = card->ctrlinfo->capi_ctrl;

 	b1dma_reset(card);

	div4->detach_ctr(ctrl);
	free_irq(card->irq, card);
	iounmap(card->mbase);
	release_region(card->port, AVMB1_PORTLEN);
	kfree(card->ctrlinfo);
	kfree(card->dma);
	kfree(card);
	if (--b1pciv4_count == 0)
		detach_capi_driver(&b1pciv4_driver);
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pciv4_driver = {
	name:            "b1pciv4",
	revision:        "0.0",
	register_appl:   b1dma_register_appl,
	release_appl:    b1dma_release_appl,
	send_message:    b1dma_send_message,
	procinfo:        b1pciv4_procinfo,
	ctr_read_proc:   b1dmactl_read_proc,
};

static struct pci_driver b1pciv4_pci_driver = {
	name:		 "b1pci",
	id_table:	 b1pci_pci_tbl,
	probe:		 b1pciv4_probe,
	remove:		 b1pciv4_remove,
};

#endif /* CONFIG_ISDN_DRV_AVM_B1PCIV4 */

/* ------------------------------------------------------------- */

static int retval_b1pci = -ENODEV;
static int retval_b1pciv4 = -ENODEV;

/* 
 * the b1pci driver will drive b1pci v4 cards as well,
 * so we try b1pci v4 first and leave the rest to
 * b1pci
 */

static int __init b1pci_init(void)
{
	int retval = 0;

	MOD_INC_USE_COUNT;
	
	rev = strchr(revision, ':') + 2;
	*strchr(rev, ' ') = 0;

	printk(KERN_INFO "b1pci: revision %s\n", rev);

#ifdef CONFIG_ISDN_DRV_AVM_B1PCIV4
	strcpy(b1pciv4_driver.revision, rev);
	retval_b1pciv4 = pci_module_init(&b1pciv4_pci_driver);
#endif

	strcpy(b1pci_driver.revision, rev);
	retval_b1pci = pci_module_init(&b1pci_pci_driver);
	
	if (retval_b1pci < 0 && retval_b1pciv4 < 0)
		retval = -ENODEV;

	MOD_DEC_USE_COUNT;
	return retval;

}

static void __exit b1pci_exit(void)
{
	if (retval_b1pci == 0)
		pci_unregister_driver(&b1pci_pci_driver);

#ifdef CONFIG_ISDN_DRV_AVM_B1PCIV4
	if (retval_b1pciv4 == 0)
		pci_unregister_driver(&b1pciv4_pci_driver);
#endif
}

module_init(b1pci_init);
module_exit(b1pci_exit);
