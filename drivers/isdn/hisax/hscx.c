/* $Id$

 * hscx.c   HSCX specific routines
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

static char *HSCXVer[] =
{"A1", "?1", "A2", "?3", "A3", "V2.1", "?6", "?7",
 "?8", "?9", "?10", "?11", "?12", "?13", "?14", "???"};

int
HscxVersion(struct IsdnCardState *cs, char *s)
{
	int verA, verB;

	verA = cs->readhscx(cs, 0, HSCX_VSTR) & 0xf;
	verB = cs->readhscx(cs, 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "%s HSCX version A: %s  B: %s\n", s,
	       HSCXVer[verA], HSCXVer[verB]);
	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf))
		return (1);
	else
		return (0);
}

void
modehscx(struct HscxState *hs, int mode, int ichan)
{
	struct IsdnCardState *cs = hs->sp;
	int hscx = hs->hscx;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "hscx %c mode %d ichan %d",
			'A' + hscx, mode, ichan);
		debugl1(cs, tmp);
	}
	hs->mode = mode;
	cs->writehscx(cs, hscx, HSCX_CCR1, 0x85);
	cs->writehscx(cs, hscx, HSCX_XAD1, 0xFF);
	cs->writehscx(cs, hscx, HSCX_XAD2, 0xFF);
	cs->writehscx(cs, hscx, HSCX_RAH2, 0xFF);
	cs->writehscx(cs, hscx, HSCX_XBCH, 0x0);
	cs->writehscx(cs, hscx, HSCX_RLCR, 0x0);
	cs->writehscx(cs, hscx, HSCX_CCR2, 0x30);
	cs->writehscx(cs, hscx, HSCX_XCCR, 7);
	cs->writehscx(cs, hscx, HSCX_RCCR, 7);

	/* Switch IOM 1 SSI */
	if ((cs->HW_Flags & HW_IOM1) && (hscx == 0))
		ichan = 1 - ichan;

	if (ichan == 0) {
		cs->writehscx(cs, hscx, HSCX_TSAX,
			      (cs->HW_Flags & HW_IOM1) ? 0x7 : 0x2f);
		cs->writehscx(cs, hscx, HSCX_TSAR,
			      (cs->HW_Flags & HW_IOM1) ? 0x7 : 0x2f);
	} else {
		cs->writehscx(cs, hscx, HSCX_TSAX, 0x3);
		cs->writehscx(cs, hscx, HSCX_TSAR, 0x3);
	}
	switch (mode) {
		case (0):
			cs->writehscx(cs, hscx, HSCX_TSAX, 0xff);
			cs->writehscx(cs, hscx, HSCX_TSAR, 0xff);
			cs->writehscx(cs, hscx, HSCX_MODE, 0x84);
			break;
		case (1):
			cs->writehscx(cs, hscx, HSCX_MODE, 0xe4);
			break;
		case (2):
			cs->writehscx(cs, hscx, HSCX_MODE, 0x8c);
			break;
	}
	if (mode)
		cs->writehscx(cs, hscx, HSCX_CMDR, 0x41);
	cs->writehscx(cs, hscx, HSCX_ISTA, 0x00);
}

