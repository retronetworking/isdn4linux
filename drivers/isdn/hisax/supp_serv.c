#include "hisax_capi.h"
#include "asn1_comp.h"
#include "asn1_enc.h"
#include "isdnl3.h"
#include "l3dss1.h"
#include "l4l3if.h"

#define T_ACTIVATE    4000
#define T_DEACTIVATE  4000
#define T_INTERROGATE 4000

static __inline__ int capiGetWord(__u8 *p, __u8 *end, __u16 *word)
{
	if (p + 2 > end) {
		return -1;
	}
	*word = *p + (*(p+1) << 8);

	return 2;
}

static __inline__ int capiGetDWord(__u8 *p, __u8 *end, __u32 *dword)
{
	if (p + 4 > end) {
		return -1;
	}
	*dword = *p + (*(p+1) << 8) + (*(p+2) << 16) + (*(p+3) << 24);

	return 4;
}

static __inline__ int capiGetStruct(__u8 *p, __u8 *_end, __u8 **strct)
{
	int len = *p++;
	__u8 *end = p + len;

	if (end > _end) return -1;

	*strct = p - 1;

	return len + 1;
}

#define CAPI_GET(func, parm) \
	ret = func(p, end, parm); \
	if (ret < 0) return -1; \
	p += ret

static __inline__ int capiGetFacReqListen(__u8 *p, __u8 *_end, struct FacReqListen *listen)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &listen->NotificationMask);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqSuspend(__u8 *p, __u8 *_end, struct FacReqSuspend *suspend)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetStruct, &suspend->CallIdentity);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqResume(__u8 *p, __u8 *_end, struct FacReqResume *resume)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetStruct, &resume->CallIdentity);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqCFActivate(__u8 *p, __u8 *_end, struct FacReqCFActivate *CFActivate)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &CFActivate->Handle);
	CAPI_GET(capiGetWord, &CFActivate->Procedure);
	CAPI_GET(capiGetWord, &CFActivate->BasicService);
	CAPI_GET(capiGetStruct, &CFActivate->ServedUserNumber);
	CAPI_GET(capiGetStruct, &CFActivate->ForwardedToNumber);
	CAPI_GET(capiGetStruct, &CFActivate->ForwardedToSubaddress);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqCFDeactivate(__u8 *p, __u8 *_end, struct FacReqCFDeactivate *CFDeactivate)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &CFDeactivate->Handle);
	CAPI_GET(capiGetWord, &CFDeactivate->Procedure);
	CAPI_GET(capiGetWord, &CFDeactivate->BasicService);
	CAPI_GET(capiGetStruct, &CFDeactivate->ServedUserNumber);

	if (p != end) return -1;
	return len + 1;
}

#define capiGetFacReqCFInterrogateParameters capiGetFacReqCFDeactivate

static __inline__ int capiGetFacReqCFInterrogateNumbers(__u8 *p, __u8 *_end, struct FacReqCFInterrogateNumbers *CFInterrogateNumbers)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &CFInterrogateNumbers->Handle);

	if (p != end) return -1;
	return len + 1;
}


static __inline__ int capiGetFacReqParm(__u8 *p, struct FacReqParm *facReqParm)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	CAPI_GET(capiGetWord, &facReqParm->Function);

	switch (facReqParm->Function) {
	case 0x0000: // GetSupportedServices
		if (*p++ != 0x00) return -1; // empty struct
		break;
	case 0x0001: // Listen
		CAPI_GET(capiGetFacReqListen, &facReqParm->u.Listen);
		break;
	case 0x0004: // Suspend
		CAPI_GET(capiGetFacReqSuspend, &facReqParm->u.Suspend);
		break;
	case 0x0005: // Resume
		CAPI_GET(capiGetFacReqResume, &facReqParm->u.Resume);
		break;
	case 0x0009: // CF Activate
		CAPI_GET(capiGetFacReqCFActivate, &facReqParm->u.CFActivate);
		break;
	case 0x000a: // CF Deactivate
		CAPI_GET(capiGetFacReqCFDeactivate, &facReqParm->u.CFDeactivate);
		break;
	case 0x000b: // CF Interrogate Parameters
		CAPI_GET(capiGetFacReqCFInterrogateParameters, &facReqParm->u.CFInterrogateParameters);
		break;
	case 0x000c: // CF Interrogate Numbers
		CAPI_GET(capiGetFacReqCFInterrogateNumbers, &facReqParm->u.CFInterrogateNumbers);
		break;
	default:
		return len + 1;
	}

	if (p != end) return -1;
	return len + 1;
}

