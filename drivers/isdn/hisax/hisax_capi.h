#ifndef __HISAX_CAPI_H__
#define __HISAX_CAPI_H__

#include "hisax.h"
#include "isdnl4.h"
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include "../avmb1/capiutil.h"
#include "../avmb1/capicmd.h"
#include "../avmb1/capilli.h"
#include "asn1.h"

// ---------------------------------------------------------------------------
// common stuff

extern struct capi_driver_interface *di;                  
extern struct capi_driver hisax_driver;

void init_listen(void);
void init_cplci(void);
void init_ncci(void);

#define SuppServiceCF          0x00000010
#define SuppServiceTP          0x00000002
#define HiSaxSupportedServices (SuppServiceCF | SuppServiceTP)

#define CAPIMSG_REQ_DATAHANDLE(m)	(m[18] | (m[19]<<8))
#define CAPIMSG_RESP_DATAHANDLE(m)	(m[12] | (m[13]<<8))

#define CMSGCMD(cmsg) CAPICMD((cmsg)->Command, (cmsg)->Subcommand)

#define CAPI_MAXPLCI 5
#define CAPI_MAXDUMMYPCS 16

struct Bprotocol {
	__u16 B1protocol;
	__u16 B2protocol;
	__u16 B3protocol;
};

__u16 q931CIPValue(struct sk_buff *skb);

struct DummyProcess {
	__u16 invokeId;
	__u16 Function;  
	__u32 Handle;
	__u16 ApplId;
	struct Contr *contr;
	struct timer_list tl;
};

void dummyPcConstr(struct DummyProcess *dummy_pc, struct Contr *contr, __u16 invokeId);
void dummyPcDestr(struct DummyProcess *dummy_pc);
void dummyPcAddTimer(struct DummyProcess *dummy_pc, int msec);

int capiEncodeFacIndSuspend(__u8 *dest, __u16  SupplementaryServiceReason);

struct FacReqListen {
	__u32 NotificationMask;
};

struct FacReqSuspend {
	__u8 *CallIdentity;
};

struct FacReqResume {
	__u8 *CallIdentity;
};

struct FacReqCFActivate {
	__u32 Handle;
	__u16 Procedure;
	__u16 BasicService;
	__u8  *ServedUserNumber;
	__u8  *ForwardedToNumber;
	__u8  *ForwardedToSubaddress;
};

struct FacReqCFDeactivate {
	__u32 Handle;
	__u16 Procedure;
	__u16 BasicService;
	__u8  *ServedUserNumber;
};

#define FacReqCFInterrogateParameters FacReqCFDeactivate

struct FacReqCFInterrogateNumbers {
	__u32 Handle;
};

struct FacReqParm {
	__u16 Function;
	union {
		struct FacReqListen Listen;
		struct FacReqSuspend Suspend;
		struct FacReqResume Resume;
		struct FacReqCFActivate CFActivate;
		struct FacReqCFDeactivate CFDeactivate;
		struct FacReqCFInterrogateParameters CFInterrogateParameters;
		struct FacReqCFInterrogateNumbers CFInterrogateNumbers;
	} u;
};

struct FacConfGetSupportedServices {
	__u16 SupplementaryServiceInfo;
	__u32 SupportedServices;
};

struct FacConfInfo {
	__u16 SupplementaryServiceInfo;
};

struct FacConfParm {
	__u16 Function;
	union {
		struct FacConfGetSupportedServices GetSupportedServices;
		struct FacConfInfo Info;
	} u;
};

// ---------------------------------------------------------------------------
// struct Contr

struct Contr {
	struct Layer4 l4; // derived from Layer4
	struct capi_ctr *ctrl;
	struct IsdnCardState *cs;
	__u32 adrController;
	int b3_mode;
	char infobuf[128];
	char msgbuf[128];
	struct Plci *plcis[CAPI_MAXPLCI];
	struct Appl *appls[CAPI_MAXAPPL];
	struct DummyProcess *dummy_pcs[CAPI_MAXDUMMYPCS];
	__u16 lastInvokeId;
};

int                   contrConstr        (struct Contr *contr, struct IsdnCardState *cs, 
					                       char *id, int protocol);
