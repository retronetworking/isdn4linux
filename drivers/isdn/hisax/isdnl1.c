/* $Id$

 * isdnl1.c     common low level stuff for Siemens Chipsetbased isdn cards
 *              based on the teles driver from Jan den Ouden
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 *
 * $Log$
 * Revision 2.6  1997/09/12 10:05:16  keil
 * ISDN_CTRL_DEBUG define
 *
 * Revision 2.5  1997/09/11 17:24:45  keil
 * Add new cards
 *
 * Revision 2.4  1997/08/15 17:47:09  keil
 * avoid oops because a uninitialised timer
 *
 * Revision 2.3  1997/08/01 11:16:40  keil
 * cosmetics
 *
 * Revision 2.2  1997/07/30 17:11:08  keil
 * L1deactivated exported
 *
 * Revision 2.1  1997/07/27 21:35:38  keil
 * new layer1 interface
 *
 * Revision 2.0  1997/06/26 11:02:53  keil
 * New Layer and card interface
 *
 * Revision 1.15  1997/05/27 15:17:55  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.14  1997/04/07 23:00:08  keil
 * GFP_KERNEL ---> GFP_ATOMIC
 *
 * Revision 1.13  1997/04/06 22:55:50  keil
 * Using SKB's
 *
 * old changes removed KKe
 *
 */

const char *l1_revision = "$Revision$";

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isdnl1.h"

#if CARD_TELES0
#include "teles0.h"
#endif

#if CARD_TELES3
#include "teles3.h"
#endif

#if CARD_AVM_A1
#include "avm_a1.h"
#endif

#if CARD_ELSA
#include "elsa.h"
#endif

#if CARD_IX1MICROR2
#include "ix1_micro.h"
#endif

#if CARD_DIEHLDIVA
#include "diva.h"
#endif

#if CARD_DYNALINK
#include "dynalink.h"
#endif

#if CARD_TELEINT
#include "teleint.h"
#endif

#if CARD_SEDLBAUER
#include "sedlbauer.h"
#endif

/* #define I4L_IRQ_FLAG SA_INTERRUPT */
#define I4L_IRQ_FLAG    0

#define HISAX_STATUS_BUFSIZE 4096
#define ISDN_CTRL_DEBUG 1
#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>
#include <linux/interrupt.h>

const char *CardType[] =
{"No Card", "Teles 16.0", "Teles 8.0", "Teles 16.3", "Creatix/Teles PnP",
 "AVM A1", "Elsa ML", "Elsa Quickstep", "Teles PCMCIA", "ITK ix1-micro Rev.2",
 "Elsa PCMCIA", "Eicon.Diehl Diva", "ISDNLink", "TeleInt", "Teles 16.3c", "Sedlbauer Speed Card"
};

extern struct IsdnCard cards[];
extern int nrcards;
extern char *HiSax_id;
extern struct IsdnBuffers *tracebuf;

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState
*
hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].cs)
			if (cards[i].cs->myid == driverid)
				return (cards[i].cs);
	return (struct IsdnCardState *) 0;
}

int
HiSax_readstatus(u_char * buf, int len, int user, int id, int channel)
{
	int count;
	u_char *p;
	struct IsdnCardState *csta = hisax_findcard(id);

	if (csta) {
		for (p = buf, count = 0; count < len; p++, count++) {
			if (user)
				put_user(*csta->status_read++, p);
			else
				*p++ = *csta->status_read++;
			if (csta->status_read > csta->status_end)
				csta->status_read = csta->status_buf;
		}
		return count;
	} else {
		printk(KERN_ERR
		 "HiSax: if_readstatus called with invalid driverId!\n");
		return -ENODEV;
	}
}

