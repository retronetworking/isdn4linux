/* $Id$
 *
 * ISDN low-level module for Eicon.Diehl active ISDN-Cards.
 * Hardware-specific code for PCI cards.
 *
 * Copyright 1998,99 by Armin Schindler (mac@topmail.de)
 * Copyright 1999    Cytronics & Melware (cytronics-melware@topmail.de)
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

#include <linux/pci.h>

#include "eicon.h"
#include "eicon_pci.h"


char *diehl_pci_revision = "$Revision$";

#if CONFIG_PCI	         /* intire stuff is only for PCI */

#undef DIEHL_PCI_DEBUG 


int diehl_pci_find_card(char *ID)
{
  if (pci_present()) { 
    struct pci_dev *pdev = NULL;  
    int pci_nextindex=0, pci_cards=0, pci_akt=0; 
    int pci_type = PCI_MAESTRA;
    int NoMorePCICards = FALSE;
    char *ram, *reg, *cfg;	
    unsigned int pram=0, preg=0, pcfg=0;
    char did[12];
    diehl_pci_card *aparms;

   if (!(aparms = (diehl_pci_card *) kmalloc(sizeof(diehl_pci_card), GFP_KERNEL))) {
                  printk(KERN_WARNING
                      "eicon_pci: Could not allocate card-struct.\n");
                  return 0;
   }

  for (pci_cards = 0; pci_cards < 0x0f; pci_cards++)
  {
  do {
      if ((pdev = pci_find_device(PCI_VENDOR_EICON,          
                                  pci_type,                  
                                  pdev)))                    
	{
              pci_nextindex++;
              break;
	}
	else {
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
           printk(KERN_INFO "eicon_pci: No supported PCI cards found.\n");
	   kfree(aparms);	
           return 0;
           }
           else
           {
           printk(KERN_INFO "eicon_pci: %d PCI card%s registered.\n",
			pci_cards, (pci_cards > 1) ? "s":"");
	   kfree(aparms);	
           return (pci_cards);
           }
        }

   pci_akt = 0;
   switch(pci_type)
   {
    case PCI_MAESTRA:
         printk(KERN_INFO "eicon_pci: DIVA Server BRI/PCI detected !\n");
          aparms->type = DIEHL_CTYPE_MAESTRA;

          aparms->irq = pdev->irq;
          preg = pdev->base_address[2] & 0xfffffffc;
          pcfg = pdev->base_address[1] & 0xffffff80;

#ifdef DIEHL_PCI_DEBUG
          printk(KERN_DEBUG "eicon_pci: irq=%d\n", aparms->irq);
          printk(KERN_DEBUG "eicon_pci: reg=0x%x\n", preg);
          printk(KERN_DEBUG "eicon_pci: cfg=0x%x\n", pcfg);
#endif
	 pci_akt = 1;
         break;

    case PCI_MAESTRAQ:
    case PCI_MAESTRAQ_U:
         printk(KERN_ERR "eicon_pci: DIVA Server 4BRI/PCI detected but not supported !\n");
         pci_cards--;
	 pci_akt = 0;
         break;

    case PCI_MAESTRAP:
         printk(KERN_INFO "eicon_pci: DIVA Server PRI/PCI detected !\n");
          aparms->type = DIEHL_CTYPE_MAESTRAP; /*includes 9M,30M*/
          aparms->irq = pdev->irq;
          pram = pdev->base_address[0] & 0xfffff000;
          preg = pdev->base_address[2] & 0xfffff000;
          pcfg = pdev->base_address[4] & 0xfffff000;

#ifdef DIEHL_PCI_DEBUG
          printk(KERN_DEBUG "eicon_pci: irq=%d\n", aparms->irq);
          printk(KERN_DEBUG "eicon_pci: ram=0x%x\n",
               (pram));
          printk(KERN_DEBUG "eicon_pci: reg=0x%x\n",
               (preg));
          printk(KERN_DEBUG "eicon_pci: cfg=0x%x\n",
               (pcfg));
#endif
	  pci_akt = 1;
	  break;	
    default:
         printk(KERN_ERR "eicon_pci: Unknown PCI card detected !\n");
         pci_cards--;
	 pci_akt = 0;
	 break;
   }

	if (pci_akt) {
		/* remapping memory */
		switch(pci_type)
		{
    		case PCI_MAESTRA:
			aparms->PCIreg = (unsigned int) preg;
			aparms->PCIcfg = (unsigned int) pcfg;
			if (check_region((aparms->PCIreg), 0x20)) {
				printk(KERN_WARNING "eicon_pci: reg port already in use !\n");
				aparms->PCIreg = 0;
				break;	
			} else {
				request_region(aparms->PCIreg, 0x20, "eicon reg");
			}
			if (check_region((aparms->PCIcfg), 0x100)) {
				printk(KERN_WARNING "eicon_pci: cfg port already in use !\n");
				aparms->PCIcfg = 0;
				break;	
			} else {
				request_region(aparms->PCIcfg, 0x100, "eicon cfg");
			}
			break;
    		case PCI_MAESTRAQ:
		case PCI_MAESTRAQ_U:
		case PCI_MAESTRAP:
			aparms->shmem = (diehl_pci_shmem *) ioremap(pram, 0x10000);
			ram = (u8 *) ((u32)aparms->shmem + MP_SHARED_RAM_OFFSET);
			reg =  ioremap(preg, 0x4000);
			cfg =  ioremap(pcfg, 0x1000);	
			aparms->PCIram = (unsigned int) ram;
			aparms->PCIreg = (unsigned int) reg;
			aparms->PCIcfg = (unsigned int) cfg;
			break;
		 }
		if ((!aparms->PCIreg) || (!aparms->PCIcfg)) {
			printk(KERN_ERR "eicon_pci: Card could not be added !\n");
			pci_cards--;
		} else {
			aparms->mvalid = 1;
	
			sprintf(did, "%s%d", (strlen(ID) < 1) ? "eicon":ID, pci_cards);

			printk(KERN_INFO "eicon_pci: DriverID: '%s'\n", did);

			if (!(diehl_addcard(aparms->type, (int) aparms, aparms->irq, did))) {
				printk(KERN_ERR "eicon_pci: Card could not be added !\n");
				pci_cards--;
			}
		}
	}

  }
 } else
	printk(KERN_ERR "eicon_pci: Kernel compiled with PCI but no PCI-bios found !\n");
 return 0;
}

