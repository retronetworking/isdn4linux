#include "hisax_capi.h"
#include "callc.h"
#include "l4l3if.h"
#include "l3dss1.h"
#include "asn1_enc.h"

#define applDebug(appl, lev, fmt, args...) \
        debug(lev, appl->contr->cs, "", fmt, ## args)

void applConstr(struct Appl *appl, struct Contr *contr, __u16 ApplId, capi_register_params *rp)
{
	HiSax_mod_inc_use_count(contr->cs);
 
	memset(appl, 0, sizeof(struct Appl));
	appl->contr = contr;
	appl->ApplId = ApplId;
	appl->MsgId = 1;
	appl->NotificationMask = 0;
	memcpy(&appl->rp, rp, sizeof(capi_register_params));
	listenConstr(&appl->listen, contr, ApplId);
}

void applDestr(struct Appl *appl)
{
	int i;

	listenDestr(&appl->listen);
	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (appl->cplcis[i]) {
			cplciDestr(appl->cplcis[i]);
			kfree(appl->cplcis[i]);
		}
	}
	HiSax_mod_dec_use_count(appl->contr->cs);
}

struct Cplci *applAdr2cplci(struct Appl *appl, __u32 adr)
{
	int i = (adr >> 8) & 0xff;

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
	       int_error();
               return 0;
       }
       return appl->cplcis[i - 1];
}

