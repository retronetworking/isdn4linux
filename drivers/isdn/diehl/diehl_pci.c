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
 * Revision 1.2  1998/06/16 21:10:45  armin
 * Added part for loading firmware. STILL UNUSABLE.
 *
 * Revision 1.1  1998/06/13 10:40:33  armin
 * First check in. YET UNUSABLE
 *
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


#if CONFIG_PCI	         /* intire stuff is only for PCI */

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
	aparms->PCIram = (unsigned int) ram;
	aparms->PCIreg = (unsigned int) reg;
	aparms->PCIcfg = (unsigned int) cfg;

	aparms->mvalid = 1;

	sprintf(did, "%s%d", (strlen(ID) < 1) ? "PCI":ID, pci_cards);

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
	        /* reset board */
        	writeb(_MP_RISC_RESET | _MP_LED1 | _MP_LED2, card->PCIreg + MP_RESET);
	        SLEEP(20);
	        writeb(0, card->PCIreg + MP_RESET);
	        SLEEP(20);

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

void
diehl_pci_rcv_dispatch(diehl_pci_card *card) {
        struct sk_buff *skb;

        if (!card) {
                printk(KERN_WARNING "diehl_pci_rcv_dispatch: NULL card!\n");
                return;
        }
	while((skb = skb_dequeue(&((diehl_card *)card->card)->rcvq))) {
		idi_handle_ind(card, skb);
	}
}

void
diehl_pci_ack_dispatch(diehl_pci_card *card) {
        struct sk_buff *skb;

        if (!card) {
                printk(KERN_WARNING "diehl_pci_ack_dispatch: NULL card!\n");
                return;
        }
	while((skb = skb_dequeue(&((diehl_card *)card->card)->rackq))) {
		idi_handle_ack(card, skb);
	}
}

void
diehl_pci_transmit(diehl_pci_card *card) {
        struct sk_buff *skb;
        struct sk_buff *skb2;
        unsigned long(flags);
        char *ram, *reg, *cfg;
	diehl_pci_pr_ram  *prram;
	diehl_pci_REQ *ReqOut, *reqbuf;
	diehl_chan *chan;
	diehl_chan_ptr *chan2;
	int ReqCount;

        if (!card) {
                printk(KERN_WARNING "diehl_pci_transmit: NULL card!\n");
                return;
        }
        ram = (char *)card->PCIram;
        reg = (char *)card->PCIreg;
        cfg = (char *)card->PCIcfg;
        prram = (diehl_pci_pr_ram *)ram;
	ReqCount = 0;
	while((skb2 = skb_dequeue(&((diehl_card *)card->card)->sndq))) { 
                save_flags(flags);
                cli();
                if (!(readb(&prram->ReqOutput) - readb(&prram->ReqInput))) {
                        restore_flags(flags);
                        skb_queue_head(&((diehl_card *)card->card)->sndq, skb2);
#ifdef DIEHL_PCI_DEBUG
                        printk(KERN_INFO "diehl_pci: transmit: Not ready\n");
#endif
                        return;
                }
		restore_flags(flags);
		chan2 = (diehl_chan_ptr *)skb2->data;
		chan = chan2->ptr;
		if (!chan->e.busy) {
		 if((skb = skb_dequeue(&chan->e.X))) { 
			reqbuf = (diehl_pci_REQ *)skb->data;
			/* get address of next available request buffer */
			ReqOut = (diehl_pci_REQ *)&prram->B[readw(&prram->NextReq)];
			writew(reqbuf->XBuffer.length, &ReqOut->XBuffer.length);
			memcpy(&ReqOut->XBuffer.P, &reqbuf->XBuffer.P, reqbuf->XBuffer.length);
			writeb(reqbuf->ReqCh, &ReqOut->ReqCh);
			writeb(reqbuf->Req, &ReqOut->Req); 
			if (!reqbuf->Reference) {
				writeb(chan->e.D3Id, &ReqOut->ReqId); 
				chan->e.ReqCh = 0; 
			}
			else {
				writeb(chan->e.B2Id, &ReqOut->ReqId); 
				chan->e.ReqCh = 1;
			}
			if ((!readb(&ReqOut->ReqId)) || (readb(&ReqOut->ReqId) == 0x20)){
			 	chan->e.ref = readw(&ReqOut->Reference); 
#if 0
		                printk(KERN_INFO "diehl_pci: Ref %d stored for Ch %d\n", 
						chan->e.ref, chan->No);
#endif
			} 
			chan->e.Req = reqbuf->Req;
			ReqCount++; 
			writew(readw(&ReqOut->next), &prram->NextReq); 
			chan->e.busy = 1; 
			dev_kfree_skb(skb);
		 }
		 dev_kfree_skb(skb2);
		} 
		else {
		skb_queue_tail(&((diehl_card *)card->card)->sackq, skb2);
		}
	}
	writeb((__u8)(readb(&prram->ReqInput) + ReqCount), &prram->ReqInput); 

	while((skb = skb_dequeue(&((diehl_card *)card->card)->sackq))) { 
		skb_queue_tail(&((diehl_card *)card->card)->sndq, skb);
	}
}