/*
 * Checks protocol file id for "F#xxxx" string fragment to
 * extract the features, supported by this protocol version.
 * binary representation of the feature string value is returned
 * in *value. The function returns 0 if feature string was not
 * found or has a wrong format, else 1.
 */
static int GetProtFeatureValue(char *sw_id, int *value)
{
  __u8 i, offset;

  while (*sw_id)
  {
    if ((sw_id[0] == 'F') && (sw_id[1] == '#'))
    {
      sw_id = &sw_id[2];
      for (i=0, *value=0; i<4; i++, sw_id++)
      {
        if ((*sw_id >= '0') && (*sw_id <= '9'))
        {
          offset = '0';
        }
        else if ((*sw_id >= 'A') && (*sw_id <= 'F'))
        {
          offset = 'A' + 10;
        }
        else if ((*sw_id >= 'a') && (*sw_id <= 'f'))
        {
          offset = 'a' + 10;
        }
        else
        {
          return 0;
        }
        *value |= (*sw_id - offset) << (4*(3-i));
      }
      return 1;
    }
    else
    {
      sw_id++;
    }
  }
  return 0;
}


void
diehl_pci_printpar(diehl_pci_card *card) {
        switch (card->type) {
                case DIEHL_CTYPE_MAESTRA:
			printk(KERN_INFO "%s at 0x%x / 0x%x, irq %d\n",
				diehl_ctype_name[card->type],
				(unsigned int)card->PCIreg,
				(unsigned int)card->PCIcfg,
				card->irq); 
			break;
                case DIEHL_CTYPE_MAESTRAQ:
                case DIEHL_CTYPE_MAESTRAQ_U:
                case DIEHL_CTYPE_MAESTRAP:
			printk(KERN_INFO "%s at 0x%x, irq %d\n",
				diehl_ctype_name[card->type],
				(unsigned int)card->shmem,
				card->irq); 
#ifdef DIEHL_PCI_DEBUG
		        printk(KERN_INFO "eicon_pci: remapped ram= 0x%x\n",(unsigned int)card->PCIram);
		        printk(KERN_INFO "eicon_pci: remapped reg= 0x%x\n",(unsigned int)card->PCIreg);
		        printk(KERN_INFO "eicon_pci: remapped cfg= 0x%x\n",(unsigned int)card->PCIcfg); 
#endif
			break;
	}
}


static void
diehl_pci_release_shmem(diehl_pci_card *card) {
	if (!card->master)
		return;
	if (card->mvalid) {
        	switch (card->type) {
                	case DIEHL_CTYPE_MAESTRA:
			        /* reset board */
				outb(0, card->PCIcfg + 0x4c);	/* disable interrupts from PLX */
				outb(0, card->PCIreg + M_RESET);
				SLEEP(20);
				outb(0, card->PCIreg + M_ADDRH);
				outw(0, card->PCIreg + M_ADDR);
				outw(0, card->PCIreg + M_DATA);

				release_region(card->PCIreg, 0x20);
				release_region(card->PCIcfg, 0x100);
				break;
                	case DIEHL_CTYPE_MAESTRAQ:
	                case DIEHL_CTYPE_MAESTRAQ_U:
	                case DIEHL_CTYPE_MAESTRAP:
			        /* reset board */
		        	writeb(_MP_RISC_RESET | _MP_LED1 | _MP_LED2, card->PCIreg + MP_RESET);
			        SLEEP(20);
			        writeb(0, card->PCIreg + MP_RESET);
			        SLEEP(20);

				iounmap((void *)card->shmem);
				iounmap((void *)card->PCIreg);
				iounmap((void *)card->PCIcfg);
				break;
		}
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
        struct sk_buff *skb, *skb2, *skb_new;
        diehl_pci_IND *ind, *ind2, *ind_new;
        diehl_chan *chan;

        if (!card) {
		if (DebugVar & 1)
	                printk(KERN_WARNING "eicon_pci_rcv_dispatch: NULL card!\n");
                return;
        }

	while((skb = skb_dequeue(&((diehl_card *)card->card)->rcvq))) {
        	ind = (diehl_pci_IND *)skb->data;

        	if ((chan = card->IdTable[ind->IndId]) == NULL) {
			if (DebugVar & 1)
		                printk(KERN_ERR "eicon_pci: Indication for unknown channel Ind=%d Id=%d\n", ind->Ind, ind->IndId);
	                dev_kfree_skb(skb);
	                continue;
	        }

		if (chan->e.complete) { /* check for rec-buffer chaining */
			if (ind->MLength == ind->RBuffer.length) {
				chan->e.complete = 1;
				idi_handle_ind(card, skb);
				continue;
			}
			else {
				chan->e.complete = 0;
				ind->Ind = ind->MInd;
				skb_queue_tail(&chan->e.R, skb);
				continue;
			}
		}
		else {
			if (!(skb2 = skb_dequeue(&chan->e.R))) {
				chan->e.complete = 1;
				if (DebugVar & 1)
	                		printk(KERN_ERR "eicon_pci: buffer incomplete, but 0 in queue\n");
	                	dev_kfree_skb(skb);
	                	dev_kfree_skb(skb2);
				continue;	
			}
	        	ind2 = (diehl_pci_IND *)skb2->data;
			skb_new = alloc_skb(((sizeof(diehl_pci_IND)-1)+ind->RBuffer.length+ind2->RBuffer.length),
					GFP_ATOMIC);
			ind_new = (diehl_pci_IND *)skb_put(skb_new,
					((sizeof(diehl_pci_IND)-1)+ind->RBuffer.length+ind2->RBuffer.length));
			ind_new->Ind = ind2->Ind;
			ind_new->IndId = ind2->IndId;
			ind_new->IndCh = ind2->IndCh;
			ind_new->MInd = ind2->MInd;
			ind_new->MLength = ind2->MLength;
			ind_new->RBuffer.length = ind2->RBuffer.length + ind->RBuffer.length;
			memcpy(&ind_new->RBuffer.P, &ind2->RBuffer.P, ind2->RBuffer.length);
			memcpy((&ind_new->RBuffer.P)+ind2->RBuffer.length, &ind->RBuffer.P, ind->RBuffer.length);
                	dev_kfree_skb(skb);
                	dev_kfree_skb(skb2);
			if (ind->MLength == ind->RBuffer.length) {
				chan->e.complete = 2;
				idi_handle_ind(card, skb_new);
				continue;
			}
			else {
				chan->e.complete = 0;
				skb_queue_tail(&chan->e.R, skb_new);
				continue;
			}
		}
	}
}

void
diehl_pci_ack_dispatch(diehl_pci_card *card) {
        struct sk_buff *skb;

        if (!card) {
		if (DebugVar & 1)
			printk(KERN_WARNING "eicon_pci_ack_dispatch: NULL card!\n");
                return;
        }
	while((skb = skb_dequeue(&((diehl_card *)card->card)->rackq))) {
		idi_handle_ack(card, skb);
	}
}


/*
 *  IO-Functions for different card-types
 */

u8 ram_inb(diehl_pci_card *card, void *adr) {
        u32 addr = (u32) adr;
        switch(card->type) {
                case DIEHL_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)card->PCIreg + M_ADDR);
                        return(inb((u16)card->PCIreg + M_DATA));
                case DIEHL_CTYPE_MAESTRAP:
                        return(readb(addr));
        }
 return(0);
}