void applSuppFacilityReq(struct Appl *appl, _cmsg *cmsg)
{
	__u8 tmp[10];
	__u16 Info;
	struct FacReqParm facReqParm;
	struct FacConfParm facConfParm;
	struct Plci *plci;
	struct Cplci *cplci;

	if (capiGetFacReqParm(cmsg->FacilityRequestParameter, &facReqParm) < 0) {
		contrAnswerCmsg(appl->contr, cmsg, CapiIllMessageParmCoding);
		return;
	}
	facConfParm.Function = facReqParm.Function;
	switch (facReqParm.Function) {
	case 0x0000: // GetSupportedServices
		Info = applGetSupportedServices(appl, &facReqParm, &facConfParm);
		break;
	case 0x0001: // Listen
		Info = applFacListen(appl, &facReqParm, &facConfParm);
		break;
	case 0x0004: // Suspend
		cplci = applAdr2cplci(appl, cmsg->adr.adrPLCI);
		if (!cplci) {
			Info = CapiIllContrPlciNcci;
			break;
		}
		Info = cplciFacSuspendReq(cplci, &facReqParm, &facConfParm);
		break;
	case 0x0005: // Resume
		plci = contrNewPlci(appl->contr);
		if (!plci) {
			Info = CapiNoPlciAvailable;
			break;
		}
		cplci = applNewCplci(appl, plci);
		if (!cplci) {
			contrDelPlci(appl->contr, plci);
			Info = CapiNoPlciAvailable;
			break;
		}
		Info = cplciFacResumeReq(cplci, &facReqParm, &facConfParm);
		if (Info == CapiSuccess)
			cmsg->adr.adrPLCI = plci->adrPLCI;
		break;
	case 0x0009: // CF Activate
		Info = applFacCFActivate(appl, &facReqParm, &facConfParm);
		break;
	case 0x000a: // CF Deactivate
		Info = applFacCFDeactivate(appl, &facReqParm, &facConfParm);
		break;
	case 0x000b: // CF Interrogate Parameters
		Info = applFacCFInterrogateParameters(appl, &facReqParm, &facConfParm);
		break;
	case 0x000c: // CF Interrogate Numbers
		Info = applFacCFInterrogateNumbers(appl, &facReqParm, &facConfParm);
		break;
	default:
		Info = CapiSuccess;
		facConfParm.u.Info.SupplementaryServiceInfo = CapiSupplementaryServiceNotSupported;
	}

	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	if (Info == 0x0000)
		capiEncodeFacConfParm(tmp, &facConfParm);
	else
		tmp[0] = 0;
	cmsg->FacilityConfirmationParameter = tmp;
	contrRecvCmsg(appl->contr, cmsg);
}

__u8 *encodeInvokeComponentHead(__u8 *p)
{
	*p++ = IE_FACILITY;
	*p++ = 0;     // length -- not known yet
	*p++ = 0x91;  // remote operations protocol
	*p++ = 0xa1;  // invoke component
	*p++ = 0;     // length -- not known yet
	return p;
}

void encodeInvokeComponentLength(__u8 *msg, __u8 *p)
{
	msg[4] = p - &msg[5];
	msg[1] = p - &msg[2];
}

struct DummyProcess *applNewDummyPc(struct Appl *appl, __u16 Function, __u32 Handle)
{
	struct DummyProcess *dummy_pc;

	dummy_pc = contrNewDummyPc(appl->contr);
	if (!dummy_pc) 
		return 0;

	dummy_pc->ApplId = appl->ApplId;
	dummy_pc->Function = Function;
	dummy_pc->Handle = Handle;
	return dummy_pc;
}

int applGetSupportedServices(struct Appl *appl, struct FacReqParm *facReqParm,
			      struct FacConfParm *facConfParm)
{
	facConfParm->u.GetSupportedServices.SupplementaryServiceInfo = CapiSuccess;
	facConfParm->u.GetSupportedServices.SupportedServices = HiSaxSupportedServices;
	return CapiSuccess;
}

