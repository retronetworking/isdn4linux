/* $Id$

 * netjet.c     low level stuff for Traverse Technologie NETJet ISDN cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to Traverse Technologie Australia for documents and informations
 *
 *
 * $Log$
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/interrupt.h>

extern const char *CardType[];

const char *NETjet_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

/* PCI stuff */
#define PCI_VENDOR_TRAVERSE_TECH 0xe159
#define PCI_NETJET_ID	0x0001

#define NETJET_CTRL	0x00
#define NETJET_DMACTRL	0x01
#define NETJET_AUXCTRL	0x02
#define NETJET_AUXDATA	0x03
#define NETJET_IRQMASK0 0x04
#define NETJET_IRQMASK1 0x05
#define NETJET_IRQSTAT0 0x06
#define NETJET_IRQSTAT1 0x07
#define NETJET_DMA_READ_START	0x08
#define NETJET_DMA_READ_IRQ	0x0c
#define NETJET_DMA_READ_END	0x10
#define NETJET_DMA_READ_ADR	0x14
#define NETJET_DMA_WRITE_START	0x18
#define NETJET_DMA_WRITE_IRQ	0x1c
#define NETJET_DMA_WRITE_END	0x20
#define NETJET_DMA_WRITE_ADR	0x24
#define NETJET_PULSE_CNT	0x28

#define NETJET_ISAC_OFF	0xc0
#define NETJET_ISACIRQ	0x10

#define NETJET_DMA_SIZE 512

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	long flags;
	u_char ret;
	
	save_flags(flags);
	cli();
	cs->hw.njet.auxd &= 0xfc;
	cs->hw.njet.auxd |= (offset>>4) & 3;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	ret = bytein(cs->hw.njet.isac + ((offset & 0xf)<<2));
	restore_flags(flags);
	return(ret);
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	long flags;
	
	save_flags(flags);
	cli();
	cs->hw.njet.auxd &= 0xfc;
	cs->hw.njet.auxd |= (offset>>4) & 3;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	byteout(cs->hw.njet.isac + ((offset & 0xf)<<2), value);
	restore_flags(flags);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	cs->hw.njet.auxd &= 0xfc;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	insb(cs->hw.njet.isac, data, size);
}

static void 
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	cs->hw.njet.auxd &= 0xfc;
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
	outsb(cs->hw.njet.isac, data, size);
}

void fill_mem(u_int *start, u_int cnt, int chan, u_char fill)
{
	u_int mask=0x000000ff, val = 0, *p=start;
	u_int i;
	
	val |= fill;
	if (chan) {
		val  <<= 8;
		mask <<= 8;
	}
	mask ^= 0xffffffff;
	for (i=0; i<cnt; i++) {
		*p   &= mask;
		*p++ |= val;
	}
}

int testcnt=5000;

void
mode_tiger(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "Tiger mode %d bchan %d/%d",
			mode, bc, bcs->channel);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;

	switch (mode) {
		case (L1_MODE_NULL):
			fill_mem(cs->bcs[0].hw.tiger.send, NETJET_DMA_SIZE,
				bc, 0xff);
			if ((cs->bcs[0].mode == L1_MODE_NULL) &&
				(cs->bcs[1].mode == L1_MODE_NULL)) {
				cs->hw.njet.dmactrl = 0;
				byteout(cs->hw.njet.base + NETJET_DMACTRL,
					cs->hw.njet.dmactrl);
				byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
			}
			break;
		case (L1_MODE_TRANS):
			break;
		case (L1_MODE_HDLC): 
			testcnt=2;
			fill_mem(cs->bcs[0].hw.tiger.send, NETJET_DMA_SIZE,
				bc, 0xff);
			if (! cs->hw.njet.dmactrl) {
				fill_mem(cs->bcs[0].hw.tiger.send, NETJET_DMA_SIZE,
					!bc, 0xff);
				cs->hw.njet.dmactrl = 1;
				byteout(cs->hw.njet.base + NETJET_DMACTRL,
					cs->hw.njet.dmactrl);
				byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0x3f);
			}
			break;
	}
}

static u_char dummyrr(struct IsdnCardState *cs, int chan, u_char off)
{
	return(5);
}

static void dummywr(struct IsdnCardState *cs, int chan, u_char off, u_char value)
{
}

static void fill_dma(struct BCState *bcs)
{
	int count;
	
	if (!bcs->hw.tiger.tx_skb)
		return;
	dev_kfree_skb(bcs->hw.tiger.tx_skb, FREE_WRITE);
	bcs->hw.tiger.tx_skb = NULL;
	if (bcs->st->lli.l1writewakeup)
		bcs->st->lli.l1writewakeup(bcs->st);
	test_and_clear_bit(BC_FLG_BUSY, &bcs->st->l1.bcs->Flag);
	bcs->event |= 1 << B_XMTBUFREADY;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void
tiger_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA_REQ):
			save_flags(flags);
			cli();
			if (st->l1.bcs->hw.tiger.tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->hw.tiger.tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
				restore_flags(flags);
			}
			break;
		case (PH_PULL_IND):
			if (st->l1.bcs->hw.tiger.tx_skb) {
				printk(KERN_WARNING "tiger_l2l1: this shouldn't happen\n");
				break;
			}
			save_flags(flags);
			cli();
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			st->l1.bcs->hw.tiger.tx_skb = skb;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			restore_flags(flags);
			break;
		case (PH_PULL_REQ):
			if (!st->l1.bcs->hw.tiger.tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL_CNF, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
	}
}

