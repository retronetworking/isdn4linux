#include "hisax_capi.h"
#include "l3dss1.h"
#include "isdnl3.h"
#include "l4l3if.h"

#define cplciDebug(cplci, lev, fmt, args...) \
        debug(lev, cplci->contr->cs, "", fmt, ## args)

unsigned char *q931IE(struct sk_buff *skb, unsigned char ie)
{
	unsigned char *p;

	p = findie(skb->data, skb->len, ie, 0);
	if (!p) {
		return 0;
	}
	return p+1;
}

__u16 q931CIPValue(struct sk_buff *skb)
{
	unsigned char *p;
	__u16 CIPValue;

	p = findie(skb->data, skb->len, IE_BEARER, 0);
	if (!p) {
		return 0;
	}
	if (memcmp(p, "\x04\x03\x80\x90\xa3", 5) == 0
	    || memcmp(p, "\x04\x03\x80\x90\xa2", 5) == 0) {
		CIPValue = 1;
	} else if (memcmp(p, "\x04\x02\x88\x90", 4) == 0) {
		CIPValue = 2;
	} else if (memcmp(p, "\x04\x02\x89\x90", 4) == 0) {
		CIPValue = 3;
	} else if (memcmp(p, "\x04\x03\x90\x90\xa3", 5) == 0
		   || memcmp(p, "\x04\x03\x90\x90\xa2", 5) == 0) {
		CIPValue = 4;
	} else {
		CIPValue = 0;
	}

	p = findie(skb->data, skb->len, IE_HLC, 0);
	if (!p) {
		return CIPValue;
	}

	switch (CIPValue) {
	case 1:
		if (memcmp(p, "\x7d\x02\x91\x81", 4) == 0) {
			CIPValue = 16;
		}
		break;
	case 4:
		if (memcmp(p, "\x7d\x02\x91\x84", 4) == 0) {
			CIPValue = 17;
		}
		break;
	}
	return CIPValue;
}

__u16 CIPValue2setup(__u16 CIPValue, struct setup_req_parm *parm)
{
	switch (CIPValue) {
	case 16:
		memcpy(parm->high_layer_compatibility, "\x7d\x02\x91\x81", 4);
		// fall through
	case 1:
		memcpy(parm->bearer_capability, "\x04\x03\x80\x90\xa3", 5);
		break;

	case 2:
		memcpy(parm->bearer_capability, "\x04\x02\x88\x90", 4);
		break;
    
	default:
		return CapiIllMessageParmCoding;
	}
	return 0;
}

__u16 CalledPartyNumber2setup(__u8 *CPN, struct setup_req_parm *parm)
{
	int len;

	if (!CPN)
		return 0;
	len = CPN[0];
	if (len == 0)
		return 0;
	if (len > 23 - 2)
		return CapiIllMessageParmCoding;
	parm->called_party_number[0] = IE_CALLED_PN; 
	memcpy(parm->called_party_number + 1, CPN, len + 1); 
	return 0;
}

__u16 CallingPartyNumber2setup(__u8 *CGN, struct setup_req_parm *parm)
{
	int len;

	if (!CGN)
		return 0;
	len = CGN[0];
	if (len == 0)
		return 0;
	if (len > 24 - 2)
		return CapiIllMessageParmCoding;
	parm->calling_party_number[0] = IE_CALLING_PN; 
	memcpy(parm->calling_party_number + 1, CGN, len + 1); 
	return 0;
}

__u16 CalledPartySubaddress2setup(__u8 *SA, struct setup_req_parm *parm)
{
	int len;

	if (!SA)
		return 0;
	len = SA[0];
	if (len == 0)
		return 0;
	if (len > 23 - 2)
		return CapiIllMessageParmCoding;
	parm->called_party_subaddress[0] = IE_CALLED_SUB; 
	memcpy(parm->called_party_subaddress + 1, SA, len + 1); 
	return 0;
}

__u16 CallingPartySubaddress2setup(__u8 *SA, struct setup_req_parm *parm)
{
	int len;

	if (!SA)
		return 0;
	len = SA[0];
	if (len == 0)
		return 0;
	if (len > 23 - 2)
		return CapiIllMessageParmCoding;
	parm->calling_party_subaddress[0] = IE_CALLING_SUB; 
	memcpy(parm->calling_party_subaddress + 1, SA, len + 1); 
	return 0;
}

__u16 BC2setup(__u8 *BC, struct setup_req_parm *parm)
{
	int len;

	if (!BC)
		return 0;
	len = BC[0];
	if (len == 0)
		return 0;
	if (len > 13 - 2)
		return CapiIllMessageParmCoding;
	parm->bearer_capability[0] = IE_BEARER; 
	memcpy(parm->bearer_capability + 1, BC, len + 1); 
	return 0;
}

__u16 LLC2setup(__u8 *LLC, struct setup_req_parm *parm)
{
	int len;

	if (!LLC)
		return 0;
	len = LLC[0];
	if (len == 0)
		return 0;
	if (len > 16 - 2)
		return CapiIllMessageParmCoding;
	parm->low_layer_compatibility[0] = IE_LLC; 
	memcpy(parm->low_layer_compatibility + 1, LLC, len + 1); 
	return 0;
}

__u16 HLC2setup(__u8 *HLC, struct setup_req_parm *parm)
{
	int len;

	if (!HLC)
		return 0;
	len = HLC[0];
	if (len == 0)
		return 0;
	if (len > 4 - 2)
		return CapiIllMessageParmCoding;
	parm->high_layer_compatibility[0] = IE_HLC; 
	memcpy(parm->high_layer_compatibility + 1, HLC, len + 1); 
	return 0;
}

__u16 UserUser2alerting_req(__u8 *UU, struct alerting_req_parm *parm)
{
	int len;

	if (!UU)
		return 0;
	len = UU[0];
	if (len == 0)
		return 0;
	if (len > 131 - 2)
		return CapiIllMessageParmCoding;
	parm->user_user[0] = IE_USER_USER; 
	memcpy(parm->user_user + 1, UU, len + 1); 
	return 0;
}

__u16 cmsg2setup_req(_cmsg *cmsg, struct setup_req_parm *parm)
{
	__u16 Info;

	Info = CIPValue2setup(cmsg->CIPValue, parm);
	if (Info)
		return Info;
	Info = CalledPartyNumber2setup(cmsg->CalledPartyNumber, parm);
	if (Info)
		return Info;
	Info = CallingPartyNumber2setup(cmsg->CallingPartyNumber, parm);
	if (Info)
		return Info;
	Info = CalledPartySubaddress2setup(cmsg->CalledPartySubaddress, parm);
	if (Info)
		return Info;
	Info = CallingPartySubaddress2setup(cmsg->CallingPartySubaddress, parm);
	if (Info)
		return Info;
	Info = BC2setup(cmsg->BC, parm);
	if (Info)
		return Info;
	Info = LLC2setup(cmsg->LLC, parm);
	if (Info)
		return Info;
	Info = HLC2setup(cmsg->HLC, parm);
	if (Info)
		return Info;
	return 0;
}

__u16 cmsg2alerting_req(_cmsg *cmsg, struct alerting_req_parm *parm)
{
	__u16 Info;

	Info = UserUser2alerting_req(cmsg->Useruserdata, parm);
	if (Info)
		return Info;

	return 0;
}

__u16 cplciCheckBprotocol(struct Cplci *cplci, _cmsg *cmsg)
{
	struct capi_ctr *ctrl = cplci->contr->ctrl;

	if (!test_bit(cmsg->B1protocol, &ctrl->profile.support1))
		return CapiB1ProtocolNotSupported;
	if (!test_bit(cmsg->B2protocol, &ctrl->profile.support2))
		return CapiB2ProtocolNotSupported;
	if (!test_bit(cmsg->B3protocol, &ctrl->profile.support3))
		return CapiB3ProtocolNotSupported;

	cplci->Bprotocol.B1protocol = cmsg->B1protocol;
	cplci->Bprotocol.B2protocol = cmsg->B2protocol;
	cplci->Bprotocol.B3protocol = cmsg->B3protocol;

	return 0;
}

// =============================================================== plci ===

/*
 * PLCI state machine
 */

enum {
	ST_PLCI_P_0,
	ST_PLCI_P_0_1,
	ST_PLCI_P_1,
	ST_PLCI_P_2,
	ST_PLCI_P_3,
	ST_PLCI_P_4,
	ST_PLCI_P_ACT,
	ST_PLCI_P_5,
	ST_PLCI_P_6,
	ST_PLCI_P_RES,
}

const ST_PLCI_COUNT = ST_PLCI_P_RES + 1;

static char *str_st_plci[] = {	
	"ST_PLCI_P_0",
	"ST_PLCI_P_0_1",
	"ST_PLCI_P_1",
	"ST_PLCI_P_2",
	"ST_PLCI_P_3",
	"ST_PLCI_P_4",
	"ST_PLCI_P_ACT",
	"ST_PLCI_P_5",
	"ST_PLCI_P_6",
	"ST_PLCI_P_RES",
}; 

enum {
	EV_PLCI_CONNECT_REQ,
	EV_PLCI_CONNECT_CONF,
	EV_PLCI_CONNECT_IND,
	EV_PLCI_CONNECT_RESP,
	EV_PLCI_CONNECT_ACTIVE_IND,
	EV_PLCI_CONNECT_ACTIVE_RESP,
	EV_PLCI_ALERT_REQ,
	EV_PLCI_INFO_REQ,
	EV_PLCI_INFO_IND,
	EV_PLCI_FACILITY_IND,
	EV_PLCI_SELECT_B_PROTOCOL_REQ,
	EV_PLCI_DISCONNECT_REQ,
	EV_PLCI_DISCONNECT_IND,
	EV_PLCI_DISCONNECT_RESP,
	EV_PLCI_SUSPEND_REQ,
	EV_PLCI_SUSPEND_CONF,
	EV_PLCI_RESUME_REQ,
	EV_PLCI_RESUME_CONF,
	EV_PLCI_CC_SETUP_IND,
	EV_PLCI_CC_SETUP_CONF_ERR,
	EV_PLCI_CC_SETUP_CONF,
	EV_PLCI_CC_SETUP_COMPL_IND,
	EV_PLCI_CC_DISCONNECT_IND,
	EV_PLCI_CC_RELEASE_IND,
	EV_PLCI_CC_RELEASE_PROC_IND,
	EV_PLCI_CC_NOTIFY_IND,
	EV_PLCI_CC_SUSPEND_ERR,
	EV_PLCI_CC_SUSPEND_CONF,
	EV_PLCI_CC_RESUME_ERR,
	EV_PLCI_CC_RESUME_CONF,
	EV_PLCI_CC_REJECT_IND,
}

const EV_PLCI_COUNT = EV_PLCI_CC_REJECT_IND + 1;

static char* str_ev_plci[] = {
	"EV_PLCI_CONNECT_REQ",
	"EV_PLCI_CONNECT_CONF",
	"EV_PLCI_CONNECT_IND",
	"EV_PLCI_CONNECT_RESP",
	"EV_PLCI_CONNECT_ACTIVE_IND",
	"EV_PLCI_CONNECT_ACTIVE_RESP",
	"EV_PLCI_ALERT_REQ",
	"EV_PLCI_INFO_REQ",
	"EV_PLCI_INFO_IND",
	"EV_PLCI_FACILITY_IND",
	"EV_PLCI_SELECT_B_PROTOCOL_REQ",
	"EV_PLCI_DISCONNECT_REQ",
	"EV_PLCI_DISCONNECT_IND",
	"EV_PLCI_DISCONNECT_RESP",
	"EV_PLCI_SUSPEND_REQ",
	"EV_PLCI_SUSPEND_CONF",
	"EV_PLCI_RESUME_REQ",
	"EV_PLCI_RESUME_CONF",
	"EV_PLCI_CC_SETUP_IND",
	"EV_PLCI_CC_SETUP_CONF_ERR",
	"EV_PLCI_CC_SETUP_CONF",
	"EV_PLCI_CC_SETUP_COMPL_IND",
	"EV_PLCI_CC_DISCONNECT_IND",
	"EV_PLCI_CC_RELEASE_IND",
	"EV_PLCI_CC_RELEASE_PROC_IND",
	"EV_PLCI_CC_NOTIFY_IND",
	"EV_PLCI_CC_SUSPEND_ERR",
	"EV_PLCI_CC_SUSPEND_CONF",
	"EV_PLCI_CC_RESUME_ERR",
	"EV_PLCI_CC_RESUME_CONF",
	"EV_PLCI_CC_REJECT_IND",
};

static struct Fsm plci_fsm =
{ 0, 0, 0, 0, 0 };

static void cplci_debug(struct FsmInst *fi, char *fmt, ...)
{
	char tmp[128];
	char *p = tmp;
	va_list args;
	struct Cplci *cplci = fi->userdata;
  
	va_start(args, fmt);
	p += sprintf(p, "PLCI 0x%x: ", cplci->adrPLCI);
	p += vsprintf(p, fmt, args);
	*p = 0;
	cplciDebug(cplci, LL_DEB_STATE, tmp);
	va_end(args);
}

inline void cplciRecvCmsg(struct Cplci *cplci, _cmsg *cmsg)
{
	contrRecvCmsg(cplci->contr, cmsg);
}

inline void cplciCmsgHeader(struct Cplci *cplci, _cmsg *cmsg, __u8 cmd, __u8 subcmd)
{
	capi_cmsg_header(cmsg, cplci->appl->ApplId, cmd, subcmd, 
			 cplci->appl->MsgId++, cplci->adrPLCI);
}

static void plci_connect_req(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Plci *plci = cplci->plci;
	struct setup_req_parm setup_req;
	_cmsg *cmsg = arg;
	__u16 Info = 0;

	FsmChangeState(fi, ST_PLCI_P_0_1);
	test_and_set_bit(PLCI_FLAG_OUTGOING, &plci->flags);

	memset(&setup_req, 0, sizeof(struct setup_req_parm));
	if ((Info = cmsg2setup_req(cmsg, &setup_req))) {
		goto answer;
	}
	if ((Info = cplciCheckBprotocol(cplci, cmsg))) {
		goto answer;
	}

	plciNewCrReq(plci);
	p_L4L3(&plci->l4_pc, CC_X_SETUP | REQUEST, &setup_req);
	
 answer:
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	if (cmsg->Info == 0) 
		cmsg->adr.adrPLCI = cplci->adrPLCI;
	cplciRecvCmsg(cplci, cmsg);
	FsmEvent(fi, EV_PLCI_CONNECT_CONF, cmsg);
}

static void plci_connect_conf(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	_cmsg *cmsg = arg;
  
	if (cmsg->Info == 0) {
		FsmChangeState(fi, ST_PLCI_P_1);
	} else {
		FsmChangeState(fi, ST_PLCI_P_0);
		applDelCplci(cplci->appl, cplci);
	}
}

static void plci_connect_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_2);
}

