/* $Id$

 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log$
 * Revision 2.0  1997/07/27 21:15:43  keil
 * New Callref based layer3
 *
 * Revision 1.17  1997/06/26 11:11:46  keil
 * SET_SKBFREE now on creation of a SKB
 *
 * Revision 1.15  1997/04/17 11:50:48  keil
 * pa->loc was undefined, if it was not send by the exchange
 *
 * Old log removed /KKe
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"
#include "l3dss1.h"
#include <linux/ctype.h>

extern char *HiSax_getrev(const char *revision);
const char *dss1_revision = "$Revision$";

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	*ptr++ = 0x1; \
	*ptr++ = cref^0x80; \
	*ptr++ = mty

static void
l3dss1_message(struct l3_process *pc, u_char mt)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, pc->callref, mt);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_release_req(struct l3_process *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	newl3state(pc, 19);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_release_cmpl(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	p = skb->data;
	pc->para.loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			pc->para.loc = *p++;
		cause = *p & 0x7f;
	}
	dev_kfree_skb(skb, FREE_READ);
	StopAllL3Timer(pc);
	pc->para.cause = cause;
	newl3state(pc, 0);
	pc->st->l3.l3l4(pc, CC_RELEASE_CNF, NULL);
	release_l3_process(pc);
}

static void
l3dss1_setup_req(struct l3_process *pc, u_char pr,
		 void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char channel = 0;
	u_char screen = 0x80;
	u_char *teln;
	u_char *msn;
	int l;

	MsgHead(p, pc->callref, MT_SETUP);

	/*
	 * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
	 */
	*p++ = 0xa1;		/* complete indicator */
	switch (pc->para.setup.si1) {
		case 1:	/* Telephony                               */
			*p++ = 0x4;	/* BC-IE-code                              */
			*p++ = 0x3;	/* Length                                  */
			*p++ = 0x90;	/* Coding Std. CCITT, 3.1 kHz audio     */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                     */
			*p++ = 0xa3;	/* A-Law Audio                             */
			break;
		case 5:	/* Datatransmission 64k, BTX               */
		case 7:	/* Datatransmission 64k                    */
		default:
			*p++ = 0x4;	/* BC-IE-code                              */
			*p++ = 0x2;	/* Length                                  */
			*p++ = 0x88;	/* Coding Std. CCITT, unrestr. dig. Inform. */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                      */
			break;
	}
	/*
	 * What about info2? Mapping to High-Layer-Compatibility?
	 */
	teln = pc->para.setup.phone;
	if (*teln) {
		/* parse number for special things */
		if (!isdigit(*teln)) {
			switch (0x5f & *teln) {
				case 'C':
					channel = 0x08;
				case 'P':
					channel |= 0x80;
					teln++;
					if (*teln == '1')
						channel |= 0x01;
					else
						channel |= 0x02;
					break;
				case 'R':
					screen = 0xA0;
					break;
				case 'D':
					screen = 0x80;
					break;
				default:
					if (pc->debug & L3_DEB_WARN)
						l3_debug(pc->st, "Wrong MSN Code");
					break;
			}
			teln++;
		}
	}
	if (channel) {
		*p++ = 0x18;	/* channel indicator */
		*p++ = 1;
		*p++ = channel;
	}
	msn = pc->para.setup.eazmsn;
	if (*msn) {
		*p++ = 0x6c;
		*p++ = strlen(msn) + (screen ? 2 : 1);
		/* Classify as AnyPref. */
		if (screen) {
			*p++ = 0x01;	/* Ext = '0'B, Type = '000'B, Plan = '0001'B. */
			*p++ = screen;
		} else
			*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		while (*msn)
			*p++ = *msn++ & 0x7f;
	}
	*p++ = 0x70;
	*p++ = strlen(teln) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */

	while (*teln)
		*p++ = *teln++ & 0x7f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T303, CC_T303);
	newl3state(pc, 1);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_call_proc(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, 0x18, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		if ((!pc->para.bchannel) && (pc->debug & L3_DEB_WARN))
			l3_debug(pc->st, "setup answer without bchannel");
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup answer without bchannel");
	dev_kfree_skb(skb, FREE_READ);
	newl3state(pc, 3);
	L3AddTimer(&pc->timer, T310, CC_T310);
	pc->st->l3.l3l4(pc, CC_PROCEEDING_IND, NULL);
}

