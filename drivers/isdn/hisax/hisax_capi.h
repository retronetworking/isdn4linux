#ifndef __HISAX_CAPI_H__
#define __HISAX_CAPI_H__

#include "hisax.h"
#include "isdnl4.h"
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include "../avmb1/capiutil.h"
#include "../avmb1/capicmd.h"
#include "../avmb1/capilli.h"

// ---------------------------------------------------------------------------
// common stuff

extern struct capi_driver_interface *di;                  
extern struct capi_driver hisax_driver;

void init_listen(void);
void init_cplci(void);
void init_ncci(void);

#define CAPIMSG_REQ_DATAHANDLE(m)	(m[18] | (m[19]<<8))
#define CAPIMSG_RESP_DATAHANDLE(m)	(m[12] | (m[13]<<8))

#define CMSGCMD(cmsg) CAPICMD((cmsg)->Command, (cmsg)->Subcommand)

#define CAPI_MAXPLCI 5

struct Bprotocol {
	__u16 B1protocol;
	__u16 B2protocol;
	__u16 B3protocol;
};

__u16 q931CIPValue(struct sk_buff *skb);

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
};

int contrConstr(struct Contr *contr, struct IsdnCardState *cs, char *id, int protocol);
void contrDestr(struct Contr *contr);
void contrDebug(struct Contr *contr, __u32 level, char *fmt, ...);
void contrRegisterAppl(struct Contr *contr, __u16 ApplId, capi_register_params *rp);
void contrReleaseAppl(struct Contr *contr, __u16 ApplId);
void contrSendMessage(struct Contr *contr, struct sk_buff *skb);
void contrLoadFirmware(struct Contr *contr);
void contrReset(struct Contr *contr);
void contrRecvCmsg(struct Contr *contr, _cmsg *cmsg);
void contrAnswerCmsg(struct Contr *contr, _cmsg *cmsg, __u16 Info);
void contrAnswerMessage(struct Contr *contr, struct sk_buff *skb, __u16 Info);
struct Plci *contrNewPlci(struct Contr *contr);
struct Appl *contrId2appl(struct Contr *contr, __u16 ApplId);
struct Plci *contrAdr2plci(struct Contr *contr, __u32 adr);
void contrDelPlci(struct Contr *contr, struct Plci *plci);

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
//#define CAPI_INFOMASK_EARLYB3   (0x0200)
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
	int                  flags;
	capi_register_params rp;
};

void applConstr(struct Appl *appl, struct Contr *contr, __u16 ApplId, capi_register_params *rp);
void applDestr(struct Appl *appl);
void applDebug(struct Appl *appl, __u32 level, char *fmt, ...);
void applSendMessage(struct Appl *appl, struct sk_buff *skb);
void applManufacturerReq(struct Appl *appl, struct sk_buff *skb);
void applD2Trace(struct Appl *appl, u_char *buf, int len);
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
	int                    nAppl;
	struct Cplci           *cplcis[CAPI_MAXAPPL];
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
	u_char cause[3];
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
struct Ncci *cplciNewNcci(struct Cplci* cplci);
void cplciDelNcci(struct Cplci* cplci);
void cplciRecvCmsg(struct Cplci *cplci, _cmsg *cmsg);
void cplciCmsgHeader(struct Cplci *cplci, _cmsg *cmsg, __u8 cmd, __u8 subcmd);

// ---------------------------------------------------------------------------
// struct Ncci

struct Ncci {
	struct Layer4 l4; // derived from Layer 4
	struct Cplci  *cplci;
	__u32 adrNCCI;
	struct Contr *contr;
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
void ncciPhActivate(struct Ncci *ncci);
void ncciLinkDown(struct Ncci *ncci);
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





#endif
