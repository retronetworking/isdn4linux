/* $Id$

 * Author       Karsten Keil (keil@isdn4linux.de)
 *              based on the teles driver from Jan den Ouden
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 */

const char *lli_revision = "$Revision$";

#include "hisax.h"
#include "callc.h"
#include "../avmb1/capicmd.h"  /* this should be moved in a common place */

#define HISAX_STATUS_BUFSIZE 4096

static struct CallcIf *c_ifs[HISAX_MAX_CARDS] = { 0, };

static int init_b_st(struct Channel *chanp, int incoming);
static void release_b_st(struct Channel *chanp);

static struct Fsm callcfsm =
{NULL, 0, 0, NULL, NULL};

/* experimental REJECT after ALERTING for CALLBACK to beat the 4s delay */
#define ALERT_REJECT 0

/* Value to delay the sending of the first B-channel paket after CONNECT
 * here is no value given by ITU, but experience shows that 300 ms will
 * work on many networks, if you or your other side is behind local exchanges
 * a greater value may be recommented. If the delay is to short the first paket
 * will be lost and autodetect on many comercial routers goes wrong !
 * You can adjust this value on runtime with
 * hisaxctrl <id> 2 <value>
 * value is in milliseconds
 */
#define DEFAULT_B_DELAY	300

/* Flags for remembering action done in lli */

#define  FLG_START_B	0

/*
 * Find CallcIf with given driverId
 */

static inline struct CallcIf *
findCallcIf(int driverid)
{
	int i;

	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (c_ifs[i])
			if (c_ifs[i]->myid == driverid)
				return c_ifs[i];
	return 0;
}

int
discard_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;
	int ret=0;

	while ((skb = skb_dequeue(q))) {
		idev_kfree_skb(skb, FREE_READ);
		ret++;
	}
	return(ret);
}

#define LL_DEB_INFO      0x00000001
#define LL_DEB_BUFFERING 0x00000800
#define LL_DEB_WARN      0x10000000

static void
ll_debug(int level, struct IsdnCardState *cs, char *fmt, ...)
{
	va_list args;

	if (!(cs->c_if->channel[0].debug & level))
		return;

	va_start(args, fmt);
	VHiSax_putstatus(cs, "LL ", fmt, args);
	va_end(args);
}

static void
link_debug(int level, struct Channel *chanp, int direction, char *fmt, ...)
{
	va_list args;
	char tmp[16];

	if (!(chanp->debug & level))
		return;

	va_start(args, fmt);
	sprintf(tmp, "Ch%d %s ", chanp->chan,
		direction ? "LL->HL" : "HL->LL");
	VHiSax_putstatus(chanp->cs, tmp, fmt, args);
	va_end(args);
}

static inline int 
statcallb(struct IsdnCardState *cs, int command, isdn_ctrl *ic)
{
	switch (command) {
	case ISDN_STAT_RUN:
		ll_debug(LL_DEB_INFO, cs, "STAT_RUN");
		break;
	case ISDN_STAT_STOP:
		ll_debug(LL_DEB_INFO, cs, "STAT_STOP");
		break;
	case ISDN_STAT_UNLOAD:
		ll_debug(LL_DEB_INFO, cs, "STAT_UNLOAD");
		break;
	case ISDN_STAT_DISCH:
		ll_debug(LL_DEB_INFO, cs, "STAT_DISCH");
		break;
	}

	ic->driver = cs->c_if->myid;
	ic->command = command;
	return cs->c_if->iif.statcallb(ic);
}
 
static inline int
HL_LL(struct Channel *chanp, int command, isdn_ctrl *ic)
{
	int ret;

	switch (command) {
	case ISDN_STAT_ICALL:
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_ICALL");
		break;
	case ISDN_STAT_ICALLW:
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_ICALLW");
		break;			       
	case ISDN_STAT_DCONN:		       
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_DCONN");
		break;			       
	case ISDN_STAT_DHUP:		       
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_DHUP");
		break;			       
	case ISDN_STAT_CAUSE:		       
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_CAUSE");
		break;			       
	case ISDN_STAT_BCONN:		       
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_BCONN %s", ic->parm.num);
		break;			       
	case ISDN_STAT_BHUP:		       
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_BHUP");
		break;			       
	case ISDN_STAT_BSENT:		       
		link_debug(LL_DEB_INFO, chanp, 0, "STAT_BSENT");
		break;
	}
	ic->arg = chanp->chan;
	ret = statcallb(chanp->cs, command, ic);
	if (command == ISDN_STAT_ICALL || command == ISDN_STAT_ICALLW) {
		link_debug(LL_DEB_INFO, chanp, 0, "ret = %d", ret);
	}
	return ret;
}

enum {
	ST_NULL,		/*  0 inactive */
	ST_OUT_DIAL,		/*  1 outgoing, SETUP send; awaiting confirm */
	ST_IN_WAIT_LL,		/*  2 incoming call received; wait for LL confirm */
	ST_IN_ALERT_SENT,	/*  3 incoming call received; ALERT send */
	ST_IN_WAIT_CONN_ACK,	/*  4 incoming CONNECT send; awaiting CONN_ACK */
	ST_WAIT_BCONN,		/*  5 CONNECT/CONN_ACK received, awaiting b-channel prot. estbl. */
	ST_ACTIVE,		/*  6 active, b channel prot. established */
	ST_WAIT_BRELEASE,	/*  7 call clear. (initiator), awaiting b channel prot. rel. */
	ST_WAIT_BREL_DISC,	/*  8 call clear. (receiver), DISCONNECT req. received */
	ST_WAIT_DCOMMAND,	/*  9 call clear. (receiver), awaiting DCHANNEL message */
	ST_WAIT_DRELEASE,	/* 10 DISCONNECT sent, awaiting RELEASE */
	ST_WAIT_D_REL_CNF,	/* 11 RELEASE sent, awaiting RELEASE confirm */
	ST_IN_PROCEED_SEND,	/* 12 incoming call, proceeding send */ 
};
  

#define STATE_COUNT (ST_IN_PROCEED_SEND + 1)

static char *strState[] =
{
	"ST_NULL",
	"ST_OUT_DIAL",
	"ST_IN_WAIT_LL",
	"ST_IN_ALERT_SENT",
	"ST_IN_WAIT_CONN_ACK",
	"ST_WAIT_BCONN",
	"ST_ACTIVE",
	"ST_WAIT_BRELEASE",
	"ST_WAIT_BREL_DISC",
	"ST_WAIT_DCOMMAND",
	"ST_WAIT_DRELEASE",
	"ST_WAIT_D_REL_CNF",
	"ST_IN_PROCEED_SEND",
};

enum {
	EV_DIAL,		/*  0 */
	EV_SETUP_CNF,		/*  1 */
	EV_ACCEPTB,		/*  2 */
	EV_DISCONNECT_IND,	/*  3 */
	EV_RELEASE, 		/*  4 */
	EV_LEASED,		/*  5 */
	EV_LEASED_REL,		/*  6 */
	EV_SETUP_IND,		/*  7 */
	EV_ACCEPTD,		/*  8 */
	EV_SETUP_CMPL_IND,	/*  9 */
	EV_BC_EST,		/* 10 */
	EV_WRITEBUF,		/* 11 */
	EV_HANGUP,		/* 12 */
	EV_BC_REL,		/* 13 */
	EV_CINF,		/* 14 */
	EV_SUSPEND,		/* 15 */
	EV_RESUME,		/* 16 */
	EV_NOSETUP_RSP,		/* 17 */
	EV_SETUP_ERR,		/* 18 */
	EV_CONNECT_ERR,		/* 19 */
	EV_PROCEED,		/* 20 */
	EV_ALERT,		/* 21 */ 
	EV_REDIR,		/* 22 */ 
};

#define EVENT_COUNT (EV_REDIR + 1)

static char *strEvent[] =
{
	"EV_DIAL",
	"EV_SETUP_CNF",
	"EV_ACCEPTB",
	"EV_DISCONNECT_IND",
	"EV_RELEASE",
	"EV_LEASED",
	"EV_LEASED_REL",
	"EV_SETUP_IND",
	"EV_ACCEPTD",
	"EV_SETUP_CMPL_IND",
	"EV_BC_EST",
	"EV_WRITEBUF",
	"EV_HANGUP",
	"EV_BC_REL",
	"EV_CINF",
	"EV_SUSPEND",
	"EV_RESUME",
	"EV_NOSETUP_RSP",
	"EV_SETUP_ERR",
	"EV_CONNECT_ERR",
	"EV_PROCEED",
	"EV_ALERT",
	"EV_REDIR",
};