/*
 * IRQ handler
 */
static void
diehl_pci_irq(int irq, void *dev_id, struct pt_regs *regs) {
        diehl_pci_card *card = (diehl_pci_card *)dev_id;
    	char *ram, *reg, *cfg;	
	diehl_pci_pr_ram  *prram;
        diehl_pci_RC *RcIn;
        diehl_pci_IND *IndIn;
	struct sk_buff *skb;
        int Count, Rc, Ind;

	ram = (char *)card->PCIram;
	reg = (char *)card->PCIreg;
	cfg = (char *)card->PCIcfg;
	prram = (diehl_pci_pr_ram *)ram;

        if (!card) {
                printk(KERN_WARNING "diehl_pci_irq: spurious interrupt %d\n", irq);
                return;
        }

	if (card->irqprobe) {
                if (readb(&ram[0x3fe])) { 
#ifdef DIEHL_PCI_DEBUG
                        printk(KERN_INFO "diehl_pci: test interrupt routine ACK\n");
#endif
                        writeb(0, &prram->RcOutput);
		        writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
		        writew(0, &cfg[MP_IRQ_RESET + 2]);
			writeb(0, &ram[0x3fe]);
                } 
		
		card->irqprobe = 0;
		return;
	}

	if (!(readb(&ram[0x3fe]))) { /* card did not interrupt */
		printk(KERN_DEBUG "diehl_pci: IRQ: card tells no interrupt!\n");
		return;
	} 

        /* if return codes are available ...  */
        if((Count = readb(&prram->RcOutput))) {
		diehl_pci_RC *ack;
                /* get the buffer address of the first return code */
                RcIn = (diehl_pci_RC *)&prram->B[readw(&prram->NextRc)];
                /* for all return codes do ...  */
                while(Count--) {

                        if((Rc=readb(&RcIn->Rc))) {
				skb = alloc_skb(sizeof(diehl_pci_RC), GFP_ATOMIC);
				ack = (diehl_pci_RC *)skb_put(skb, sizeof(diehl_pci_RC));
				ack->Rc = Rc;
				ack->RcId = readb(&RcIn->RcId);
				ack->RcCh = readb(&RcIn->RcCh);
				ack->Reference = readw(&RcIn->Reference);
#ifdef DIEHL_PCI_DEBUG
                        	printk(KERN_INFO "diehl_pci: IRQ Rc=%d Id=%d Ch=%d Ref=%d\n",
					Rc,ack->RcId,ack->RcCh,ack->Reference);
#endif
                        	writeb(0, &RcIn->Rc);
				 skb_queue_tail(&((diehl_card *)card->card)->rackq, skb);
				 diehl_schedule_ack((diehl_card *)card->card);
                        }
                        /* get buffer address of next return code   */
                        RcIn = (diehl_pci_RC *)&prram->B[readw(&RcIn->next)];
                }
                /* clear all return codes (no chaining!) */
                writeb(0, &prram->RcOutput);

        }
        /* if indications are available ... */
        if((Count = readb(&prram->IndOutput))) {
		diehl_pci_IND *ind;
                /* get the buffer address of the first indication */
                IndIn = (diehl_pci_IND *)&prram->B[readw(&prram->NextInd)];
                /* for all indications do ... */
                while(Count--) {
			Ind = readb(&IndIn->Ind);
			if(Ind) {
				int len = readw(&IndIn->RBuffer.length);
				skb = alloc_skb(sizeof(diehl_pci_IND), GFP_ATOMIC);
				ind = (diehl_pci_IND *)skb_put(skb, sizeof(diehl_pci_IND));
				ind->Ind = Ind;
				ind->IndId = readb(&IndIn->IndId);
				ind->IndCh = readb(&IndIn->IndCh);
				ind->MInd = readb(&IndIn->MInd);
				ind->MLength = readw(&IndIn->MLength);
				ind->RBuffer.length = len;
#ifdef DIEHL_PCI_DEBUG
	                        printk(KERN_INFO "diehl_pci: IRQ Ind=%d Id=%d Ch=%d MInd=%d MLen=%d Len=%d\n",
				Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,len);
#endif
				memcpy(&ind->RBuffer.P, &IndIn->RBuffer.P, len);
				skb_queue_tail(&((diehl_card *)card->card)->rcvq, skb);
				diehl_schedule_rx((diehl_card *)card->card);
				writeb(0, &IndIn->Ind);
                        }
                        /* get buffer address of next indication  */
                        IndIn = (diehl_pci_IND *)&prram->B[readw(&IndIn->next)];
                }
                writeb(0, &prram->IndOutput);
        }

	/* clear interrupt */
	writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
	writew(0, &cfg[MP_IRQ_RESET + 2]); 
	writeb(0, &ram[0x3fe]); 

  return;
}