void
HiSax_putstatus(struct IsdnCardState *csta, char *buf)
{
#if ISDN_CTRL_DEBUG
	long flags;
	int len, count, i;
	u_char *p;
	isdn_ctrl ic;

	save_flags(flags);
	cli();
	count = 0;
	len = strlen(buf);

	if (!csta) {
		printk(KERN_WARNING "HiSax: No CardStatus for message %s", buf);
		restore_flags(flags);
		return;
	}
	for (p = buf, i = len; i > 0; i--, p++) {
		*csta->status_write++ = *p;
		if (csta->status_write > csta->status_end)
			csta->status_write = csta->status_buf;
		count++;
	}
	restore_flags(flags);
	if (count) {
		ic.command = ISDN_STAT_STAVAIL;
		ic.driver = csta->myid;
		ic.arg = count;
		csta->iif.statcallb(&ic);
	}
#else
	printk(KERN_WARNING "%s", buf);
#endif
}

int
ll_run(struct IsdnCardState *csta)
{
	long flags;
	isdn_ctrl ic;

	save_flags(flags);
	cli();
	ic.driver = csta->myid;
	ic.command = ISDN_STAT_RUN;
	csta->iif.statcallb(&ic);
	restore_flags(flags);
	return 0;
}

void
ll_stop(struct IsdnCardState *csta)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_STOP;
	ic.driver = csta->myid;
	csta->iif.statcallb(&ic);
	CallcFreeChan(csta);
}

static void
ll_unload(struct IsdnCardState *csta)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_UNLOAD;
	ic.driver = csta->myid;
	csta->iif.statcallb(&ic);
	if (csta->status_buf)
		kfree(csta->status_buf);
	csta->status_read = NULL;
	csta->status_write = NULL;
	csta->status_end = NULL;
	kfree(csta->dlogspace);
}

void
debugl1(struct IsdnCardState *cs, char *msg)
{
	char tmp[256], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Card %d %s\n", tm, cs->cardnr + 1, msg);
	HiSax_putstatus(cs, tmp);
}

void
L1activated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		if (st->l1.act_state == 1)
			st->l1.act_state = 2;
		st->l1.l1man(st, PH_ACTIVATE, NULL);
		st = st->next;
	}
}

void
L1deactivated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		st->l1.act_state = 0;
		st->l1.l1man(st, PH_DEACTIVATE, NULL);
		st = st->next;
	}
}

int
L1act_wanted(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st)
		if (st->l1.act_state)
			return (!0);
		else
			st = st->next;
	return (0);
}

static void
DChannel_proc_xmt(struct IsdnCardState *cs)
{
	struct PStack *stptr;

	if (cs->tx_skb)
		return;

	stptr = cs->stlist;
	while (stptr != NULL)
		if (stptr->l1.requestpull) {
			stptr->l1.requestpull = 0;
			stptr->l1.l1l2(stptr, PH_PULL_ACK, NULL);
			break;
		} else
			stptr = stptr->next;
}