static inline void
lli_deliver_cause(struct Channel *chanp)
{
	isdn_ctrl ic;

	if (chanp->proc->para.cause == NO_CAUSE)
		return;
	if (chanp->cs->protocol == ISDN_PTYPE_EURO)
		sprintf(ic.parm.num, "E%02X%02X", chanp->proc->para.loc & 0x7f,
			chanp->proc->para.cause & 0x7f);
	else
		sprintf(ic.parm.num, "%02X%02X", chanp->proc->para.loc & 0x7f,
			chanp->proc->para.cause & 0x7f);
	HL_LL(chanp, ISDN_STAT_CAUSE, &ic);
}

static inline void
lli_close(struct FsmInst *fi)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_NULL);
	chanp->Flags = 0;
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
}

static void
lli_leased_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
	int ret;

	if (!chanp->leased)
		return;
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
	FsmChangeState(fi, ST_IN_WAIT_LL);
	link_debug(LL_DEB_INFO, chanp, 0, "STAT_ICALL_LEASED");
	ic.parm.setup.si1 = 7;
	ic.parm.setup.si2 = 0;
	ic.parm.setup.plan = 0;
	ic.parm.setup.screen = 0;
	sprintf(ic.parm.setup.eazmsn,"%d", chanp->chan + 1);
	sprintf(ic.parm.setup.phone,"LEASED%d", chanp->cs->c_if->myid);
	ret = HL_LL(chanp, (chanp->chan < 2) ? ISDN_STAT_ICALL : ISDN_STAT_ICALLW, &ic);
	if (!ret) {
		chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
		FsmChangeState(fi, ST_NULL);
	}
}


/*
 * Dial out
 */
static void
lli_init_bchan_out(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	HL_LL(chanp, ISDN_STAT_DCONN, &ic);
	init_b_st(chanp, 0);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lli_prep_dialout(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmDelTimer(&chanp->drel_timer, 60);
	FsmDelTimer(&chanp->dial_timer, 73);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = 0;
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
	if (chanp->leased) {
		lli_init_bchan_out(fi, event, arg);
	} else {
		FsmChangeState(fi, ST_OUT_DIAL);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP | REQUEST, chanp);
	}
}

static void
lli_resume(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmDelTimer(&chanp->drel_timer, 60);
	FsmDelTimer(&chanp->dial_timer, 73);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = 0;
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
	if (chanp->leased) {
		lli_init_bchan_out(fi, event, arg);
	} else {
		FsmChangeState(fi, ST_OUT_DIAL);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_RESUME | REQUEST, chanp);
	}
}

static void
lli_go_active(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;


	FsmChangeState(fi, ST_ACTIVE);
	chanp->data_open = !0;
	if (chanp->bcs->conmsg)
		strcpy(ic.parm.num, chanp->bcs->conmsg);
	else
		ic.parm.num[0] = 0;
	HL_LL(chanp, ISDN_STAT_BCONN, &ic);
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_CONN, (void *) (long)chanp->chan);
}


/*
 * RESUME
 */

/* incomming call */

static void
lli_deliver_call(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
	int ret;

	chanp->cs->cardmsg(chanp->cs, MDL_INFO_SETUP, (void *) (long)chanp->chan);
	/*
	 * Report incoming calls only once to linklevel, use CallFlags
	 * which is set to 3 with each broadcast message in isdnl1.c
	 * and resetted if a interface  answered the STAT_ICALL.
	 */
	if (1) { /* for only one TEI */
		FsmChangeState(fi, ST_IN_WAIT_LL);
		/*
		 * No need to return "unknown" for calls without OAD,
		 * cause that's handled in linklevel now (replaced by '0')
		 */
		ic.parm.setup = chanp->proc->para.setup;
		ret = HL_LL(chanp, (chanp->chan < 2) ? ISDN_STAT_ICALL : ISDN_STAT_ICALLW, &ic);
		link_debug(LL_DEB_INFO, chanp, 1, "statcallb ret=%d", ret);

		switch (ret) {
			case 1:	/* OK, someone likes this call */
				FsmDelTimer(&chanp->drel_timer, 61);
				FsmChangeState(fi, ST_IN_ALERT_SENT);
				chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING | REQUEST, chanp->proc);
				break;
			case 5: /* direct redirect */
			case 4: /* Proceeding desired */
				FsmDelTimer(&chanp->drel_timer, 61);
				FsmChangeState(fi, ST_IN_PROCEED_SEND);
				chanp->d_st->lli.l4l3(chanp->d_st, CC_PROCEED_SEND | REQUEST, chanp->proc);
				if (ret == 5) {
					chanp->setup = ic.parm.setup;
					chanp->d_st->lli.l4l3(chanp->d_st, CC_REDIR | REQUEST, chanp->proc);
				}
				break;
			case 2:	/* Rejecting Call */
				break;
			case 0:	/* OK, nobody likes this call */
			default:	/* statcallb problems */
				chanp->d_st->lli.l4l3(chanp->d_st, CC_IGNORE | REQUEST, chanp->proc);
				chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
				FsmChangeState(fi, ST_NULL);
				break;
		}
	} else {
		chanp->d_st->lli.l4l3(chanp->d_st, CC_IGNORE | REQUEST, chanp->proc);
		chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
	}
}

static void
lli_send_dconnect(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP | RESPONSE, chanp->proc);
}

static void
lli_send_alert(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	FsmChangeState(fi, ST_IN_ALERT_SENT);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING | REQUEST, chanp->proc);
}

static void
lli_send_redir(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->d_st->lli.l4l3(chanp->d_st, CC_REDIR | REQUEST, chanp->proc);
}

static void
lli_init_bchan_in(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_BCONN);
	HL_LL(chanp, ISDN_STAT_DCONN, &ic);
	chanp->l2_active_protocol = chanp->l2_protocol;
	chanp->incoming = !0;
	init_b_st(chanp, !0);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lli_setup_rsp(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	if (chanp->leased) {
		lli_init_bchan_in(fi, event, arg);
	} else {
		FsmChangeState(fi, ST_IN_WAIT_CONN_ACK);
#ifdef WANT_ALERT
		chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING | REQUEST, chanp->proc);
#endif
		chanp->d_st->lli.l4l3(chanp->d_st, CC_SETUP | RESPONSE, chanp->proc);
	}
}

/* Call suspend */

static void
lli_suspend(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->d_st->lli.l4l3(chanp->d_st, CC_SUSPEND | REQUEST, chanp->proc);
}

/* Call clearing */

static void
lli_leased_hup(struct FsmInst *fi, struct Channel *chanp)
{
	isdn_ctrl ic;

	sprintf(ic.parm.num, "L0010");
	HL_LL(chanp, ISDN_STAT_CAUSE, &ic);
	HL_LL(chanp, ISDN_STAT_DHUP, &ic);
	lli_close(fi);
}

static void
lli_disconnect_req(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	if (chanp->leased) {
		lli_leased_hup(fi, chanp);
	} else {
		FsmChangeState(fi, ST_WAIT_DRELEASE);
		chanp->proc->para.cause = 0x10;	/* Normal Call Clearing */
		chanp->d_st->lli.l4l3(chanp->d_st, CC_DISCONNECT | REQUEST,
			chanp->proc);
	}
}

static void
lli_disconnect_reject(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	if (chanp->leased) {
		lli_leased_hup(fi, chanp);
	} else {
		FsmChangeState(fi, ST_WAIT_DRELEASE);
		chanp->proc->para.cause = 0x15;	/* Call Rejected */
		chanp->d_st->lli.l4l3(chanp->d_st, CC_DISCONNECT | REQUEST,
			chanp->proc);
	}
}

static void
lli_dhup_close(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	if (chanp->leased) {
		lli_leased_hup(fi, chanp);
	} else {
		lli_deliver_cause(chanp);
		HL_LL(chanp, ISDN_STAT_DHUP, &ic);
		lli_close(fi);
	}
}