/* show header information of code file */
static
void diehl_pci_print_hdr(unsigned char *code, int offset)
{
  unsigned char hdr[80];
  int i;

  i = 0;
  while ((i < (sizeof(hdr) -1))
          && (code[offset + i] != '\0')
          && (code[offset + i] != '\r')
          && (code[offset + i] != '\n'))
   {
     hdr[i] = code[offset + i];
     i++;
   }
   hdr[i] = '\0';
   printk(KERN_DEBUG "diehl_pci: loading %s\n", hdr);
}


/*
 * Configure a card, download code into card,
 * check if we get interrupts and return 0 on succes.
 * Return -ERRNO on failure.
 */
int
diehl_pci_load(diehl_pci_card *card, diehl_pci_codebuf *cb) {
        diehl_pci_boot    *boot;
	diehl_pci_pr_ram  *prram;
        int               i,j;
        int               timeout;
	unsigned int	  offset, offp=0, size, length;
	unsigned long int signature = 0,cmd = 0;
        diehl_pci_codebuf cbuf;
        unsigned char     *code;
	unsigned char	  req_int;
    	char *ram, *reg, *cfg;	

        if (copy_from_user(&cbuf, cb, sizeof(diehl_pci_codebuf)))
                return -EFAULT;

        boot = &card->shmem->boot;
	ram = (char *)card->PCIram;
	reg = (char *)card->PCIreg;
	cfg = (char *)card->PCIcfg;
	prram = (diehl_pci_pr_ram *)ram;

	/* reset board */
	writeb(_MP_RISC_RESET | _MP_LED1 | _MP_LED2, card->PCIreg + MP_RESET);
	SLEEP(20);
	writeb(0, card->PCIreg + MP_RESET);
	SLEEP(20);

	/* set command count to 0 */
	writel(0, &boot->reserved); 

	/* check if CPU increments the life word */
        i = readw(&boot->live);
        SLEEP(20);
        if (i == readw(&boot->live)) {
           printk(KERN_ERR "diehl_pci: card is reset, but CPU not running !\n");
           return -EIO;
         }
#ifdef DIEHL_PCI_DEBUG
	 printk(KERN_DEBUG "diehl_pci: reset card OK (CPU running)\n");
#endif

	/* download firmware : DSP and Protocol */
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: downloading firmware...\n");
#endif

       	/* Allocate code-buffer */
       	if (!(code = kmalloc(400, GFP_KERNEL))) {
                printk(KERN_WARNING "diehl_pci_boot: Couldn't allocate code buffer\n");
       	        return -ENOMEM;
        }
        writel(MP_PROTOCOL_ADDR, &boot->addr); /* RISC code entry point */
        for (j = 0; j <= cbuf.dsp_code_num; j++)
         {
	   if (j==0) size = cbuf.protocol_len;
		else size = cbuf.dsp_code_len[j];	

           if (j==1) writel(MP_DSP_ADDR, &boot->addr); /* DSP code entry point */

           offset = 0;
           do  /* download block of up to 400 bytes */
            {
              length = ((size - offset) >= 400) ? 400 : (size - offset);

        	if (copy_from_user(code, (&cb->code) + offp + offset, length)) {
                	kfree(code);
	                return -EFAULT;
        	}

		if (offset == 0)
	           	diehl_pci_print_hdr(code, j ? 0x00 : 0x80); 

              for (i = 0; i < length; i+=4)
               {
                 writel(((u32 *)code)[i >> 2],&boot->data[i]); 
               }

               /* verify block */
              for (i = 0; i < length; i+=4)
               {
                if (((u32 *)code)[i >> 2] != readl(&boot->data[i]))
                 {
                  printk(KERN_ERR "diehl_pci: code block verify failed !\n");
		  kfree(code);
                  return -EIO;
                 }
               } 

              /* tell card the length (in words) */
              writel(((length + 3) / 4), &boot->len);
              writel(2, &boot->cmd); /* DIVAS_LOAD_CMD */

              /* wait till card ACKs */
	      timeout = jiffies + 20;
              while (timeout > jiffies) {
                cmd = readl(&boot->cmd);
                if (!cmd) break;
                SLEEP(2);
               }
              if (cmd)
               {
                printk(KERN_ERR "diehl_pci: timeout, no ACK to load !\n");
		kfree(code);
                return -EIO;
               }

              /* move onto next block */
              offset += length;
            } while (offset < size);
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: %d bytes loaded.\n", offset);
#endif
	 offp += size;
         }
	 kfree(code);	

	/* initialize the adapter data structure */
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: initializing adapter data structure...\n");
#endif
        /* clear out config space */
        for (i = 0; i < 256; i++) writeb(0, &ram[i]);

        /* copy configuration down to the card */
        writeb(cbuf.tei, &ram[8]);
        writeb(cbuf.nt2, &ram[9]);
        writeb(0, &ram[10]);
        writeb(cbuf.WatchDog, &ram[11]);
        writeb(cbuf.Permanent, &ram[12]);
        writeb(cbuf.XInterface, &ram[13]);
        writeb(cbuf.StableL2, &ram[14]);
        writeb(cbuf.NoOrderCheck, &ram[15]);
        writeb(cbuf.HandsetType, &ram[16]);
        writeb(0, &ram[17]);
        writeb(cbuf.LowChannel, &ram[18]);
        writeb(cbuf.ProtVersion, &ram[19]);
        writeb(cbuf.Crc4, &ram[20]);
        for (i = 0; i < 32; i++)
         {
           writeb(cbuf.l[0].oad[i], &ram[32 + i]);
           writeb(cbuf.l[0].osa[i], &ram[64 + i]);
           writeb(cbuf.l[0].spid[i], &ram[96 + i]);
           writeb(cbuf.l[1].oad[i], &ram[128 + i]);
           writeb(cbuf.l[1].osa[i], &ram[160 + i]);
           writeb(cbuf.l[1].spid[i], &ram[192 + i]);
         }
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: configured card OK\n");
#endif

	/* start adapter */
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: tell card to start...\n");
#endif
        writel(MP_PROTOCOL_ADDR, &boot->addr); /* RISC code entry point */
        writel(3, &boot->cmd); /* DIVAS_START_CMD */

        /* wait till card ACKs */
        timeout = jiffies + (5*HZ);
        while (timeout > jiffies) {
           signature = readl(&boot->signature);
           if ((signature >> 16) == DIVAS_SIGNATURE) break;
           SLEEP(2);
         }
        if ((signature >> 16) != DIVAS_SIGNATURE)
         {
#ifdef DIEHL_PCI_DEBUG
           printk(KERN_ERR "diehl_pci: signature 0x%lx expected 0x%x\n",(signature >> 16),DIVAS_SIGNATURE);
#endif
           printk(KERN_ERR "diehl_pci: timeout, protocol code not running !\n");
           return -EIO;
         }
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: Protocol code running, signature OK\n");
#endif

	/* get serial number and number of channels supported by card */
        card->channels = readb(&ram[0x3f6]);
        card->serial = readl(&ram[0x3f0]);
        printk(KERN_INFO "diehl_pci: Supported channels : %d\n", card->channels);
        printk(KERN_INFO "diehl_pci: Card serial no. = %lu\n", card->serial);

	/* test interrupt */
	readb(&ram[0x3fe]);
        writeb(0, &ram[0x3fe]); /* reset any pending interrupt */
	readb(&ram[0x3fe]);

        writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
        writew(0, &cfg[MP_IRQ_RESET + 2]);

        card->irqprobe = 1;

	if (!card->ivalid) {
	        if (request_irq(card->irq, &diehl_pci_irq, 0, "Diehl PCI ISDN", card)) 
        	 {
	          printk(KERN_ERR "diehl_pci: Couldn't request irq %d\n", card->irq);
        	  return -EIO;
	         }
	}
	card->ivalid = 1;

        req_int = readb(&prram->ReadyInt);
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "diehl_pci: testing interrupt\n");
#endif
        req_int++;
        /* Trigger an interrupt and check if it is delivered */
        writeb(req_int, &prram->ReadyInt);

        timeout = jiffies + 20;
        while (timeout > jiffies) {
          if (card->irqprobe != 1) break;
          SLEEP(2);
         }
        if (card->irqprobe == 1) {
           free_irq(card->irq, card);
	   card->ivalid = 0;
           printk(KERN_ERR "diehl_pci: getting no interrupts !\n");
           return -EIO;
         }

   /* initializing some variables */
   for(j=0; j<256; j++) card->IdTable[j] = NULL;
   for(j=0; j<((diehl_card *)card->card)->nchannels; j++) {
		((diehl_card *)card->card)->bch[j].e.busy = 0;
		((diehl_card *)card->card)->bch[j].e.D3Id = 0;
		((diehl_card *)card->card)->bch[j].e.B2Id = 0;
		((diehl_card *)card->card)->bch[j].e.ref = 0;
		((diehl_card *)card->card)->bch[j].e.Req = 0;
   }

   printk(KERN_INFO "diehl_pci: Card started OK\n");

 return 0;
}

#endif	/* CONFIG_PCI */