static void
DChannel_proc_rcv(struct IsdnCardState *cs)
{
	struct sk_buff *skb, *nskb;
	struct PStack *stptr;
	int found, tei, sapi;
	char tmp[64];

	if (cs->HW_Flags & FLG_L1TIMER_ACT) {
		del_timer(&cs->l1timer);
		cs->HW_Flags &= ~FLG_L1TIMER;
		L1activated(cs);
	}
	while ((skb = skb_dequeue(&cs->rq))) {
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA", 1);
#endif
		stptr = cs->stlist;
		sapi = skb->data[0] >> 2;
		tei = skb->data[1] >> 1;

		if (tei == GROUP_TEI) {
			if (sapi == CTRL_SAPI) {	/* sapi 0 */
				cs->CallFlags = 3;
				if (cs->dlogflag) {
					LogFrame(cs, skb->data, skb->len);
					dlogframe(cs, skb->data + 3, skb->len - 3,
						  "Q.931 frame network->user broadcast");
				}
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1l2(stptr, PH_DATA, nskb);
					else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			} else if (sapi == TEI_SAPI) {
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1tei(stptr, PH_DATA, nskb);
					else
						printk(KERN_WARNING "HiSax: tei broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			}
			dev_kfree_skb(skb, FREE_READ);
		} else if (sapi == CTRL_SAPI) {
			found = 0;
			while (stptr != NULL)
				if (tei == stptr->l2.tei) {
					stptr->l1.l1l2(stptr, PH_DATA, skb);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found) {
				/* BD 10.10.95
				 * Print out D-Channel msg not processed
				 * by isdn4linux
				 */

				if ((!(skb->data[0] >> 2)) && (!(skb->data[2] & 0x01))) {
					sprintf(tmp,
						"Q.931 frame network->user with tei %d (not for us)",
						skb->data[1] >> 1);
					LogFrame(cs, skb->data, skb->len);
					dlogframe(cs, skb->data + 4, skb->len - 4, tmp);
				}
				dev_kfree_skb(skb, FREE_READ);
			}
		}
	}
}

static void
DChannel_bh(struct IsdnCardState *cs)
{
	if (!cs)
		return;

	if (test_and_clear_bit(L1_PH_ACT, &cs->event))
		if (cs->HW_Flags & FLG_L1TIMER_DEACT) {
			del_timer(&cs->l1timer);
			cs->HW_Flags &= ~FLG_L1TIMER_DEACT;
		} else {
			if (cs->HW_Flags & FLG_L1TIMER) {
				del_timer(&cs->l1timer);
				cs->HW_Flags &= ~FLG_L1TIMER;
			}
			cs->HW_Flags |= FLG_L1TIMER_ACT;
			init_timer(&cs->l1timer);
			cs->l1timer.expires = jiffies + ((110 * HZ) / 1000);
			add_timer(&cs->l1timer);
		}
	if (test_and_clear_bit(L1_PH_DEACT, &cs->event))
		if (L1act_wanted(cs)) {
			if (cs->HW_Flags & FLG_L1TIMER) {
				del_timer(&cs->l1timer);
				cs->HW_Flags &= ~FLG_L1TIMER;
			}
			cs->HW_Flags |= FLG_L1TIMER_DEACT;
			init_timer(&cs->l1timer);
			cs->l1timer.expires = jiffies + ((600 * HZ) / 1000);
			add_timer(&cs->l1timer);
		} else
			L1deactivated(cs);

	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
}

static void
L1_timer_handler(struct IsdnCardState *cs)
{
	if (cs->HW_Flags & FLG_L1TIMER_DEACT) {
		cs->HW_Flags &= ~FLG_L1TIMER_DEACT;
		L1deactivated(cs);
	} else if (cs->HW_Flags & FLG_L1TIMER_ACT) {
		cs->HW_Flags &= ~FLG_L1TIMER_ACT;
		L1activated(cs);
	} else if (cs->HW_Flags & FLG_L1TIMER_DBUSY) {
		cs->HW_Flags &= ~FLG_L1TIMER_DBUSY;
		debugl1(cs, "D-Channel Busy");
	}
}

static void
BChannel_proc_xmt(struct BCState *bcs)
{
	struct PStack *st = bcs->st;

	if (bcs->Flag & BC_FLG_BUSY)
		return;

	if (st->l1.requestpull) {
		st->l1.requestpull = 0;
		st->l1.l1l2(st, PH_PULL_ACK, NULL);
	}
	if (!(bcs->Flag & BC_FLG_ACTIV))
		if (!(bcs->Flag & BC_FLG_BUSY) && (!skb_queue_len(&bcs->squeue)))
			st->ma.manl1(st, PH_DEACTIVATE, 0);
}

static void
BChannel_proc_rcv(struct BCState *bcs)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&bcs->rqueue))) {
		bcs->st->l1.l1l2(bcs->st, PH_DATA, skb);
	}
}

static void
BChannel_bh(struct BCState *bcs)
{
	if (!bcs)
		return;
	if (test_and_clear_bit(B_RCVBUFREADY, &bcs->event))
		BChannel_proc_rcv(bcs);
	if (test_and_clear_bit(B_XMTBUFREADY, &bcs->event))
		BChannel_proc_xmt(bcs);
}