static void
lli_reject_req(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	if (chanp->leased) {
		lli_leased_hup(fi, chanp);
		return;
	}
#ifndef ALERT_REJECT
	chanp->proc->para.cause = 0x15;	/* Call Rejected */
	chanp->d_st->lli.l4l3(chanp->d_st, CC_REJECT | REQUEST, chanp->proc);
	lli_dhup_close(fi, event, arg);
#else
	FsmRestartTimer(&chanp->drel_timer, 40, EV_HANGUP, NULL, 63);
	FsmChangeState(fi, ST_IN_ALERT_SENT);
	chanp->d_st->lli.l4l3(chanp->d_st, CC_ALERTING | REQUEST, chanp->proc);
#endif
}

static void
lli_disconn_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_BRELEASE);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
}

static void
lli_start_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	if (chanp->leased) {
		lli_leased_hup(fi, chanp);
	} else {
		lli_disconnect_req(fi, event, arg);
	}
}

static void
lli_rel_b_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	release_b_st(chanp);
	lli_start_disc(fi, event, arg);
}

static void
lli_bhup_disc(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
 
	HL_LL(chanp, ISDN_STAT_BHUP, &ic);
	lli_rel_b_disc(fi, event, arg);
}

static void
lli_bhup_rel_b(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_WAIT_DCOMMAND);
	chanp->data_open = 0;
	HL_LL(chanp, ISDN_STAT_BHUP, &ic);
	release_b_st(chanp);
}

static void
lli_release_bchan(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	FsmChangeState(fi, ST_WAIT_BREL_DISC);
	chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
}


static void
lli_rel_b_dhup(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	release_b_st(chanp);
	lli_dhup_close(fi, event, arg);
}

static void
lli_bhup_dhup(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	HL_LL(chanp, ISDN_STAT_BHUP, &ic);
	lli_rel_b_dhup(fi, event, arg);
}

static void
lli_abort(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
	lli_bhup_dhup(fi, event, arg);
}
 
static void
lli_release_req(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	if (chanp->leased) {
		lli_leased_hup(fi, chanp);
	} else {
		FsmChangeState(fi, ST_WAIT_D_REL_CNF);
		chanp->d_st->lli.l4l3(chanp->d_st, CC_RELEASE | REQUEST,
			chanp->proc);
	}
}

static void
lli_rel_b_release_req(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	release_b_st(chanp);
	lli_release_req(fi, event, arg);
}

static void
lli_bhup_release_req(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;
 
	HL_LL(chanp, ISDN_STAT_BHUP, &ic);
	lli_rel_b_release_req(fi, event, arg);
}


/* processing charge info */
static void
lli_charge_info(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	sprintf(ic.parm.num, "%d", chanp->proc->para.chargeinfo);
	HL_LL(chanp, ISDN_STAT_CINF, &ic);
}

/* error procedures */

static void
lli_dchan_not_ready(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	HL_LL(chanp, ISDN_STAT_DHUP, &ic); 
}

static void
lli_no_setup_rsp(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	HL_LL(chanp, ISDN_STAT_DHUP, &ic);
	lli_close(fi); 
}

static void
lli_error(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_WAIT_DRELEASE);
}

static void
lli_failure_l(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	FsmChangeState(fi, ST_NULL);
	sprintf(ic.parm.num, "L%02X%02X", 0, 0x2f);
	HL_LL(chanp, ISDN_STAT_CAUSE, &ic);
	HL_LL(chanp, ISDN_STAT_DHUP, &ic);
	chanp->Flags = 0;
	chanp->cs->cardmsg(chanp->cs, MDL_INFO_REL, (void *) (long)chanp->chan);
}

static void
lli_rel_b_fail(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	release_b_st(chanp);
	lli_failure_l(fi, event, arg);
}

static void
lli_bhup_fail(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;
	isdn_ctrl ic;

	HL_LL(chanp, ISDN_STAT_BHUP, &ic);
	lli_rel_b_fail(fi, event, arg);
}

static void
lli_failure_a(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp = fi->userdata;

	chanp->data_open = 0;
	chanp->b_st->lli.l4l3(chanp->b_st, DL_RELEASE | REQUEST, NULL);
	lli_bhup_fail(fi, event, arg);
}

/* *INDENT-OFF* */
static struct FsmNode fnlist[] HISAX_INITDATA =
{
        {ST_NULL,               EV_DIAL,                lli_prep_dialout},
        {ST_NULL,               EV_RESUME,              lli_resume},
        {ST_NULL,               EV_SETUP_IND,           lli_deliver_call},
        {ST_NULL,               EV_LEASED,              lli_leased_in},
        {ST_OUT_DIAL,           EV_SETUP_CNF,           lli_init_bchan_out},
        {ST_OUT_DIAL,           EV_HANGUP,              lli_disconnect_req},
        {ST_OUT_DIAL,           EV_DISCONNECT_IND,      lli_release_req},
        {ST_OUT_DIAL,           EV_RELEASE,             lli_dhup_close},
        {ST_OUT_DIAL,           EV_NOSETUP_RSP,         lli_no_setup_rsp},
        {ST_OUT_DIAL,           EV_SETUP_ERR,           lli_error},
        {ST_IN_WAIT_LL,         EV_LEASED_REL,          lli_failure_l},
        {ST_IN_WAIT_LL,         EV_ACCEPTD,             lli_setup_rsp},
        {ST_IN_WAIT_LL,         EV_HANGUP,              lli_reject_req},
        {ST_IN_WAIT_LL,         EV_DISCONNECT_IND,      lli_release_req},
        {ST_IN_WAIT_LL,         EV_RELEASE,             lli_dhup_close},
        {ST_IN_ALERT_SENT,      EV_SETUP_CMPL_IND,      lli_init_bchan_in},
        {ST_IN_ALERT_SENT,      EV_ACCEPTD,             lli_send_dconnect},
        {ST_IN_ALERT_SENT,      EV_HANGUP,              lli_disconnect_reject},
        {ST_IN_ALERT_SENT,      EV_DISCONNECT_IND,      lli_release_req},
        {ST_IN_ALERT_SENT,      EV_RELEASE,             lli_dhup_close},
	{ST_IN_ALERT_SENT,	EV_REDIR,		lli_send_redir},
	{ST_IN_PROCEED_SEND,	EV_REDIR,		lli_send_redir},
	{ST_IN_PROCEED_SEND,	EV_ALERT,		lli_send_alert},
	{ST_IN_PROCEED_SEND,	EV_ACCEPTD,		lli_send_dconnect},
	{ST_IN_PROCEED_SEND,	EV_HANGUP,		lli_disconnect_reject},
	{ST_IN_PROCEED_SEND,	EV_DISCONNECT_IND,	lli_dhup_close},
        {ST_IN_ALERT_SENT,      EV_RELEASE,             lli_dhup_close},
        {ST_IN_WAIT_CONN_ACK,   EV_SETUP_CMPL_IND,      lli_init_bchan_in},
        {ST_IN_WAIT_CONN_ACK,   EV_HANGUP,              lli_disconnect_req},
        {ST_IN_WAIT_CONN_ACK,   EV_DISCONNECT_IND,      lli_release_req},
        {ST_IN_WAIT_CONN_ACK,   EV_RELEASE,             lli_dhup_close},
        {ST_IN_WAIT_CONN_ACK,   EV_CONNECT_ERR,         lli_error},
        {ST_WAIT_BCONN,         EV_BC_EST,              lli_go_active},
        {ST_WAIT_BCONN,         EV_BC_REL,              lli_rel_b_disc},
        {ST_WAIT_BCONN,         EV_HANGUP,              lli_rel_b_disc},
        {ST_WAIT_BCONN,         EV_DISCONNECT_IND,      lli_rel_b_release_req},
        {ST_WAIT_BCONN,         EV_RELEASE,             lli_rel_b_dhup},
        {ST_WAIT_BCONN,         EV_LEASED_REL,          lli_rel_b_fail},
        {ST_WAIT_BCONN,         EV_CINF,                lli_charge_info},
        {ST_ACTIVE,             EV_CINF,                lli_charge_info},
        {ST_ACTIVE,             EV_BC_REL,              lli_bhup_rel_b},
        {ST_ACTIVE,             EV_SUSPEND,             lli_suspend},
        {ST_ACTIVE,             EV_HANGUP,              lli_disconn_bchan},
        {ST_ACTIVE,             EV_DISCONNECT_IND,      lli_release_bchan},
        {ST_ACTIVE,             EV_RELEASE,             lli_abort},
        {ST_ACTIVE,             EV_LEASED_REL,          lli_failure_a},
        {ST_WAIT_BRELEASE,      EV_BC_REL,              lli_bhup_disc},
        {ST_WAIT_BRELEASE,      EV_DISCONNECT_IND,      lli_bhup_release_req},
        {ST_WAIT_BRELEASE,      EV_RELEASE,             lli_bhup_dhup},
        {ST_WAIT_BRELEASE,      EV_LEASED_REL,          lli_bhup_fail},
        {ST_WAIT_BREL_DISC,     EV_BC_REL,              lli_bhup_release_req},
        {ST_WAIT_BREL_DISC,     EV_RELEASE,             lli_bhup_dhup},
        {ST_WAIT_DCOMMAND,      EV_HANGUP,              lli_start_disc},
        {ST_WAIT_DCOMMAND,      EV_DISCONNECT_IND,      lli_release_req},
        {ST_WAIT_DCOMMAND,      EV_RELEASE,             lli_dhup_close},
        {ST_WAIT_DCOMMAND,      EV_LEASED_REL,          lli_failure_l},
        {ST_WAIT_DRELEASE,      EV_RELEASE,             lli_dhup_close},
        {ST_WAIT_DRELEASE,      EV_DIAL,                lli_dchan_not_ready},
  /* ETS 300-104 16.1 */
        {ST_WAIT_D_REL_CNF,     EV_RELEASE,             lli_dhup_close},
        {ST_WAIT_D_REL_CNF,     EV_DIAL,                lli_dchan_not_ready},
};
/* *INDENT-ON* */