u16 ram_inw(diehl_pci_card *card, void *adr) {
        u32 addr = (u32) adr;
        switch(card->type) {
                case DIEHL_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)card->PCIreg + M_ADDR);
                        return(inw((u16)card->PCIreg + M_DATA));
                case DIEHL_CTYPE_MAESTRAP:
                        return(readw(addr));
        }
 return(0);
}

void ram_outb(diehl_pci_card *card, void *adr, u8 data) {
        u32 addr = (u32) adr;
        switch(card->type) {
                case DIEHL_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)card->PCIreg + M_ADDR);
                        outb((u8)data, (u16)card->PCIreg + M_DATA);
                        break;
                case DIEHL_CTYPE_MAESTRAP:
                        writeb(data, addr);
                        break;
        }
}

void ram_outw(diehl_pci_card *card, void *adr , u16 data) {
        u32 addr = (u32) adr;
        switch(card->type) {
                case DIEHL_CTYPE_MAESTRA:
                        outw((u16)addr, (u16)card->PCIreg + M_ADDR);
                        outw((u16)data, (u16)card->PCIreg + M_DATA);
                        break;
                case DIEHL_CTYPE_MAESTRAP:
                        writew(data, addr);
                        break;
        }
}

void ram_copyfromcard(diehl_pci_card *card, void *adrto, void *adr, int len) {
        int i;
        switch(card->type) {
                case DIEHL_CTYPE_MAESTRA:
                        for(i = 0; i < len; i++) {
                                writeb(ram_inb(card, adr + i), adrto + i);
                        }
                        break;
                case DIEHL_CTYPE_MAESTRAP:
                        memcpy(adrto, adr, len);
                        break;
        }
}

void ram_copytocard(diehl_pci_card *card, void *adrto, void *adr, int len) {
        int i;
        switch(card->type) {
                case DIEHL_CTYPE_MAESTRA:
                        for(i = 0; i < len; i++) {
                                ram_outb(card, adrto + i, readb(adr + i));
                        }
                        break;
                case DIEHL_CTYPE_MAESTRAP:
                        memcpy(adrto, adr, len);
                        break;
        }
}

/*
 * Upload buffer content to adapters shared memory
 * on verify error, 1 is returned and a message is printed on screen
 * else 0 is returned
 * Can serve IO-Type and Memory type adapters
 */