static void plci_suspend_req(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Plci *plci = cplci->plci;
	struct suspend_req_parm *suspend_req = arg;

	p_L4L3(&plci->l4_pc, CC_X_SUSPEND | REQUEST, suspend_req); 
}

static void plci_resume_req(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Plci *plci = cplci->plci;
	struct resume_req_parm *resume_req = arg;
	
	// we already sent CONF with Info = SuppInfo = 0

	FsmChangeState(fi, ST_PLCI_P_RES);

	plciNewCrReq(plci);
	p_L4L3(&plci->l4_pc, CC_X_RESUME | REQUEST, resume_req);
}

static void plci_alert_req(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Plci *plci = cplci->plci;
	struct alerting_req_parm alerting_req;
	_cmsg *cmsg = arg;
	__u16 Info;
	
	if (test_and_set_bit(PLCI_FLAG_ALERTING, &plci->flags)) {
		Info = 0x0003; // other app is already alerting
	} else {
		memset(&alerting_req, 0, sizeof(struct alerting_req_parm));
		Info = cmsg2alerting_req(cmsg, &alerting_req);
		if (Info == 0) {
			p_L4L3(&plci->l4_pc, CC_ALERTING | REQUEST, &alerting_req); 
		}
	}
	
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_connect_resp(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Plci *plci = cplci->plci;
	struct disconnect_req_parm disconnect_req;
	unsigned char cause[4];
	_cmsg *cmsg = arg;

	switch (cmsg->Reject) {
	case 0 : // accept
		if (cplciCheckBprotocol(cplci, cmsg)) {
			int_error();
		}
		cplciClearOtherApps(cplci);
		p_L4L3(&plci->l4_pc, CC_SETUP | RESPONSE, 0);
		FsmChangeState(fi, ST_PLCI_P_4);
		break;
	default : // ignore, reject 
		memcpy(cause, "\x08\x02\x80", 3);
		switch (cmsg->Reject) {
		case 2: cause[3] = 0x90; break; // normal call clearing
		case 3: cause[3] = 0x91; break; // user busy
		case 4: cause[3] = 0xac; break; // req circuit/channel not avail
		case 5: cause[3] = 0x9d; break; // fac rejected
		case 6: cause[3] = 0x86; break; // channel unacceptable
		case 7: cause[3] = 0xd8; break; // incompatible dest
		case 8: cause[3] = 0x9b; break; // dest out of order
		default:
			if ((cmsg->Reject & 0xff00) == 0x3400) {
				cause[3] = cmsg->Reject & 0xff;
			} else {
				cause[3] = 0x90; break; // normal call clearing
			}
		}

		if (cmsg->Reject != 1) { // ignore
			cplciClearOtherApps(cplci);
		}
		plciDetachCplci(plci, cplci);
		if (plci->nAppl == 0) {
			if (test_bit(PLCI_FLAG_ALERTING, &plci->flags)) { 
				// if we already answered, we can't just ignore but must clear actively
				memset(&disconnect_req, 0, sizeof(struct disconnect_req_parm));
				memcpy(&disconnect_req.cause, cause, 4);
				p_L4L3(&plci->l4_pc, CC_X_DISCONNECT | REQUEST, &disconnect_req);
			} else {
				p_L4L3(&plci->l4_pc, CC_IGNORE | REQUEST, 0);
			}
		}
		
		capi_cmsg_answer(cmsg);
		cmsg->Command = CAPI_DISCONNECT;
		cmsg->Subcommand = CAPI_IND;
		cmsg->Messagenumber = cplci->appl->MsgId++;
		FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, cmsg);
		cplciRecvCmsg(cplci, cmsg);
	}
}