static void
l3dss1_setup_ack(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, 0x18, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		if ((!pc->para.bchannel) && (pc->debug & L3_DEB_WARN))
			l3_debug(pc->st, "setup answer without bchannel");
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup answer without bchannel");
	dev_kfree_skb(skb, FREE_READ);
	newl3state(pc, 2);
	L3AddTimer(&pc->timer, T304, CC_T304);
	pc->st->l3.l3l4(pc, CC_MORE_INFO, NULL);
}

static void
l3dss1_disconnect(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	StopAllL3Timer(pc);
	p = skb->data;
	pc->para.loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			pc->para.loc = *p++;
		cause = *p & 0x7f;
	}
	dev_kfree_skb(skb, FREE_READ);
	newl3state(pc, 12);
	pc->para.cause = cause;
	pc->st->l3.l3l4(pc, CC_DISCONNECT_IND, NULL);
}

static void
l3dss1_connect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb, FREE_READ);
	L3DelTimer(&pc->timer);	/* T310 */
	newl3state(pc, 10);
	pc->st->l3.l3l4(pc, CC_SETUP_CNF, NULL);
}

static void
l3dss1_alerting(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb, FREE_READ);
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	pc->st->l3.l3l4(pc, CC_ALERTING_IND, NULL);
}

static void
l3dss1_setup(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;

	p = skb->data;

	/*
	 * Channel Identification
	 */
	p = skb->data;
	if ((p = findie(p, skb->len, 0x18, 0))) {
		pc->para.bchannel = p[2] & 0x3;
		if (pc->para.bchannel)
			bcfound++;
		else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup without bchannel");
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup without bchannel");

	/*
	   * Bearer Capabilities
	 */
	p = skb->data;
	if ((p = findie(p, skb->len, 0x04, 0))) {
		pc->para.setup.si2 = 0;
		switch (p[2] & 0x1f) {
			case 0x00:
				/* Speech */
			case 0x10:
				/* 3.1 Khz audio */
				pc->para.setup.si1 = 1;
				break;
			case 0x08:
				/* Unrestricted digital information */
				pc->para.setup.si1 = 7;
				break;
			case 0x09:
				/* Restricted digital information */
				pc->para.setup.si1 = 2;
				break;
			case 0x11:
				/* Unrestr. digital information  with tones/announcements */
				pc->para.setup.si1 = 3;
				break;
			case 0x18:
				/* Video */
				pc->para.setup.si1 = 4;
				break;
			default:
				pc->para.setup.si1 = 0;
		}
	} else if (pc->debug & L3_DEB_WARN)
		l3_debug(pc->st, "setup without bearer capabilities");

	p = skb->data;
	if ((p = findie(p, skb->len, 0x70, 0)))
		iecpy(pc->para.setup.eazmsn, p, 1);
	else
		pc->para.setup.eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, 0x6c, 0))) {
		pc->para.setup.plan = p[2];
		if (p[2] & 0x80) {
			iecpy(pc->para.setup.phone, p, 1);
			pc->para.setup.screen = 0;
		} else {
			iecpy(pc->para.setup.phone, p, 2);
			pc->para.setup.screen = p[3];
		}
	} else {
		pc->para.setup.phone[0] = 0;
		pc->para.setup.plan = 0;
		pc->para.setup.screen = 0;
	}
	dev_kfree_skb(skb, FREE_READ);

	if (bcfound) {
		if ((pc->para.setup.si1 != 7) && (pc->debug & L3_DEB_WARN)) {
			sprintf(tmp, "non-digital call: %s -> %s",
				pc->para.setup.phone,
				pc->para.setup.eazmsn);
			l3_debug(pc->st, tmp);
		}
		newl3state(pc, 6);
		pc->st->l3.l3l4(pc, CC_SETUP_IND, NULL);
	} else
		release_l3_process(pc);
}