void                  contrDestr         (struct Contr *contr);
void                  contrDebug         (struct Contr *contr, __u32 level, char *fmt, ...);
void                  contrRegisterAppl  (struct Contr *contr, __u16 ApplId, capi_register_params *rp);
void                  contrReleaseAppl   (struct Contr *contr, __u16 ApplId);
void                  contrSendMessage   (struct Contr *contr, struct sk_buff *skb);
void                  contrLoadFirmware  (struct Contr *contr);
void                  contrReset         (struct Contr *contr);
void                  contrRecvCmsg      (struct Contr *contr, _cmsg *cmsg);
void                  contrAnswerCmsg    (struct Contr *contr, _cmsg *cmsg, __u16 Info);
void                  contrAnswerMessage (struct Contr *contr, struct sk_buff *skb, __u16 Info);
struct Plci *         contrNewPlci       (struct Contr *contr);
struct Appl *         contrId2appl       (struct Contr *contr, __u16 ApplId);
struct Plci *         contrAdr2plci      (struct Contr *contr, __u32 adr);
void                  contrDelPlci       (struct Contr *contr, struct Plci *plci);
void                  contrDummyInd      (struct Contr *contr, struct sk_buff *skb);
struct DummyProcess * contrNewDummyPc    (struct Contr* contr);
struct DummyProcess * contrId2DummyPc    (struct Contr* contr, __u16 invokeId);

// ---------------------------------------------------------------------------
// struct Listen

#define CAPI_INFOMASK_CAUSE     (0x0001)
#define CAPI_INFOMASK_DATETIME  (0x0002)
#define CAPI_INFOMASK_DISPLAY   (0x0004)
#define CAPI_INFOMASK_USERUSER  (0x0008)
#define CAPI_INFOMASK_PROGRESS  (0x0010)
#define CAPI_INFOMASK_FACILITY  (0x0020)
//#define CAPI_INFOMASK_CHARGE    (0x0040)
//#define CAPI_INFOMASK_CALLEDPN  (0x0080)
#define CAPI_INFOMASK_CHANNELID (0x0100)
#define CAPI_INFOMASK_EARLYB3   (0x0200)
//#define CAPI_INFOMASK_REDIRECT  (0x0400)

struct Listen {
	struct Contr *contr;
	__u16 ApplId;
	__u32 InfoMask;
	__u32 CIPmask;
	__u32 CIPmask2;
	struct FsmInst listen_m;
};

void listenConstr(struct Listen *listen, struct Contr *contr, __u16 ApplId);
void listenDestr(struct Listen *listen);
void listenDebug(struct Listen *listen, __u32 level, char *fmt, ...);
void listenSendMessage(struct Listen *listen, struct sk_buff *skb);
int listenHandle(struct Listen *listen, __u16 CIPValue);

// ---------------------------------------------------------------------------
// struct Appl

#define APPL_FLAG_D2TRACE 1

struct Appl {
	struct Contr         *contr;
	__u16                ApplId;
	__u16                MsgId;
	struct Listen        listen;
	struct Cplci         *cplcis[CAPI_MAXPLCI];
	__u32                NotificationMask;
	int                  flags;
	capi_register_params rp;
};

void applConstr(struct Appl *appl, struct Contr *contr, __u16 ApplId, capi_register_params *rp);
void applDestr(struct Appl *appl);
void applDebug(struct Appl *appl, __u32 level, char *fmt, ...);
void applSendMessage(struct Appl *appl, struct sk_buff *skb);
void applFacilityReq(struct Appl *appl, struct sk_buff *skb);
void applSuppFacilityReq(struct Appl *appl, _cmsg *cmsg);
int applGetSupportedServices(struct Appl *appl, struct FacReqParm *facReqParm, 
			      struct FacConfParm *facConfParm);
int applFacListen(struct Appl *appl, struct FacReqParm *facReqParm,
		   struct FacConfParm *facConfParm);
int applFacCFActivate(struct Appl *appl, struct FacReqParm *facReqParm,
		       struct FacConfParm *facConfParm);
int applFacCFDeactivate(struct Appl *appl, struct FacReqParm *facReqParm,
			 struct FacConfParm *facConfParm);
int applFacCFInterrogateNumbers(struct Appl *appl, struct FacReqParm *facReqParm,
				 struct FacConfParm *facConfParm);
int applFacCFInterrogateParameters(struct Appl *appl, struct FacReqParm *facReqParm,
				    struct FacConfParm *facConfParm);
void applManufacturerReq(struct Appl *appl, struct sk_buff *skb);
void applD2Trace(struct Appl *appl, u_char *buf, int len);
struct DummyProcess *applNewDummyPc(struct Appl *appl, __u16 Function, __u32 Handle);
struct Cplci *applNewCplci(struct Appl *appl, struct Plci *plci);
struct Cplci *applAdr2cplci(struct Appl *appl, __u32 adr);
void applDelCplci(struct Appl *appl, struct Cplci *cplci);

// ---------------------------------------------------------------------------
// struct Plci

#define PLCI_FLAG_ALERTING 1
#define PLCI_FLAG_OUTGOING 2

struct Plci {
	struct l4_process l4_pc;
	struct Contr  *contr;
	__u32 adrPLCI;
	int flags;
	int nAppl;
	struct Cplci *cplcis[CAPI_MAXAPPL];
};