void
HiSax_addlist(struct IsdnCardState *cs,
	      struct PStack *st)
{
	st->next = cs->stlist;
	cs->stlist = st;
}

void
HiSax_rmlist(struct IsdnCardState *cs,
	     struct PStack *st)
{
	struct PStack *p;

	if (cs->stlist == st)
		cs->stlist = st->next;
	else {
		p = cs->stlist;
		while (p)
			if (p->next == st) {
				p->next = st->next;
				return;
			} else
				p = p->next;
	}
}

void
init_bcstate(struct IsdnCardState *cs,
	     int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	bcs->cs = cs;
	bcs->channel = bc;
	bcs->tqueue.next = 0;
	bcs->tqueue.sync = 0;
	bcs->tqueue.routine = (void *) (void *) BChannel_bh;
	bcs->tqueue.data = bcs;
	bcs->Flag = 0;
}

int
get_irq(int cardnr, void *routine)
{
	struct IsdnCard *card = cards + cardnr;
	long flags;

	save_flags(flags);
	cli();
	if (request_irq(card->cs->irq, routine,
			I4L_IRQ_FLAG, "HiSax", NULL)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
		       card->cs->irq);
		restore_flags(flags);
		return (0);
	}
	irq2dev_map[card->cs->irq] = (void *) card->cs;
	restore_flags(flags);
	return (1);
}

static void
release_irq(int cardnr)
{
	struct IsdnCard *card = cards + cardnr;

	irq2dev_map[card->cs->irq] = NULL;
	free_irq(card->cs->irq, NULL);
}

static void
closecard(int cardnr)
{
	struct IsdnCardState *csta = cards[cardnr].cs;
	struct sk_buff *skb;

	del_timer(&csta->l1timer);
	del_timer(&csta->t3);
	csta->bcs->BC_Close(csta->bcs + 1);
	csta->bcs->BC_Close(csta->bcs);

	if (csta->rcvbuf) {
		kfree(csta->rcvbuf);
		csta->rcvbuf = NULL;
	}
	while ((skb = skb_dequeue(&csta->rq))) {
		dev_kfree_skb(skb, FREE_READ);
	}
	while ((skb = skb_dequeue(&csta->sq))) {
		dev_kfree_skb(skb, FREE_WRITE);
	}
	if (csta->tx_skb) {
		dev_kfree_skb(csta->tx_skb, FREE_WRITE);
		csta->tx_skb = NULL;
	}
	switch (csta->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			release_io_teles0(cards + cardnr);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_TELESPCMCIA:
			release_io_teles3(cards + cardnr);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			release_io_avm_a1(cards + cardnr);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_PNP:
		case ISDN_CTYPE_ELSA_PCMCIA:
			release_io_elsa(cards + cardnr);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			release_io_ix1micro(cards + cardnr);
			break;
#endif
#if CARD_DIEHLDIVA
		case ISDN_CTYPE_DIEHLDIVA:
			release_io_diva(cards + cardnr);
			break;
#endif
#if CARD_DYNALINK
		case ISDN_CTYPE_DYNALINK:
			release_io_dynalink(cards + cardnr);
			break;
#endif
#if CARD_TELEINT
		case ISDN_CTYPE_TELEINT:
			release_io_TeleInt(cards + cardnr);
			break;
#endif
#if CARD_SEDLBAUER
		case ISDN_CTYPE_SEDLBAUER:
			release_io_sedlbauer(cards + cardnr);
			break;
#endif
		default:
			break;
	}
	ll_unload(csta);
}