#define FNCOUNT (sizeof(fnlist)/sizeof(struct FsmNode))

HISAX_INITFUNC(void
CallcNew(void))
{
	callcfsm.state_count = STATE_COUNT;
	callcfsm.event_count = EVENT_COUNT;
	callcfsm.strEvent = strEvent;
	callcfsm.strState = strState;
	FsmNew(&callcfsm, fnlist, FNCOUNT);
}

void
CallcFree(void)
{
	FsmFree(&callcfsm);
}

static void
release_b_st(struct Channel *chanp)
{
	struct PStack *st = chanp->b_st;

	if(test_and_clear_bit(FLG_START_B, &chanp->Flags)) {
		chanp->bcs->BC_Close(chanp->bcs);
		switch (chanp->l2_active_protocol) {
			case (ISDN_PROTO_L2_X75I):
				releasestack_isdnl2(st);
				break;
			case (ISDN_PROTO_L2_HDLC):
			case (ISDN_PROTO_L2_TRANS):
			case (ISDN_PROTO_L2_MODEM):
			case (ISDN_PROTO_L2_FAX):
				releasestack_transl2(st);
				break;
		}
	} 
}

struct Channel
*selectfreechannel(struct PStack *st, int bch)
{
	struct IsdnCardState *cs = st->l1.hardware;
	struct Channel *chanp = st->lli.userdata;
	int i;

	if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags))
		i=1;
	else
		i=0;

	if (!bch) {
		i = 2; /* virtual channel */
		chanp += 2;
	}

	while (i < ((bch) ? cs->chanlimit : (2 + MAX_WAITING_CALLS))) {
		if (chanp->fi.state == ST_NULL)
			return (chanp);
		chanp++;
		i++;
	}

	if (bch) /* number of channels is limited */ {
		i = 2; /* virtual channel */
		chanp = st->lli.userdata;
		chanp += i;
		while (i < (2 + MAX_WAITING_CALLS)) {
			if (chanp->fi.state == ST_NULL)
				return (chanp);
			chanp++;
			i++;
		}
	}
	return (NULL);
}

static void stat_redir_result(struct IsdnCardState *cs, int chan, ulong result)
{
	isdn_ctrl ic;
  
	(ulong)(ic.parm.num[0]) = result;
	HL_LL(cs->c_if->channel + chan, ISDN_STAT_REDIR, &ic);
}

static void
dchan_l3l4(struct PStack *st, int pr, void *arg)
{
	struct l3_process *pc = arg;
	struct IsdnCardState *cs = st->l1.hardware;
	struct Channel *chanp;

	if(!pc)
		return;

	if (pr == (CC_SETUP | INDICATION)) {
		if (!(chanp = selectfreechannel(pc->st, pc->para.bchannel))) {
			pc->para.cause = 0x11;	/* User busy */
			pc->st->lli.l4l3(pc->st, CC_REJECT | REQUEST, pc);
		} else {
			chanp->proc = pc;
			pc->chan = chanp;
			FsmEvent(&chanp->fi, EV_SETUP_IND, NULL);
		}
		return;
	}
	if (!(chanp = pc->chan))
		return;

	switch (pr) {
		case (CC_DISCONNECT | INDICATION):
			FsmEvent(&chanp->fi, EV_DISCONNECT_IND, NULL);
			break;
		case (CC_RELEASE | CONFIRM):
			FsmEvent(&chanp->fi, EV_RELEASE, NULL);
			break;
		case (CC_SUSPEND | CONFIRM):
			FsmEvent(&chanp->fi, EV_RELEASE, NULL);
			break;
		case (CC_RESUME | CONFIRM):
			FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
			break;
		case (CC_RESUME_ERR):
			FsmEvent(&chanp->fi, EV_RELEASE, NULL);
			break;
		case (CC_RELEASE | INDICATION):
			FsmEvent(&chanp->fi, EV_RELEASE, NULL);
			break;
		case (CC_SETUP_COMPL | INDICATION):
			FsmEvent(&chanp->fi, EV_SETUP_CMPL_IND, NULL);
			break;
		case (CC_SETUP | CONFIRM):
			FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
			break;
		case (CC_CHARGE | INDICATION):
			FsmEvent(&chanp->fi, EV_CINF, NULL);
			break;
		case (CC_NOSETUP_RSP):
			FsmEvent(&chanp->fi, EV_NOSETUP_RSP, NULL);
			break;
		case (CC_SETUP_ERR):
			FsmEvent(&chanp->fi, EV_SETUP_ERR, NULL);
			break;
		case (CC_CONNECT_ERR):
			FsmEvent(&chanp->fi, EV_CONNECT_ERR, NULL);
			break;
		case (CC_RELEASE_ERR):
			FsmEvent(&chanp->fi, EV_RELEASE, NULL);
			break;
		case (CC_PROCEED_SEND | INDICATION):
		case (CC_PROCEEDING | INDICATION):
		case (CC_ALERTING | INDICATION):
		case (CC_PROGRESS | INDICATION):
		case (CC_NOTIFY | INDICATION):
			break;
		case (CC_REDIR | INDICATION):
			stat_redir_result(cs, chanp->chan, pc->redir_result); 
			break;
		default:
			ll_debug(LL_DEB_WARN, chanp->cs, 
				 "Ch %d L3->L4 unknown primitive %#x", chanp->chan, pr);
	}
}

static void
dummy_pstack(struct PStack *st, int pr, void *arg) {
	printk(KERN_WARNING"call to dummy_pstack pr=%04x arg %lx\n", pr, (long)arg);
}

static void
init_PStack(struct PStack **stp) {
	*stp = kmalloc(sizeof(struct PStack), GFP_ATOMIC);
	(*stp)->next = NULL;
	(*stp)->l1.l1l2 = dummy_pstack;
	(*stp)->l1.l1hw = dummy_pstack;
	(*stp)->l1.l1tei = dummy_pstack;
	(*stp)->l2.l2tei = dummy_pstack;
	(*stp)->l2.l2l1 = dummy_pstack;
	(*stp)->l2.l2l3 = dummy_pstack;
	(*stp)->l3.l3l2 = dummy_pstack;
	(*stp)->l3.l3ml3 = dummy_pstack;
	(*stp)->l3.l3l4 = dummy_pstack;
	(*stp)->lli.l4l3 = dummy_pstack;
	(*stp)->ma.layer = dummy_pstack;
}