int diehl_upload(t_dsp_download_space   *p_para,
            __u16                 length,   /* byte count */
            __u8                  *buffer,
            int                   verify)
{
  __u32               i, dwdata = 0, val = 0, timeout;
  __u16               data;
  diehl_pci_boot *boot = 0;

  switch (p_para->type) /* actions depend on type of union */
  {
    case DL_PARA_IO_TYPE:
      for (i=0; i<length; i+=2)
      {
	outb ((u8) ((p_para->dat.io.r3addr + i) >> 16), p_para->dat.io.ioADDRH);
	outw ((u16) (p_para->dat.io.r3addr + i), p_para->dat.io.ioADDR); 
	/* outw (((u16 *)code)[i >> 1], p_para->dat.io.ioDATA); */
	outw (*(u16 *)&buffer[i], p_para->dat.io.ioDATA); 
      }
      if (verify) /* check written block */
      {
        for (i=0; i<length; i+=2)
        {
	  outb ((u8) ((p_para->dat.io.r3addr + i) >> 16), p_para->dat.io.ioADDRH);
          outw ((u16) (p_para->dat.io.r3addr + i), p_para->dat.io.ioADDR); 
          data = inw(p_para->dat.io.ioDATA);
          if (data != *(u16 *)&buffer[i])
          {
            p_para->dat.io.r3addr  += i;
            p_para->dat.io.BadData  = data;
            p_para->dat.io.GoodData = *(u16 *)&buffer[i];
            return 1;
          }
        }
      }
      break;

    case DL_PARA_MEM_TYPE:
      boot = p_para->dat.mem.boot;
      writel(p_para->dat.mem.r3addr, &boot->addr);
      for (i=0; i<length; i+=4)
      {
        writel(((u32 *)buffer)[i >> 2], &boot->data[i]);
      }
      if (verify) /* check written block */
      {
        for (i=0; i<length; i+=4)
        {
          dwdata = readl(&boot->data[i]);
          if (((u32 *)buffer)[i >> 2] != dwdata)
          {
            p_para->dat.mem.r3addr  += i;
            p_para->dat.mem.BadData  = dwdata;
            p_para->dat.mem.GoodData = ((u32 *)buffer)[i >> 2];
            return 1;
          }
        }
      }
      writel(((length + 3) / 4), &boot->len);  /* len in dwords */
      writel(2, &boot->cmd);

	timeout = jiffies + 20;
	while (timeout > jiffies) {
		val = readl(&boot->cmd);
		if (!val) break;
		SLEEP(2);
	}
	if (val)
         {
		p_para->dat.mem.timeout = 1;
		return 1;
	 }
      break;
  }
  return 0;
}



/*
 *  Transmit-Function
 */