void
hscx_sched_event(struct HscxState *hsp, int event)
{
	hsp->event |= 1 << event;
	queue_task(&hsp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static void
hscx_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;
	long flags;

	switch (pr) {
		case (PH_DATA):
			save_flags(flags);
			cli();
			if (hsp->tx_skb) {
				skb_queue_tail(&hsp->squeue, skb);
				restore_flags(flags);
			} else {
				restore_flags(flags);
				hsp->tx_skb = skb;
				hsp->count = 0;
				sp->hscx_fill_fifo(hsp);
			}
			break;
		case (PH_DATA_PULLED):
			if (hsp->tx_skb) {
				printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
				break;
			}
			hsp->tx_skb = skb;
			hsp->count = 0;
			sp->hscx_fill_fifo(hsp);
			break;
		case (PH_REQUEST_PULL):
			if (!hsp->tx_skb) {
				st->l1.requestpull = 0;
				st->l1.l1l2(st, PH_PULL_ACK, NULL);
			} else
				st->l1.requestpull = !0;
			break;
	}

}

void
close_hscxstate(struct HscxState *hs)
{
	struct sk_buff *skb;

	modehscx(hs, 0, 0);
	hs->inuse = 0;
	if (hs->init) {
		if (hs->rcvbuf) {
			kfree(hs->rcvbuf);
			hs->rcvbuf = NULL;
		}
		while ((skb = skb_dequeue(&hs->rqueue))) {
			dev_kfree_skb(skb, FREE_READ);
		}
		while ((skb = skb_dequeue(&hs->squeue))) {
			dev_kfree_skb(skb, FREE_WRITE);
		}
		if (hs->tx_skb) {
			dev_kfree_skb(hs->tx_skb, FREE_WRITE);
			hs->tx_skb = NULL;
		}
	}
	hs->init = 0;
}

static int
open_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	if (!hsp->init) {
		if (!(hsp->rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for hscx_rcvbuf\n");
			return (1);
		}
		skb_queue_head_init(&hsp->rqueue);
		skb_queue_head_init(&hsp->squeue);
	}
	hsp->init = !0;

	hsp->tx_skb = NULL;
	hsp->event = 0;
	hsp->rcvidx = 0;
	hsp->tx_cnt = 0;
	return (0);
}

static void
hscx_manl1(struct PStack *st, int pr,
	   void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;

	switch (pr) {
		case (PH_ACTIVATE):
			hsp->active = !0;
			modehscx(hsp, st->l1.hscxmode, st->l1.hscxchannel);
			st->l1.l1man(st, PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE):
			if (!hsp->tx_skb)
				modehscx(hsp, 0, 0);

			hsp->active = 0;
			break;
	}
}

int
setstack_hscx(struct PStack *st, struct HscxState *hs)
{
	if (open_hscxstate(st->l1.hardware, hs->hscx))
		return (-1);

	st->l1.hscx = hs->hscx;
	st->l2.l2l1 = hscx_l2l1;
	st->ma.manl1 = hscx_manl1;
	setstack_manager(st);
	st->l1.act_state = 0;
	st->l1.requestpull = 0;

	hs->st = st;
	return (0);
}

void
clear_pending_hscx_ints(struct IsdnCardState *cs)
{
	int val;
	char tmp[64];

	val = cs->readhscx(cs, 1, HSCX_ISTA);
	sprintf(tmp, "HSCX B ISTA %x", val);
	debugl1(cs, tmp);
	if (val & 0x01) {
		val = cs->readhscx(cs, 1, HSCX_EXIR);
		sprintf(tmp, "HSCX B EXIR %x", val);
		debugl1(cs, tmp);
	} else if (val & 0x02) {
		val = cs->readhscx(cs, 0, HSCX_EXIR);
		sprintf(tmp, "HSCX A EXIR %x", val);
		debugl1(cs, tmp);
	}
	val = cs->readhscx(cs, 0, HSCX_ISTA);
	sprintf(tmp, "HSCX A ISTA %x", val);
	debugl1(cs, tmp);
	val = cs->readhscx(cs, 1, HSCX_STAR);
	sprintf(tmp, "HSCX B STAR %x", val);
	debugl1(cs, tmp);
	val = cs->readhscx(cs, 0, HSCX_STAR);
	sprintf(tmp, "HSCX A STAR %x", val);
	debugl1(cs, tmp);
	cs->writehscx(cs, 0, HSCX_MASK, 0xFF);
	cs->writehscx(cs, 1, HSCX_MASK, 0xFF);
	cs->writehscx(cs, 0, HSCX_MASK, 0);
	cs->writehscx(cs, 1, HSCX_MASK, 0);
}