static void
init_d_st(struct Channel *chanp)
{
	struct PStack *st;
	struct IsdnCardState *cs = chanp->cs;
	char tmp[16];

	init_PStack(&chanp->d_st);
	st = chanp->d_st;
	st->next = NULL;
	HiSax_addlist(cs, st);
	setstack_HiSax(st, cs);
	st->l2.sap = 0;
	st->l2.tei = -1;
	st->l2.flag = 0;
	test_and_set_bit(FLG_MOD128, &st->l2.flag);
	test_and_set_bit(FLG_LAPD, &st->l2.flag);
	test_and_set_bit(FLG_ORIG, &st->l2.flag);
	st->l2.maxlen = MAX_DFRAME_LEN;
	st->l2.window = 1;
	st->l2.T200 = 1000;	/* 1000 milliseconds  */
	st->l2.N200 = 3;	/* try 3 times        */
	st->l2.T203 = 10000;	/* 10000 milliseconds */
	if (test_bit(FLG_TWO_DCHAN, &cs->HW_Flags))
		sprintf(tmp, "DCh%d Q.921 ", chanp->chan);
	else
		sprintf(tmp, "DCh Q.921 ");
	setstack_isdnl2(st, tmp);
	setstack_l3dc(st, chanp);
	st->lli.userdata = chanp;
	st->l3.l3l4 = dchan_l3l4;
}

static void
callc_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct Channel *chanp = fi->userdata;
	char tmp[16];

	va_start(args, fmt);
	sprintf(tmp, "Ch%d callc ", chanp->chan);
	VHiSax_putstatus(chanp->cs, tmp, fmt, args);
	va_end(args);
}

static void
channelConstr(struct Channel *chanp, struct CallcIf *c_if, int chan)
{
	chanp->cs = c_if->cs;
	chanp->c_if = c_if;
	chanp->bcs = &c_if->cs->bcs[chan];
	chanp->chan = chan;
	chanp->incoming = 0;
	chanp->Flags = 0;
	chanp->leased = 0;
	init_PStack(&chanp->b_st);
	chanp->b_st->l1.delay = DEFAULT_B_DELAY;
	chanp->fi.fsm = &callcfsm;
	chanp->fi.state = ST_NULL;
	chanp->fi.debug = LL_DEB_WARN;
	chanp->fi.userdata = chanp;
	chanp->fi.printdebug = callc_debug;
	FsmInitTimer(&chanp->fi, &chanp->dial_timer);
	FsmInitTimer(&chanp->fi, &chanp->drel_timer);
	if (!chan || test_bit(FLG_TWO_DCHAN, &c_if->cs->HW_Flags)) {
		init_d_st(chanp);
	} else {
	        chanp->d_st = c_if->channel[0].d_st;
	}
	chanp->data_open = 0;
	chanp->tx_cnt = 0;
}

static void
release_d_st(struct Channel *chanp)
{
	struct PStack *st = chanp->d_st;

	if (!st)
		return;
	releasestack_isdnl2(st);
	releasestack_isdnl3(st);
	HiSax_rmlist(st->l1.hardware, st);
	kfree(st);
	chanp->d_st = NULL;
}

static void
channelDestr(struct Channel *chanp)
{
	FsmDelTimer(&chanp->drel_timer, 74);
	FsmDelTimer(&chanp->dial_timer, 75);
	if ((chanp->chan == 0) || test_bit(FLG_TWO_DCHAN, &chanp->cs->HW_Flags))
		release_d_st(chanp);
	else
		chanp->d_st = 0;
	if (chanp->b_st) {
		release_b_st(chanp);
		kfree(chanp->b_st);
		chanp->b_st = 0;
	} else {
		printk(KERN_WARNING "CallcFreeChan b_st ch%d allready freed\n", chanp->chan);
	}
}

static void
ll_writewakeup(struct Channel *chanp, int len)
{
	isdn_ctrl ic;

	ic.parm.length = len;
	HL_LL(chanp, ISDN_STAT_BSENT, &ic);
}

static void
lldata_handler(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (DL_DATA  | INDICATION):
			if (chanp->data_open)
				chanp->cs->c_if->iif.rcvcallb_skb(chanp->cs->c_if->myid, chanp->chan, skb);
			else {
				idev_kfree_skb(skb, FREE_READ);
			}
			break;
      	        case (DL_DATA | CONFIRM):
		        /* the original length of the skb is saved in priority */
			chanp->tx_cnt -= skb->priority;
			if (skb->pkt_type != PACKET_NOACK)
				ll_writewakeup(chanp, skb->priority);
			break;
		case (DL_ESTABLISH | INDICATION):
		case (DL_ESTABLISH | CONFIRM):
			FsmEvent(&chanp->fi, EV_BC_EST, NULL);
			break;
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			FsmEvent(&chanp->fi, EV_BC_REL, NULL);
			break;
		default:
			printk(KERN_WARNING "lldata_handler unknown primitive %#x\n",
				pr);
			break;
	}
}

static int
init_b_st(struct Channel *chanp, int incoming)
{
	struct PStack *st = chanp->b_st;
	struct IsdnCardState *cs = chanp->cs;
	char tmp[16];

	chanp->tx_cnt = 0;
	st->l1.hardware = cs;
	if (chanp->leased)
		st->l1.bc = chanp->chan & 1;
	else
		st->l1.bc = chanp->proc->para.bchannel - 1;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
		case (ISDN_PROTO_L2_HDLC):
			st->l1.mode = B1_MODE_HDLC;
			break;
		case (ISDN_PROTO_L2_TRANS):
			st->l1.mode = B1_MODE_TRANS;
			break;
		case (ISDN_PROTO_L2_MODEM):
			st->l1.mode = B1_MODE_MODEM;
			break;
		case (ISDN_PROTO_L2_FAX):
			st->l1.mode = B1_MODE_FAX;
			break;
	}
	chanp->bcs->conmsg = NULL;
	if (chanp->bcs->BC_SetStack(st, chanp->bcs))
		return (-1);
	st->l2.flag = 0;
	test_and_set_bit(FLG_LAPB, &st->l2.flag);
	st->l2.AddressA = 0x03;
	st->l2.AddressB = 0x01;
	st->l2.maxlen = MAX_DATA_SIZE;
	if (!incoming)
		test_and_set_bit(FLG_ORIG, &st->l2.flag);
	st->l2.T200 = 1000;	/* 1000 milliseconds */
	st->l2.window = 7;
	st->l2.N200 = 4;	/* try 4 times       */
	st->l2.T203 = 5000;	/* 5000 milliseconds */
	st->l3.debug = 0;
	switch (chanp->l2_active_protocol) {
		case (ISDN_PROTO_L2_X75I):
			setstack_isdnl2(st, tmp);
			sprintf(tmp, "Ch%d X.75", chanp->chan);
			st->l2.l2m.debug = chanp->debug & 16;
			st->l2.debug = chanp->debug & 64;
			break;
		case (ISDN_PROTO_L2_HDLC):
		case (ISDN_PROTO_L2_TRANS):
		case (ISDN_PROTO_L2_MODEM):
		case (ISDN_PROTO_L2_FAX):
			setstack_transl2(st);
			break;
	}
	st->l2.l2l3 = lldata_handler;
	st->lli.userdata = chanp;
	test_and_set_bit(FLG_START_B, &chanp->Flags);
	setstack_l3bc(st, chanp);
	return (0);
}

static void
leased_l4l3(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (DL_DATA | REQUEST):
			link_debug(LL_DEB_WARN, chanp, 0, "leased line d-channel DATA");
			idev_kfree_skb(skb, FREE_READ);
			break;
		case (DL_ESTABLISH | REQUEST):
			st->l2.l2l1(st, PH_ACTIVATE | REQUEST, NULL);
			break;
		case (DL_RELEASE | REQUEST):
			break;
		default:
			printk(KERN_WARNING "transd_l4l3 unknown primitive %#x\n",
				pr);
			break;
	}
}