static void
l3dss1_reset(struct l3_process *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3dss1_setup_rsp(struct l3_process *pc, u_char pr,
		 void *arg)
{
	newl3state(pc, 8);
	l3dss1_message(pc, MT_CONNECT);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3dss1_connect_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb, FREE_READ);
	newl3state(pc, 10);
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_SETUP_COMPLETE_IND, NULL);
}

static void
l3dss1_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x10;

	if (pc->para.cause > 0)
		cause = pc->para.cause;

	StopAllL3Timer(pc);

	MsgHead(p, pc->callref, MT_DISCONNECT);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 11);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3dss1_reject_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x95;

	if (pc->para.cause > 0)
		cause = pc->para.cause;

	MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	pc->st->l3.l3l4(pc, CC_RELEASE_IND, NULL);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_release(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int cause = -1;

	p = skb->data;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		if (*p++ == 2)
			pc->para.loc = *p++;
		cause = *p & 0x7f;
	}
	dev_kfree_skb(skb, FREE_READ);
	StopAllL3Timer(pc);
	pc->para.cause = cause;
	l3dss1_message(pc, MT_RELEASE_COMPLETE);
	pc->st->l3.l3l4(pc, CC_RELEASE_IND, NULL);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_alert_req(struct l3_process *pc, u_char pr,
		 void *arg)
{
	newl3state(pc, 7);
	l3dss1_message(pc, MT_ALERTING);
}

static void
l3dss1_status_enq(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb, FREE_READ);

	MsgHead(p, pc->callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x9E;		/* answer status enquire */

	*p++ = 0x14;		/* CallState */
	*p++ = 0x1;
	*p++ = pc->state & 0x3f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

static void
l3dss1_t303(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		L3DelTimer(&pc->timer);
		l3dss1_setup_req(pc, pr, arg);
	} else {
		L3DelTimer(&pc->timer);
		pc->st->l3.l3l4(pc, CC_NOSETUP_RSP_ERR, NULL);
		release_l3_process(pc);
	}
}

static void
l3dss1_t304(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3dss1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_SETUP_ERR, NULL);

}

static void
l3dss1_t305(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;
	u_char cause = 0x90;

	L3DelTimer(&pc->timer);
	if (pc->para.cause > 0)
		cause = pc->para.cause;

	MsgHead(p, pc->callref, MT_RELEASE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = cause;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 19);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_t310(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3dss1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_SETUP_ERR, NULL);
}

static void
l3dss1_t313(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3dss1_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc, CC_CONNECT_ERR, NULL);
}

static void
l3dss1_t308_1(struct l3_process *pc, u_char pr, void *arg)
{
	newl3state(pc, 19);
	L3DelTimer(&pc->timer);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_2);
}

static void
l3dss1_t308_2(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_RELEASE_ERR, NULL);
	release_l3_process(pc);
}

static void
l3dss1_restart(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc, CC_DLRL, NULL);
	release_l3_process(pc);
}

static void
l3dss1_status(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	char tmp[64], *t;
	int l;
	struct sk_buff *skb = arg;
	
	p = skb->data;
	t = tmp;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		l = *p++;
		t += sprintf(t,"Status CR %x Cause:", pc->callref);
		while (l--)
			t += sprintf(t," %2x",*p++);
	} else
		sprintf(t,"Status CR %x no Cause", pc->callref);
	l3_debug(pc->st, tmp);
	p = skb->data;
	t = tmp;
	t += sprintf(t,"Status state %x ", pc->state);
	if ((p = findie(p, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1== *p++)
			t += sprintf(t,"peer state %x" , *p);
		else
			t += sprintf(t,"peer state len error");
	} else
		sprintf(t,"no peer state");
	l3_debug(pc->st, tmp);
	dev_kfree_skb(skb, FREE_READ);
}