void
diehl_pci_transmit(diehl_pci_card *card) {
        struct sk_buff *skb;
        struct sk_buff *skb2;
        unsigned long flags;
        char *ram, *reg, *cfg;
	diehl_pci_pr_ram  *prram = 0;
	diehl_pci_REQ *ReqOut, *reqbuf;
	diehl_chan *chan;
	diehl_chan_ptr *chan2;
	int ReqCount;

        if (!card) {
		if (DebugVar & 1)
                	printk(KERN_WARNING "eicon_pci_transmit: NULL card!\n");
                return;
        }
        ram = (char *)card->PCIram;
        reg = (char *)card->PCIreg;
        cfg = (char *)card->PCIcfg;

	switch(card->type) {
		case DIEHL_CTYPE_MAESTRAP:
			prram = (diehl_pci_pr_ram *)ram;
			break;
		case DIEHL_CTYPE_MAESTRA:
			prram = 0;
			break;
	}

	ReqCount = 0;
	while((skb2 = skb_dequeue(&((diehl_card *)card->card)->sndq))) { 
                save_flags(flags);
                cli();
                if (!(ram_inb(card, &prram->ReqOutput) - ram_inb(card, &prram->ReqInput))) {
                        restore_flags(flags);
                        skb_queue_head(&((diehl_card *)card->card)->sndq, skb2);
			if (DebugVar & 32)
                        	printk(KERN_INFO "eicon_pci: transmit: Card not ready\n");
                        return;
                }
		restore_flags(flags);
		chan2 = (diehl_chan_ptr *)skb2->data;
		chan = chan2->ptr;
		if (!chan->e.busy) {
		 if((skb = skb_dequeue(&chan->e.X))) { 
                	save_flags(flags);
	                cli();
			reqbuf = (diehl_pci_REQ *)skb->data;
			/* get address of next available request buffer */
			ReqOut = (diehl_pci_REQ *)&prram->B[ram_inw(card, &prram->NextReq)];
			ram_outw(card, &ReqOut->XBuffer.length, reqbuf->XBuffer.length);
			ram_copytocard(card, &ReqOut->XBuffer.P, &reqbuf->XBuffer.P, reqbuf->XBuffer.length);
			ram_outb(card, &ReqOut->ReqCh, reqbuf->ReqCh);
			ram_outb(card, &ReqOut->Req, reqbuf->Req); 

			if (reqbuf->ReqId &0x1f) { /* if this is no ASSIGN */
				if (!reqbuf->Reference) {
					ram_outb(card, &ReqOut->ReqId, chan->e.D3Id); 
					chan->e.ReqCh = 0; 
				}
				else {
					ram_outb(card, &ReqOut->ReqId, chan->e.B2Id); 
					chan->e.ReqCh = 1;
					if (((reqbuf->Req & 0x0f) == 0x08) ||
					   ((reqbuf->Req & 0x0f) == 0x01)) { /* Send Data */
						chan->waitq = reqbuf->XBuffer.length;
						chan->waitpq += reqbuf->XBuffer.length;
					}
				}
			} else {	/* It is an ASSIGN */
				ram_outb(card, &ReqOut->ReqId, reqbuf->ReqId); 
				if (!reqbuf->Reference) 
					chan->e.ReqCh = 0; 
				 else
					chan->e.ReqCh = 1; 
			} 
		 	chan->e.ref = ram_inw(card, &ReqOut->Reference);
			chan->e.Req = reqbuf->Req;
			ReqCount++; 
			ram_outw(card, &prram->NextReq, ram_inw(card, &ReqOut->next)); 
			chan->e.busy = 1; 
			restore_flags(flags);
			if (DebugVar & 32)
	                	printk(KERN_DEBUG "eicon_pci: Req=%x,Id=%x,Ch=%x Len=%x\n", reqbuf->Req, 
							ram_inb(card, &ReqOut->ReqId),
							reqbuf->ReqCh, reqbuf->XBuffer.length); 
			dev_kfree_skb(skb);
		 }
		 dev_kfree_skb(skb2);
		} 
		else {
		skb_queue_tail(&((diehl_card *)card->card)->sackq, skb2);
		if (DebugVar & 32)
                	printk(KERN_INFO "eicon_pci: transmit: busy chan %d\n", chan->No); 
		}
	}
	ram_outb(card, &prram->ReqInput, (__u8)(ram_inb(card, &prram->ReqInput) + ReqCount)); 

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
	diehl_pci_pr_ram  *prram = 0;
        diehl_pci_RC *RcIn;
        diehl_pci_IND *IndIn;
	struct sk_buff *skb;
        int Count, Rc, Ind;

        if (!card) {
                printk(KERN_WARNING "eicon_pci_irq: spurious interrupt %d\n", irq);
                return;
        }

	ram = (char *)card->PCIram;
	reg = (char *)card->PCIreg;
	cfg = (char *)card->PCIcfg;

	switch(card->type) {
		case DIEHL_CTYPE_MAESTRAP:
			prram = (diehl_pci_pr_ram *)ram;
			break;
		case DIEHL_CTYPE_MAESTRA:
			prram = 0;
			break;
	}

	if (card->irqprobe) {
#ifdef DIEHL_PCI_DEBUG
 	        printk(KERN_INFO "eicon_pci: test interrupt routine ACK\n");
#endif		
		switch(card->type) {
			case DIEHL_CTYPE_MAESTRAP:
	        		if (readb(&ram[0x3fe])) { 
        		               	writeb(0, &prram->RcOutput);
				        writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
				        writew(0, &cfg[MP_IRQ_RESET + 2]);
					writeb(0, &ram[0x3fe]);
       			        } 
				break;
			case DIEHL_CTYPE_MAESTRA:
				outb(0x08, card->PCIreg + M_RESET);
				break;
		}
		card->irqprobe = 0;
		return;
	}

	switch(card->type) {
		case DIEHL_CTYPE_MAESTRAP:
			if (!(readb(&ram[0x3fe]))) { /* card did not interrupt */
				if (DebugVar & 1)
					printk(KERN_DEBUG "eicon_pci: IRQ: card tells no interrupt!\n");
				return;
			} 
			break;
		case DIEHL_CTYPE_MAESTRA:
			outw(0x3fe, card->PCIreg + M_ADDR);
			if (!(inb(card->PCIreg + M_DATA))) { /* card did not interrupt */
				if (DebugVar & 1)
					printk(KERN_DEBUG "eicon_pci: IRQ: card tells no interrupt!\n");
				return;
			} 
			break;
	}

        /* if return codes are available ...  */
        if((Count = ram_inb(card, &prram->RcOutput))) {
		diehl_pci_RC *ack;
                /* get the buffer address of the first return code */
                RcIn = (diehl_pci_RC *)&prram->B[ram_inw(card, &prram->NextRc)];
                /* for all return codes do ...  */
                while(Count--) {

                        if((Rc=ram_inb(card, &RcIn->Rc))) {
				skb = alloc_skb(sizeof(diehl_pci_RC), GFP_ATOMIC);
				ack = (diehl_pci_RC *)skb_put(skb, sizeof(diehl_pci_RC));
				ack->Rc = Rc;
				ack->RcId = ram_inb(card, &RcIn->RcId);
				ack->RcCh = ram_inb(card, &RcIn->RcCh);
				ack->Reference = ram_inw(card, &RcIn->Reference);
				if (DebugVar & 64)
	                        	printk(KERN_INFO "eicon_pci: IRQ Rc=%d Id=%d Ch=%d Ref=%d\n",
						Rc,ack->RcId,ack->RcCh,ack->Reference);
                        	ram_outb(card, &RcIn->Rc, 0);
				 skb_queue_tail(&((diehl_card *)card->card)->rackq, skb);
				 diehl_schedule_ack((diehl_card *)card->card);
                        }
                        /* get buffer address of next return code   */
                        RcIn = (diehl_pci_RC *)&prram->B[ram_inw(card, &RcIn->next)];
                }
                /* clear all return codes (no chaining!) */
                ram_outb(card, &prram->RcOutput, 0);
        }
        /* if indications are available ... */
        if((Count = ram_inb(card, &prram->IndOutput))) {
		diehl_pci_IND *ind;
                /* get the buffer address of the first indication */
                IndIn = (diehl_pci_IND *)&prram->B[ram_inw(card, &prram->NextInd)];
                /* for all indications do ... */
                while(Count--) {
			Ind = ram_inb(card, &IndIn->Ind);
			if(Ind) {
				int len = ram_inw(card, &IndIn->RBuffer.length);
				skb = alloc_skb((sizeof(diehl_pci_IND) + len - 1), GFP_ATOMIC);
				ind = (diehl_pci_IND *)skb_put(skb, (sizeof(diehl_pci_IND) + len - 1));
				ind->Ind = Ind;
				ind->IndId = ram_inb(card, &IndIn->IndId);
				ind->IndCh = ram_inb(card, &IndIn->IndCh);
				ind->MInd  = ram_inb(card, &IndIn->MInd);
				ind->MLength = ram_inw(card, &IndIn->MLength);
				ind->RBuffer.length = len;
				if (DebugVar & 64)
	                        	printk(KERN_INFO "eicon_pci: IRQ Ind=%d Id=%d Ch=%d MInd=%d MLen=%d Len=%d\n",
					Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,len);
                                ram_copyfromcard(card, &ind->RBuffer.P, &IndIn->RBuffer.P, len);
				skb_queue_tail(&((diehl_card *)card->card)->rcvq, skb);
				diehl_schedule_rx((diehl_card *)card->card);
				ram_outb(card, &IndIn->Ind, 0);
                        }
                        /* get buffer address of next indication  */
                        IndIn = (diehl_pci_IND *)&prram->B[ram_inw(card, &IndIn->next)];
                }
                ram_outb(card, &prram->IndOutput, 0);
        }

	/* clear interrupt */
	switch(card->type) {
		case DIEHL_CTYPE_MAESTRAP:
			writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
			writew(0, &cfg[MP_IRQ_RESET + 2]); 
			writeb(0, &ram[0x3fe]); 
			break;
		case DIEHL_CTYPE_MAESTRA:
			outb(0x08, card->PCIreg + M_RESET);
			outw(0x3fe, card->PCIreg + M_ADDR);
			outb(0, card->PCIreg + M_DATA);
			break;
	}

  return;
}


