/*
 * $Id$
 * 
 * Module for AVM B1 PCI-card.
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log$
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include "compat.h"
#include <linux/capi.h>
#include <linux/b1lli.h>

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif

#ifndef PCI_DEVICE_ID_AVM_B1
#define PCI_DEVICE_ID_AVM_B1	0x700
#endif

static char *revision = "$Revision$";

/* ------------------------------------------------------------- */

#ifdef HAS_NEW_SYMTAB
MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");
#endif

/* ------------------------------------------------------------- */

/* ------------------------------------------------------------- */
/* -------- Init & Cleanup ------------------------------------- */
/* ------------------------------------------------------------- */

/*
 * init / exit functions
 */

#ifdef MODULE
#define b1pci_init init_module
#endif

int b1pci_init(void)
{
	char *p;
	char rev[10];
	int rc;
	int pci_index;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 1);
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, " ??? ");


#ifdef CONFIG_PCI
	if (!pcibios_present()) {
		printk(KERN_ERR "b1pci: no PCI-BIOS present\n");
		return -EIO;
	}

	printk(KERN_INFO "b1pci: revision %s\n", rev);

	for (pci_index = 0; pci_index < 8; pci_index++) {
		unsigned char pci_bus, pci_device_fn;
		unsigned int ioaddr;
		unsigned char irq;

		if (pcibios_find_device (PCI_VENDOR_ID_AVM,
					PCI_DEVICE_ID_AVM_B1, pci_index,
					&pci_bus, &pci_device_fn) != 0) {
			continue;
		}
		pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &irq);
		pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_1, &ioaddr);
		/* Strip the I/O address out of the returned value */
		ioaddr &= PCI_BASE_ADDRESS_IO_MASK;
		printk(KERN_INFO
			"b1pci.c: PCI BIOS reports AVM-B1 at i/o %#x, irq %d,",
			ioaddr,
			irq);
		if ((rc = avmb1_probecard(ioaddr, irq)) != 0) {
			printk(" but cannot be detected.\n");
			return rc;
		}
		printk(" ok.\n");
		if ((rc = avmb1_addcard(ioaddr, irq)) != 0)
			return rc;
	}
	return 0;
#else
	printk(KERN_ERR "b1pci: kernel not compiled with PCI.\n");
	return -EIO;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
}
#endif
