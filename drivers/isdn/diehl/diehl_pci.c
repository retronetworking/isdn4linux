/* $Id$
 *
 * ISDN low-level module for DIEHL active ISDN-Cards.
 * Hardware-specific code for PCI cards.
 *
 * Copyright 1998 by Armin Schindler (mac@gismo.telekom.de)
 *
 * Thanks to	Eicon Technology Diehl GmbH & Co. oHG for 
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
 */

#include <linux/pci.h>

#include "diehl.h"
#include "diehl_pci.h"

/* Macro for delay via schedule() */
#define SLEEP(j) {                     \
  current->state = TASK_INTERRUPTIBLE; \
  current->timeout = jiffies + j;      \
  schedule();                          \
}

char *diehl_pci_revision = "$Revision$";


#if CONFIG_PCI		/* intire stuff is only for PCI */

#undef DIEHL_PCI_DEBUG  /* if you want diehl_pci more verbose */
     

int diehl_pci_find_card(char *ID)
{

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
  if (pci_present()) {
    struct pci_dev *pdev = NULL;
#else
  if (pcibios_present()) {
    int PCIValueI;
    char PCIValueB;
    char pci_bus, pci_device_fn;
    int pci_index=0; 
#endif

    int pci_nextindex=0, pci_cards=0, pci_akt=0; 
    int pci_type = PCI_MAESTRA;
    int NoMorePCICards = FALSE;
    char *ram, *reg, *cfg;	
    unsigned int pram=0, preg=0, pcfg=0;
    char did[12];
    diehl_pci_card *aparms;

   if (!(aparms = (diehl_pci_card *) kmalloc(sizeof(diehl_pci_card), GFP_KERNEL))) {
                  printk(KERN_WARNING
                      "diehl_pci: Could not allocate card-struct.\n");
                  return 0;
   }

  for (pci_cards = 0; pci_cards < 0x0f; pci_cards++)
  {
  do {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
      if ((pdev = pci_find_device(PCI_VENDOR_EICON,
                                  pci_type,
                                  pdev)))
#else
        pci_index = pci_nextindex;
        if (!(pcibios_find_device(PCI_VENDOR_EICON, pci_type,
            pci_index, &pci_bus, &pci_device_fn)) )
#endif
             {
              pci_nextindex++;
              break;
             }
             else
             {
              pci_nextindex = 0;
              switch (pci_type) /* switch to next card type */
               {
               case PCI_MAESTRA:
                 pci_type = PCI_MAESTRAQ; break;
               case PCI_MAESTRAQ:
                 pci_type = PCI_MAESTRAQ_U; break;
               case PCI_MAESTRAQ_U:
                 pci_type = PCI_MAESTRAP; break;
               default:
               case PCI_MAESTRAP:
                 NoMorePCICards = TRUE;
               }
             }
     }
     while (!NoMorePCICards);
     if (NoMorePCICards)
        {
           if (pci_cards < 1) {
           printk(KERN_INFO "diehl_pci: No PCI cards found.\n");
	   kfree(aparms);	
           return 0;
           }
           else
           {
           printk(KERN_INFO "diehl_pci: %d PCI card%s registered.\n",
			pci_cards, (pci_cards > 1) ? "s":"");
	   kfree(aparms);	
           return (pci_cards);
           }
        }
   pci_akt = 0;
   switch(pci_type)
   {
    case PCI_MAESTRA:
         printk(KERN_INFO "diehl_pci: DIVA Server BRI/PCI detected !\n");
          aparms->type = DIEHL_CTYPE_MAESTRA;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
          aparms->irq = pdev->irq;
          preg = pdev->base_address[2];
          pcfg = pdev->base_address[1];
#else      
          pcibios_read_config_byte(pci_bus, pci_device_fn, PCI_INTERRUPT_LINE, &PCIValueB);
          aparms->irq = PCIValueB;
          pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_2, &PCIValueI);
          preg = PCIValueI & 0xfffffffc;  /* I/O area for adapter */
          pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_1, &PCIValueI);
          pcfg = PCIValueI & 0xffffff80;  /* I/O area for PLX  */
#endif

#ifdef DIEHL_PCI_DEBUG
          printk(KERN_DEBUG "diehl_pci: irq=%d\n", aparms->irq);
          printk(KERN_DEBUG "diehl_pci: reg=0x%x\n",
                (preg));
          printk(KERN_DEBUG "diehl_pci: cfg=0x%x\n",
                (pcfg));
#endif
         /*  Not supported yet */
         printk(KERN_ERR "diehl_pci: DIVA Server BRI/PCI not supported !\n");
         pci_cards--;
	 pci_akt = 0;
         break;

    case PCI_MAESTRAQ:
    case PCI_MAESTRAQ_U:
         printk(KERN_ERR "diehl_pci: DIVA Server 4BRI/PCI detected but not supported !\n");
         pci_cards--;
	 pci_akt = 0;
         break;

    default:
    case PCI_MAESTRAP:
         printk(KERN_INFO "diehl_pci: DIVA Server PRI/PCI detected !\n");
          aparms->type = DIEHL_CTYPE_MAESTRAP; /*includes 9M,30M*/
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,92)
          aparms->irq = pdev->irq;
          pram = pdev->base_address[0];
          preg = pdev->base_address[2];
          pcfg = pdev->base_address[4];
#else      
          pcibios_read_config_byte(pci_bus, pci_device_fn, PCI_INTERRUPT_LINE, &PCIValueB);
          aparms->irq = PCIValueB;
          pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_0, &PCIValueI);
          pram = PCIValueI & 0xfffff000;  /* 64k memory */
          pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_2, &PCIValueI);
          preg = PCIValueI & 0xfffff000;  /* 16k memory */
          pcibios_read_config_dword(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_4, &PCIValueI);
          pcfg = PCIValueI & 0xfffff000;  /*  4k memory */
