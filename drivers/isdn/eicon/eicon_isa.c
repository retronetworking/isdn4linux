/* $Id$
 *
 * ISDN low-level module for Eicon.Diehl active ISDN-Cards.
 * Hardware-specific code for old ISA cards.
 *
 * Copyright 1998    by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1998,99 by Armin Schindler (mac@topmail.de)
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
 * Revision 1.1  1999/01/01 18:09:43  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#include "eicon.h"
#include "eicon_isa.h"

#define check_shmem   check_region
#define release_shmem release_region
#define request_shmem request_region

char *diehl_isa_revision = "$Revision$";

/* Mask for detecting invalid IRQ parameter */
static int diehl_isa_valid_irq[] = {
	0x1c1c, /* 2, 3, 4, 10, 11, 12 */
	0x1c1c,
	0x1cbc, /* 2, 3, 4, 5, 7, 10, 11, 12 */
	0x1c1c  /* Quadro same as SX? */
};

/* Mask for detecting invalid membase parameter */   
static unsigned long diehl_isa_valid_mem[] = {
	0x1fff,
	0x1fff,
	0x7ff,
	0x1fff
};
     
#if 0
static void
diehl_isa_dumpdata(parm_buf *b) {
	char s[3024];
	char *p;
	int  i;

	for (i=0, p=s; i<b->len; i++) {
		sprintf(p,"%02x ",b->buf[i]);
		p += 3;
	}
	*p = 0;
	printk("DUMP: %s\n",s);
}
#endif

/*
 * IRQ handler
 */
static void
diehl_isa_irq(int irq, void *dev_id, struct pt_regs *regs) {
	diehl_isa_card *card = (diehl_isa_card *)dev_id;
	diehl_isa_com *com;
	unsigned char tmp;
	struct sk_buff *skb;

	if (!card) {
		printk(KERN_WARNING "eicon_isa_irq: spurious interrupt %d\n", irq);
		return;
	}
	com = &card->shmem->com;
	/* clear interrupt line */
	readb(&card->intack);
	if (card->irqprobe) {
		/* during IRQ-probe, just cont interrupts */
		writeb(0, &card->intack);
		writeb(0, &com->Rc);
		card->irqprobe++;
		return;
	}
	if ((tmp = readb(&com->Rc)) != 0) {
		diehl_ack *ack;
		if (DebugVar & 64)
			printk("diehl_int: Rc=%d\n", tmp);
		skb = alloc_skb(sizeof(diehl_ack), GFP_ATOMIC);
		ack = (diehl_ack *)skb_put(skb, sizeof(diehl_ack));
		ack->ret = tmp;
		ack->id = readb(&com->RcId);
		ack->ch = readb(&com->RcCh);
		if ((tmp & 0xf0) == 0xe0)
			writeb(0, &com->Req);
		writeb(0, &com->Rc);
		skb_queue_tail(&((diehl_card *)card->card)->rackq, skb);
		diehl_schedule_ack((diehl_card *)card->card);
	}
	if ((tmp = readb(&com->Ind)) != 0) {
		int len = readw(&com->RBuffer.len);
		diehl_indhdr *ind;

		if (DebugVar & 16)
			printk(KERN_DEBUG "eicon_ind Ind=%d\n", tmp);
		skb = alloc_skb(len+sizeof(diehl_indhdr), GFP_ATOMIC);
		skb_reserve(skb, sizeof(diehl_indhdr));
		ind = (diehl_indhdr *)skb_push(skb, sizeof(diehl_indhdr));
		ind->ret = tmp;
		ind->id = readb(&com->IndId);
		ind->ch = readb(&com->IndCh);
		ind->more = readb(&com->MInd);
		memcpy_fromio(skb_put(skb, len), &com->RBuffer.buf, len);
		writeb(0, &com->Ind);
		skb_queue_tail(&((diehl_card *)card->card)->rcvq, skb);
		diehl_schedule_rx((diehl_card *)card->card);
	}
	/* enable interrupt again */
	writeb(0, &card->intack);
}

static void
diehl_isa_release_shmem(diehl_isa_card *card) {
	if (!card->master)
		return;
	if (card->mvalid)
		release_shmem((unsigned long)card->shmem, card->ramsize);
	card->mvalid = 0;
}

