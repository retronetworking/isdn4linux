#include "hisax_capi.h"
#include "l4l3if.h"
#include "stack.h"

#define contrDebug(contr, lev, fmt, args...) \
        debug(lev, contr->cs, "Contr ", fmt, ## args)

static void contr_l3l4(struct PStack *st, int pr, void *arg);
//static void d2_listener(struct IsdnCardState *cs, u_char *buf, int len);

int contrConstr(struct Contr *contr, struct IsdnCardState *cs, char *id, int protocol)
{ 
	char tmp[10];

	memset(contr, 0, sizeof(struct Contr));
	contr->l4.l3l4 = contr_l3l4;
	contr->adrController = cs->cardnr + 1;
	contr->cs = cs;
	contr->cs->debug |= LL_DEB_STATE | LL_DEB_INFO;

	contr->b3_mode = protocol + B3_MODE_CC;
	if (contr->b3_mode != B3_MODE_DSS1 && contr->b3_mode != B3_MODE_LEASED) {
		contrDebug(contr, LL_DEB_WARN, "HiSax: CAPI: Using protocol E-DSS1!");
		contr->b3_mode = B3_MODE_DSS1;
	}

	sprintf(tmp, "HiSax%d", cs->cardnr + 1);
	contr->ctrl = di->attach_ctr(&hisax_driver, tmp, contr);

	if (!contr->ctrl)
		return -ENODEV;
	return 0;
}

void contrDestr(struct Contr *contr)
{
	int i;

	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (contr->plcis[i]) {
			plciDestr(contr->plcis[i]);
			kfree(contr->plcis[i]);
		}
	}
	di->detach_ctr(contr->ctrl);
}

void contrRun(struct Contr *contr)
{
	struct capi_ctr *ctrl = contr->ctrl;
	struct StackParams sp;

	contr->l4.st = kmalloc(sizeof(struct PStack), GFP_ATOMIC);
	if (!contr->l4.st) {
		int_error();
		return;
	}

//	contr->cs->d2_listener = d2_listener;
	
	sp.b1_mode = B1_MODE_HDLC;
	sp.b2_mode = B2_MODE_LAPD;
	sp.b3_mode = contr->b3_mode;
	init_st(&contr->l4, contr->cs, &sp, CHANNEL_D);
	strncpy(ctrl->manu, "ISDN4Linux, (C) Kai Germaschewski", CAPI_MANUFACTURER_LEN);
	strncpy(ctrl->serial, "0001", CAPI_SERIAL_LEN);
	ctrl->version.majorversion = 2;
	ctrl->version.minorversion = 0;
	ctrl->version.majormanuversion = 1;
	ctrl->version.minormanuversion = 1;
	memset(&ctrl->profile, 0, sizeof(struct capi_profile));
	ctrl->profile.ncontroller = 1;
	ctrl->profile.nbchannel = 2;
	ctrl->profile.goptions = 0x1; // internal controller supported
	ctrl->profile.support1 = 3; // HDLC, TRANS
	ctrl->profile.support2 = 3; // X75SLP, TRANS
	ctrl->profile.support3 = 1; // TRANS

	ctrl->ready(ctrl);
}

void contrStop(struct Contr *contr)
{
//	contr->cs->d2_listener = 0;

	if (contr->l4.st) {
		release_st(contr->l4.st);
		kfree(contr->l4.st);
	}
}

struct Appl *contrId2appl(struct Contr *contr, __u16 ApplId)
{
	if ((ApplId < 1) || (ApplId > CAPI_MAXAPPL)) {
		int_error();
		return 0;
	}
	return contr->appls[ApplId - 1];
}

struct Plci *contrAdr2plci(struct Contr *contr, __u32 adr)
{
	int i = (adr >> 8);

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return 0;
	}
	return contr->plcis[i - 1];
}

void contrRegisterAppl(struct Contr *contr, __u16 ApplId, capi_register_params *rp)
{ 
	struct Appl *appl;

	appl = contrId2appl(contr, ApplId);
	if (appl) {
		int_error();
		return;
	}
	appl = kmalloc(sizeof(struct Appl), GFP_KERNEL);
	if (!appl) {
		int_error();
		return;
	}
	contr->appls[ApplId - 1] = appl;
	applConstr(appl, contr, ApplId, rp);
	contr->ctrl->appl_registered(contr->ctrl, ApplId);
}