static void plci_connect_active_ind(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_ACT);
	cplci->ncci = cplciNewNcci(cplci);
	if (!cplci->ncci) {
		int_error();
		return;
	}
	ncciLinkUp(cplci->ncci);
	if (!test_bit(PLCI_FLAG_OUTGOING, &cplci->plci->flags)) {
		ncciPhActivate(cplci->ncci);
	}
}

static void plci_connect_active_resp(struct FsmInst *fi, int event, void *arg)
{
}

static void plci_disconnect_req(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Plci *plci = cplci->plci;
	struct disconnect_req_parm disconnect_req;
	_cmsg *cmsg = arg;

	FsmChangeState(fi, ST_PLCI_P_5);
	

	if (!plci) {
		int_error();
		return;
	}
	capi_cmsg_answer(cmsg);
	cmsg->Reason = 0; // disconnect initiated
	cplciRecvCmsg(cplci, cmsg);

	if (cplci->ncci) {
		ncciLinkDown(cplci->ncci);
	}
	memset(&disconnect_req, 0, sizeof(struct disconnect_req_parm));
	memcpy(&disconnect_req.cause, "\x08\x02\x80\x90", 4); // normal call clearing
	p_L4L3(&plci->l4_pc, CC_X_DISCONNECT | REQUEST, &disconnect_req);
}