static void
diehl_isa_release_irq(diehl_isa_card *card) {
	if (!card->master)
		return;
	if (card->ivalid)
		free_irq(card->irq, card);
	card->ivalid = 0;
}

void
diehl_isa_release(diehl_isa_card *card) {
	diehl_isa_release_irq(card);
	diehl_isa_release_shmem(card);
}

void
diehl_isa_printpar(diehl_isa_card *card) {
	switch (card->type) {
		case DIEHL_CTYPE_S:
		case DIEHL_CTYPE_SX:
		case DIEHL_CTYPE_SCOM:
		case DIEHL_CTYPE_QUADRO:
		case DIEHL_CTYPE_PRI:
			printk(KERN_INFO "%s at %08lx, irq %d\n",
			       diehl_ctype_name[card->type],
			       (unsigned long)card->shmem,
			       card->irq);
	}
}

void
diehl_isa_transmit(diehl_isa_card *card) {
	diehl_isa_com *com;
	struct sk_buff *skb;
	unsigned long(flags);
	diehl_req *reqbuf;

	if (!card) {
		printk(KERN_WARNING "eicon_isa_transmit: NULL card!\n");
		return;
	}
	com = &card->shmem->com;
	while ((skb = skb_dequeue(&((diehl_card *)card->card)->sndq))) {
		save_flags(flags);
		cli();
		if (readb(&com->Req) || (readb(&com->XLock))) {
			restore_flags(flags);
			skb_queue_head(&((diehl_card *)card->card)->sndq, skb);
			return;
		}
		writeb(1, &com->XLock);
		restore_flags(flags);
		/* No fragmentation should be necessary here */
		reqbuf = (diehl_req *)skb->data;
		skb_pull(skb, sizeof(diehl_req));
		if (skb->len > 269) {
			if (DebugVar & 1)
				printk(KERN_WARNING "eicon_isa_transmit: skb > 269 bytes!!!\n");
			writeb(0, &com->XLock);
			return;
		}
		writew(skb->len, &com->XBuffer.len);
		memcpy_toio(&com->XBuffer.buf, skb->data, skb->len);
		writeb(reqbuf->ch, &com->ReqCh);
		writeb(reqbuf->id, &com->ReqId);
		writeb(reqbuf->code, &com->Req);
		skb_push(skb, sizeof(diehl_req));
		/* Queue packet for later ACK */
		skb_queue_tail(&((diehl_card *)card->card)->sackq, skb);
	}
}

/*
 * Configure a card, download code into card,
 * check if we get interrupts and return card-type on succes.
 * Return -ERRNO on failure.
 */