#endif

#ifdef DIEHL_PCI_DEBUG
          printk(KERN_DEBUG "diehl_pci: irq=%d\n", aparms->irq);
          printk(KERN_DEBUG "diehl_pci: ram=0x%x\n",
               (pram));
          printk(KERN_DEBUG "diehl_pci: reg=0x%x\n",
               (preg));
          printk(KERN_DEBUG "diehl_pci: cfg=0x%x\n",
               (pcfg));
#endif
	  pci_akt = 1;
	  break;	
   }

	if (pci_akt) {
	/* remapping memory */
	aparms->shmem = (diehl_pci_shmem *) ioremap(pram, 0x10000);
	ram = (u8 *) ((u32)aparms->shmem + MP_SHARED_RAM_OFFSET);
	reg =  ioremap(preg, 0x4000);
	cfg =  ioremap(pcfg, 0x1000);	
	aparms->PCIram = (unsigned int *) ram;
	aparms->PCIreg = (unsigned int *) reg;
	aparms->PCIcfg = (unsigned int *) cfg;

	aparms->mvalid = 1;

	if (strlen(ID) < 1)
		sprintf(did, "PCI%d", pci_cards);
	 else
		sprintf(did, "%s%d", ID, pci_cards);

	printk(KERN_INFO "diehl_pci: DriverID: '%s'\n", did);

	if (!(diehl_addcard(aparms->type, (int) aparms, aparms->irq, did))) {
		printk(KERN_ERR "diehl_pci: Card could not be added !\n");
		pci_cards--;
	 }
	}

  }
 } else
	printk(KERN_ERR "diehl_pci: Kernel compiled with PCI but no PCI-bios found !\n");
 return 0;
}

void
diehl_pci_printpar(diehl_pci_card *card) {
        switch (card->type) {
                case DIEHL_CTYPE_MAESTRA:
                case DIEHL_CTYPE_MAESTRAQ:
                case DIEHL_CTYPE_MAESTRAQ_U:
                case DIEHL_CTYPE_MAESTRAP:
                      printk(KERN_INFO "%s at 0x%x, irq %d\n",
                               diehl_ctype_name[card->type],
                               (unsigned int)card->shmem,
                               card->irq); 
#ifdef DIEHL_PCI_DEBUG
           printk(KERN_INFO "diehl_pci: remapped ram= 0x%x\n",(unsigned int)card->PCIram);
           printk(KERN_INFO "diehl_pci: remapped reg= 0x%x\n",(unsigned int)card->PCIreg);
           printk(KERN_INFO "diehl_pci: remapped cfg= 0x%x\n",(unsigned int)card->PCIcfg); 
#endif
	}
}


static void
diehl_pci_release_shmem(diehl_pci_card *card) {
	if (!card->master)
		return;
	if (card->mvalid) {
		iounmap((void *)card->shmem);
		iounmap((void *)card->PCIreg);
		iounmap((void *)card->PCIcfg);
	}
	card->mvalid = 0;
}

static void
diehl_pci_release_irq(diehl_pci_card *card) {
	if (!card->master)
		return;
	if (card->ivalid)
		free_irq(card->irq, card);
	card->ivalid = 0;
}

void
diehl_pci_release(diehl_pci_card *card) {
        diehl_pci_release_irq(card);
        diehl_pci_release_shmem(card);
}


/*
 * IRQ handler
 */
static void
diehl_pci_irq(int irq, void *dev_id, struct pt_regs *regs) {
        diehl_pci_card *card = (diehl_pci_card *)dev_id;
        /* diehl_isa_com *com; */
        unsigned char tmp;
        struct sk_buff *skb;

        if (!card) {
                printk(KERN_WARNING "diehl_pci_irq: spurious interrupt %d\n", irq);
                return;
        }

	/* Now interrupt stuff */

}



#endif	/* CONFIG_PCI */