/* show header information of code file */
static
int diehl_pci_print_hdr(unsigned char *code, int offset)
{
  unsigned char hdr[80];
  int i, fvalue = 0;

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
   printk(KERN_DEBUG "eicon_pci: loading %s\n", hdr);
   if (GetProtFeatureValue(hdr, &fvalue)) return(fvalue);
    else return(0);
}


/*
 * Configure a card, download code into BRI card,
 * check if we get interrupts and return 0 on succes.
 * Return -ERRNO on failure.
 */
int
diehl_pci_load_bri(diehl_pci_card *card, diehl_pci_codebuf *cb) {
        int               i,j;
        int               timeout;
	unsigned int	  offset, offp=0, size, length;
	int		  signature = 0;
	int		  FeatureValue = 0;
        diehl_pci_codebuf cbuf;
	t_dsp_download_space dl_para;
	t_dsp_download_desc  dsp_download_table;
        unsigned char     *code;
	unsigned int	  reg;
	unsigned int	  cfg;

        if (copy_from_user(&cbuf, cb, sizeof(diehl_pci_codebuf)))
                return -EFAULT;

	reg = card->PCIreg;
	cfg = card->PCIcfg;

	/* reset board */
	outb(0, reg + M_RESET);
	SLEEP(10);
	outb(0, reg + M_ADDRH);
	outw(0, reg + M_ADDR);
	outw(0, reg + M_DATA);

#ifdef DIEHL_PCI_DEBUG
	 printk(KERN_DEBUG "eicon_pci: reset card\n");
#endif

	/* clear shared memory */
	outb(0xff, reg + M_ADDRH);
	outw(0, reg + M_ADDR);
	for(i = 0; i < 0xffff; i++) outw(0, reg + M_DATA);
	SLEEP(10);

#ifdef DIEHL_PCI_DEBUG
	 printk(KERN_DEBUG "eicon_pci: clear shared memory\n");
#endif

	/* download protocol and dsp file */

#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: downloading firmware...\n");
#endif

       	/* Allocate code-buffer */
       	if (!(code = kmalloc(400, GFP_KERNEL))) {
                printk(KERN_WARNING "eicon_pci_boot: Couldn't allocate code buffer\n");
       	        return -ENOMEM;
        }

	/* prepare protocol upload */
	dl_para.type		= DL_PARA_IO_TYPE;
	dl_para.dat.io.ioADDR	= reg + M_ADDR;
	dl_para.dat.io.ioADDRH	= reg + M_ADDRH;
	dl_para.dat.io.ioDATA	= reg + M_DATA;

	for (j = 0; j <= cbuf.dsp_code_num; j++) 
	 {	
	   if (j == 0)  size = cbuf.protocol_len;
	           else size = cbuf.dsp_code_len[j];

        	offset = 0;

		if (j == 0) dl_para.dat.io.r3addr = 0;
		if (j == 1) dl_para.dat.io.r3addr = M_DSP_CODE_BASE +
					((sizeof(__u32) + (sizeof(dsp_download_table) * 35) + 3) &0xfffffffc);
		if (j == 2) dl_para.dat.io.r3addr = M_DSP_CODE_BASE;
		if (j == 3) dl_para.dat.io.r3addr = M_DSP_CODE_BASE + sizeof(__u32);

           do  /* download block of up to 400 bytes */
            {
              length = ((size - offset) >= 400) ? 400 : (size - offset);

        	if (copy_from_user(code, (&cb->code) + offp + offset, length)) {
                	kfree(code);
	                return -EFAULT;
        	}

		if ((offset == 0) && (j < 2)) {
		       	FeatureValue = diehl_pci_print_hdr(code, j ? 0x00 : 0x80); 
#ifdef DIEHL_PCI_DEBUG
	if (FeatureValue) printk(KERN_DEBUG "eicon_pci: Feature Value : 0x%04x.\n", FeatureValue);
#endif
			if ((j==0) && (!(FeatureValue & PROTCAP_TELINDUS))) {
                  		printk(KERN_ERR "eicon_pci: Protocol Code cannot handle Telindus\n");
				kfree(code);
		                return -EFAULT;
			}
		}

		if (diehl_upload(&dl_para, length, code, 1))
		{
                  printk(KERN_ERR "eicon_pci: code block check failed at 0x%x !\n",dl_para.dat.io.r3addr);
		  kfree(code);
                  return -EIO;
		}
              /* move onto next block */
              offset += length;
	      dl_para.dat.io.r3addr += length;
            } while (offset < size);

#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: %d bytes loaded.\n", offset);
#endif
	offp += size;
	}
	kfree(code);	

	/* clear signature */
	outb(0xff, reg + M_ADDRH);
	outw(0x1e, reg + M_ADDR);
	outw(0, reg + M_DATA);

#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: copy configuration data into shared memory...\n");
#endif
	/* copy configuration data into shared memory */
	outw(8, reg + M_ADDR); outb(cbuf.tei, reg + M_DATA);
	outw(9, reg + M_ADDR); outb(cbuf.nt2, reg + M_DATA);
	outw(10,reg + M_ADDR); outb(0, reg + M_DATA);
	outw(11,reg + M_ADDR); outb(cbuf.WatchDog, reg + M_DATA);
	outw(12,reg + M_ADDR); outb(cbuf.Permanent, reg + M_DATA);
	outw(13,reg + M_ADDR); outb(0, reg + M_DATA);                 /* XInterface */
	outw(14,reg + M_ADDR); outb(cbuf.StableL2, reg + M_DATA);
	outw(15,reg + M_ADDR); outb(cbuf.NoOrderCheck, reg + M_DATA);
	outw(16,reg + M_ADDR); outb(0, reg + M_DATA);                 /* HandsetType */
	outw(17,reg + M_ADDR); outb(0, reg + M_DATA);                 /* SigFlags */
	outw(18,reg + M_ADDR); outb(cbuf.LowChannel, reg + M_DATA);
	outw(19,reg + M_ADDR); outb(cbuf.ProtVersion, reg + M_DATA);
	outw(20,reg + M_ADDR); outb(cbuf.Crc4, reg + M_DATA);
	outw(21,reg + M_ADDR); outb((cbuf.Loopback) ? 2:0, reg + M_DATA);

	for (i=0;i<32;i++)
	{
		outw( 32+i, reg + M_ADDR); outb(cbuf.l[0].oad[i], reg + M_DATA);
		outw( 64+i, reg + M_ADDR); outb(cbuf.l[0].osa[i], reg + M_DATA);
		outw( 96+i, reg + M_ADDR); outb(cbuf.l[0].spid[i], reg + M_DATA);
		outw(128+i, reg + M_ADDR); outb(cbuf.l[1].oad[i], reg + M_DATA);
		outw(160+i, reg + M_ADDR); outb(cbuf.l[1].osa[i], reg + M_DATA);
		outw(192+i, reg + M_ADDR); outb(cbuf.l[1].spid[i], reg + M_DATA);
	}

#ifdef DIEHL_PCI_DEBUG
           printk(KERN_ERR "eicon_pci: starting CPU...\n");
#endif
	/* let the CPU run */
	outw(0x08, reg + M_RESET);

        timeout = jiffies + (5*HZ);
        while (timeout > jiffies) {
	   outw(0x1e, reg + M_ADDR);	
           signature = inw(reg + M_DATA);
           if (signature == DIVAS_SIGNATURE) break;
           SLEEP(2);
         }
        if (signature != DIVAS_SIGNATURE)
         {
#ifdef DIEHL_PCI_DEBUG
           printk(KERN_ERR "eicon_pci: signature 0x%x expected 0x%x\n",signature,DIVAS_SIGNATURE);
#endif
           printk(KERN_ERR "eicon_pci: Timeout, protocol code not running !\n");
           return -EIO; 
         }
#ifdef DIEHL_PCI_DEBUG
        printk(KERN_DEBUG "eicon_pci: Protocol code running, signature OK\n");
#endif

        /* get serial number and number of channels supported by card */
	outb(0xff, reg + M_ADDRH);
	outw(0x3f6, reg + M_ADDR);
        card->channels = inw(reg + M_DATA);
        card->serial = (u32)inw(cfg + 0x22) << 16 | (u32)inw(cfg + 0x26);
        printk(KERN_INFO "eicon_pci: Supported channels : %d\n", card->channels);
        printk(KERN_INFO "eicon_pci: Card serial no. = %lu\n", card->serial);

        /* test interrupt */
        card->irqprobe = 1;

        if (!card->ivalid) {
                if (request_irq(card->irq, &diehl_pci_irq, 0, "Eicon PCI ISDN", card))
                 {
                  printk(KERN_ERR "eicon_pci: Couldn't request irq %d\n", card->irq);
                  return -EIO;
                 }
        }
        card->ivalid = 1;

#ifdef DIEHL_PCI_DEBUG
        printk(KERN_DEBUG "eicon_pci: testing interrupt\n");
#endif
        /* Trigger an interrupt and check if it is delivered */
        outb(0x41, cfg + 0x4c);		/* enable PLX for interrupts */
	outb(0x89, reg + M_RESET);	/* place int request */

        timeout = jiffies + 20;
        while (timeout > jiffies) {
          if (card->irqprobe != 1) break;
          SLEEP(5);
         }
        if (card->irqprobe == 1) {
           free_irq(card->irq, card); 
           card->ivalid = 0; 
           printk(KERN_ERR "eicon_pci: Getting no interrupts !\n");
           return -EIO;
         }

   /* initializing some variables */
   for(j=0; j<256; j++) card->IdTable[j] = NULL;
   for(j=0; j< (((diehl_card *)card->card)->nchannels + 1); j++) {
                ((diehl_card *)card->card)->bch[j].e.busy = 0;
                ((diehl_card *)card->card)->bch[j].e.D3Id = 0;
                ((diehl_card *)card->card)->bch[j].e.B2Id = 0;
                ((diehl_card *)card->card)->bch[j].e.ref = 0;
                ((diehl_card *)card->card)->bch[j].e.Req = 0;
                ((diehl_card *)card->card)->bch[j].e.complete = 1;
                ((diehl_card *)card->card)->bch[j].fsm_state = DIEHL_STATE_NULL;
   }

   printk(KERN_INFO "eicon_pci: Card successfully started\n");

 return 0;
}