static void plci_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_5);
	if (cplci->ncci)
		ncciLinkDown(cplci->ncci);
}

static void plci_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	// facility_ind Resume: Reason = 0
	struct Cplci *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_ACT);
	cplci->ncci = cplciNewNcci(cplci);
	if (!cplci->ncci) {
		int_error();
		return;
	}
	ncciLinkUp(cplci->ncci);
}

static void plci_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_6);
}

static void plci_disconnect_resp(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_0);
	applDelCplci(cplci->appl, cplci);
}

static void plci_cc_setup_conf(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	_cmsg cmsg;

	memset(&cmsg, 0, sizeof(_cmsg));
	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	if (skb) {
#if 0
		cmsg.ConnectedNumber        = q931ConnectedNumber(skb);
		cmsg.ConnectedSubaddress    = q931ConnectedSubaddress(skb);
#endif
		cmsg.LLC                    = q931IE(skb, IE_LLC);
	}
	FsmEvent(fi, EV_PLCI_CONNECT_ACTIVE_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_conf_err(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	_cmsg cmsg;

	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	cmsg.Reason = 0;
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_ind(struct FsmInst *fi, int event, void *arg)
{ 
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	_cmsg cmsg;

	memset(&cmsg, 0, sizeof(_cmsg));
	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT, CAPI_IND);
	
	cmsg.CIPValue               = q931CIPValue(skb);
	cmsg.CalledPartyNumber      = q931IE(skb, IE_CALLED_PN);
	cmsg.CallingPartyNumber     = q931IE(skb, IE_CALLING_PN);
	cmsg.CalledPartySubaddress  = q931IE(skb, IE_CALLED_SUB);
	cmsg.CallingPartySubaddress = q931IE(skb, IE_CALLING_SUB);
	cmsg.BC                     = q931IE(skb, IE_BEARER);
	cmsg.LLC                    = q931IE(skb, IE_LLC);
	cmsg.HLC                    = q931IE(skb, IE_HLC);
	// all else set to default
	
	FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_compl_ind(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	_cmsg cmsg;

	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_ACTIVE_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	unsigned char *cause = 0;

	if (skb) 
		cause = q931IE(skb, IE_CAUSE);
	if (cause)
		memcpy(cplci->cause, cause, 3);
	
// FIXME: if not early B3-Connect
	if (cplci->ncci) {
		ncciLinkDown(cplci->ncci);
	}
	p_L4L3(&cplci->plci->l4_pc, CC_RELEASE | REQUEST, 0);
}

static void plci_cc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	unsigned char *cause = 0;
	_cmsg cmsg;
	
	plciDetachCplci(cplci->plci, cplci);

	if (cplci->ncci) {
		ncciLinkDown(cplci->ncci);
	}
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (skb)
		cause = q931IE(skb, IE_CAUSE);
	if (cause) {
		cmsg.Reason = 0x3400 | cause[2];
	} else if (cplci->cause[0]) {
		cmsg.Reason = 0x3400 | cplci->cause[2];
	} else {
		cmsg.Reason = 0;
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_notify_ind(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	unsigned char *notify;
	_cmsg cmsg;
	__u8 tmp[10], *p;

	notify = q931IE(skb, IE_NOTIFY);
	if (!notify)
		return;

	if (notify[0] != 1) // len != 1
		return;

	switch (notify[1]) {
	case 0x80: // user suspended
	case 0x81: // user resumed
		if (!(cplci->appl->NotificationMask & SuppServiceTP))
			break;
		cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
		p = &tmp[1];
		p += capiEncodeWord(p, 0x8002 + (notify[1] & 1)); // Suspend/Resume Notification
		*p++ = 0; // empty struct
		tmp[0] = p - &tmp[1];
		cmsg.FacilitySelector = 0x0003;
		cmsg.FacilityIndicationParameter = tmp;
		cplciRecvCmsg(cplci, &cmsg);
		break;
	}

}

static void plci_suspend_reply(struct Cplci *cplci, __u16 SuppServiceReason)
{
	_cmsg cmsg;
	__u8 tmp[10], *p;

	cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
	p = &tmp[1];
	p += capiEncodeWord(p, 0x0004); // Suspend
	p += capiEncodeFacIndSuspend(p, SuppServiceReason);
	tmp[0] = p - &tmp[1];
	cmsg.FacilitySelector = 0x0003;
	cmsg.FacilityIndicationParameter = tmp;
	cplciRecvCmsg(cplci, &cmsg);

	if (SuppServiceReason == CapiSuccess)
		FsmEvent(&cplci->plci_m, EV_PLCI_SUSPEND_CONF, &cmsg);
}

static void plci_cc_suspend_err(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	unsigned char *cause = 0;
	__u16 SuppServiceReason;
	
	if (skb) { // reject from network
		cause = q931IE(skb, IE_CAUSE);
		if (cause)
			SuppServiceReason = 0x3400 | cause[2];
		else
			SuppServiceReason = CapiProtocolErrorLayer3;
	} else { // timeout
		SuppServiceReason = CapiTimeOut;
	}
	plci_suspend_reply(cplci, SuppServiceReason);
}

static void plci_cc_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	_cmsg cmsg;

	plci_suspend_reply(cplci, CapiSuccess);
	
	plciDetachCplci(cplci->plci, cplci);

	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_resume_err(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct sk_buff *skb = arg;
	unsigned char *cause = 0;
	_cmsg cmsg;
	
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (skb) { // reject from network
		plciDetachCplci(cplci->plci, cplci);
		cause = q931IE(skb, IE_CAUSE);
		if (cause)
			cmsg.Reason = 0x3400 | cause[2];
		else
			cmsg.Reason = 0;
	} else { // timeout
		cmsg.Reason = CapiProtocolErrorLayer1;
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	_cmsg cmsg;
	__u8 tmp[10], *p;
	
	cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
	p = &tmp[1];
	p += capiEncodeWord(p, 0x0005); // Suspend
	p += capiEncodeFacIndSuspend(p, CapiSuccess);
	tmp[0] = p - &tmp[1];
	cmsg.FacilitySelector = 0x0003;
	cmsg.FacilityIndicationParameter = tmp;
	contrRecvCmsg(cplci->contr, &cmsg);

	FsmEvent(&cplci->plci_m, EV_PLCI_RESUME_CONF, &cmsg);
}

static void plci_select_b_protocol_req(struct FsmInst *fi, int event, void *arg)
{
	struct Cplci *cplci = fi->userdata;
	struct Ncci *ncci = cplci->ncci;
	struct StackParams sp;
	int bchannel, retval;
	_cmsg *cmsg = arg;
	__u16 Info = 0;

	Info = cplciCheckBprotocol(cplci, cmsg);

	if (!ncci) {
		int_error();
		return;
	}
	if (!ncci->l4.st) {
 		int_error();
		return;
	}
	release_st(ncci->l4.st);
	memset(ncci->l4.st, 0, sizeof(struct PStack));
	bchannel = cplci->plci->l4_pc.l3pc->para.bchannel - 1;
	sp.b1_mode = cplci->Bprotocol.B1protocol;
	sp.b2_mode = cplci->Bprotocol.B2protocol;
	sp.b3_mode = cplci->Bprotocol.B3protocol;
	sp.headroom = 22; // reserve space for DATA_B3 IND message in skb's
	retval = init_st(&ncci->l4, cplci->contr->cs, &sp, bchannel);

	// FIXME: phActivate?

	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_info_req_overlap(struct FsmInst *fi, int event, void *arg)
{
}

static void plci_info_req(struct FsmInst *fi, int event, void *arg)
{
}

static struct FsmNode fn_plci_list[] =
{
  {ST_PLCI_P_0,                EV_PLCI_CONNECT_REQ,           plci_connect_req},
  {ST_PLCI_P_0,                EV_PLCI_CONNECT_IND,           plci_connect_ind},
  {ST_PLCI_P_0,                EV_PLCI_RESUME_REQ,            plci_resume_req},
  {ST_PLCI_P_0,                EV_PLCI_CC_SETUP_IND,          plci_cc_setup_ind},

  {ST_PLCI_P_0_1,              EV_PLCI_CONNECT_CONF,          plci_connect_conf},

  {ST_PLCI_P_1,                EV_PLCI_CONNECT_ACTIVE_IND,    plci_connect_active_ind},
  {ST_PLCI_P_1,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_1,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_1,                EV_PLCI_INFO_REQ,              plci_info_req_overlap},
  {ST_PLCI_P_1,                EV_PLCI_CC_SETUP_CONF,         plci_cc_setup_conf},
  {ST_PLCI_P_1,                EV_PLCI_CC_SETUP_CONF_ERR,     plci_cc_setup_conf_err},
  {ST_PLCI_P_1,                EV_PLCI_CC_DISCONNECT_IND,     plci_cc_disconnect_ind},
  {ST_PLCI_P_1,                EV_PLCI_CC_RELEASE_PROC_IND,   plci_cc_setup_conf_err},
  {ST_PLCI_P_1,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},
  {ST_PLCI_P_1,                EV_PLCI_CC_REJECT_IND,         plci_cc_release_ind},

  {ST_PLCI_P_2,                EV_PLCI_ALERT_REQ,             plci_alert_req},
  {ST_PLCI_P_2,                EV_PLCI_CONNECT_RESP,          plci_connect_resp},
  {ST_PLCI_P_2,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_2,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_2,                EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_2,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_4,                EV_PLCI_CONNECT_ACTIVE_IND,    plci_connect_active_ind},
  {ST_PLCI_P_4,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_4,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_4,                EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_4,                EV_PLCI_CC_SETUP_COMPL_IND,    plci_cc_setup_compl_ind},
  {ST_PLCI_P_4,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_ACT,              EV_PLCI_CONNECT_ACTIVE_RESP,   plci_connect_active_resp},
  {ST_PLCI_P_ACT,              EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_ACT,              EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_ACT,              EV_PLCI_SELECT_B_PROTOCOL_REQ, plci_select_b_protocol_req},
  {ST_PLCI_P_ACT,              EV_PLCI_SUSPEND_REQ,           plci_suspend_req},
  {ST_PLCI_P_ACT,              EV_PLCI_SUSPEND_CONF,          plci_suspend_conf},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_DISCONNECT_IND,     plci_cc_disconnect_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_NOTIFY_IND,         plci_cc_notify_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_SUSPEND_ERR,        plci_cc_suspend_err},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_SUSPEND_CONF,       plci_cc_suspend_conf},

  {ST_PLCI_P_5,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_5,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_6,                EV_PLCI_DISCONNECT_RESP,       plci_disconnect_resp},

  {ST_PLCI_P_RES,              EV_PLCI_RESUME_CONF,           plci_resume_conf},
  {ST_PLCI_P_RES,              EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_RES,              EV_PLCI_CC_RESUME_ERR,         plci_cc_resume_err},
  {ST_PLCI_P_RES,              EV_PLCI_CC_RESUME_CONF,        plci_cc_resume_conf},

#if 0
  {ST_PLCI_P_0,                EV_PLCI_FACILITY_IND,          plci_facility_ind_p_0_off_hook},

  {ST_PLCI_P_0_1,              EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_1,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_2,                EV_PLCI_FACILITY_IND,          plci_facility_ind_p_2_off_hook},

  {ST_PLCI_P_3,                EV_PLCI_CONNECT_RESP,          plci_connect_resp},
  {ST_PLCI_P_3,                EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_3,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_3,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_3,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},
  {ST_PLCI_P_3,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_4,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_ACT,              EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_5,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},
#endif
};

const FN_PLCI_COUNT = sizeof(fn_plci_list)/sizeof(struct FsmNode);

void cplciConstr(struct Cplci *cplci, struct Appl *appl, struct Plci *plci)
{
	memset(cplci, 0, sizeof(struct Cplci));
	cplci->adrPLCI = plci->adrPLCI;
	cplci->appl = appl;
	cplci->plci = plci;
	cplci->contr = plci->contr;
	cplci->plci_m.fsm        = &plci_fsm;
	cplci->plci_m.state      = ST_PLCI_P_0;
	cplci->plci_m.debug      = 1;
	cplci->plci_m.userdata   = cplci;
	cplci->plci_m.printdebug = cplci_debug;
}

void cplciDestr(struct Cplci *cplci)
{
	if (cplci->plci) {
 		plciDetachCplci(cplci->plci, cplci);
	}
	if (cplci->ncci) {
		int_error();
		ncciDestr(cplci->ncci);
		kfree(cplci->ncci);
	}
}

void cplci_l3l4(struct Cplci *cplci, int pr, void *arg)
{
	switch (pr) {
	case CC_SETUP | INDICATION:
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, arg);
		cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, arg);
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_IND, arg); 
		break;
	case CC_SETUP_ERR | CONFIRM:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_CONF_ERR, arg); 
		break;
	case CC_SETUP | CONFIRM:	
		cplciInfoIndIE(cplci, IE_DATE, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, arg);
		cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, arg);
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_CONF, arg); 
		break;
	case CC_SETUP_COMPL | INDICATION:
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, arg);
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_COMPL_IND, arg); 
		break;
	case CC_DISCONNECT | INDICATION:
		cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, arg);
	  	FsmEvent(&cplci->plci_m, EV_PLCI_CC_DISCONNECT_IND, arg); 
		break;
	case CC_RELEASE | INDICATION:
		cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);
		cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, arg);
	        FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_IND, arg); 
		break;
	case CC_RELEASE | CONFIRM:
		cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);
		cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, arg);
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_IND, arg);
		break;
	case CC_RELEASE_CR | INDICATION:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_PROC_IND, arg); 
		break;
	case CC_REJECT | INDICATION:
		cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_REJECT_IND, arg); 
		break;
	case CC_MORE_INFO | INDICATION:
		cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, arg);
		break;
	case CC_PROCEEDING | INDICATION:
		cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, arg);
		break;
	case CC_ALERTING | INDICATION:
		cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);	
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, arg);
		cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, arg);
		break;
	case CC_PROGRESS | INDICATION:
		cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, arg);
		cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, arg);
		cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, arg);
		cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, arg);	
		cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, arg);
		break;
	case CC_CHARGE | INDICATION:
		break;
	case CC_SUSPEND | CONFIRM:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_SUSPEND_CONF, arg); 
		break;
	case CC_SUSPEND_ERR:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_SUSPEND_ERR, arg); 
		break;
	case CC_RESUME | CONFIRM:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_RESUME_CONF, arg); 
		break;
	case CC_RESUME_ERR:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_RESUME_ERR, arg); 
		break;
	case CC_NOTIFY | INDICATION:
		FsmEvent(&cplci->plci_m, EV_PLCI_CC_NOTIFY_IND, arg); 
		break;
	default:
		cplciDebug(cplci, LL_DEB_WARN, 
			   "cplci_handle_call_control: pr 0x%x not handled", pr);
		break;
	}
}