static void
leased_l1l2(struct PStack *st, int pr, void *arg)
{
	struct Channel *chanp = (struct Channel *) st->lli.userdata;
	struct sk_buff *skb = arg;
	int i,event = EV_LEASED_REL;

	switch (pr) {
		case (PH_DATA | INDICATION):
			link_debug(LL_DEB_WARN, chanp, 0, "leased line d-channel DATA");
			idev_kfree_skb(skb, FREE_READ);
			break;
      	        case (PH_DATA | CONFIRM):
		        /* the original length of the skb is saved in priority */
			chanp->tx_cnt -= skb->priority;
			if (skb->pkt_type != PACKET_NOACK)
				ll_writewakeup(chanp, skb->priority);
			break;
		case (PH_ACTIVATE | INDICATION):
		case (PH_ACTIVATE | CONFIRM):
			event = EV_LEASED;
		case (PH_DEACTIVATE | INDICATION):
		case (PH_DEACTIVATE | CONFIRM):
			if (test_bit(FLG_TWO_DCHAN, &chanp->cs->HW_Flags))
				i = 1;
			else
				i = 0;
			while (i < 2) {
				FsmEvent(&chanp->fi, event, NULL);
				chanp++;
				i++;
			}
			break;
		default:
			printk(KERN_WARNING
				"transd_l1l2 unknown primitive %#x\n", pr);
			break;
	}
}

static void
distr_debug(struct IsdnCardState *csta, int debugflags)
{
	int i;
	struct Channel *chanp = csta->c_if->channel;

	for (i = 0; i < (2 + MAX_WAITING_CALLS) ; i++) {
		chanp[i].debug = debugflags | LL_DEB_WARN;
		chanp[i].fi.debug = debugflags & 2;
		chanp[i].d_st->l2.l2m.debug = debugflags & 8;
		chanp[i].b_st->l2.l2m.debug = debugflags & 0x10;
		chanp[i].d_st->l2.debug = debugflags & 0x20;
		chanp[i].b_st->l2.debug = debugflags & 0x40;
		chanp[i].d_st->l3.l3m.debug = debugflags & 0x80;
		chanp[i].b_st->l3.l3m.debug = debugflags & 0x100;
		chanp[i].b_st->ma.tei_m.debug = debugflags & 0x200;
		chanp[i].b_st->ma.debug = debugflags & 0x200;
		chanp[i].d_st->l1.l1m.debug = debugflags & 0x1000;
		chanp[i].b_st->l1.l1m.debug = debugflags & 0x2000;
	}
	if (debugflags & 4)
		csta->debug |= DEB_DLOG_HEX;
	else
		csta->debug &= ~DEB_DLOG_HEX;
}

static char tmpbuf[256];

static void
capi_debug(struct Channel *chanp, capi_msg *cm)
{
	char *t = tmpbuf;

	t += QuickHex(t, (u_char *)cm, (cm->Length>50)? 50: cm->Length);
	t--;
	*t= 0;
	HiSax_putstatus(chanp->cs, "Ch", "%d CAPIMSG %s", chanp->chan, tmpbuf);
}

void
lli_got_fac_req(struct Channel *chanp, capi_msg *cm) {
	if ((cm->para[0] != 3) || (cm->para[1] != 0))
		return;
	if (cm->para[2]<3)
		return;
	if (cm->para[4] != 0)
		return;
	switch(cm->para[3]) {
		case 4: /* Suspend */
			strncpy(chanp->setup.phone, &cm->para[5], cm->para[5] +1);
			FsmEvent(&chanp->fi, EV_SUSPEND, cm);
			break;
		case 5: /* Resume */
			strncpy(chanp->setup.phone, &cm->para[5], cm->para[5] +1);
			if (chanp->fi.state == ST_NULL) {
				FsmEvent(&chanp->fi, EV_RESUME, cm);
			} else {
				FsmDelTimer(&chanp->dial_timer, 72);
				FsmAddTimer(&chanp->dial_timer, 80, EV_RESUME, cm, 73);
			}
			break;
	}
}

void
lli_got_manufacturer(struct Channel *chanp, struct IsdnCardState *cs, capi_msg *cm) {
	if ((cs->typ == ISDN_CTYPE_ELSA) || (cs->typ == ISDN_CTYPE_ELSA_PNP) ||
		(cs->typ == ISDN_CTYPE_ELSA_PCI)) {
		if (cs->hw.elsa.MFlag) {
			cs->cardmsg(cs, CARD_AUX_IND, cm->para);
		}
	}
}


/***************************************************************/
/* Limit the available number of channels for the current card */
/***************************************************************/
static int 
set_channel_limit(struct IsdnCardState *cs, int chanmax)
{
	isdn_ctrl ic;
	int i, ii;

	if ((chanmax < 0) || (chanmax > 2))
		return(-EINVAL);
	cs->chanlimit = 0;
	for (ii = 0; ii < 2; ii++) {
		if (ii >= chanmax)
			ic.parm.num[0] = 0; /* disabled */
		else
			ic.parm.num[0] = 1; /* enabled */
		i = HL_LL(cs->c_if->channel + ii, ISDN_STAT_DISCH, &ic);
		if (i) return(-EINVAL);
		if (ii < chanmax) 
			cs->chanlimit++;
	}
	return(0);
} /* set_channel_limit */