void applSendMessage(struct Appl *appl, struct sk_buff *skb)
{
	struct Plci *plci;
	struct Cplci *cplci;

	switch (CAPICMD(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data))) {

	// for NCCI state machine
	case CAPI_DATA_B3_REQ:
		cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
		if (!cplci) {
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		if (!cplci->ncci) {
			int_error();
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		ncciSendMessage(cplci->ncci, skb);
		break;
	case CAPI_DATA_B3_RESP:
	case CAPI_CONNECT_B3_REQ:
	case CAPI_CONNECT_B3_RESP:
	case CAPI_CONNECT_B3_ACTIVE_RESP:
	case CAPI_DISCONNECT_B3_REQ:
	case CAPI_DISCONNECT_B3_RESP:
		cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
		if (!cplci) {
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		if (!cplci->ncci) {
			int_error();
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		ncciSendMessage(cplci->ncci, skb);
		break;
	// for PLCI state machine
	case CAPI_ALERT_REQ:
	case CAPI_CONNECT_RESP:
	case CAPI_CONNECT_ACTIVE_RESP:
	case CAPI_DISCONNECT_REQ:
	case CAPI_DISCONNECT_RESP:
		cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
		if (!cplci) {
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		cplciSendMessage(cplci, skb);
		break;
	case CAPI_CONNECT_REQ:
		plci = contrNewPlci(appl->contr);
		if (!plci) {
			contrAnswerMessage(appl->contr, skb, CapiNoPlciAvailable);
			goto free;
		}
		cplci = applNewCplci(appl, plci);
		if (!cplci) {
			contrDelPlci(appl->contr, plci);
			contrAnswerMessage(appl->contr, skb, CapiNoPlciAvailable);
			goto free;
		}
		cplciSendMessage(cplci, skb);
		break;

	// for LISTEN state machine
	case CAPI_LISTEN_REQ:
		listenSendMessage(&appl->listen, skb);
		break;

	// other
	case CAPI_FACILITY_REQ:
		applFacilityReq(appl, skb);
		break;
	case CAPI_FACILITY_RESP:
		goto free;
	case CAPI_MANUFACTURER_REQ:
		applManufacturerReq(appl, skb);
		break;
	case CAPI_INFO_RESP:
		goto free;
	default:
		applDebug(appl, LL_DEB_WARN, "applSendMessage: %#x %#x not handled!", 
			  CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data));
		if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_REQ)
			contrAnswerMessage(appl->contr, skb, 
					   CapiMessageNotSupportedInCurrentState);
		goto free;
	}

	return;

 free:
	idev_kfree_skb(skb, FREE_READ);
}

void applFacilityReq(struct Appl *appl, struct sk_buff *skb)
{
	_cmsg cmsg;
	capi_message2cmsg(&cmsg, skb->data);

	switch (cmsg.FacilitySelector) {
	case 0x0003: // SupplementaryServices
		applSuppFacilityReq(appl, &cmsg);
		break;
	default:
		int_error();
	}
	
	idev_kfree_skb(skb, FREE_READ);
}

void applSuppFacilityReq(struct Appl *appl, _cmsg *cmsg)
{
	if (cmsg->FacilityRequestParameter[0] < 3) {
		contrAnswerCmsg(appl->contr, cmsg, CapiIllMessageParmCoding);
		return;
	} // FIXME more checking
	switch (*((__u16*)(cmsg->FacilityRequestParameter+1))) {
	case 0x0000: // GetSupportedServices
		applGetSupportedServices(appl, cmsg);
		break;
	case 0x0001: // Listen
		applFacListen(appl, cmsg);
		break;
	case 0x0009: // CF Activate
		applFacCFActivate(appl, cmsg);
		break;
	case 0x000a: // CF Deactivate
		applFacCFDeactivate(appl, cmsg);
		break;
	default:
		capi_cmsg_answer(cmsg);
		cmsg->Info = 0x0000;
		cmsg->FacilityConfirmationParameter = "\x05\x00\x00\x02\x0e\x30";
		// 0x09 struct len
		//   0xxxxx Function
		//   0x02   struct len
		//     0x300e      Facility not supported
		cmsg->FacilityConfirmationParameter[1] = cmsg->FacilityRequestParameter[1];
		cmsg->FacilityConfirmationParameter[2] = cmsg->FacilityRequestParameter[2];
		contrRecvCmsg(appl->contr, cmsg);
		return;
	}
}

void applGetSupportedServices(struct Appl *appl, _cmsg *cmsg)
{
	capi_cmsg_answer(cmsg);
	cmsg->Info = 0x0000;
	cmsg->FacilityConfirmationParameter = "\x09\x00\x00\x06\x00\x00\x10\x00\x00\x00";
	// 0x09 struct len
	//   0x0000 Function GetSupportedServices
	//   0x06   struct len
	//     0x0000      success
	//     0x000010000 Supported Services
	contrRecvCmsg(appl->contr, cmsg);
}

void applFacListen(struct Appl *appl, _cmsg *cmsg)
{
	if (cmsg->FacilityRequestParameter[0] != 7) {
		contrAnswerCmsg(appl->contr, cmsg, CapiIllMessageParmCoding);
	}
	if (cmsg->FacilityRequestParameter[3] != 4) {
		contrAnswerCmsg(appl->contr, cmsg, CapiIllMessageParmCoding);
	}
	appl->NotificationMask = *((__u32*)(cmsg->FacilityRequestParameter+4));
	capi_cmsg_answer(cmsg);
	cmsg->Info = 0x0000;
	cmsg->FacilityConfirmationParameter = "\x05\x01\x00\x02\x00\x00";
	// 0x05 struct len
	//   0x0001 Function Listen
	//   0x02   struct len
	//     0x0000      success
	contrRecvCmsg(appl->contr, cmsg);
}

static __u16 lastInvokeId = 0;

void applFacCFActivate(struct Appl *appl, _cmsg *cmsg)
{
        __u8 tmp[255], t2[255];
	__u8 *p;
	__u32 handle;
	__u16 procedure;
	__u16 basicService;
	__u8 *servedUserNumber, *forwardedToNumber, *forwardedToSubaddress;
	struct sk_buff *skb;
	struct DummyProcess *dummy_pc;
	int len;

	lastInvokeId = (lastInvokeId + 1) & 0xffff;

	dummy_pc = contrNewDummyPc(appl->contr, lastInvokeId);
	if (!dummy_pc) {
		int_error(); // FIXME
		return;
	}

	p = cmsg->FacilityRequestParameter + 4;

	handle = *p++;
	handle |= *p++ << 8;
	handle |= *p++ << 16;
	handle |= *p++ << 24;

	procedure = *p++;
	procedure |= *p++ << 8;

	basicService = *p++;
	basicService |= *p++ << 8;
	
	servedUserNumber = p;
	p += *p + 1;
	
	forwardedToNumber = p;
	p += *p + 1;
	
	forwardedToSubaddress = p;
	p += *p + 1;
	
	tmp[0] = MT_FACILITY;
	tmp[1] = IE_FACILITY;
	tmp[2] = 0;     // length
	tmp[3] = 0x91;  // remote operations protocol
	tmp[4] = 0xa1;  // invoke component
	tmp[5] = 0;     // length

	p = &tmp[6];

	len = encodeInt(t2, lastInvokeId);
	memcpy(p, t2, len); p += len;

	len = encodeInt(t2, 0x07); // activationDiversion
	memcpy(p, t2, len); p += len;

	len = encodeActivationDiversion(t2, procedure, basicService, forwardedToNumber,
					forwardedToSubaddress, servedUserNumber);
	memcpy(p, t2, len); p += len;

	tmp[5] = p - &tmp[6];
	tmp[2] = p - &tmp[3];

	len = p - tmp;
	skb = alloc_skb(len+16, GFP_ATOMIC);
	skb_reserve(skb, 16);
	memcpy(skb_put(skb, len), tmp, len); \

	L4L3(&appl->contr->l4, CC_DUMMY | REQUEST, skb);
	
	dummy_pc->Handle = handle;
	dummy_pc->Function = 0x0009;
	dummy_pc->ApplId = appl->ApplId;
	dummyPcAddTimer(dummy_pc, 4000);

	capi_cmsg_answer(cmsg);
	cmsg->Info = 0x0000;
	cmsg->FacilityConfirmationParameter = "\x05\x09\x00\x02\x00\x00";
	// 0x05 struct len
	//   0x0009 Function CFActivate
	//   0x02   struct len
	//     0x0000      success
	contrRecvCmsg(appl->contr, cmsg);
}

void applFacCFDeactivate(struct Appl *appl, _cmsg *cmsg)
{
        __u8 tmp[255], t2[255];
	__u8 *p;
	__u32 handle;
	__u16 procedure;
	__u16 basicService;
	__u8 *servedUserNumber;
	struct sk_buff *skb;
	struct DummyProcess *dummy_pc;
	int len;

	lastInvokeId = (lastInvokeId + 1) & 0xffff;

	dummy_pc = contrNewDummyPc(appl->contr, lastInvokeId);
	if (!dummy_pc) {
		int_error(); // FIXME
		return;
	}

	p = cmsg->FacilityRequestParameter + 4;

	handle = *p++;
	handle |= *p++ << 8;
	handle |= *p++ << 16;
	handle |= *p++ << 24;

	procedure = *p++;
	procedure |= *p++ << 8;

	basicService = *p++;
	basicService |= *p++ << 8;
	
	servedUserNumber = p;
	p += *p + 1;
	
	tmp[0] = MT_FACILITY;
	tmp[1] = IE_FACILITY;
	tmp[2] = 0;     // length
	tmp[3] = 0x91;  // remote operations protocol
	tmp[4] = 0xa1;  // invoke component
	tmp[5] = 0;     // length

	p = &tmp[6];

	len = encodeInt(t2, lastInvokeId);
	memcpy(p, t2, len); p += len;

	len = encodeInt(t2, 0x08); // dectivationDiversion
	memcpy(p, t2, len); p += len;

	len = encodeDeactivationDiversion(t2, procedure, basicService, servedUserNumber);
	memcpy(p, t2, len); p += len;

	tmp[5] = p - &tmp[6];
	tmp[2] = p - &tmp[3];

	len = p - tmp;
	skb = alloc_skb(len+16, GFP_ATOMIC);
	skb_reserve(skb, 16);
	memcpy(skb_put(skb, len), tmp, len); \

	L4L3(&appl->contr->l4, CC_DUMMY | REQUEST, skb);

	dummy_pc->Handle = handle;
	dummy_pc->Function = 0x000a;
	dummy_pc->ApplId = appl->ApplId;

	capi_cmsg_answer(cmsg);
	cmsg->Info = 0x0000;
	cmsg->FacilityConfirmationParameter = "\x05\x0a\x00\x02\x00\x00";
	// 0x05 struct len
	//   0x000a Function CFDeactivate
	//   0x02   struct len
	//     0x0000      success
	contrRecvCmsg(appl->contr, cmsg);
}

struct Cplci *applNewCplci(struct Appl *appl, struct Plci *plci)
{
	struct Cplci *cplci;
	int i = (plci->adrPLCI >> 8);

	if (appl->cplcis[i - 1]) {
		int_error();
		return 0;
	}
	cplci = kmalloc(sizeof(struct Cplci), GFP_ATOMIC);
	cplciConstr(cplci, appl, plci);
	appl->cplcis[i - 1] = cplci;
	plciAttachCplci(plci, cplci);
	return cplci;
}

void applDelCplci(struct Appl *appl, struct Cplci *cplci)
{
	int i = cplci->adrPLCI >> 8;

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return;
	}
	if (appl->cplcis[i-1] != cplci) {
		int_error();
		return;
	}
	cplciDestr(cplci);
	kfree(cplci);
	appl->cplcis[i-1] = 0;
}

#define CLASS_I4L                   0x00
#define FUNCTION_I4L_LEASED_IN      0x01
#define FUNCTION_I4L_DEC_USE_COUNT  0x02
#define FUNCTION_I4L_INC_USE_COUNT  0x03

#define CLASS_AVM                   0x00
#define FUNCTION_AVM_D2_TRACE       0x01


struct I4LLeasedManuData {
	__u8 Length;
	__u8 BChannel;
};

struct AVMD2Trace {
	__u8 Length;
	__u8 data[4];
};

struct ManufacturerReq {
	__u32 Class;
	__u32 Function;
	union {
		struct I4LLeasedManuData leased;
		struct AVMD2Trace d2trace;
	} f;
}; 

void applManufacturerReqAVM(struct Appl *appl, struct sk_buff *skb)
{
	struct ManufacturerReq *manuReq;

	manuReq = (struct ManufacturerReq *)&skb->data[16];
	if (manuReq->Class != CLASS_AVM) {
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown class %#x\n", manuReq->Class);
		goto out;
	}
	switch (manuReq->Function) {
	case FUNCTION_AVM_D2_TRACE:
		if (skb->len != 16 + 8 + 5)
			goto out;
		if (manuReq->f.d2trace.Length != 4)
			goto out;
		if (memcmp(manuReq->f.d2trace.data, "\200\014\000\000", 4) == 0) {
			test_and_set_bit(APPL_FLAG_D2TRACE, &appl->flags);
		} else if (memcmp(manuReq->f.d2trace.data, "\000\000\000\000", 4) == 0) {
			test_and_clear_bit(APPL_FLAG_D2TRACE, &appl->flags);
		} else {
			int_error();
		}
		break;
	default:
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown function %#x\n", manuReq->Function);
	}

 out:
	idev_kfree_skb(skb, FREE_READ);
}

void applManufacturerReqI4L(struct Appl *appl, struct sk_buff *skb)
{
	int bchannel;
	struct ManufacturerReq *manuReq;

	manuReq = (struct ManufacturerReq *)&skb->data[16];
	if (manuReq->Class != CLASS_I4L) {
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown class %#x\n", manuReq->Class);
		goto out;
	}
	switch (manuReq->Function) {
	case FUNCTION_I4L_LEASED_IN:
		if (skb->len < 16 + 8 + 2)
			goto out;
		if (manuReq->f.leased.Length != 2)
			goto out;
		bchannel = manuReq->f.leased.BChannel;
		if (bchannel < 1 || bchannel > 2)
			goto out;
		L4L3(&appl->contr->l4, CC_SETUP | INDICATION, &bchannel);
		break;
	case FUNCTION_I4L_DEC_USE_COUNT:
		HiSax_mod_dec_use_count(appl->contr->cs);
		break;
	case FUNCTION_I4L_INC_USE_COUNT:
		HiSax_mod_inc_use_count(appl->contr->cs);
		break;
	default:
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown function %#x\n", manuReq->Function);
	}

 out:
	idev_kfree_skb(skb, FREE_READ);
}

void applManufacturerReq(struct Appl *appl, struct sk_buff *skb)
{
	if (skb->len < 16 + 8) {
		return;
	}
	if (memcmp(&skb->data[12], "AVM!", 4) == 0) {
		applManufacturerReqAVM(appl, skb);
	}
	if (memcmp(&skb->data[12], "I4L!", 4) == 0) {
		applManufacturerReqI4L(appl, skb);
	}
	return;
}

void applD2Trace(struct Appl *appl, u_char *buf, int len)
{
	_cmsg cmsg;
	__u8 manuData[255];

	if (!test_bit(APPL_FLAG_D2TRACE, &appl->flags))
		return;
	
	memset(&cmsg, 0, sizeof(_cmsg));
	capi_cmsg_header(&cmsg, appl->ApplId, CAPI_MANUFACTURER, CAPI_IND, 
			 appl->MsgId++, appl->contr->adrController);
	cmsg.ManuID = 0x214D5641; // "AVM!"
	cmsg.Class = CLASS_AVM;
	cmsg.Function = FUNCTION_AVM_D2_TRACE;
	cmsg.ManuData = (_cstruct) &manuData;
	manuData[0] = 2 + len; // length
	manuData[1] = 0x80;
	manuData[2] = 0x0f;
	memcpy(&manuData[3], buf, len);
	
	contrRecvCmsg(appl->contr, &cmsg);
}