void cplciSendMessage(struct Cplci *cplci, struct sk_buff *skb)
{
	int retval = 0;
	_cmsg cmsg;
	capi_message2cmsg(&cmsg, skb->data);

	switch (CMSGCMD(&cmsg)) {
	case CAPI_ALERT_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_ALERT_REQ, &cmsg);
		break;
	case CAPI_CONNECT_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_REQ, &cmsg);
		break;
	case CAPI_CONNECT_RESP:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_RESP, &cmsg);
		break;
	case CAPI_DISCONNECT_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_REQ, &cmsg);
		break;
	case CAPI_DISCONNECT_RESP:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_RESP, &cmsg);
		break;
	case CAPI_CONNECT_ACTIVE_RESP:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_ACTIVE_RESP, &cmsg);
		break;
	case CAPI_SELECT_B_PROTOCOL_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_SELECT_B_PROTOCOL_REQ, &cmsg);
		break;
	default:
		int_error();
		retval = -1;
	}
	if (retval) { 
		if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_REQ) {
			contrAnswerMessage(cplci->contr, skb, 
					   CapiMessageNotSupportedInCurrentState);
		}
	}
	idev_kfree_skb(skb, FREE_READ);
}

int cplciFacSuspendReq(struct Cplci *cplci, struct FacReqParm *facReqParm,
		     struct FacConfParm *facConfParm)
{
	struct suspend_req_parm suspend_req;
	__u8 *CallIdentity;

