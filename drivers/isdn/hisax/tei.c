/* $Id$

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log$
 * Revision 1.8  1997/04/07 22:59:08  keil
 * GFP_KERNEL --> GFP_ATOMIC
 *
 * Revision 1.7  1997/04/06 22:54:03  keil
 * Using SKB's
 *
 * Revision 1.6  1997/02/09 00:25:12  keil
 * new interface handling, one interface per card
 *
 * Revision 1.5  1997/01/27 15:57:51  keil
 * cosmetics
 *
 * Revision 1.4  1997/01/21 22:32:44  keil
 * Tei verify request
 *
 * Revision 1.3  1997/01/04 13:45:02  keil
 * cleanup,adding remove tei request (thanks to Sim Yskes)
 *
 * Revision 1.2  1996/12/08 19:52:39  keil
 * minor debug fix
 *
 * Revision 1.1  1996/10/13 20:04:57  keil
 * Initial revision
 *
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl2.h"
#include <linux/random.h>

const char *tei_revision = "$Revision$";

#define ID_REQUEST	1
#define ID_ASSIGNED	2
#define ID_DENIED	3
#define ID_CHK_REQ	4
#define ID_CHK_RES	5
#define ID_REMOVE	6
#define ID_VERIFY	7

#define GROUP_TEI	127
#define TEI_SAPI	63
#define TEI_ENTITY_ID	0xf

struct Fsm teifsm =
{NULL, 0, 0, NULL, NULL};

void	tei_handler(struct PStack *st, u_char pr, struct sk_buff *skb);

enum {
	ST_TEI_NOP,
	ST_TEI_IDREQ,
	ST_TEI_IDVERIFY,
};

#define TEI_STATE_COUNT (ST_TEI_IDVERIFY+1)

static char *strTeiState[] =
{
	"ST_TEI_NOP",
	"ST_TEI_IDREQ",
	"ST_TEI_IDVERIFY",
};

enum {
	EV_T202,
};

#define TEI_EVENT_COUNT (EV_T202+1)

static char *strTeiEvent[] =
{
	"EV_T202",
};

unsigned int
random_ri(void)
{
	unsigned int x;

	get_random_bytes(&x, sizeof(x));
	return (x & 0xffff);
}

static struct PStack *
find_ri(struct PStack *st, int ri)
{
	struct PStack *ptr = *(st->l1.stlistp);

	while (ptr)
		if (ptr->l2.ri == ri)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
}

static struct PStack *
findtei(struct PStack *st, int tei)
{
	struct PStack *ptr = *(st->l1.stlistp);

	if (tei == 127)
		return (NULL);

	while (ptr)
		if (ptr->l2.tei == tei)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
}

static void
put_tei_msg(struct PStack *st, u_char m_id, unsigned int ri, u_char ai)
{
	struct sk_buff *skb;
	u_char *bp;

	if (!(skb = alloc_skb(8, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No skb for TEI manager\n");
		return;
	}
	SET_SKB_FREE(skb);
	bp = skb_put(skb, 3);
	bp[0] = (TEI_SAPI<<2);
	bp[1] = (GROUP_TEI<<1) | 0x1;
	bp[2] = UI;
	bp = skb_put(skb, 5);
	bp[0] = TEI_ENTITY_ID;
	bp[1] = ri >> 8;
	bp[2] = ri & 0xff;
	bp[3] = m_id;
	bp[4] = (ai << 1) | 1;
	st->l2.l2l1(st, PH_DATA, skb);
}

static void
get_tei_msg(struct PStack *st,  u_char m_id, unsigned int ri, u_char ai)
{
	struct PStack *otsp, *ptr;
	char tmp[64];
	struct IsdnCardState *cs;

	sprintf(tmp, "get_tei_msg m_id %d ri %d ai %d", m_id, ri, ai);
	st->l2.l2m.printdebug(&st->l2.l2m, tmp);
	switch (m_id) {
		case (ID_ASSIGNED):
			FsmDelTimer(&st->l2.t200_timer, 1);
			FsmChangeState(&st->l2.l2m, ST_TEI_NOP);
			if (st->l3.debug) {
				sprintf(tmp, "identity assign ri %d ai %d", ri, ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if ((otsp = findtei(st, ai))) { /* same tei is in use */
				sprintf(tmp, "possible duplicate assignment tei %d",
					ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				otsp->l2.l2tei(otsp, MDL_VERIFY, NULL);
			} else if ((otsp = find_ri(st, ri))) {
				if (st->l3.debug) {
					sprintf(tmp, "ri %d --> tei %d", ri, ai);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				otsp->ma.manl2(otsp, MDL_ASSIGN, (void *) (int) ai);
				cs = (struct IsdnCardState *) otsp->l1.hardware;
				cs->cardmsg(cs, MDL_ASSIGN, NULL);
			} else {
				if (st->l3.debug) {
					sprintf(tmp, "ri %d not found", ri);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
			}
			if (--st->l2.sow) {
				st->l2.sow = 0;
				tei_handler(st, st->l2.vr,
					(struct sk_buff *) st->l2.va);
			}
			break;
		case (ID_DENIED):
			FsmDelTimer(&st->l2.t200_timer, 1);
			FsmChangeState(&st->l2.l2m, ST_TEI_NOP);
			if (st->l3.debug) {
				sprintf(tmp, "identity denied for ri %d ai %d",
					ri, ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if ((otsp = find_ri(st, ri))) {
				if (st->l3.debug) {
					sprintf(tmp, "ri %d denied tei %d",
						ri, ai);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				otsp->l2.tei = 255;
				otsp->l2.ri  = -1;
				otsp->ma.manl2(otsp, MDL_REMOVE, 0);
				cs = (struct IsdnCardState *) otsp->l1.hardware;
				cs->cardmsg(cs, MDL_REMOVE, NULL);
			}
			if (--st->l2.sow) {
				st->l2.sow = 0;
				tei_handler(st, st->l2.vr,
					(struct sk_buff *) st->l2.va);
			}
			break;
		case (ID_CHK_REQ):
			FsmDelTimer(&st->l2.t200_timer, 1);
			FsmChangeState(&st->l2.l2m, ST_TEI_NOP);
			if (st->l3.debug) {
				sprintf(tmp, "checking identity for %d", ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if (ai == 0x7f) {
				ptr = *(st->l1.stlistp);
				while (ptr) {
					if ((ptr->l2.tei & 0x7f) != 0x7f) {
						if (st->l3.debug) {
							sprintf(tmp, "check response for tei %d",
								ptr->l2.tei);
							st->l2.l2m.printdebug(&st->l2.l2m, tmp);
						}
						/* send identity check response (user->network) */
						put_tei_msg(st, ID_CHK_RES, random_ri(), ptr->l2.tei);
					}
					ptr = ptr->next;
				}
			} else {
				otsp = findtei(st, ai);
				if (!otsp)
					break;
				if (st->l3.debug) {
					sprintf(tmp, "check response for tei %d",
						otsp->l2.tei);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				/* send identity check response (user->network) */
				put_tei_msg(st, ID_CHK_RES, random_ri(), otsp->l2.tei);
			}
			if (--st->l2.sow) {
				st->l2.sow = 0;
				tei_handler(st, st->l2.vr,
					(struct sk_buff *) st->l2.va);
			}
			break;
		case (ID_REMOVE):
			FsmChangeState(&st->l2.l2m, ST_TEI_NOP);
			if (st->l3.debug) {
				sprintf(tmp, "removal for %d", ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			if (ai == 0x7f) {
				ptr = *(st->l1.stlistp);
				while (ptr) {
					if ((ptr->l2.tei & 0x7f) != 0x7f) {
						if (st->l3.debug) {
							sprintf(tmp, "remove tei %d",
								ptr->l2.tei);
							st->l2.l2m.printdebug(&st->l2.l2m, tmp);
						}
						ptr->l2.tei = 255;
						ptr->ma.manl2(ptr, MDL_REMOVE, 0);
						cs = (struct IsdnCardState *) 
							ptr->l1.hardware;
						cs->cardmsg(cs, MDL_REMOVE, NULL);
					}
					ptr = ptr->next;
				}
			} else {
				otsp = findtei(st, ai);
				if (!otsp)
					break;
				if (st->l3.debug) {
					sprintf(tmp, "remove tei %d",
					     otsp->l2.tei);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				}
				otsp->l2.tei = 255;
				otsp->ma.manl2(otsp, MDL_REMOVE, 0);
				cs = (struct IsdnCardState *) otsp->l1.hardware;
				cs->cardmsg(cs, MDL_REMOVE, NULL);
			}
			if (--st->l2.sow) {
				st->l2.sow = 0;
				tei_handler(st, st->l2.vr,
					(struct sk_buff *) st->l2.va);
			}
			break;
		default:
			if (st->l3.debug) {
				sprintf(tmp, "message unknown %d ai %d", m_id, ai);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
	}
}


void
tei_handler(struct PStack *st,
	    u_char pr, struct sk_buff *skb)
{
	char tmp[32];
	int data = (int) skb;
	struct PStack *org;
	
	if (st->l2.sow) { /* request pending */
		if (st->l2.sow==1) {
			if ((pr != st->l2.uihsize) || (data !=st->l2.ihsize)) {
				st->l2.sow++;
				st->l2.vr = pr;
				st->l2.va = data;
			}
		} else {
			if (st->l3.debug) {
				sprintf(tmp, "more as 2 %d requests", pr);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
		}
		return;
	}
	
	switch (pr) {
		case (MDL_ASSIGN):
			org = (struct PStack *) data;
			st->l2.sow = 1;
			st->l2.uihsize = pr;
			st->l2.ihsize = data;
			org->l2.ri = random_ri();
			if (st->l3.debug) {
				sprintf(tmp, "assign request ri %d", org->l2.ri);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			put_tei_msg(st, ID_REQUEST, org->l2.ri, 127);
			FsmChangeState(&st->l2.l2m, ST_TEI_IDREQ);
			FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_T202,
				(void *)data, 1);
			st->l2.n200=3;
			break;
		case (MDL_VERIFY):
			st->l2.sow = 1;
			st->l2.uihsize = pr;
			if (st->l3.debug) {
				sprintf(tmp, "id verify request");
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			}
			put_tei_msg(st, ID_VERIFY, 0, data);
			FsmChangeState(&st->l2.l2m, ST_TEI_IDVERIFY);
			FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_T202,
				(void *)data, 2);
			st->l2.n200=2;
			break;
		default:
			break;
	}
}

static void
tei_l1l2(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	char tmp[32];
	
	switch (pr) {
		case (PH_DATA):
			if (skb->len<3) {
				sprintf(tmp, "short mgr frame %ld", skb->len);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			} else if (((skb->data[0]>>2) != TEI_SAPI) || 
				((skb->data[1]>>1) != GROUP_TEI)) {
				sprintf(tmp, "wrong mgr sapi/tei %x/%x",
					skb->data[0], skb->data[1]);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			} else if ((skb->data[2] & 0xef) != UI) {
				sprintf(tmp, "mgr frame is not ui %x",
					skb->data[2]);
				st->l2.l2m.printdebug(&st->l2.l2m, tmp);
			} else {
				skb_pull(skb, 3);
				if (skb->len<5) {
					sprintf(tmp, "short mgr frame %ld", skb->len);
					st->l2.l2m.printdebug(&st->l2.l2m, tmp);
				} else if (skb->data[0] != TEI_ENTITY_ID) {
				/* wrong management entity identifier, ignore */
					printk(KERN_WARNING "tei handler wrong entity id %x\n",
						skb->data[0]);
				} else
					get_tei_msg(st, skb->data[3],
						(skb->data[1] << 8) | skb->data[2],
						skb->data[4] >> 1);
			}
			dev_kfree_skb(skb, FREE_READ);
			break;
		default:
			break;
	}
}

static void
del_t202(struct FsmInst *fi, int event, void *arg)
{
        struct PStack *st = fi->userdata;

	FsmDelTimer(&st->l2.t200_timer, 1);
	FsmChangeState(fi, ST_TEI_NOP);
}

static void
id_req_t202(struct FsmInst *fi, int event, void *arg)
{
        struct PStack *l2st,*st = fi->userdata;
        int ri = (int) arg;
        char tmp[32];
        struct IsdnCardState *cs;

	if ((l2st = find_ri(st, ri))) {
		if (--st->l2.n200) {
			l2st->l2.ri = random_ri();
			put_tei_msg(st, ID_REQUEST, l2st->l2.ri, 127);
			FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_T202,
				(void *)l2st->l2.ri, 3);
		} else {
			l2st->l2.tei = 255;
			l2st->ma.manl2(l2st, MDL_REMOVE, 0);
			cs = (struct IsdnCardState *) l2st->l1.hardware;
			cs->cardmsg(cs, MDL_REMOVE, NULL);
		  	FsmChangeState(fi, ST_TEI_NOP);
			if (--st->l2.sow) {
				st->l2.sow = 0;
				tei_handler(st, st->l2.vr,
					(struct sk_buff *) st->l2.va);
			}
		}
	} else {
		sprintf(tmp, "t202 no %4x ri", ri);
		st->l2.l2m.printdebug(&st->l2.l2m, tmp);
		FsmChangeState(fi, ST_TEI_NOP);
	}
}        

static void
id_verify_t202(struct FsmInst *fi, int event, void *arg)
{
        struct PStack *l2st,*st = fi->userdata;
        int tei = (int) arg;
        char tmp[32];
	struct IsdnCardState *cs;

	if (--st->l2.n200) {
		put_tei_msg(st, ID_VERIFY, 0, (u_char) tei);
		FsmAddTimer(&st->l2.t200_timer, st->l2.t200, EV_T202, arg, 4);
	} else {
		if ((l2st = findtei(st, (u_char) tei))) {
			l2st->l2.tei = 255;
			l2st->ma.manl2(l2st, MDL_REMOVE, 0);
			cs = (struct IsdnCardState *) l2st->l1.hardware;
			cs->cardmsg(cs, MDL_REMOVE, NULL);
		} else {
			sprintf(tmp, "t202 no %2x tei", tei);
			st->l2.l2m.printdebug(&st->l2.l2m, tmp);
		}
		FsmChangeState(fi, ST_TEI_NOP);
		if (--st->l2.sow) {
			st->l2.sow = 0;
			tei_handler(st, st->l2.vr,
				(struct sk_buff *) st->l2.va);
		}
	}
}        


static void
dummy(struct PStack *st, int pr, void *arg) {};

static void
tei_man(struct PStack *st, int i, void *v)
{

	printk(KERN_DEBUG "tei_man\n");
}

static void
tei_l2tei(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *sp = st->l1.hardware;

	tei_handler(sp->teistack, pr, (struct sk_buff *) st);
}

void
setstack_tei(struct PStack *st)
{
	st->l2.l2tei = tei_l2tei;
}

static void
tei_debug(struct FsmInst *fi, char *s)
{
	struct PStack *st = fi->userdata;
	char tm[32], str[256];

	jiftime(tm, jiffies);
	sprintf(str, "%s %s %s\n", tm, st->l2.debug_id, s);
	HiSax_putstatus(st->l1.hardware, str);
}

void
init_tei(struct IsdnCardState *sp, int protocol)
{
	struct PStack *st;
	char tmp[128];

	st = (struct PStack *) kmalloc(sizeof(struct PStack), GFP_ATOMIC);
	setstack_HiSax(st, sp);
	st->l2.window = 1;
	st->l2.orig = !0;
	st->protocol = protocol;

	st->l2.t200 = 2000;	/* T202  2000 milliseconds */
	st->l2.n200 = 3;	/* try 3 times */

	st->l2.sap = 63;
	st->l2.tei = 127;
	st->l2.ri = -1;
	st->l2.ces = -1;

	sprintf(tmp, "Card %d tei", sp->cardnr + 1);
	st->l1.l1l2 = tei_l1l2;
	st->l3.l3l2 = dummy;
	st->ma.manl2 = dummy;

	st->l2.debug = 1;
	st->l2.l2m.fsm = &teifsm;
	st->l2.l2m.state = ST_TEI_NOP;
	st->l2.l2m.debug = 1;
	st->l2.l2m.userdata = st;
	st->l2.l2m.userint = 0;
	st->l2.l2m.printdebug = tei_debug;
	strcpy(st->l2.debug_id, tmp);
	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;
	FsmInitTimer(&st->l2.l2m, &st->l2.t200_timer);

	st->l2.debug = 0;
	st->l3.debug = 0;

	st->l2.l2l3 = (void *) tei_handler;
	st->l1.l1man = tei_man;
	st->l2.l2man = tei_man;
	st->l4.l2writewakeup = NULL;

	HiSax_addlist(sp, st);
	sp->teistack = st;
}

void
release_tei(struct IsdnCardState *sp)
{
	struct PStack *st = sp->teistack;

	FsmDelTimer(&st->l2.t200_timer, 1);
	HiSax_rmlist(sp, st);
	kfree((void *) st);
}

static struct FsmNode TeiFnList[] =
{
	{ST_TEI_NOP, EV_T202, del_t202},
	{ST_TEI_IDREQ, EV_T202, id_req_t202},
	{ST_TEI_IDVERIFY, EV_T202, id_verify_t202},
};

#define TEI_FN_COUNT (sizeof(TeiFnList)/sizeof(struct FsmNode))

void
TeiNew(void)
{
	teifsm.state_count = TEI_STATE_COUNT;
	teifsm.event_count = TEI_EVENT_COUNT;
	teifsm.strEvent = strTeiEvent;
	teifsm.strState = strTeiState;
	FsmNew(&teifsm, TeiFnList, TEI_FN_COUNT);
}

void
TeiFree(void)
{
	FsmFree(&teifsm);
}