int
diehl_isa_load(diehl_isa_card *card, diehl_isa_codebuf *cb) {
	diehl_isa_boot    *boot;
	int               tmp;
	int               primary;
	int               cprimary;
	int               timeout;
	diehl_isa_codebuf cbuf;
	unsigned char     *code;
	unsigned char     *p;

	if (copy_from_user(&cbuf, cb, sizeof(diehl_isa_codebuf)))
		return -EFAULT;
	/* Allocate code-buffer and copy code from userspace */
	if (cbuf.bootstrap_len > 1024) {
		printk(KERN_WARNING "eicon_isa_boot: Invalid bootstrap size %ld\n",
		       cbuf.bootstrap_len);
		return -EINVAL;
	}
	if ((cbuf.boot_opt != DIEHL_ISA_BOOT_NORMAL) &&
	    (cbuf.boot_opt != DIEHL_ISA_BOOT_MEMCHK)) {
		printk(KERN_WARNING "eicon_isa_boot: Invalid bootstrap option %d\n",
		       cbuf.boot_opt);
		return -EINVAL;
	}
	if (!(code = kmalloc(cbuf.bootstrap_len + cbuf.firmware_len, GFP_KERNEL))) {
		printk(KERN_WARNING "eicon_isa_boot: Couldn't allocate code buffer\n");
		return -ENOMEM;
	}
	if (copy_from_user(code, &cb->code, cbuf.bootstrap_len + cbuf.firmware_len)) {
		kfree(code);
		return -EFAULT;
	}
	switch (card->type & 0x0f) {
		case DIEHL_CTYPE_S:
		case DIEHL_CTYPE_SX:
		case DIEHL_CTYPE_SCOM:
		case DIEHL_CTYPE_QUADRO:
			card->ramsize  = RAMSIZE;
			card->intack   = (__u8 *)card->shmem + INTACK;
			card->startcpu = (__u8 *)card->shmem + STARTCPU;
			card->stopcpu  = (__u8 *)card->shmem + STOPCPU;
			primary = 0;
			break;
		case DIEHL_CTYPE_PRI:
			card->ramsize  = RAMSIZE_P;
			card->intack   = (__u8 *)card->shmem + INTACK_P;
			card->startcpu = (__u8 *)card->shmem + STARTCPU_P;
			card->stopcpu  = (__u8 *)card->shmem + STOPCPU_P;
			primary = 1;
			break;
		default:
			printk(KERN_WARNING "eicon_isa_boot: Unknown card type %d\n", card->type);
			kfree(code);
			return -EINVAL;
	}
        if ((!card->mvalid) && card->master) {
		/* Check for valid shmem address */
		if (((unsigned long)card->shmem < 0x0c0000) ||
		    ((unsigned long)card->shmem > 0xde000) ||
		    ((unsigned long)card->shmem & diehl_isa_valid_mem[card->type & 0x0f])) {
			printk(KERN_WARNING "eicon_isa_boot: illegal shmem: 0x%08lx\n",
			       (unsigned long)card->shmem);
			kfree(code);
			return -EINVAL;
		}
		/* Register shmem */
                if (check_shmem((unsigned long)card->shmem, card->ramsize)) {
                        printk(KERN_WARNING "eicon_isa_boot: memory at 0x%08lx already in use.\n",
                               (unsigned long)card->shmem);
			kfree(code);
                        return -EBUSY;
                }
                request_shmem((unsigned long)card->shmem, card->ramsize, "Diehl ISA ISDN");
                card->mvalid = 1;
        }

	card->irqprobe = 1;
	if ((!card->ivalid) && card->master) {
		/* Check for valid IRQ */
		if ((card->irq < 0) || (card->irq > 15) || 
		    (!((1 << card->irq) & diehl_isa_valid_irq[card->type & 0x0f]))) {
			printk(KERN_WARNING "eicon_isa_boot: illegal irq: %d\n", card->irq);
			diehl_isa_release_shmem(card);
			kfree(code);
			return -EINVAL;
		}
		/* Register irq */
		if (!request_irq(card->irq, &diehl_isa_irq, 0, "Eicon ISA ISDN", card))
			card->ivalid = 1;
		else {
			printk(KERN_WARNING "eicon_isa_boot: irq %d already in use.\n",
			       card->irq);
			diehl_isa_release_shmem(card);
			kfree(code);
			return -EBUSY;
		}
	}

	/* Check for PRI adapter */
	cprimary  = 0;
	writew(0x55aa, (unsigned long)card->shmem + 0x402);
	if (readw((unsigned long)card->shmem + 0x402) == 0x55aa) {
		writew(0, (unsigned long)card->shmem + 0x402);
		if (readw((unsigned long)card->shmem + 0x402) == 0)
			cprimary = 1;
	}
	if (cprimary != primary) {
		printk(KERN_WARNING "eicon_isa_boot: PRI check failed %d %d\n", primary, cprimary);
		diehl_isa_release(card);
		kfree(code);
		return -EIO;
	}

	/* clear any pending irq's */
	readb(card->intack);
	/* set reset-line active */
	writeb(0, card->stopcpu);
	/* clear irq-requests */
	writeb(0, card->intack);
	readb(card->intack);
		
	/* Copy code into card */
	memcpy_toio(&card->shmem->c, code, cbuf.bootstrap_len);

	/* if 16k-ramsize, duplicate the reset-jump-code */
	if (card->ramsize == 0x4000)
		memcpy_toio((__u8 *)card->shmem + 0x3ff0, &code[0x3f0], 12);
		
	/* Check for properly loaded code */
	if (!check_signature((unsigned long)&card->shmem->c, code, 1020)) {
		printk(KERN_WARNING "eicon_isa_boot: Could not load bootcode\n");
		diehl_isa_release(card);
		kfree(code);
		return -EIO;
	}
	boot = &card->shmem->boot;

	/* Delay 0.2 sec. */
	SLEEP(20);
		
	/* Set Bootstrap flags */
	writeb(cbuf.boot_opt, &boot->ctrl);
		
	/* Start CPU */
	writeb(0, card->startcpu);

	/* Delay 0.2 sec. */
	SLEEP(20);

	if ((readb(&boot->ctrl) == 1) || (readb(&boot->ctrl) == 2)) {
		printk(KERN_WARNING "eicon_isa_boot: CPU start failed\n");
		diehl_isa_release(card);
		kfree(code);
		return -EIO;
	}

	if (cbuf.boot_opt == DIEHL_ISA_BOOT_MEMCHK)
		printk(KERN_INFO "Testing Adapter memory ...\n");
	/* Wait max 22 sec for bootstrap/memtest finished */
	timeout = jiffies + (HZ * 22);
	while (timeout > jiffies) {
		if (readb(&boot->ctrl) == 0)
			break;
		SLEEP(10);
	}
	if (readb(&boot->ctrl) != 0) {
		printk(KERN_WARNING "eicon_isa_boot: CPU test failed\n");
		diehl_isa_release(card);
		kfree(code);
		return -EIO;
	}

	/* Check for memory-test errors */
	if (readw(&boot->ebit)) {
		printk(KERN_WARNING "eicon_isa_boot: memory test failed (bit 0x%04x at 0x%08x)\n",
		       readw(&boot->ebit), readl(&boot->eloc));
		diehl_isa_release(card);
		kfree(code);
		return -EIO;
	}

	/* Check card type and memory size */
	if ((tmp = readb(&boot->card)) != card->type) {
		printk(KERN_WARNING "Card type mismatch %d != %d\n",
		       tmp, card->type);
		diehl_isa_release(card);
		kfree(code);
		return -EINVAL;
	}
	tmp = readb(&boot->msize);
	if (tmp != 8 && tmp != 16 && tmp != 24 &&
	    tmp != 32 && tmp != 48 && tmp != 60) {
		printk(KERN_WARNING "eicon_isa_boot: invalid memsize\n");
		diehl_isa_release(card);
		kfree(code);
		return -EIO;
	}

	/* Download firmware */
	printk(KERN_INFO "eicon_isa_boot: Eicon %s %dkB, loading firmware ...\n", 
	       diehl_ctype_name[card->type],
	       tmp * 16);
	tmp = cbuf.firmware_len >> 8;
	p = code;
	p += cbuf.bootstrap_len;
	while (tmp--) {
		memcpy_toio(&boot->b, p, 256);
		writeb(1, &boot->ctrl);
		timeout = jiffies + 10;
		while (timeout > jiffies) {
			if (readb(&boot->ctrl) == 0)
				break;
			SLEEP(2);
		}
		if (readb(&boot->ctrl)) {
			printk(KERN_WARNING "eicon_isa_boot: dowload timeout\n");
			diehl_isa_release(card);
			kfree(code);
			return -EIO;
		}
		p += 256;
	}
	kfree(code);

	/* Initialize firmware parameters */
	memcpy_toio(&card->shmem->c[8], &cbuf.tei, 14);
	memcpy_toio(&card->shmem->c[32], &cbuf.oad, 96);
	memcpy_toio(&card->shmem->c[128], &cbuf.oad, 96);
	
	/* Start firmware, wait for signature */
	writeb(2, &boot->ctrl);
	timeout = jiffies + (5*HZ);
	while (timeout > jiffies) {
		if (readw(&boot->signature) == 0x4447)
			break;
		SLEEP(2);
	}
	if (readw(&boot->signature) != 0x4447) {
		printk(KERN_WARNING "eicon_isa_boot: firmware selftest failed %04x\n",
		       readw(&boot->signature));
		diehl_isa_release(card);
		return -EIO;
	}

	/* clear irq-requests, reset irq-count */
	writeb(0, card->intack);
	readb(card->intack);
	card->irqprobe = 1;

	/* Trigger an interrupt and check if it is delivered */
	writeb(1, &card->shmem->com.ReadyInt);
	timeout = jiffies + 20;
	while (timeout > jiffies) {
		if (card->irqprobe > 1)
			break;
		SLEEP(2);
	}
	if (card->irqprobe == 1) {
		printk(KERN_WARNING "eicon_isa_boot: IRQ test failed\n");
		diehl_isa_release(card);
		return -EIO;
	}

	/* Enable normal IRQ processing */
	card->irqprobe = 0;
	return 0;
}