/*
 * Configure a card, download code into PRI card,
 * check if we get interrupts and return 0 on succes.
 * Return -ERRNO on failure.
 */
int
diehl_pci_load_pri(diehl_pci_card *card, diehl_pci_codebuf *cb) {
        diehl_pci_boot    *boot;
	diehl_pci_pr_ram  *prram;
        int               i,j;
        int               timeout;
	int		  FeatureValue = 0;
	unsigned int	  offset, offp=0, size, length;
	unsigned long int signature = 0;
	t_dsp_download_space dl_para;
	t_dsp_download_desc  dsp_download_table;
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
           printk(KERN_ERR "eicon_pci: card is reset, but CPU not running !\n");
           return -EIO;
         }
#ifdef DIEHL_PCI_DEBUG
	 printk(KERN_DEBUG "eicon_pci: reset card OK (CPU running)\n");
#endif

	/* download firmware : DSP and Protocol */
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: downloading firmware...\n");
#endif

       	/* Allocate code-buffer */
       	if (!(code = kmalloc(400, GFP_KERNEL))) {
                printk(KERN_WARNING "eicon_pci_boot: Couldn't allocate code buffer\n");
       	        return -ENOMEM;
        }

	/* prepare protocol upload */
	dl_para.type		= DL_PARA_MEM_TYPE;
	dl_para.dat.mem.boot	= boot;

        for (j = 0; j <= cbuf.dsp_code_num; j++)
         {
	   if (j==0) size = cbuf.protocol_len;
		else size = cbuf.dsp_code_len[j];	

           if (j==1) writel(MP_DSP_ADDR, &boot->addr); /* DSP code entry point */

		if (j == 0) dl_para.dat.io.r3addr = MP_PROTOCOL_ADDR;
		if (j == 1) dl_para.dat.io.r3addr = MP_DSP_CODE_BASE +
					((sizeof(__u32) + (sizeof(dsp_download_table) * 35) + 3) &0xfffffffc);
		if (j == 2) dl_para.dat.io.r3addr = MP_DSP_CODE_BASE;
		if (j == 3) dl_para.dat.io.r3addr = MP_DSP_CODE_BASE + sizeof(__u32);

           offset = 0;
           do  /* download block of up to 400 bytes */
            {
              length = ((size - offset) >= 400) ? 400 : (size - offset);

        	if (copy_from_user(code, (&cb->code) + offp + offset, length)) {
                	kfree(code);
	                return -EFAULT;
        	}

		if ((offset == 0) && (j < 2)) {
	           	FeatureValue = diehl_pci_print_hdr(code, j ? 0x00 : 0x80); 
#ifdef DIEHL_PCI_DEBUG
	if (FeatureValue) printk(KERN_DEBUG "eicon_pci: Feature Value : 0x%x.\n", FeatureValue);
#endif
			if ((j==0) && (!(FeatureValue & PROTCAP_TELINDUS))) {
                  		printk(KERN_ERR "eicon_pci: Protocol Code cannot handle Telindus\n");
				kfree(code);
		                return -EFAULT;
			}
		}

		if (diehl_upload(&dl_para, length, code, 1))
		{
		  if (dl_para.dat.mem.timeout == 0)
	                  printk(KERN_ERR "eicon_pci: code block check failed at 0x%x !\n",dl_para.dat.io.r3addr);
			else
			  printk(KERN_ERR "eicon_pci: timeout, no ACK to load !\n");
		  kfree(code);
                  return -EIO;
		}

              /* move onto next block */
              offset += length;
	      dl_para.dat.mem.r3addr += length;
            } while (offset < size);
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: %d bytes loaded.\n", offset);
#endif
	 offp += size;
         }
	 kfree(code);	

	/* initialize the adapter data structure */
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: copy configuration data into shared memory...\n");
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
	printk(KERN_DEBUG "eicon_pci: configured card OK\n");