int applFacListen(struct Appl *appl, struct FacReqParm *facReqParm,
		   struct FacConfParm *facConfParm)
{
	if (facReqParm->u.Listen.NotificationMask &~ HiSaxSupportedServices) {
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSupplementaryServiceNotSupported;
	} else {
		appl->NotificationMask = facReqParm->u.Listen.NotificationMask;
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	}
	return CapiSuccess;
}

int applFacCFActivate(struct Appl *appl, struct FacReqParm *facReqParm,
		       struct FacConfParm *facConfParm)
{
	struct DummyProcess *dummy_pc;
	struct facility_req_parm facility_req;
	__u8 *p;

	dummy_pc = applNewDummyPc(appl, facReqParm->Function, facReqParm->u.CFActivate.Handle);
	if (!dummy_pc)
		return CAPI_MSGOSRESOURCEERR;

	p = encodeInvokeComponentHead(facility_req.facility);
	p += encodeInt(p, dummy_pc->invokeId);
	p += encodeInt(p, 0x07); // activationDiversion
	p += encodeActivationDiversion(p, &facReqParm->u.CFActivate);
	encodeInvokeComponentLength(facility_req.facility, p);

	L4L3(&appl->contr->l4, CC_DUMMY | REQUEST, &facility_req);
	dummyPcAddTimer(dummy_pc, T_ACTIVATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

int applFacCFDeactivate(struct Appl *appl, struct FacReqParm *facReqParm,
			 struct FacConfParm *facConfParm)
{
	struct DummyProcess *dummy_pc;
	struct facility_req_parm facility_req;
	__u8 *p;

	dummy_pc = applNewDummyPc(appl, facReqParm->Function, facReqParm->u.CFDeactivate.Handle);
	if (!dummy_pc) 
		return CAPI_MSGOSRESOURCEERR;

	p = encodeInvokeComponentHead(facility_req.facility);
	p += encodeInt(p, dummy_pc->invokeId);
	p += encodeInt(p, 0x08); // dectivationDiversion
	p += encodeDeactivationDiversion(p, &facReqParm->u.CFDeactivate);
	encodeInvokeComponentLength(facility_req.facility, p);

	L4L3(&appl->contr->l4, CC_DUMMY | REQUEST, &facility_req);
	dummyPcAddTimer(dummy_pc, T_DEACTIVATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

int applFacCFInterrogateParameters(struct Appl *appl, struct FacReqParm *facReqParm,
				    struct FacConfParm *facConfParm)
{
	struct DummyProcess *dummy_pc;
	struct facility_req_parm facility_req;
	__u8 *p;

	dummy_pc = applNewDummyPc(appl, facReqParm->Function, 
				  facReqParm->u.CFInterrogateParameters.Handle);
	if (!dummy_pc) 
		return CAPI_MSGOSRESOURCEERR;

	p = encodeInvokeComponentHead(facility_req.facility);
	p += encodeInt(p, dummy_pc->invokeId);
	p += encodeInt(p, 0x0b); // interrogationDiversion
	p += encodeInterrogationDiversion(p,  &facReqParm->u.CFInterrogateParameters);
	encodeInvokeComponentLength(facility_req.facility, p);

	L4L3(&appl->contr->l4, CC_DUMMY | REQUEST, &facility_req);
	dummyPcAddTimer(dummy_pc, T_INTERROGATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

int applFacCFInterrogateNumbers(struct Appl *appl, struct FacReqParm *facReqParm,
				 struct FacConfParm *facConfParm)
{
	struct DummyProcess *dummy_pc;
	struct facility_req_parm facility_req;
	__u8 *p;

	dummy_pc = applNewDummyPc(appl, facReqParm->Function, 
				  facReqParm->u.CFInterrogateNumbers.Handle);
	if (!dummy_pc) 
		return CAPI_MSGOSRESOURCEERR;

	p = encodeInvokeComponentHead(facility_req.facility);
	p += encodeInt(p, dummy_pc->invokeId);
	p += encodeInt(p, 0x11); // InterrogateServedUserNumbers
	encodeInvokeComponentLength(facility_req.facility, p);

	L4L3(&appl->contr->l4, CC_DUMMY | REQUEST, &facility_req);
	dummyPcAddTimer(dummy_pc, T_INTERROGATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
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

struct DummyProcess *contrNewDummyPc(struct Contr* contr)
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
	dummyPcConstr(dummy_pc, contr, ++contr->lastInvokeId);

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


	del_timer(&dummy_pc->tl);
	appl = contrId2appl(contr, dummy_pc->ApplId);
	if (!appl)
		return;
	
	capi_cmsg_header(&cmsg, dummy_pc->ApplId, CAPI_FACILITY, CAPI_IND, 
			 appl->MsgId++, contr->adrController);
	p = &tmp[1];
	p += capiEncodeWord(p, dummy_pc->Function);
	p += capiEncodeFacIndCFact(p, CapiTimeOut, dummy_pc->Handle);
	tmp[0] = p - &tmp[1];
	contrDelDummyPc(contr, dummy_pc);
	cmsg.FacilityIndicationParameter = tmp;
	contrRecvCmsg(contr, &cmsg);
}

#if 0
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
#endif

void contrDummyFacility(struct Contr *contr, struct sk_buff *skb)
{
	struct Appl *appl;
        int ie_len;
	struct asn1_parm parm;
	struct DummyProcess *dummy_pc;
	_cmsg cmsg;
	__u8 tmp[255];
        __u8 *p, *end;
	__u16 ApplId;

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
#if 0
		printk("invokeId %d\n", parm.u.inv.invokeId);
		printk("operationValue %d\n", parm.u.inv.operationValue);
#endif
		switch (parm.u.inv.operationValue) {
		case 0x0009: 
#if 0
			printk("procedure %d basicService %d\n", parm.c.inv.o.actNot.procedure,
			       parm.c.inv.o.actNot.basicService);
			printServedUserNr(&parm.c.inv.o.actNot.servedUserNr);
			printAddress(&parm.c.inv.o.actNot.address);
#endif
			for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
				appl = contrId2appl(contr, ApplId);
				if (!appl)
					continue;
				if (!(appl->NotificationMask & 0x00000010))
					continue;
				
				capi_cmsg_header(&cmsg, ApplId, CAPI_FACILITY, CAPI_IND, 
						 appl->MsgId++, contr->adrController);
				p = &tmp[1];
				p += capiEncodeWord(p, 0x8006);
				p += capiEncodeFacIndCFNotAct(p, &parm.u.inv.o.actNot);
				tmp[0] = p - &tmp[1];
				cmsg.FacilitySelector = 0x0003;
				cmsg.FacilityIndicationParameter = tmp;
				contrRecvCmsg(contr, &cmsg);
			}
			break;
		case 0x000a: 
#if 0
			printk("procedure %d basicService %d\n", parm.c.inv.o.deactNot.procedure,
			       parm.c.inv.o.deactNot.basicService);
			printServedUserNr(&parm.c.inv.o.deactNot.servedUserNr);
#endif
			for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
				appl = contrId2appl(contr, ApplId);
				if (!appl)
					continue;
				if (!(appl->NotificationMask & 0x00000010))
					continue;
				
				capi_cmsg_header(&cmsg, ApplId, CAPI_FACILITY, CAPI_IND, 
						 appl->MsgId++, contr->adrController);
				p = &tmp[1];
				p += capiEncodeWord(p, 0x8007);
				p += capiEncodeFacIndCFNotDeact(p, &parm.u.inv.o.deactNot);
				tmp[0] = p - &tmp[1];
				cmsg.FacilitySelector = 0x0003;
				cmsg.FacilityIndicationParameter = tmp;
				contrRecvCmsg(contr, &cmsg);
			}
			break;
		default:
			int_error();
		}
		break;
	case returnResult:
		dummy_pc = contrId2DummyPc(contr, parm.u.retResult.invokeId);
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
							       &parm.u.retResult.o.resultList);
			break;
		case 0x000c:
			p += capiEncodeFacIndCFinterNumbers(p, 0, dummy_pc->Handle, 
							    &parm.u.retResult.o.list);
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
		dummy_pc = contrId2DummyPc(contr, parm.u.retResult.invokeId);
		if (!dummy_pc)
			return;

		appl = contrId2appl(contr, dummy_pc->ApplId);
		if (!appl)
			return;

		capi_cmsg_header(&cmsg, dummy_pc->ApplId, CAPI_FACILITY, CAPI_IND, 
				 appl->MsgId++, contr->adrController);
		p = &tmp[1];
		p += capiEncodeWord(p, dummy_pc->Function);
		p += capiEncodeFacIndCFact(p, 0x3600 | (parm.u.retError.errorValue &0xff), 
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