void contrReleaseAppl(struct Contr *contr, __u16 ApplId)
{ 
	struct Appl *appl;

	appl = contrId2appl(contr, ApplId);
	if (!appl) {
		int_error();
		return;
	}
	applDestr(appl);
	kfree(appl);
	contr->appls[ApplId - 1] = 0;
	contr->ctrl->appl_released(contr->ctrl, ApplId);
}

void contrSendMessage(struct Contr *contr, struct sk_buff *skb)
{ 
	struct Appl *appl;
	int ApplId;

	ApplId = CAPIMSG_APPID(skb->data);
	appl = contrId2appl(contr, ApplId);
	if (!appl) {
		int_error();
		return;
	}
	applSendMessage(appl, skb);
}

void contrD2Trace(struct Contr *contr, u_char *buf, int len)
{
	struct Appl *appl;
	__u16 applId;

	for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
		appl = contrId2appl(contr, applId);
		if (appl) {
			applD2Trace(appl, buf, len);
		}
	}
}

void contrRecvCmsg(struct Contr *contr, _cmsg *cmsg)
{
	struct sk_buff *skb;
	int len;
	
	capi_cmsg2message(cmsg, contr->msgbuf);
	len = CAPIMSG_LEN(contr->msgbuf);
	if (!(skb = alloc_skb(len, GFP_ATOMIC))) {
		int_error();
		return;
	}
	
	memcpy(skb_put(skb, len), contr->msgbuf, len);
	contr->ctrl->handle_capimsg(contr->ctrl, cmsg->ApplId, skb);
}

void contrAnswerCmsg(struct Contr *contr, _cmsg *cmsg, __u16 Info)
{
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	contrRecvCmsg(contr, cmsg);
}

void contrAnswerMessage(struct Contr *contr, struct sk_buff *skb, __u16 Info)
{
	_cmsg cmsg;
	capi_message2cmsg(&cmsg, skb->data);
	contrAnswerCmsg(contr, &cmsg, Info);
}

struct Plci *contrNewPlci(struct Contr *contr)
{
	struct Plci *plci;
	int i;

	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (!contr->plcis[i])
			break;
	}
	if (i == CAPI_MAXPLCI) {
		return 0;
	}
	plci = kmalloc(sizeof(struct Plci), GFP_ATOMIC);
	if (!plci) {
		int_error();
		return 0;
	}
	contr->plcis[i] = plci;
	plciConstr(plci, contr, (i+1) << 8 | contr->adrController);
	return plci;
}

void contrDelPlci(struct Contr *contr, struct Plci *plci)
{
	int i = plci->adrPLCI >> 8;

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return;
	}
	if (contr->plcis[i-1] != plci) {
		int_error();
		return;
	}
	plciDestr(plci);
	kfree(plci);
	contr->plcis[i-1] = 0;
}

static void contr_l3l4(struct PStack *st, int pr, void *arg)
{
	struct Contr *contr = (struct Contr *)st->l4;
	struct l3_process *pc;
	struct Plci *plci;

	hdebug();
	switch (pr) {
	case CC_NEW_CR | INDICATION:
		pc = arg;
		plci = contrNewPlci(contr);
		if (!plci) {
			int_error();
			return;
		} 
		plciNewCrInd(plci, pc);
		break;
	default:
		contrDebug(contr, LL_DEB_WARN, __FUNCTION__ ": unknown pr %#x\n", pr);
		break;
	}
}

void contrPutStatus(struct Contr *contr, char *msg)
{
	printk(KERN_DEBUG "HiSax: %s", msg);
}

struct Contr *newContr(struct IsdnCardState *cs, char *id, int protocol)
{
	struct Contr *contr;

	contr = kmalloc(sizeof(struct Contr), GFP_KERNEL);
	if (!contr)
		return 0;
	if (contrConstr(contr, cs, id, protocol) != 0) {
		kfree(contr);
		return 0;
	}
	return contr;
}

void delContr(struct Contr *contr)
{
	contrDestr(contr);
	kfree(contr);
}

#if 0
static void d2_listener(struct IsdnCardState *cs, u_char *buf, int len)
{
	struct Contr *contr = cs->contr;

	if (!contr) {
		int_error();
		return;
	}

	contrD2Trace(contr, buf, len);
}
#endif