int
HiSax_command(isdn_ctrl * ic)
{
	struct IsdnCardState *csta = findCallcIf(ic->driver)->cs;
	struct PStack *st;
	struct Channel *chanp;
	int i;
	u_int num;

	if (!csta) {
		printk(KERN_ERR
		"HiSax: if_command %d called with invalid driverId %d!\n",
			ic->command, ic->driver);
		return -ENODEV;
	}
	switch (ic->command) {
		case (ISDN_CMD_SETEAZ):
			chanp = csta->c_if->channel + ic->arg;
			break;
		case (ISDN_CMD_SETL2):
			chanp = csta->c_if->channel + (ic->arg & 0xff);
			link_debug(LL_DEB_INFO, chanp, 1, "SETL2 card %d %ld",
				   csta->cardnr + 1, ic->arg >> 8);
			chanp->l2_protocol = ic->arg >> 8;
			break;
		case (ISDN_CMD_SETL3):
			chanp = csta->c_if->channel + (ic->arg & 0xff);
			link_debug(LL_DEB_INFO, chanp, 1, "SETL3 card %d %ld",
				   csta->cardnr + 1, ic->arg >> 8);
			chanp->l3_protocol = ic->arg >> 8;
			break;
		case (ISDN_CMD_DIAL):
			chanp = csta->c_if->channel + (ic->arg & 0xff);
			link_debug(LL_DEB_INFO, chanp, 1, "DIAL %s -> %s (%d,%d)",
				   ic->parm.setup.eazmsn, ic->parm.setup.phone,
				   ic->parm.setup.si1, ic->parm.setup.si2);
			chanp->setup = ic->parm.setup;
			if (!strcmp(chanp->setup.eazmsn, "0"))
				chanp->setup.eazmsn[0] = '\0';
			/* this solution is dirty and may be change, if
			 * we make a callreference based callmanager */
			if (chanp->fi.state == ST_NULL) {
				FsmEvent(&chanp->fi, EV_DIAL, NULL);
			} else {
				FsmDelTimer(&chanp->dial_timer, 70);
				FsmAddTimer(&chanp->dial_timer, 50, EV_DIAL, NULL, 71);
			}
			break;
		case (ISDN_CMD_ACCEPTB):
			chanp = csta->c_if->channel + ic->arg;
			link_debug(LL_DEB_INFO, chanp, 1, "ACCEPTB");
			FsmEvent(&chanp->fi, EV_ACCEPTB, NULL);
			break;
		case (ISDN_CMD_ACCEPTD):
			chanp = csta->c_if->channel + ic->arg;
			link_debug(LL_DEB_INFO, chanp, 1, "ACCEPTD");
			FsmEvent(&chanp->fi, EV_ACCEPTD, NULL);
			break;
		case (ISDN_CMD_HANGUP):
			chanp = csta->c_if->channel + ic->arg;
			link_debug(LL_DEB_INFO, chanp, 1, "HANGUP");
			FsmEvent(&chanp->fi, EV_HANGUP, NULL);
			break;
		case (CAPI_PUT_MESSAGE):
			chanp = csta->c_if->channel + ic->arg;
			if (chanp->debug & 1)
				capi_debug(chanp, &ic->parm.cmsg);
			if (ic->parm.cmsg.Length < 8)
				break;
			switch(ic->parm.cmsg.Command) {
				case CAPI_FACILITY:
					if (ic->parm.cmsg.Subcommand == CAPI_REQ)
						lli_got_fac_req(chanp, &ic->parm.cmsg);
					break;
				case CAPI_MANUFACTURER:
					if (ic->parm.cmsg.Subcommand == CAPI_REQ)
						lli_got_manufacturer(chanp, csta, &ic->parm.cmsg);
					break;
				default:
					break;
			}
			break;
		case (ISDN_CMD_LOCK):
			HiSax_mod_inc_use_count(csta);
			break;
		case (ISDN_CMD_UNLOCK):
			HiSax_mod_dec_use_count(csta);
			break;
		case (ISDN_CMD_IOCTL):
			switch (ic->arg) {
				case (0):
					num = *(unsigned int *) ic->parm.num;
					HiSax_reportcard(csta->cardnr, num);
					break;
				case (1):
					num = *(unsigned int *) ic->parm.num;
					distr_debug(csta, num);
					printk(KERN_DEBUG "HiSax: debugging flags card %d set to %x\n",
						csta->cardnr + 1, num);
					HiSax_putstatus(csta, "debugging flags ",
						"card %d set to %x", csta->cardnr + 1, num);
					break;
				case (2):
					num = *(unsigned int *) ic->parm.num;
					csta->c_if->channel[0].b_st->l1.delay = num;
					csta->c_if->channel[1].b_st->l1.delay = num;
					HiSax_putstatus(csta, "delay ", "card %d set to %d ms",
						csta->cardnr + 1, num);
					printk(KERN_DEBUG "HiSax: delay card %d set to %d ms\n",
						csta->cardnr + 1, num);
					break;
				case (3):
					for (i = 0; i < *(unsigned int *) ic->parm.num; i++)
						HiSax_mod_dec_use_count(csta);
					break;
				case (4):
					for (i = 0; i < *(unsigned int *) ic->parm.num; i++)
						HiSax_mod_inc_use_count(csta);
					break;
				case (5):	/* set card in leased mode */
					num = *(unsigned int *) ic->parm.num;
					if ((num <1) || (num > 2)) {
						HiSax_putstatus(csta, "Set LEASED ",
							"wrong channel %d", num);
						printk(KERN_WARNING "HiSax: Set LEASED wrong channel %d\n",
							num);
					} else {
						num--;
						chanp = csta->c_if->channel +num;
						chanp->leased = 1;
						HiSax_putstatus(csta, "Card",
							"%d channel %d set leased mode\n",
							csta->cardnr + 1, num + 1);
						chanp->d_st->l1.l1l2 = leased_l1l2;
						chanp->d_st->lli.l4l3 = leased_l4l3;
						chanp->d_st->lli.l4l3(chanp->d_st,
							DL_ESTABLISH | REQUEST, NULL);
					}
					break;
				case (6):	/* set B-channel test loop */
					num = *(unsigned int *) ic->parm.num;
					if (csta->stlist)
						csta->stlist->l2.l2l1(csta->stlist,
							PH_TESTLOOP | REQUEST, (void *) (long)num);
					break;
				case (7):	/* set card in PTP mode */
					num = *(unsigned int *) ic->parm.num;
					if (test_bit(FLG_TWO_DCHAN, &csta->HW_Flags)) {
						printk(KERN_ERR "HiSax PTP mode only with one TEI possible\n");
					} else if (num) {
						test_and_set_bit(FLG_PTP, &csta->c_if->channel[0].d_st->l2.flag);
						test_and_set_bit(FLG_FIXED_TEI, &csta->c_if->channel[0].d_st->l2.flag);
						csta->c_if->channel[0].d_st->l2.tei = 0;
						HiSax_putstatus(csta, "set card ", "in PTP mode");
						printk(KERN_DEBUG "HiSax: set card in PTP mode\n");
						printk(KERN_INFO "LAYER2 WATCHING ESTABLISH\n");
						csta->c_if->channel[0].d_st->lli.l4l3(csta->c_if->channel[0].d_st,
							DL_ESTABLISH | REQUEST, NULL);
					} else {
						test_and_clear_bit(FLG_PTP, &csta->c_if->channel[0].d_st->l2.flag);
						test_and_clear_bit(FLG_FIXED_TEI, &csta->c_if->channel[0].d_st->l2.flag);
						HiSax_putstatus(csta, "set card ", "in PTMP mode");
						printk(KERN_DEBUG "HiSax: set card in PTMP mode\n");
					}
					break;
				case (8):	/* set card in FIXED TEI mode */
					num = *(unsigned int *) ic->parm.num;
					chanp = csta->c_if->channel + (num & 1);
					num = num >>1;
					if (num == 127) {
						test_and_clear_bit(FLG_FIXED_TEI, &chanp->d_st->l2.flag);
						chanp->d_st->l2.tei = -1;
						HiSax_putstatus(csta, "set card ", "in VAR TEI mode");
						printk(KERN_DEBUG "HiSax: set card in VAR TEI mode\n");
					} else {
						test_and_set_bit(FLG_FIXED_TEI, &chanp->d_st->l2.flag);
						chanp->d_st->l2.tei = num;
						HiSax_putstatus(csta, "set card ", "in FIXED TEI (%d) mode", num);
						printk(KERN_DEBUG "HiSax: set card in FIXED TEI (%d) mode\n",
							num);
					}
					chanp->d_st->lli.l4l3(chanp->d_st,
						DL_ESTABLISH | REQUEST, NULL);
					break;
				case (11):
					num = csta->debug & DEB_DLOG_HEX;
					csta->debug = *(unsigned int *) ic->parm.num;
					csta->debug |= num;
					HiSax_putstatus(csta, "l1 debugging ",
						"flags card %d set to %x",
						csta->cardnr + 1, csta->debug);
					printk(KERN_DEBUG "HiSax: l1 debugging flags card %d set to %x\n",
						csta->cardnr + 1, csta->debug);
					break;
				case (13):
					csta->c_if->channel[0].d_st->l3.debug = *(unsigned int *) ic->parm.num;
					csta->c_if->channel[1].d_st->l3.debug = *(unsigned int *) ic->parm.num;
					HiSax_putstatus(csta, "l3 debugging ",
						"flags card %d set to %x\n", csta->cardnr + 1,
						*(unsigned int *) ic->parm.num);
					printk(KERN_DEBUG "HiSax: l3 debugging flags card %d set to %x\n",
						csta->cardnr + 1, *(unsigned int *) ic->parm.num);
					break;
				case (10):
					i = *(unsigned int *) ic->parm.num;
					return(set_channel_limit(csta, i));
			        default:
					if (csta->auxcmd)
						return(csta->auxcmd(csta, ic));
					printk(KERN_DEBUG "HiSax: invalid ioclt %d\n",
						(int) ic->arg);
					return (-EINVAL);
			}
			break;
		
	        case (ISDN_CMD_PROCEED):
			chanp = csta->c_if->channel + ic->arg;
			link_debug(LL_DEB_INFO, chanp, 1, "PROCEED");
			FsmEvent(&chanp->fi, EV_PROCEED, NULL);
			break;

		case (ISDN_CMD_ALERT):
			chanp = csta->c_if->channel + ic->arg;
			link_debug(LL_DEB_INFO, chanp, 1, "ALERT");
			FsmEvent(&chanp->fi, EV_ALERT, NULL);
			break;

		case (ISDN_CMD_REDIR):
			chanp = csta->c_if->channel + ic->arg;
			link_debug(LL_DEB_INFO, chanp, 1, "REDIR");
			chanp->setup = ic->parm.setup;
			FsmEvent(&chanp->fi, EV_REDIR, NULL);
			break;

		/* protocol specific io commands */
		case (ISDN_CMD_PROT_IO):
			for (st = csta->stlist; st; st = st->next)
				if (st->protocol == (ic->arg & 0xFF))
					return(st->lli.l4l3_proto(st, ic));
			return(-EINVAL);
			break;
		default:
			if (csta->auxcmd)
				return(csta->auxcmd(csta, ic));
			return(-EINVAL);
	}
	return (0);
}