static int
checkcard(int cardnr, char *id)
{
	long flags;
	int ret = 0;
	struct IsdnCard *card = cards + cardnr;
	struct IsdnCardState *cs;

	save_flags(flags);
	cli();
	if (!(cs = (struct IsdnCardState *)
	      kmalloc(sizeof(struct IsdnCardState), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for IsdnCardState(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	card->cs = cs;
	cs->cardnr = cardnr;
#if TEI_PER_CARD
	cs->HW_Flags = 0;
#else
	cs->HW_Flags = FLG_TWO_DCHAN;
#endif
	cs->protocol = card->protocol;
	cs->l1timer.function = (void *) L1_timer_handler;
	cs->l1timer.data = (long) cs;
	init_timer(&cs->l1timer);
	init_timer(&cs->t3);

	if ((card->typ > 0) && (card->typ < 31)) {
		if (!((1 << card->typ) & SUPORTED_CARDS)) {
			printk(KERN_WARNING
			     "HiSax: Support for %s Card not selected\n",
			       CardType[card->typ]);
			restore_flags(flags);
			return (0);
		}
	} else {
		printk(KERN_WARNING
		       "HiSax: Card Type %d out of range\n",
		       card->typ);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->dlogspace = kmalloc(4096, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for dlogspace(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->status_buf = kmalloc(HISAX_STATUS_BUFSIZE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cardnr + 1);
		kfree(cs->dlogspace);
		restore_flags(flags);
		return (0);
	}
	cs->status_read = cs->status_buf;
	cs->status_write = cs->status_buf;
	cs->status_end = cs->status_buf + HISAX_STATUS_BUFSIZE - 1;
	cs->typ = card->typ;
	cs->CallFlags = 0;
	strcpy(cs->iif.id, id);
	cs->iif.channels = 2;
	cs->iif.maxbufsize = MAX_DATA_SIZE;
	cs->iif.hl_hdrlen = MAX_HEADER_LEN;
	cs->iif.features =
	    ISDN_FEATURE_L2_X75I |
	    ISDN_FEATURE_L2_HDLC |
	    ISDN_FEATURE_L2_TRANS |
	    ISDN_FEATURE_L3_TRANS |
#ifdef	CONFIG_HISAX_1TR6
	    ISDN_FEATURE_P_1TR6 |
#endif
#ifdef	CONFIG_HISAX_EURO
	    ISDN_FEATURE_P_EURO |
#endif
#ifdef        CONFIG_HISAX_NI1
	    ISDN_FEATURE_P_NI1 |
#endif
	    0;

	cs->iif.command = HiSax_command;
	cs->iif.writecmd = NULL;
	cs->iif.writebuf_skb = HiSax_writebuf_skb;
	cs->iif.readstat = HiSax_readstatus;
	register_isdn(&cs->iif);
	cs->myid = cs->iif.channels;
	restore_flags(flags);
	printk(KERN_NOTICE
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cardnr + 1,
	       (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (card->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (card->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
	       (card->protocol == ISDN_PTYPE_NI1) ? "NI1" :
	       "NONE", cs->iif.id, cs->myid);
	switch (card->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			ret = setup_teles0(card);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_TELESPCMCIA:
			ret = setup_teles3(card);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			ret = setup_avm_a1(card);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_PNP:
		case ISDN_CTYPE_ELSA_PCMCIA:
			ret = setup_elsa(card);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			ret = setup_ix1micro(card);
			break;
#endif
#if CARD_DIEHLDIVA
		case ISDN_CTYPE_DIEHLDIVA:
			ret = setup_diva(card);
			break;
#endif
#if CARD_DYNALINK
		case ISDN_CTYPE_DYNALINK:
			ret = setup_dynalink(card);
			break;
#endif
#if CARD_TELEINT
		case ISDN_CTYPE_TELEINT:
			ret = setup_TeleInt(card);
			break;
#endif
#if CARD_SEDLBAUER
		case ISDN_CTYPE_SEDLBAUER:
			ret = setup_sedlbauer(card);
			break;
#endif
		default:
			printk(KERN_WARNING "HiSax: Unknown Card Typ %d\n",
			       card->typ);
			ll_unload(cs);
			return (0);
	}
	if (!ret) {
		ll_unload(cs);
		return (0);
	}
	if (!(cs->rcvbuf = kmalloc(MAX_DFRAME_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for isac rcvbuf\n");
		return (1);
	}
	cs->rcvidx = 0;
	cs->tx_skb = NULL;
	cs->tx_cnt = 0;
	cs->event = 0;
	cs->tqueue.next = 0;
	cs->tqueue.sync = 0;
	cs->tqueue.routine = (void *) (void *) DChannel_bh;
	cs->tqueue.data = cs;

	skb_queue_head_init(&cs->rq);
	skb_queue_head_init(&cs->sq);

	cs->stlist = NULL;
	cs->ph_active = 0;
	cs->dlogflag = 0;
	cs->debug = L1_DEB_WARN;
	init_bcstate(cs, 0);
	init_bcstate(cs, 1);

	switch (card->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			ret = initteles0(cs);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_TELESPCMCIA:
			ret = initteles3(cs);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			ret = initavm_a1(cs);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_PNP:
		case ISDN_CTYPE_ELSA_PCMCIA:
			ret = initelsa(cs);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			ret = initix1micro(cs);
			break;
#endif
#if CARD_DIEHLDIVA
		case ISDN_CTYPE_DIEHLDIVA:
			ret = initdiva(cs);
			break;
#endif
#if CARD_DYNALINK
		case ISDN_CTYPE_DYNALINK:
			ret = initdynalink(cs);
			break;
#endif
#if CARD_TELEINT
		case ISDN_CTYPE_TELEINT:
			ret = initTeleInt(cs);
			break;
#endif
#if CARD_SEDLBAUER
		case ISDN_CTYPE_SEDLBAUER:
			ret = initsedlbauer(cs);
			break;
#endif
		default:
			ret = 0;
			break;
	}
	if (!ret) {
		closecard(cardnr);
		return (0);
	}
	init_tei(cs, cs->protocol);
	CallcNewChan(cs);
	ll_run(cs);
	return (1);
}

void
HiSax_shiftcards(int idx)
{
	int i;

	for (i = idx; i < 15; i++)
		memcpy(&cards[i], &cards[i + 1], sizeof(cards[i]));
}

int
HiSax_inithardware(void)
{
	int foundcards = 0;
	int i = 0;
	int t = ',';
	int flg = 0;
	char *id;
	char *next_id = HiSax_id;
	char ids[20];

	if (strchr(HiSax_id, ','))
		t = ',';
	else if (strchr(HiSax_id, '%'))
		t = '%';

	while (i < nrcards) {
		if (cards[i].typ < 1)
			break;
		id = next_id;
		if ((next_id = strchr(id, t))) {
			*next_id++ = 0;
			strcpy(ids, id);
			flg = i + 1;
		} else {
			next_id = id;
			if (flg >= i)
				strcpy(ids, id);
			else
				sprintf(ids, "%s%d", id, i);
		}
		if (checkcard(i, ids)) {
			foundcards++;
			i++;
		} else {
			printk(KERN_WARNING "HiSax: Card %s not installed !\n",
			       CardType[cards[i].typ]);
			if (cards[i].cs)
				kfree((void *) cards[i].cs);
			cards[i].cs = NULL;
			HiSax_shiftcards(i);
		}
	}
	return foundcards;
}

void
HiSax_closehardware(void)
{
	int i;
	long flags;

	save_flags(flags);
	cli();
	for (i = 0; i < nrcards; i++)
		if (cards[i].cs) {
			ll_stop(cards[i].cs);
			release_tei(cards[i].cs);
			release_irq(i);
			closecard(i);
			kfree((void *) cards[i].cs);
			cards[i].cs = NULL;
		}
	TeiFree();
	Isdnl2Free();
	CallcFree();
	restore_flags(flags);
}

void
HiSax_reportcard(int cardnr)
{
	struct IsdnCardState *cs = cards[cardnr].cs;
	struct PStack *stptr;
	struct l3_process *pc;
	int j, i = 1;

	printk(KERN_DEBUG "HiSax: reportcard No %d\n", cardnr + 1);
	printk(KERN_DEBUG "HiSax: Type %s\n", CardType[cs->typ]);
	printk(KERN_DEBUG "HiSax: debuglevel %x\n", cs->debug);
	printk(KERN_DEBUG "HiSax: HiSax_reportcard address 0x%lX\n",
	       (ulong) & HiSax_reportcard);
	printk(KERN_DEBUG "HiSax: cs 0x%lX\n", (ulong) cs);
	printk(KERN_DEBUG "HiSax: cs stl 0x%lX\n", (ulong) & (cs->stlist));
	stptr = cs->stlist;
	while (stptr != NULL) {
		printk(KERN_DEBUG "HiSax: dst%d 0x%lX\n", i, (ulong) stptr);
		printk(KERN_DEBUG "HiSax: dst%d stp 0x%lX\n", i, (ulong) stptr->l1.stlistp);
		printk(KERN_DEBUG "HiSax:   tei %d sapi %d\n",
		       stptr->l2.tei, stptr->l2.sap);
		printk(KERN_DEBUG "HiSax:      man 0x%lX\n", (ulong) stptr->ma.layer);
		pc = stptr->l3.proc;
		while (pc) {
			printk(KERN_DEBUG "HiSax: l3proc %x 0x%lX\n", pc->callref,
			       (ulong) pc);
			printk(KERN_DEBUG "HiSax:    state %d  st 0x%lX chan 0x%lX\n",
			    pc->state, (ulong) pc->st, (ulong) pc->chan);
			pc = pc->next;
		}
		stptr = stptr->next;
		i++;
	}
	for (j = 0; j < 2; j++) {
		printk(KERN_DEBUG "HiSax: ch%d 0x%lX\n", j,
		       (ulong) & cs->channel[j]);
		stptr = cs->channel[j].b_st;
		i = 1;
		while (stptr != NULL) {
			printk(KERN_DEBUG "HiSax:  b_st%d 0x%lX\n", i, (ulong) stptr);
			printk(KERN_DEBUG "HiSax:    man 0x%lX\n", (ulong) stptr->ma.layer);
			stptr = stptr->next;
			i++;
		}
	}
}

#ifdef L2FRAME_DEBUG		/* psa */

char *
l2cmd(u_char cmd)
{
	switch (cmd & ~0x10) {
		case 1:
			return "RR";
		case 5:
			return "RNR";
		case 9:
			return "REJ";
		case 0x6f:
			return "SABME";
		case 0x0f:
			return "DM";
		case 3:
			return "UI";
		case 0x43:
			return "DISC";
		case 0x63:
			return "UA";
		case 0x87:
			return "FRMR";
		case 0xaf:
			return "XID";
		default:
			if (!(cmd & 1))
				return "I";
			else
				return "invalid command";
	}
}

static char tmp[20];

char *
l2frames(u_char * ptr)
{
	switch (ptr[2] & ~0x10) {
		case 1:
		case 5:
		case 9:
			sprintf(tmp, "%s[%d](nr %d)", l2cmd(ptr[2]), ptr[3] & 1, ptr[3] >> 1);
			break;
		case 0x6f:
		case 0x0f:
		case 3:
		case 0x43:
		case 0x63:
		case 0x87:
		case 0xaf:
			sprintf(tmp, "%s[%d]", l2cmd(ptr[2]), (ptr[2] & 0x10) >> 4);
			break;
		default:
			if (!(ptr[2] & 1)) {
				sprintf(tmp, "I[%d](ns %d, nr %d)", ptr[3] & 1, ptr[2] >> 1, ptr[3] >> 1);
				break;
			} else
				return "invalid command";
	}


	return tmp;
}

void
Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir)
{
	char tmp[132];
	u_char *ptr;

	ptr = skb->data;

	if (ptr[0] & 1 || !(ptr[1] & 1))
		debugl1(cs, "Addres not LAPD");
	else {
		sprintf(tmp, "%s %s: %s%c (sapi %d, tei %d)",
			(dir ? "<-" : "->"), buf, l2frames(ptr),
			((ptr[0] & 2) >> 1) == dir ? 'C' : 'R', ptr[0] >> 2, ptr[1] >> 1);
		debugl1(cs, tmp);
	}
}

#endif