static void
l3dss1_global_restart(struct l3_process *pc, u_char pr, void *arg)
{
	u_char tmp[32];
	u_char *p;
	u_char ri;
	int l;
	struct sk_buff *skb = arg;
	struct l3_process *up;
	
	newl3state(pc, 2);
	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_RESTART_IND, 0))) {
	        ri = p[2];
	        sprintf(tmp, "Restart %x", ri);
	} else {
		sprintf(tmp, "Restart without restart IE");
		ri = 0x86;
	}
	l3_debug(pc->st, tmp);
	dev_kfree_skb(skb, FREE_READ);
	newl3state(pc, 2);
	up = pc->st->l3.proc;
	while (up) {
		up->st->lli.l4l3(up->st, CC_RESTART, up);
		up = up->next;
	}
	p = tmp;
	MsgHead(p, pc->callref, MT_RESTART_ACKNOWLEDGE);
	*p++ = 0x79; /* RESTART Ind */
	*p++ = 1;
	*p++ = ri;
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 0);
	pc->st->l3.l3l2(pc->st, DL_DATA, skb);
}

/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_SETUP_REQ, l3dss1_setup_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(10),
	 CC_DISCONNECT_REQ, l3dss1_disconnect_req},
	{SBIT(12),
	 CC_RELEASE_REQ, l3dss1_release_req},
	{ALL_STATES,
	 CC_DLRL, l3dss1_reset},
	{ALL_STATES,
	 CC_RESTART, l3dss1_restart},
	{SBIT(6),
	 CC_IGNORE, l3dss1_reset},
	{SBIT(6),
	 CC_REJECT_REQ, l3dss1_reject_req},
	{SBIT(6),
	 CC_ALERTING_REQ, l3dss1_alert_req},
	{SBIT(6) | SBIT(7),
	 CC_SETUP_RSP, l3dss1_setup_rsp},
	{SBIT(1),
	 CC_T303, l3dss1_t303},
	{SBIT(2),
	 CC_T304, l3dss1_t304},
	{SBIT(3),
	 CC_T310, l3dss1_t310},
	{SBIT(8),
	 CC_T313, l3dss1_t313},
	{SBIT(11),
	 CC_T305, l3dss1_t305},
	{SBIT(19),
	 CC_T308_1, l3dss1_t308_1},
	{SBIT(19),
	 CC_T308_2, l3dss1_t308_2},
};

static int downsllen = sizeof(downstatelist) /
sizeof(struct stateentry);

static struct stateentry datastatelist[] =
{
	{ALL_STATES,
	 MT_STATUS_ENQUIRY, l3dss1_status_enq},
	{ALL_STATES,
	 MT_STATUS, l3dss1_status},
	{SBIT(0) | SBIT(6),
	 MT_SETUP, l3dss1_setup},
	{SBIT(1) | SBIT(2),
	 MT_CALL_PROCEEDING, l3dss1_call_proc},
	{SBIT(1),
	 MT_SETUP_ACKNOWLEDGE, l3dss1_setup_ack},
	{SBIT(1) | SBIT(2) | SBIT(3),
	 MT_ALERTING, l3dss1_alerting},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19),
	 MT_RELEASE_COMPLETE, l3dss1_release_cmpl},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19),
	 MT_RELEASE, l3dss1_release},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10),
	 MT_DISCONNECT, l3dss1_disconnect},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4),
	 MT_CONNECT, l3dss1_connect},
	{SBIT(8),
	 MT_CONNECT_ACKNOWLEDGE, l3dss1_connect_ack},
};

static int datasllen = sizeof(datastatelist) / sizeof(struct stateentry);

static struct stateentry globalmes_list[] =
{
	{ALL_STATES,
         MT_STATUS, l3dss1_status},
	{SBIT(0),
	 MT_RESTART, l3dss1_global_restart},
/*	{SBIT(1),
	 MT_RESTART_ACKNOWLEDGE, l3dss1_restart_ack},                                  
*/
};
static int globalm_len = sizeof(globalmes_list) / sizeof(struct stateentry);

#if 0
static struct stateentry globalcmd_list[] =
{
	{ALL_STATES,
         CC_STATUS, l3dss1_status_req},
	{SBIT(0),
	 CC_RESTART, l3dss1_restart_req},
};