int
HiSax_writebuf_skb(int id, int chan, int ack, struct sk_buff *skb)
{
	struct IsdnCardState *csta = findCallcIf(id)->cs;
	struct Channel *chanp;
	struct PStack *st;
	int len = skb->len;
	unsigned long flags;
	struct sk_buff *nskb;

	if (!csta) {
		printk(KERN_ERR
			"HiSax: if_sendbuf called with invalid driverId!\n");
		return -ENODEV;
	}
	chanp = csta->c_if->channel + chan;
	st = chanp->b_st;
	if (!chanp->data_open) {
		link_debug(LL_DEB_WARN, chanp, 1, "writebuf: channel not open");
		return -EIO;
	}
	if (len > MAX_DATA_SIZE) {
		link_debug(LL_DEB_WARN, chanp, 1, "writebuf: packet too large (%d bytes)", len);
		printk(KERN_WARNING "HiSax_writebuf: packet too large (%d bytes) !\n",
			len);
		return -EINVAL;
	}
	if (len) {
		if ((len + chanp->tx_cnt) > MAX_DATA_MEM) {
			/* Must return 0 here, since this is not an error
			 * but a temporary lack of resources.
			 */
			link_debug(LL_DEB_BUFFERING, chanp, 1, "writebuf: no buffers for %d bytes", len);
			return 0;
		} else
			link_debug(LL_DEB_BUFFERING, chanp, 1, "writebuf %d/%d/%d", len, chanp->tx_cnt,MAX_DATA_MEM);
		save_flags(flags);
		cli();
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb) {
			chanp->tx_cnt += nskb->len;
			if (!ack)
				nskb->pkt_type = PACKET_NOACK;
			/* I'm misusing the priority field here to save the length of the
			 * original skb.
			 * Since the skb is cloned, this should be okay.
			 * --KG
			 */
                        nskb->priority = nskb->len;
			st->l3.l3l2(st, DL_DATA | REQUEST, nskb);
			idev_kfree_skb_any(skb, FREE_WRITE);
		} else
			len = 0;
		restore_flags(flags);
	}
	return (len);
}

int
HiSax_read_status(u_char * buf, int len, int user, int id, int channel)
{
	int count,cnt;
	u_char *p = buf;
	struct CallcIf *c_if = findCallcIf(id);

	if (!c_if) {
		printk(KERN_ERR
		       "HiSax: if_readstatus called with invalid driverId!\n");
		return -ENODEV;
	}

	if (len > HISAX_STATUS_BUFSIZE) {
		printk(KERN_WARNING "HiSax: status overflow readstat %d/%d\n",
		       len, HISAX_STATUS_BUFSIZE);
	}
	count = c_if->status_end - c_if->status_read + 1;
	if (count >= len)
		count = len;
	if (user)
		copy_to_user(p, c_if->status_read, count);
	else
		memcpy(p, c_if->status_read, count);
	c_if->status_read += count;
	if (c_if->status_read > c_if->status_end)
		c_if->status_read = c_if->status_buf;
	p += count;
	count = len - count;
	while (count) {
		if (count > HISAX_STATUS_BUFSIZE)
			cnt = HISAX_STATUS_BUFSIZE;
		else
			cnt = count;
		if (user)
			copy_to_user(p, c_if->status_read, cnt);
		else
			memcpy(p, c_if->status_read, cnt);
		p += cnt;
		c_if->status_read += cnt % HISAX_STATUS_BUFSIZE;
		count -= cnt;
	}
	return len;
}

// =================================================================
// Interface to config.c

int
callcIfConstr(struct CallcIf *c_if, struct IsdnCardState *cs, char *id)
{
	memset(c_if, 0, sizeof(struct CallcIf));

	c_if->cs = cs;

	// status ring buffer

 	if (!(c_if->status_buf = kmalloc(HISAX_STATUS_BUFSIZE, GFP_KERNEL))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cs->cardnr + 1);
		return -ENOMEM;
	}
	c_if->status_read = c_if->status_buf;
	c_if->status_write = c_if->status_buf;
	c_if->status_end = c_if->status_buf + HISAX_STATUS_BUFSIZE - 1;
	
	// register to LL

	strcpy(c_if->iif.id, id);
	
	c_if->iif.channels = 2;
	c_if->iif.maxbufsize = MAX_DATA_SIZE;
	c_if->iif.hl_hdrlen = MAX_HEADER_LEN;
	c_if->iif.features = cs->features;
	c_if->iif.command = HiSax_command;
	c_if->iif.writecmd = 0;
	c_if->iif.writebuf_skb = HiSax_writebuf_skb;
	c_if->iif.readstat = HiSax_read_status;
	register_isdn(&c_if->iif);

	c_if->myid = c_if->iif.channels;

	printk(KERN_INFO
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cs->cardnr + 1,
	       (cs->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (cs->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (cs->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
	       (cs->protocol == ISDN_PTYPE_NI1) ? "NI1" :
	       "NONE", c_if->iif.id, c_if->myid);

	return 0;
}

void 
callcIfDestr(struct CallcIf *c_if)
{
	isdn_ctrl ic;

	// unregister from LL

	statcallb(c_if->cs, ISDN_STAT_UNLOAD, &ic);

	// status ring buffer

	if (c_if->status_buf) {
		kfree(c_if->status_buf);
	}
}

void
callcIfRun(struct CallcIf *c_if)
{
	int i;
	long flags;
	isdn_ctrl ic;

	// init Channels

	for (i = 0; i < 2 + MAX_WAITING_CALLS; i++) 
		channelConstr(&c_if->channel[i], c_if, i);
	printk(KERN_INFO "HiSax: 2 channels added\n");
	printk(KERN_INFO "HiSax: MAX_WAITING_CALLS added\n");

	// signal to LL

	save_flags(flags);
	cli();
	c_if->iif.features = c_if->cs->features;
	statcallb(c_if->cs, ISDN_STAT_RUN, &ic);
	restore_flags(flags);
}

void 
callcIfStop(struct CallcIf *c_if)
{
	int i;
	isdn_ctrl ic;

	// signal to LL

	statcallb(c_if->cs, ISDN_STAT_STOP, &ic);

	// release Channels

	for (i = 0; i < 2 + MAX_WAITING_CALLS; i++) 
		channelDestr(&c_if->channel[i]);
}

void
callcIfPutStatus(struct CallcIf *c_if, char *msg)
{
	int i;
	int len, count;
	isdn_ctrl ic;

	count = strlen(msg);
	if (count > HISAX_STATUS_BUFSIZE) {
		printk(KERN_WARNING "HiSax: status overflow %d/%d\n",
			count, HISAX_STATUS_BUFSIZE);
		return;
	}
	len = count;
	i = c_if->status_end - c_if->status_write +1;
	if (i >= len)
		i = len;
	len -= i;
	memcpy(c_if->status_write, msg, i);
	c_if->status_write += i;
	if (c_if->status_write > c_if->status_end) {
		c_if->status_write = c_if->status_buf;
	}
	msg += i;
	if (len) {
	        memcpy(c_if->status_write, msg, len);
		c_if->status_write += len;
	}
	ic.arg = count;
	statcallb(c_if->cs, ISDN_STAT_STAVAIL, &ic);
}

struct CallcIf *
newCallcIf(struct IsdnCardState *cs, char *id)
{
	struct CallcIf *c_if;
	int i;

	c_if = kmalloc(sizeof(struct CallcIf), GFP_KERNEL);
	if (!c_if) {
		return 0;
	}
	if (callcIfConstr(c_if, cs, id)) {
		kfree(c_if);
		return 0;
	}
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		if (!c_ifs[i]) {
			break;
		}
	}
	if (i == HISAX_MAX_CARDS) {
		int_error();
		callcIfDestr(c_if);
		kfree(c_if);
		return 0;
	}
	c_ifs[i] = c_if;
	return c_if;

}

void
delCallcIf(struct CallcIf *c_if)
{
	int i;

	if (!c_if) {
		int_error();
		return;
	}
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		if (c_ifs[i] == c_if) {
			break;
		}
	}
	callcIfDestr(c_if);
	kfree(c_if);
	if (i == HISAX_MAX_CARDS) {
		int_error();
		return;
	}
	c_ifs[i] = 0;
}