#endif

	/* start adapter */
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: tell card to start...\n");
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
           printk(KERN_ERR "eicon_pci: signature 0x%lx expected 0x%x\n",(signature >> 16),DIVAS_SIGNATURE);
#endif
           printk(KERN_ERR "eicon_pci: timeout, protocol code not running !\n");
           return -EIO;
         }
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: Protocol code running, signature OK\n");
#endif

	/* get serial number and number of channels supported by card */
        card->channels = readb(&ram[0x3f6]);
        card->serial = readl(&ram[0x3f0]);
        printk(KERN_INFO "eicon_pci: Supported channels : %d\n", card->channels);
        printk(KERN_INFO "eicon_pci: Card serial no. = %lu\n", card->serial);

	/* test interrupt */
	readb(&ram[0x3fe]);
        writeb(0, &ram[0x3fe]); /* reset any pending interrupt */
	readb(&ram[0x3fe]);

        writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
        writew(0, &cfg[MP_IRQ_RESET + 2]);

        card->irqprobe = 1;

	if (!card->ivalid) {
	        if (request_irq(card->irq, &diehl_pci_irq, 0, "Eicon PCI ISDN", card)) 
        	 {
	          printk(KERN_ERR "eicon_pci: Couldn't request irq %d\n", card->irq);
        	  return -EIO;
	         }
	}
	card->ivalid = 1;

        req_int = readb(&prram->ReadyInt);
#ifdef DIEHL_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: testing interrupt\n");
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
           printk(KERN_ERR "eicon_pci: Getting no interrupts !\n");
           return -EIO;
         }

   /* initializing some variables */
   for(j=0; j<256; j++) card->IdTable[j] = NULL;
   for(j=0; j< (((diehl_card *)card->card)->nchannels + 1); j++) {
		((diehl_card *)card->card)->bch[j].e.busy = 0;
		((diehl_card *)card->card)->bch[j].e.D3Id = 0;
		((diehl_card *)card->card)->bch[j].e.B2Id = 0;
		((diehl_card *)card->card)->bch[j].e.ref = 0;
		((diehl_card *)card->card)->bch[j].e.Req = 0;
                ((diehl_card *)card->card)->bch[j].e.complete = 1;
                ((diehl_card *)card->card)->bch[j].fsm_state = DIEHL_STATE_NULL;
   }

   printk(KERN_INFO "eicon_pci: Card successfully started\n");

 return 0;
}

#endif	/* CONFIG_PCI */

