#include "hisax_capi.h"
#include "callc.h"
#include "l4l3if.h"
#include "stack.h"
#include "l3dss1.h"
#include "asn1_comp.h"
#include "asn1.h"

#define contrDebug(contr, lev, fmt, args...) \
        debug(lev, contr->cs, "Contr ", fmt, ## args)

static void contr_l3l4(struct PStack *st, int pr, void *arg);
static void d2_listener(struct IsdnCardState *cs, u_char *buf, int len);

int contrConstr(struct Contr *contr, struct IsdnCardState *cs, char *id, int protocol)
{ 
	char tmp[10];

	memset(contr, 0, sizeof(struct Contr));
	contr->l4.l3l4 = contr_l3l4;
	contr->adrController = cs->cardnr + 1;
	contr->cs = cs;
	contr->cs->debug |= LL_DEB_STATE | LL_DEB_INFO | DEB_DLOG_HEX;

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
	for (i = 0; i < CAPI_MAXDUMMYPCS; i++) {
		if (contr->dummy_pcs[i]) {
			dummyPcDestr(contr->dummy_pcs[i]);
			kfree(contr->dummy_pcs[i]);
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
	ctrl->profile.goptions = 0x11; // internal controller, supplementary services
	ctrl->profile.support1 = 3; // HDLC, TRANS
	ctrl->profile.support2 = 3; // X75SLP, TRANS
	ctrl->profile.support3 = 1; // TRANS

	contr->cs->d2_listener = d2_listener;
	
	HiSax_mod_inc_use_count(contr->cs);
	ctrl->ready(ctrl);
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

void contrLoadFirmware(struct Contr *contr)
{
	contrRun(contr); // loading firmware is not necessary, so we just go running
}

void contrReset(struct Contr *contr)
{
	int ApplId;
	struct Appl *appl;

	for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
		appl = contrId2appl(contr, ApplId);
		if (appl)
			applDestr(appl);
		kfree(appl);
		contr->appls[ApplId - 1] = 0;
	}

	contr->cs->d2_listener = 0;

	if (contr->l4.st) {
		release_st(contr->l4.st);
		kfree(contr->l4.st);
		contr->l4.st = 0;
	}
	contr->ctrl->reseted(contr->ctrl);
	HiSax_mod_dec_use_count(contr->cs);
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

void dummyProcessTimeout(unsigned long arg);

void dummyPcConstr(struct DummyProcess *dummy_pc, struct Contr *contr, __u16 invokeId)
{
	memset(dummy_pc, 0, sizeof(struct DummyProcess));
	dummy_pc->contr = contr;
	dummy_pc->invokeId = invokeId;
}

void dummyPcDestr(struct DummyProcess *dummy_pc)
{
	del_timer(&dummy_pc->tl);
}

void dummyPcAddTimer(struct DummyProcess *dummy_pc, int msec)
{
	dummy_pc->tl.function = dummyProcessTimeout;
	dummy_pc->tl.data = (unsigned long) dummy_pc;
	init_timer(&dummy_pc->tl);
	dummy_pc->tl.expires = jiffies + (msec * HZ) / 1000;
	add_timer(&dummy_pc->tl);
}

struct DummyProcess *contrNewDummyPc(struct Contr* contr, __u16 invokeId)
{	
	struct DummyProcess *dummy_pc;
	int i;

	for (i = 0; i < CAPI_MAXDUMMYPCS; i++) {
		if (!contr->dummy_pcs[i])
			break;
	}
	if (i == CAPI_MAXDUMMYPCS)
		return 0;
	dummy_pc = kmalloc(sizeof(struct DummyProcess), GFP_ATOMIC);
	if (!dummy_pc) {
		int_error();
		return 0;
	}
	contr->dummy_pcs[i] = dummy_pc;
	dummyPcConstr(dummy_pc, contr, invokeId);

	return dummy_pc;
}

void contrDelDummyPc(struct Contr* contr, struct DummyProcess *dummy_pc)
{
	int i;

	for (i = 0; i < CAPI_MAXDUMMYPCS; i++) {
		if (contr->dummy_pcs[i] == dummy_pc)
			break;
	}
	if (i == CAPI_MAXDUMMYPCS) {
		int_error();
		return;
	}
	contr->dummy_pcs[i] = 0;
	dummyPcDestr(dummy_pc);
	kfree(dummy_pc);
}

struct DummyProcess *contrId2DummyPc(struct Contr* contr, __u16 invokeId)
{
	int i;

	for (i = 0; i < CAPI_MAXDUMMYPCS; i++) {
		if (contr->dummy_pcs[i])
			if (contr->dummy_pcs[i]->invokeId == invokeId) 
				break;
	}
	if (i == CAPI_MAXDUMMYPCS)
		return 0;
	
	return contr->dummy_pcs[i];
}

void dummyProcessTimeout(unsigned long arg)
{
	struct DummyProcess *dummy_pc = (struct DummyProcess *) arg;
	struct Contr* contr = dummy_pc->contr;
	struct Appl *appl;
	__u8 tmp[10], *p;
	_cmsg cmsg;


	printk("EXPIRE !!!!!!!!!!!!!!!!!!!!!!!!\n");
	del_timer(&dummy_pc->tl);

	appl = contrId2appl(contr, dummy_pc->ApplId);
	if (!appl)
		return;
	
	capi_cmsg_header(&cmsg, dummy_pc->ApplId, CAPI_FACILITY, CAPI_IND, 
			 appl->MsgId++, contr->adrController);
	p = &tmp[1];
	p += capiEncodeWord(p, dummy_pc->Function);
	p += capiEncodeFacIndCFact(p, 0x3303, dummy_pc->Handle);
	tmp[0] = p - &tmp[1];
	cmsg.FacilityIndicationParameter = tmp;
	contrRecvCmsg(contr, &cmsg);
	contrDelDummyPc(contr, dummy_pc);
}

void printPublicPartyNumber(struct PublicPartyNumber *publicPartyNumber)
{
	printk("(%d) %s\n", publicPartyNumber->publicTypeOfNumber, 
	       publicPartyNumber->numberDigits);
}

void printPartyNumber(struct PartyNumber *partyNumber)
{
	switch (partyNumber->type) {
	case 0: 
		printk("unknown %s\n", partyNumber->p.unknown);
		break;
	case 1:
		printPublicPartyNumber(&partyNumber->p.publicPartyNumber);
		break;
	}
}

void printServedUserNr(struct ServedUserNr *servedUserNr)
{
	if (servedUserNr->all) {
		printk("all\n");
	} else {
		printPartyNumber(&servedUserNr->partyNumber);
	}
}

void printAddress(struct Address *address)
{
	printPartyNumber(&address->partyNumber);
	if (address->partySubaddress[0]) {
		printk("sub %s\n", address->partySubaddress);
	}
}

void contrDummyFacility(struct Contr *contr, struct sk_buff *skb)
{
	struct Appl *appl;
	__u8 tmp[128];
        int ie_len;
	_cmsg cmsg;
	struct asn1_parm parm;
        __u8 *p, *end;
	struct DummyProcess *dummy_pc;

        p = findie(skb->data, skb->len, IE_FACILITY, 0);
        if (!p) {
		int_error();
                return;
	}
        p++;
        ie_len = *p++;
        end = p + ie_len;
        if (end > skb->data + skb->len) {
                int_error();
                return;
        }

        if (*p++ != 0x91) { // Supplementary Service Applications
		int_error();
                return;
        }
	ParseComponent(&parm, p, end);
	switch (parm.comp) {
	case invoke:
		printk("invokeId %d\n", parm.c.inv.invokeId);
		printk("operationValue %d\n", parm.c.inv.operationValue);
		switch (parm.c.inv.operationValue) {
		case 0x0009: 
			printk("procedure %d basicService %d\n", parm.c.inv.o.actNot.procedure,
			       parm.c.inv.o.actNot.basicService);
			printServedUserNr(&parm.c.inv.o.actNot.servedUserNr);
			printAddress(&parm.c.inv.o.actNot.address);

			appl = contrId2appl(contr, 1); // FIXME !!!!!!!!!!!!!!!!!!!
			if (!appl)
				return;
			
			capi_cmsg_header(&cmsg, 1/**/, CAPI_FACILITY, CAPI_IND, 
					 appl->MsgId++, contr->adrController);
			p = &tmp[1];
			p += capiEncodeWord(p, 0x8006);
			p += capiEncodeFacIndCFNotAct(p, &parm.c.inv.o.actNot);
			tmp[0] = p - &tmp[1];
			cmsg.FacilityIndicationParameter = tmp;
			contrRecvCmsg(contr, &cmsg);
			break;
		case 0x000a: 
			printk("procedure %d basicService %d\n", parm.c.inv.o.deactNot.procedure,
			       parm.c.inv.o.deactNot.basicService);
			printServedUserNr(&parm.c.inv.o.deactNot.servedUserNr);

			appl = contrId2appl(contr, 1); // FIXME !!!!!!!!!!!!!!!!!!!
			if (!appl)
				return;
			
			capi_cmsg_header(&cmsg, 1/**/, CAPI_FACILITY, CAPI_IND, 
					 appl->MsgId++, contr->adrController);
			p = &tmp[1];
			p += capiEncodeWord(p, 0x8007);
			p += capiEncodeFacIndCFNotDeact(p, &parm.c.inv.o.deactNot);
			tmp[0] = p - &tmp[1];
			cmsg.FacilityIndicationParameter = tmp;
			contrRecvCmsg(contr, &cmsg);
			break;
		default:
			int_error();
		}
		break;
	case returnResult:
		dummy_pc = contrId2DummyPc(contr, parm.c.retResult.invokeId);
		if (!dummy_pc)
			return;

		appl = contrId2appl(contr, dummy_pc->ApplId);
		if (!appl)
			return;

		capi_cmsg_header(&cmsg, dummy_pc->ApplId, CAPI_FACILITY, CAPI_IND, 
				 appl->MsgId++, contr->adrController);
		p = &tmp[1];
		p += capiEncodeWord(p, dummy_pc->Function);
		switch (dummy_pc->Function) {
		case 0x0009:
			p += capiEncodeFacIndCFact(p, 0, dummy_pc->Handle);
			break;
		case 0x000a:
			p += capiEncodeFacIndCFdeact(p, 0, dummy_pc->Handle);
			break;
		case 0x000b:
			p += capiEncodeFacIndCFinterParameters(p, 0, dummy_pc->Handle, 
							       &parm.c.retResult.o.resultList);
			break;
		case 0x000c:
			p += capiEncodeFacIndCFinterNumbers(p, 0, dummy_pc->Handle, 
							    &parm.c.retResult.o.list);
			break;
		default:
			int_error();
			break;
		}
		tmp[0] = p - &tmp[1];
		cmsg.FacilityIndicationParameter = tmp;
		contrRecvCmsg(contr, &cmsg);
		contrDelDummyPc(contr, dummy_pc);
		break;
	case returnError:
		dummy_pc = contrId2DummyPc(contr, parm.c.retResult.invokeId);
		if (!dummy_pc)
			return;

		appl = contrId2appl(contr, dummy_pc->ApplId);
		if (!appl)
			return;

		capi_cmsg_header(&cmsg, dummy_pc->ApplId, CAPI_FACILITY, CAPI_IND, 
				 appl->MsgId++, contr->adrController);
		p = &tmp[1];
		p += capiEncodeWord(p, dummy_pc->Function);
		p += capiEncodeFacIndCFact(p, 0x3600 | (parm.c.retError.errorValue &0xff), 
				       dummy_pc->Handle);
		tmp[0] = p - &tmp[1];
		cmsg.FacilityIndicationParameter = tmp;
		contrRecvCmsg(contr, &cmsg);
		contrDelDummyPc(contr, dummy_pc);
		break;
	default:
		int_error();
	}
}

void contrDummyInd(struct Contr *contr, struct sk_buff *skb)
{
	__u8 mt;

	mt = skb->data[2];
	switch (mt) {
	case MT_FACILITY:
		contrDummyFacility(contr, skb);
		break;
	default:
		int_error();
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

	switch (pr) {
	case CC_NEW_CR | INDICATION:
		pc = arg;
		plci = contrNewPlci(contr);
		if (!plci) {
			pc->l4pc = NULL;
			return;
		} 
		plciNewCrInd(plci, pc);
		break;
	case CC_DUMMY | INDICATION:
		contrDummyInd(contr, arg);
		break;
	default:
		contrDebug(contr, LL_DEB_WARN, __FUNCTION__ ": unknown pr %#x", pr);
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

static void d2_listener(struct IsdnCardState *cs, u_char *buf, int len)
{
	struct Contr *contr = cs->contr;

	if (!contr) {
		int_error();
		return;
	}

	contrD2Trace(contr, buf, len);
}
