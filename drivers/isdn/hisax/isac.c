/* $Id$

 * isac.c   ISAC specific routines
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 * Revision 1.2  1997/06/26 11:16:15  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

static char *ISACVer[] =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

void
ISACVersion(struct IsdnCardState *cs, char *s)
{
	int val;

	val = cs->readisac(cs, ISAC_RBCH);
	printk(KERN_INFO "%s ISAC version : %s\n", s, ISACVer[(val >> 5) & 3]);
}

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC) {
		char tmp[32];
		sprintf(tmp, "ph_command %d", command);
		debugl1(cs, tmp);
	}
	cs->writeisac(cs, ISAC_CIX0, (command << 2) | 3);
}

static void
L1_T3_handler(struct IsdnCardState *cs)
{
	ph_command(cs, ISAC_CMD_RS);
}

void
initisac(struct IsdnCardState *cs)
{
	cs->writeisac(cs, ISAC_MASK, 0xff);
	if (cs->HW_Flags & HW_IOM1) {
		/* IOM 1 Mode */
		cs->writeisac(cs, ISAC_ADF2, 0x0);
		cs->writeisac(cs, ISAC_SPCR, 0xa);
		cs->writeisac(cs, ISAC_ADF1, 0x2);
		cs->writeisac(cs, ISAC_STCR, 0x70);
		cs->writeisac(cs, ISAC_MODE, 0xc9);
	} else {
		/* IOM 2 Mode */
		cs->writeisac(cs, ISAC_ADF2, 0x80);
		cs->writeisac(cs, ISAC_SQXR, 0x2f);
		cs->writeisac(cs, ISAC_SPCR, 0x00);
		cs->writeisac(cs, ISAC_STCR, 0x70);
		cs->writeisac(cs, ISAC_MODE, 0xc9);
		cs->writeisac(cs, ISAC_TIMR, 0x00);
		cs->writeisac(cs, ISAC_ADF1, 0x00);
	}
	cs->t3.function = (void *) L1_T3_handler;
	cs->t3.data = (long) cs;
	init_timer(&cs->t3);
	ph_command(cs, ISAC_CMD_RS);
	cs->writeisac(cs, ISAC_MASK, 0x0);
}

void
isac_empty_fifo(struct IsdnCardState *cs, int count)
{
	u_char *ptr;
	long flags;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "isac_empty_fifo");

	if ((cs->rcvidx + count) >= MAX_DFRAME_LEN) {
		if (cs->debug & L1_DEB_WARN) {
			char tmp[40];
			sprintf(tmp, "isac_empty_fifo overrun %d",
				cs->rcvidx + count);
			debugl1(cs, tmp);
		}
		cs->writeisac(cs, ISAC_CMDR, 0x80);
		cs->rcvidx = 0;
		return;
	}
	ptr = cs->rcvbuf + cs->rcvidx;
	cs->rcvidx += count;
	save_flags(flags);
	cli();
	cs->readisacfifo(cs, ptr, count);
	cs->writeisac(cs, ISAC_CMDR, 0x80);
	restore_flags(flags);
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, tmp);
	}
}

static void
isac_fill_fifo(struct IsdnCardState *cs)
{
	int count, more;
	u_char *ptr;
	long flags;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "isac_fill_fifo");

	if (!cs->tx_skb)
		return;

	count = cs->tx_skb->len;
	if (count <= 0)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	save_flags(flags);
	cli();
	ptr = cs->tx_skb->data;
	skb_pull(cs->tx_skb, count);
	cs->tx_cnt += count;
	cs->writeisacfifo(cs, ptr, count);
	cs->writeisac(cs, ISAC_CMDR, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (cs->HW_Flags & FLG_L1TIMER) {
		debugl1(cs, "isac_fill_fifo timer running");
		del_timer(&cs->l1timer);
		cs->HW_Flags &= ~FLG_L1TIMER;
	}
	init_timer(&cs->l1timer);
	cs->l1timer.expires = jiffies + ((80 * HZ) / 1000);
	add_timer(&cs->l1timer);
	cs->HW_Flags |= FLG_L1TIMER_DBUSY;
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_fill_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, tmp);
	}
}