void plciConstr(struct Plci *plci, struct Contr *contr, __u32 adrPLCI);
void plciDestr(struct Plci *plci);
void plciDebug(struct Plci *plci, __u32 level, char *fmt, ...);
void plci_l3l4(struct Plci *plci, int pr, void *arg);
void plciAttachCplci(struct Plci *plci, struct Cplci *cplci);
void plciDetachCplci(struct Plci *plci, struct Cplci *cplci);
void plciNewCrInd(struct Plci *plci, struct l3_process *l3_pc);
void plciNewCrReq(struct Plci *plci);

// ---------------------------------------------------------------------------
// struct Cplci

struct Cplci {
	__u32 adrPLCI;
	struct Plci *plci;
	struct Appl *appl;
	struct Ncci *ncci;
	struct Contr *contr;
	struct FsmInst plci_m;
	u_char cause[3]; // we may get a cause from l3 DISCONNECT message
   	                 // which we'll need send in DISCONNECT_IND caused by
	                 // l3 RELEASE message
	struct Bprotocol Bprotocol;
};

void cplciConstr(struct Cplci *cplci, struct Appl *appl, struct Plci *plci);
void cplciDestr(struct Cplci *cplci);
void cplciDebug(struct Cplci *cplci, __u32 level, char *fmt, ...);
void cplci_l3l4(struct Cplci *cplci, int pr, void *arg);
void cplciSendMessage(struct Cplci *cplci, struct sk_buff *skb);
void cplciClearOtherApps(struct Cplci *cplci);
void cplciInfoIndMsg(struct Cplci *cplci,  __u32 mask, void *arg);
void cplciInfoIndIE(struct Cplci *cplci, unsigned char ie, __u32 mask, void *arg);
void cplciRecvCmsg(struct Cplci *cplci, _cmsg *cmsg);
void cplciCmsgHeader(struct Cplci *cplci, _cmsg *cmsg, __u8 cmd, __u8 subcmd);
void cplciLinkUp(struct Cplci *cplci);
void cplciLinkDown(struct Cplci *cplci);
int cplciFacSuspendReq(struct Cplci *cplci, struct FacReqParm *facReqParm,
		       struct FacConfParm *facConfParm);
int cplciFacResumeReq(struct Cplci *cplci, struct FacReqParm *facReqParm,
		      struct FacConfParm *facConfParm);

// ---------------------------------------------------------------------------
// struct Ncci

struct Ncci {
	struct Layer4 l4; // derived from Layer 4
	__u32 adrNCCI;
	struct Contr *contr;
	struct Cplci *cplci;
	struct Appl *appl;
	struct FsmInst ncci_m;
	int window;
	struct { 
		struct sk_buff *skb; 
		__u16 DataHandle;
		__u16 MsgId;
	} xmit_skb_handles[CAPI_MAXDATAWINDOW];
	struct sk_buff *recv_skb_handles[CAPI_MAXDATAWINDOW];
};

void ncciConstr(struct Ncci *ncci, struct Cplci *cplci);
void ncciDestr(struct Ncci *ncci);
void ncciSendMessage(struct Ncci *ncci, struct sk_buff *skb);
void ncci_l3l4(struct Ncci *ncci, int pr, void *arg);
void ncciLinkUp(struct Ncci *ncci);
void ncciLinkDown(struct Ncci *ncci);
void ncciInitSt(struct Ncci *ncci);
void ncciReleaseSt(struct Ncci *ncci);
__u16 ncciSelectBprotocol(struct Ncci *ncci);
void ncciRecvCmsg(struct Ncci *ncci, _cmsg *cmsg);
void ncciCmsgHeader(struct Ncci *ncci, _cmsg *cmsg, __u8 cmd, __u8 subcmd);


static inline void
L4L3(struct Layer4 *l4, int pr, void *arg)
{
	if (!l4) {
		int_error();
		return;
	}
	l4->st->l3.l4l3(l4->st, pr, arg);
}

int capiEncodeWord(__u8 *dest, __u16 i);
int capiEncodeDWord(__u8 *dest, __u32 i);
int capiEncodeFacIndCFact(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle);
int capiEncodeFacIndCFdeact(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle);
int capiEncodeFacIndCFNotAct(__u8 *dest, struct ActDivNotification *actNot);
int capiEncodeFacIndCFNotDeact(__u8 *dest, struct DeactDivNotification *deactNot);
int capiEncodeFacIndCFinterParameters(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle, 
				      struct IntResultList *intResultList);
int capiEncodeFacIndCFinterNumbers(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle, 
				   struct ServedUserNumberList *list);
int capiEncodeFacConfParm(__u8 *dest, struct FacConfParm *facConfParm);

#endif