void
close_tigerstate(struct BCState *bcs)
{
	struct sk_buff *skb;

	mode_tiger(bcs, 0, 0);
	if (test_bit(BC_FLG_INIT, &bcs->Flag)) {
		while ((skb = skb_dequeue(&bcs->rqueue))) {
			dev_kfree_skb(skb, FREE_READ);
		}
		while ((skb = skb_dequeue(&bcs->squeue))) {
			dev_kfree_skb(skb, FREE_WRITE);
		}
		if (bcs->hw.tiger.tx_skb) {
			dev_kfree_skb(bcs->hw.tiger.tx_skb, FREE_WRITE);
			bcs->hw.tiger.tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
	test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
}

static int
open_tigerstate(struct IsdnCardState *cs,
	      int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->hw.tiger.tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static void
tiger_manl1(struct PStack *st, int pr,
	  void *arg)
{
	switch (pr) {
		case (PH_ACTIVATE_REQ):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			mode_tiger(st->l1.bcs, st->l1.mode, st->l1.bc);
			st->l1.l1man(st, PH_ACTIVATE_CNF, NULL);
			break;
		case (PH_DEACTIVATE_REQ):
			if (!test_bit(BC_FLG_BUSY, &st->l1.bcs->Flag))
				mode_tiger(st->l1.bcs, 0, 0);
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			break;
	}
}

int
setstack_tiger(struct PStack *st, struct BCState *bcs)
{
	if (open_tigerstate(st->l1.hardware, bcs->channel))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = tiger_l2l1;
	st->ma.manl1 = tiger_manl1;
	setstack_manager(st);
	bcs->st = st;
	return (0);
}

 
__initfunc(void
inittiger(struct IsdnCardState *cs))
{
	char tmp[128];

	if (!(cs->bcs[0].hw.tiger.send = kmalloc(NETJET_DMA_SIZE * sizeof(unsigned int),
		GFP_ATOMIC | GFP_DMA))) {
		printk(KERN_WARNING
		       "HiSax: No memory for tiger.send\n");
		return;
	}
	cs->bcs[1].hw.tiger.send = cs->bcs[0].hw.tiger.send;
	sprintf(tmp, "tiger: send buf %x - %x", (u_int)cs->bcs[0].hw.tiger.send,
		(u_int)(cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE - 1));
	debugl1(cs, tmp);
	outl_p((u_int)cs->bcs[0].hw.tiger.send,
		cs->hw.njet.base + NETJET_DMA_WRITE_START);
	outl_p((u_int)(cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE/2),
		cs->hw.njet.base + NETJET_DMA_WRITE_IRQ);
	outl_p((u_int)(cs->bcs[0].hw.tiger.send + NETJET_DMA_SIZE -1),
		cs->hw.njet.base + NETJET_DMA_WRITE_END);
	if (!(cs->bcs[0].hw.tiger.rec = kmalloc(NETJET_DMA_SIZE * sizeof(unsigned int),
		GFP_ATOMIC | GFP_DMA))) {
		printk(KERN_WARNING
		       "HiSax: No memory for tiger.rec\n");
		return;
	}
	sprintf(tmp, "tiger: rec buf %x - %x", (u_int)cs->bcs[0].hw.tiger.rec,
		(u_int)(cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE - 1));
	debugl1(cs, tmp);
	cs->bcs[1].hw.tiger.rec = cs->bcs[0].hw.tiger.rec;
	outl_p((u_int)cs->bcs[0].hw.tiger.rec,
		cs->hw.njet.base + NETJET_DMA_READ_START);
	outl_p((u_int)(cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE/2),
		cs->hw.njet.base + NETJET_DMA_READ_IRQ);
	outl_p((u_int)(cs->bcs[0].hw.tiger.rec + NETJET_DMA_SIZE -1),
		cs->hw.njet.base + NETJET_DMA_READ_END);
	sprintf(tmp, "tiger: dmacfg  %x/%x  pulse=%d",
		inl_p(cs->hw.njet.base + NETJET_DMA_READ_ADR),
		inl_p(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
		bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
	debugl1(cs, tmp);
	cs->bcs[0].BC_SetStack = setstack_tiger;
	cs->bcs[1].BC_SetStack = setstack_tiger;
	cs->bcs[0].BC_Close = close_tigerstate;
	cs->bcs[1].BC_Close = close_tigerstate;
}

void
releasetiger(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.tiger.send) {
		kfree(cs->bcs[0].hw.tiger.send);
		cs->bcs[0].hw.tiger.send = NULL;
	}
	if (cs->bcs[1].hw.tiger.send) {
		cs->bcs[1].hw.tiger.send = NULL;
	}
	if (cs->bcs[0].hw.tiger.rec) {
		kfree(cs->bcs[0].hw.tiger.rec);
		cs->bcs[0].hw.tiger.rec = NULL;
	}
	if (cs->bcs[1].hw.tiger.rec) {
		cs->bcs[1].hw.tiger.rec = NULL;
	}
}

static void
netjet_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, sval, stat = 1;
	char tmp[128];

	if (!cs) {
		printk(KERN_WARNING "NETjet: Spurious interrupt!\n");
		return;
	}
	if (((sval = bytein(cs->hw.njet.base + NETJET_IRQSTAT1)) &
		NETJET_ISACIRQ) || stat) {
		val = ReadISAC(cs, ISAC_ISTA);
		sprintf(tmp, "tiger: i1 %x %x", sval, val);
		debugl1(cs, tmp);
		if (val) {
			isac_interrupt(cs, val);
			stat |= 2;
		}
	}
	if ((sval = bytein(cs->hw.njet.base + NETJET_IRQSTAT0))) {
		sprintf(tmp, "tiger: i0 %x   %x/%x  pulse=%d",
			sval, inl_p(cs->hw.njet.base + NETJET_DMA_READ_ADR),
			inl_p(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
			bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
		debugl1(cs, tmp);
	}
	if (!testcnt--) {
		cs->hw.njet.dmactrl = 0;
		byteout(cs->hw.njet.base + NETJET_DMACTRL,
			cs->hw.njet.dmactrl);
	}
	if (stat & 2) {
		WriteISAC(cs, ISAC_MASK, 0xFF);
		WriteISAC(cs, ISAC_MASK, 0x0);
	}
}

static void
reset_netjet(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	sti();
	cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
	schedule();
	cs->hw.njet.ctrl_reg = 0;  /* Reset Off */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
	schedule();
	restore_flags(flags);
	cs->hw.njet.auxd = 0;
	byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
}

void
release_io_netjet(struct IsdnCardState *cs)
{
	byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, 0);
	releasetiger(cs);
	release_region(cs->hw.njet.base, 256);
}


static int
NETjet_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_netjet(cs);
			return(0);
		case CARD_RELEASE:
			release_io_netjet(cs);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &netjet_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			inittiger(cs);
			clear_pending_isac_ints(cs);
			initisac(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}



static 	int pci_index __initdata = 0;

__initfunc(int
setup_netjet(struct IsdnCard *card))
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
#if CONFIG_PCI
	u_char pci_bus, pci_device_fn, pci_irq;
	u_int pci_ioaddr, found;
#endif

	strcpy(tmp, NETjet_revision);
	printk(KERN_INFO "HiSax: Traverse Tech. NETjet driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NETJET)
		return(0);
#if CONFIG_PCI
	found = 0;
	for (; pci_index < 0xff; pci_index++) {
		if (pcibios_find_device(PCI_VENDOR_TRAVERSE_TECH,
			PCI_NETJET_ID, pci_index, &pci_bus, &pci_device_fn)
			== PCIBIOS_SUCCESSFUL)
			found = 1;
		else
			break;
		/* get IRQ */
		pcibios_read_config_byte(pci_bus, pci_device_fn,
			PCI_INTERRUPT_LINE, &pci_irq);

		/* get IO address */
		pcibios_read_config_dword(pci_bus, pci_device_fn,
			PCI_BASE_ADDRESS_0, &pci_ioaddr);
		if (found)
			break;
	}
	if (!found) {
		printk(KERN_WARNING "NETjet: No PCI card found\n");
		return(0);
	}
	if (!pci_irq) {
		printk(KERN_WARNING "NETjet: No IRQ for PCI card found\n");
		return(0);
	}
	if (!pci_ioaddr) {
		printk(KERN_WARNING "NETjet: No IO-Adr for PCI card found\n");
		return(0);
	}
	pci_ioaddr &= ~3; /* remove io/mem flag */
	cs->hw.njet.base = pci_ioaddr; 
	cs->hw.njet.auxa = pci_ioaddr + NETJET_AUXDATA;
	cs->hw.njet.isac = pci_ioaddr | NETJET_ISAC_OFF;
	cs->irq = pci_irq;
	bytecnt = 256;
#else
	printk(KERN_WARNING "NETjet: NO_PCI_BIOS\n");
	printk(KERN_WARNING "NETjet: unable to config NETJET PCI\n");
	return (0);
#endif /* CONFIG_PCI */
	printk(KERN_INFO
		"NETjet: PCI card configured at 0x%x IRQ %d\n",
		cs->hw.njet.base, cs->irq);
	if (check_region(cs->hw.njet.base, bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.njet.base,
		       cs->hw.njet.base + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.njet.base, bytecnt, "netjet isdn");
	}
	reset_netjet(cs);
	cs->readisac  = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo  = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg  = &dummyrr;
	cs->BC_Write_Reg = &dummywr;
	cs->BC_Send_Data = &fill_dma;
	cs->cardmsg = &NETjet_card_msg;
	ISACVersion(cs, "NETjet:");
	return (1);
}