static int globalc_len = sizeof(globalcmd_list) / sizeof(struct stateentry);
#endif
/* *INDENT-ON* */

static void
global_handler(struct PStack *st, int mt, struct sk_buff *skb)
{
	int i;
	char tmp[64];
	struct l3_process *proc = st->l3.global;
	
	for (i = 0; i < globalm_len; i++)
		if ((mt == globalmes_list[i].primitive) &&
		    ((1 << proc->state) & globalmes_list[i].state))
			break;
	if (i == globalm_len) {
		dev_kfree_skb(skb, FREE_READ);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1 global state %d mt %x unhandled",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		return;
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1 global %d mt %x",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		globalmes_list[i].rout(proc, mt, skb);
	}
}

static void
dss1up(struct PStack *st, int pr, void *arg)
{
	int i, mt, cr;
	struct sk_buff *skb = arg;
	struct l3_process *proc;
	char tmp[80];

	if (skb->data[0] != PROTO_DIS_EURO) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "dss1up%sunexpected discriminator %x message len %ld",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				skb->data[0], skb->len);
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb, FREE_READ);
		return;
	}
	cr = getcallref(skb->data);
	mt = skb->data[skb->data[1] + 2];
	if (!cr) {				/* Global CallRef */
		global_handler(st, mt, skb);
		return;
	} else if (cr == -1) {			/* Dummy Callref */
		dev_kfree_skb(skb, FREE_READ);
		return;
	} else if (!(proc = getl3proc(st, cr))) {
		if (mt == MT_SETUP) {
			if (!(proc = new_l3_process(st, cr))) {
				dev_kfree_skb(skb, FREE_READ);
				return;
			}
		} else {
			dev_kfree_skb(skb, FREE_READ);
			return;
		}
	}
	for (i = 0; i < datasllen; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == datasllen) {
		dev_kfree_skb(skb, FREE_READ);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1up%sstate %d mt %x unhandled",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		return;
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1up%sstate %d mt %x",
				(pr == DL_DATA) ? " " : "(broadcast) ",
				proc->state, mt);
			l3_debug(st, tmp);
		}
		datastatelist[i].rout(proc, pr, skb);
	}
}

static void
dss1down(struct PStack *st, int pr, void *arg)
{
	int i, cr;
	struct l3_process *proc;
	struct Channel *chan;
	char tmp[80];

	if (CC_SETUP_REQ == pr) {
		chan = arg;
		cr = newcallref();
		cr |= 0x80;
		if (!(proc = new_l3_process(st, cr))) {
			return;
		} else {
			proc->chan = chan;
			chan->proc = proc;
			proc->para.setup = chan->setup;
			proc->callref = cr;
		}
	} else {
		proc = arg;
	}
	for (i = 0; i < downsllen; i++)
		if ((pr == downstatelist[i].primitive) &&
		    ((1 << proc->state) & downstatelist[i].state))
			break;
	if (i == downsllen) {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1down state %d prim %d unhandled",
				proc->state, pr);
			l3_debug(st, tmp);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "dss1down state %d prim %d",
				proc->state, pr);
			l3_debug(st, tmp);
		}
		downstatelist[i].rout(proc, pr, arg);
	}
}

void
setstack_dss1(struct PStack *st)
{
	char tmp[64];

	st->lli.l4l3 = dss1down;
	st->l2.l2l3 = dss1up;
	st->l3.N303 = 1;
	if (!(st->l3.global = kmalloc(sizeof(struct l3_process), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for dss1 global CR\n");
	} else {
		st->l3.global->state = 0;
		st->l3.global->callref = 0;
		st->l3.global->next = NULL;
		st->l3.global->debug = L3_DEB_WARN;
		st->l3.global->st = st;
		st->l3.global->N303 = 1;
		L3InitTimer(st->l3.global, &st->l3.global->timer);
	}
	strcpy(tmp, dss1_revision);
	printk(KERN_NOTICE "HiSax: DSS1 Rev. %s\n", HiSax_getrev(tmp));
}