	CallIdentity = facReqParm->u.Suspend.CallIdentity;
	if (CallIdentity[0] > 8) 
		return CapiIllMessageParmCoding;
	
	suspend_req.call_identity[0] = IE_CALL_ID;
	memcpy(&suspend_req.call_identity[1], CallIdentity, CallIdentity[0] + 1);
	
	if (FsmEvent(&cplci->plci_m, EV_PLCI_SUSPEND_REQ, &suspend_req)) {
		// no routine
		facConfParm->u.Info.SupplementaryServiceInfo = 
			CapiRequestNotAllowedInThisState;
	} else {
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	}
	return CapiSuccess;
}

int cplciFacResumeReq(struct Cplci *cplci, struct FacReqParm *facReqParm,
		     struct FacConfParm *facConfParm)
{
	struct resume_req_parm resume_req;
	int len;
	__u8 *CallIdentity;

	CallIdentity = facReqParm->u.Resume.CallIdentity;
	len = CallIdentity[0];
	if (len > 8) {
		applDelCplci(cplci->appl, cplci);
		return CapiIllMessageParmCoding;
	}
	
	resume_req.call_identity[0] = IE_CALL_ID;
	memcpy(&resume_req.call_identity[1], CallIdentity, len + 1);
	FsmEvent(&cplci->plci_m, EV_PLCI_RESUME_REQ, &resume_req);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

void cplciClearOtherApps(struct Cplci *cplci)
{
	struct Plci *plci = cplci->plci;
	struct Cplci *cp;
	__u16 applId;
	_cmsg cm;

	for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
		cp = plci->cplcis[applId - 1];
		if (cp && (cp != cplci)) {
			plciDetachCplci(plci, cp);
			
			cplciCmsgHeader(cp, &cm, CAPI_DISCONNECT, CAPI_IND);
			cm.Reason = 0x3304; // other application got the call
			FsmEvent(&cp->plci_m, EV_PLCI_DISCONNECT_IND, &cm);
			cplciRecvCmsg(cp, &cm);
		}
	} 
}