void
isac_sched_event(struct IsdnCardState *cs, int event)
{
	cs->event |= 1 << event;
	queue_task(&cs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
isac_new_ph(struct IsdnCardState *cs)
{
	int enq;

	enq = L1act_wanted(cs);

	switch (cs->ph_state) {
		case (ISAC_IND_EI):
			if (cs->ph_active > 3) {
				isac_sched_event(cs, L1_PH_DEACT);
				cs->ph_active = 3;
			} else if (cs->ph_active)
				cs->ph_active--;
			ph_command(cs, ISAC_CMD_DUI);
			break;
		case (ISAC_IND_DID):
			if (cs->ph_active > 3) {
				isac_sched_event(cs, L1_PH_DEACT);
				cs->ph_active = 3;
			} else if (cs->ph_active)
				cs->ph_active--;
			if (enq) {
				del_timer(&cs->t3);
				init_timer(&cs->t3);
				/* T3 must not more as 30s */
				cs->t3.expires = jiffies + 25 * HZ;
				add_timer(&cs->t3);
				ph_command(cs, ISAC_CMD_TIM);
			}
			break;
		case (ISAC_IND_DR):
			if (cs->ph_active > 3) {
				isac_sched_event(cs, L1_PH_DEACT);
				cs->ph_active = 3;
			} else if (cs->ph_active)
				cs->ph_active--;
			if (enq) {
				del_timer(&cs->t3);
				init_timer(&cs->t3);
				/* T3 must not more as 30s */
				cs->t3.expires = jiffies + 25 * HZ;
				add_timer(&cs->t3);
				ph_command(cs, ISAC_CMD_TIM);
			}
#if 0
			else
				ph_command(cs, ISAC_CMD_DUI);
#endif
			break;
		case (ISAC_IND_PU):
			if (cs->ph_active > 3) {
				isac_sched_event(cs, L1_PH_DEACT);
				cs->ph_active = 3;
			} else if (cs->ph_active)
				cs->ph_active--;
			if (enq)
				ph_command(cs, ISAC_CMD_AR8);
			break;
		case (ISAC_IND_AI8):
			del_timer(&cs->t3);
			ph_command(cs, ISAC_CMD_AR8);
			cs->ph_active = 5;
			isac_sched_event(cs, L1_PH_ACT);
#if 0
			if (!cs->tx_skb)
				cs->tx_skb = skb_dequeue(&cs->sq);
			if (cs->tx_skb) {
				cs->tx_cnt = 0;
				isac_fill_fifo(cs);
			}
#endif
			break;
		case (ISAC_IND_AI10):
			del_timer(&cs->t3);
			ph_command(cs, ISAC_CMD_AR10);
			cs->ph_active = 5;
			isac_sched_event(cs, L1_PH_ACT);
#if 0
			if (!cs->tx_skb)
				cs->tx_skb = skb_dequeue(&cs->sq);
			if (cs->tx_skb) {
				cs->tx_cnt = 0;
				isac_fill_fifo(cs);
			}
#endif
			break;
		case (ISAC_IND_RSY):
		case (ISAC_IND_ARD):
			if (cs->ph_active > 3) {
				isac_sched_event(cs, L1_PH_DEACT);
				cs->ph_active = 3;
			} else if (cs->ph_active)
				cs->ph_active--;
			break;
		default:
			if (cs->ph_active > 3) {
				isac_sched_event(cs, L1_PH_DEACT);
				cs->ph_active = 3;
			} else if (cs->ph_active)
				cs->ph_active--;
			break;
	}
}

void
isac_interrupt(struct IsdnCardState *cs, u_char val)
{
	u_char exval, v1;
	struct sk_buff *skb;
	unsigned int count;
	char tmp[32];
#if ARCOFI_USE
	struct BufHeader *ibh;
	u_char *ptr;
#endif

	if (cs->debug & L1_DEB_ISAC) {
		sprintf(tmp, "ISAC interrupt %x", val);
		debugl1(cs, tmp);
	}
	if (val & 0x80) {	/* RME */
		exval = cs->readisac(cs, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ISAC RDO");
			if (!exval & 0x20)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ISAC CRC error");
			cs->writeisac(cs, ISAC_CMDR, 0x80);
		} else {
			count = cs->readisac(cs, ISAC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			isac_empty_fifo(cs, count);
			if ((count = cs->rcvidx) > 0) {
				cs->rcvidx = 0;
				if (!(skb = alloc_skb(count, GFP_ATOMIC)))
					printk(KERN_WARNING "Elsa: D receive out of memory\n");
				else {
					SET_SKB_FREE(skb);
					memcpy(skb_put(skb, count), cs->rcvbuf, count);
					skb_queue_tail(&cs->rq, skb);
				}
			}
		}
		cs->rcvidx = 0;
		isac_sched_event(cs, D_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		isac_empty_fifo(cs, 32);
	}
	if (val & 0x20) {	/* RSC */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		if (cs->HW_Flags & FLG_L1TIMER_DBUSY) {
			del_timer(&cs->l1timer);
			cs->HW_Flags &= ~FLG_L1TIMER_DBUSY;
		}
		if (cs->tx_skb)
			if (cs->tx_skb->len) {
				isac_fill_fifo(cs);
				goto afterXPR;
			} else {
				dev_kfree_skb(cs->tx_skb, FREE_WRITE);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			}
		if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
			cs->tx_cnt = 0;
			isac_fill_fifo(cs);
		} else
			isac_sched_event(cs, D_XMTBUFREADY);
	}
      afterXPR:
	if (val & 0x04) {	/* CISQ */
		cs->ph_state = (cs->readisac(cs, ISAC_CIX0) >> 2) & 0xf;
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "l1state %d", cs->ph_state);
			debugl1(cs, tmp);
		}
		isac_new_ph(cs);
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = cs->readisac(cs, ISAC_EXIR);
		if (cs->debug & L1_DEB_WARN) {
			sprintf(tmp, "ISAC EXIR %02x", exval);
			debugl1(cs, tmp);
		}
		if (exval & 0x08) {
			v1 = cs->readisac(cs, ISAC_MOSR);
			if (cs->debug & L1_DEB_WARN) {
				sprintf(tmp, "ISAC MOSR %02x", v1);
				debugl1(cs, tmp);
			}
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!cs->mon_rx)
					if (BufPoolGet(&(cs->mon_rx), &(cs->rbufpool),
					    GFP_ATOMIC, (void *) 1, 3)) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of buffers!");
						cs->writeisac(cs, ISAC_MOCR, 0x0a);
						goto afterMONR0;
					} else
						cs->mon_rxp = 0;
				ibh = cs->mon_rx;
				ptr = DATAPTR(ibh);
				ptr += cs->mon_rxp;
				cs->mon_rxp++;
				if (cs->mon_rxp >= 3072) {
					cs->writeisac(cs, ISAC_MOCR, 0x0a);
					cs->mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR0;
				}
				*ptr = cs->readisac(cs, ISAC_MOR0);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC MOR0 %02x", *ptr);
					debugl1(cs, tmp);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!cs->mon_rx)
					if (BufPoolGet(&(cs->mon_rx), &(cs->rbufpool),
					    GFP_ATOMIC, (void *) 1, 3)) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ISAC MON RX out of buffers!");
						cs->writeisac(cs, ISAC_MOCR, 0xa0);
						goto afterMONR1;
					} else
						cs->mon_rxp = 0;
				ibh = cs->mon_rx;
				ptr = DATAPTR(ibh);
				ptr += cs->mon_rxp;
				cs->mon_rxp++;
				if (cs->mon_rxp >= 3072) {
					cs->writeisac(cs, ISAC_MOCR, 0xa0);
					cs->mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ISAC MON RX overflow!");
					goto afterMONR1;
				}
				*ptr = cs->readisac(cs, ISAC_MOR1);
				if (cs->debug & L1_DEB_WARN) {
					sprintf(tmp, "ISAC MOR1 %02x", *ptr);
					debugl1(cs, tmp);
				}
			}
		      afterMONR1:
			if (v1 & 0x04) {
				cs->writeisac(cs, ISAC_MOCR, 0x0a);
				cs->mon_rx->datasize = cs->mon_rxp;
				cs->mon_flg |= MON0_RX;
			}
			if (v1 & 0x40) {
				cs->writeisac(cs, ISAC_MOCR, 0xa0);
				cs->mon_rx->datasize = cs->mon_rxp;
				cs->mon_flg |= MON1_RX;
			}
			if (v1 == 0x02) {
				ibh = cs->mon_tx;
				if (!ibh) {
					cs->writeisac(cs, ISAC_MOCR, 0x0a);
					goto AfterMOX0;
				}
				count = ibh->datasize - cs->mon_txp;
				if (count <= 0) {
					cs->writeisac(cs, ISAC_MOCR, 0x0f);
					BufPoolRelease(cs->mon_tx);
					cs->mon_tx = NULL;
					cs->mon_txp = 0;
					cs->mon_flg |= MON0_TX;
					goto AfterMOX0;
				}
				ptr = DATAPTR(ibh);
				ptr += cs->mon_txp;
				cs->mon_txp++;
				cs->writeisac(cs, ISAC_MOX0, *ptr);
			}
		      AfterMOX0:
			if (v1 == 0x20) {
				ibh = cs->mon_tx;
				if (!ibh) {
					cs->writeisac(cs, ISAC_MOCR, 0xa0);
					goto AfterMOX1;
				}
				count = ibh->datasize - cs->mon_txp;
				if (count <= 0) {
					cs->writeisac(cs, ISAC_MOCR, 0xf0);
					BufPoolRelease(cs->mon_tx);
					cs->mon_tx = NULL;
					cs->mon_txp = 0;
					cs->mon_flg |= MON1_TX;
					goto AfterMOX1;
				}
				ptr = DATAPTR(ibh);
				ptr += cs->mon_txp;
				cs->mon_txp++;
				cs->writeisac(cs, ISAC_MOX1, *ptr);
			}
		      AfterMOX1:
#endif
		}
	}
}

static void
ISAC_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	char str[64];

	switch (pr) {
		case (PH_DATA):
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				if ((cs->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
					LogFrame(cs, skb->data, skb->len);
					sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
					dlogframe(cs, skb->data + 4, skb->len - 4,
						  str);
				}
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				isac_fill_fifo(cs);
			}
			break;
		case (PH_DATA_PULLED):
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if ((cs->dlogflag) && (!(skb->data[2] & 1))) {	/* I-FRAME */
				LogFrame(cs, skb->data, skb->len);
				sprintf(str, "Q.931 frame user->network tei %d", st->l2.tei);
				dlogframe(cs, skb->data + 4, skb->len - 4,
					  str);
			}
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			isac_fill_fifo(cs);
			break;
		case (PH_REQUEST_PULL):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				st->l1.requestpull = 0;
				st->l1.l1l2(st, PH_PULL_ACK, NULL);
			} else
				st->l1.requestpull = !0;
			break;
	}
}

static void
ISAC_manl1(struct PStack *st, int pr,
	   void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct PStack *ps;
	long flags;
	u_char val = 0;
	char tmp[32];

	switch (pr) {
		case (PH_ACTIVATE):
			if (cs->debug) {
				sprintf(tmp, "PH_ACT ph_active %d", cs->ph_active);
				debugl1(cs, tmp);
			}
			save_flags(flags);
			cli();
			if (cs->ph_active & 4) {
				cs->ph_active = 5;
				st->l1.act_state = 2;
				restore_flags(flags);
				L1activated(cs);
			} else {
				st->l1.act_state = 1;
				cs->ph_active = 0;
				restore_flags(flags);
				del_timer(&cs->t3);
				init_timer(&cs->t3);
				/* T3 must not more as 30s */
				cs->t3.expires = jiffies + 25 * HZ;
				add_timer(&cs->t3);
				if ((cs->ph_state == ISAC_IND_EI) ||
				    (cs->ph_state == ISAC_IND_DR))
					ph_command(cs, ISAC_CMD_TIM);
				else
					ph_command(cs, ISAC_CMD_RS);
			}
			break;
		case (PH_DEACTIVATE):
			st->l1.act_state = 0;
			if (cs->debug) {
				sprintf(tmp, "PH_DEACT ph_active %d", cs->ph_active);
				debugl1(cs, tmp);
			}
			ps = cs->stlist;
			flags = 0;
			while (ps) {
				if (ps->l1.act_state)
					flags = 1;
				ps = ps->next;
			}
			if (!flags) {
				if (cs->ph_active == 5)
					cs->ph_active = 4;
			}
			break;
		case (PH_TEST_LOOP):
			if (1 & (int) arg) {
				val |= 0x0c;
				debugl1(cs, "PH_TEST_LOOP B1");
			}
			if (2 & (int) arg) {
				val |= 0x3;
				debugl1(cs, "PH_TEST_LOOP B2");
			}
			if (!val)
				debugl1(cs, "PH_TEST_LOOP DISABLED");
			if (cs->HW_Flags & HW_IOM1) {
				/* IOM 1 Mode */
				if (!val) {
					cs->writeisac(cs, ISAC_SPCR, 0xa);
					cs->writeisac(cs, ISAC_ADF1, 0x2);
				} else {
					cs->writeisac(cs, ISAC_SPCR, val);
					cs->writeisac(cs, ISAC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				cs->writeisac(cs, ISAC_SPCR, val);
				if (val)
					cs->writeisac(cs, ISAC_ADF1, 0x8);
				else
					cs->writeisac(cs, ISAC_ADF1, 0x0);
			}
			break;
	}
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.hardware = cs;
	st->protocol = cs->protocol;

	setstack_tei(st);
	setstack_manager(st);

	st->l1.stlistp = &(cs->stlist);
	st->l1.act_state = 0;
	st->l2.l2l1 = ISAC_l2l1;
	st->ma.manl1 = ISAC_manl1;
	st->l1.requestpull = 0;
}

void
clear_pending_isac_ints(struct IsdnCardState *cs)
{
	int val;
	char tmp[64];

	val = cs->readisac(cs, ISAC_STAR);
	sprintf(tmp, "ISAC STAR %x", val);
	debugl1(cs, tmp);
	val = cs->readisac(cs, ISAC_MODE);
	sprintf(tmp, "ISAC MODE %x", val);
	debugl1(cs, tmp);
	val = cs->readisac(cs, ISAC_ADF2);
	sprintf(tmp, "ISAC ADF2 %x", val);
	debugl1(cs, tmp);
	val = cs->readisac(cs, ISAC_ISTA);
	sprintf(tmp, "ISAC ISTA %x", val);
	debugl1(cs, tmp);
	if (val & 0x01) {
		val = cs->readisac(cs, ISAC_EXIR);
		sprintf(tmp, "ISAC EXIR %x", val);
		debugl1(cs, tmp);
	} else if (val & 0x04) {
		val = cs->readisac(cs, ISAC_CIR0);
		sprintf(tmp, "ISAC CIR0 %x", val);
		debugl1(cs, tmp);
	}
	cs->writeisac(cs, ISAC_MASK, 0xFF);
	cs->writeisac(cs, ISAC_MASK, 0);
	cs->writeisac(cs, ISAC_CMDR, 0x41);
}
