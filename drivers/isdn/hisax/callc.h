#ifndef __CALLC_H__
#define __CALLC_H__

#include "isdnl4.h"

struct D_Layer4 {
	struct Layer4 l4;
	struct Channel *chan;
};

// =================================================================
// struct Channel

struct Channel {
	struct Layer4 l4; // derived from Layer 4
	struct CallcIf *c_if;
	struct D_Layer4 d_l4;
	struct IsdnCardState *cs;
	int chan;
	int tx_cnt;
	struct FsmInst fi;
	int l2_protocol, l2_active_protocol;
	int l3_protocol;
	int data_open;
	struct l4_process l4pc;
	int leased;
	int debug;
};

void HiSax_mod_inc_use_count(struct IsdnCardState *cs);
void HiSax_mod_dec_use_count(struct IsdnCardState *cs);

// =================================================================
// Interface to config.c

struct CallcIf {
	struct IsdnCardState *cs;
	int b3_mode;
	int myid;
	isdn_if iif;
	u_char *status_buf;
	u_char *status_read;
	u_char *status_write;
	u_char *status_end;
	struct Channel channel[2+MAX_WAITING_CALLS];
};

#ifdef CONFIG_HISAX_LLI
extern const char *lli_revision;

struct CallcIf *newCallcIf(struct IsdnCardState *cs, char *id, int protocol);
void delCallcIf(struct CallcIf *c_if);
void callcIfRun(struct CallcIf *c_if);
void callcIfStop(struct CallcIf *c_if);
void callcIfPutStatus(struct CallcIf *c_if, char *msg);
void CallcNew(void);
void CallcFree(void);
#endif

#ifdef CONFIG_HISAX_CAPI
struct Contr *newContr(struct IsdnCardState *cs, char *id, int protocol);
void delContr(struct Contr *contr);
void contrRun(struct Contr *contr);
void contrStop(struct Contr *contr);
void contrPutStatus(struct Contr *contr, char *msg);
int CapiNew(void);
void CapiFree(void);
#endif

#endif