void cplciInfoIndMsg(struct Cplci *cplci,  __u32 mask, void *arg)
{
	struct sk_buff *skb = arg;
	unsigned char mt;
	_cmsg cmsg;

	if (!(cplci->appl->listen.InfoMask & mask))
		return;
	if (!skb)
		return;

	mt = skb->data[skb->data[1] + 2];
	
	cplciCmsgHeader(cplci, &cmsg, CAPI_INFO, CAPI_IND);
	cmsg.InfoNumber = 0x8000 | mt;
	cmsg.InfoElement = 0;
	cplciRecvCmsg(cplci, &cmsg);
}

void cplciInfoIndIE(struct Cplci *cplci, unsigned char ie, __u32 mask, void *arg)
{
	struct sk_buff *skb = arg;
	unsigned char *iep;
	_cmsg cmsg;

	if (!(cplci->appl->listen.InfoMask & mask))
		return;
	if (!skb)
		return;
	iep = q931IE(skb, ie);
	if (!iep)
		return;

	cplciCmsgHeader(cplci, &cmsg, CAPI_INFO, CAPI_IND);
	cmsg.InfoNumber = ie;
	cmsg.InfoElement = iep;
	cplciRecvCmsg(cplci, &cmsg);
}

void init_cplci(void)
{
	plci_fsm.state_count = ST_PLCI_COUNT;
	plci_fsm.event_count = EV_PLCI_COUNT;
	plci_fsm.strEvent = str_ev_plci;
	plci_fsm.strState = str_st_plci;
	
	FsmNew(&plci_fsm, fn_plci_list, FN_PLCI_COUNT);
}

struct Ncci *cplciNewNcci(struct Cplci* cplci)
{
	struct Ncci *ncci;
	
	ncci = kmalloc(sizeof(struct Ncci), GFP_ATOMIC);
	if (!ncci) {
		int_error();
		return 0;
	}
	ncciConstr(ncci, cplci);
	cplci->ncci = ncci;
	return ncci;
}

void cplciDelNcci(struct Cplci *cplci)
{
	ncciDestr(cplci->ncci);
	kfree(cplci->ncci);
	cplci->ncci = 0;
}

