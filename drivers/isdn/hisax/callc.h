#ifndef __CALLC_H__
#define __CALLC_H__

#include "isdnl4.h"

// =================================================================
// struct Channel

struct Channel {
	struct PStack *b_st, *d_st;
	struct CallcIf *c_if;
	struct IsdnCardState *cs;
	int chan;
	int tx_cnt;
	int incoming;
	struct FsmInst fi;
	int l2_protocol, l2_active_protocol;
	int l3_protocol;
	int data_open;
	struct l4_process l4pc;
	setup_parm setup;	/* from isdnif.h numbers and Serviceindicator */
	int Flags;		/* for remembering action done in l4 */
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

struct CallcIf *newCallcIf(struct IsdnCardState *cs, char *id, int protocol);
void delCallcIf(struct CallcIf *c_if);
void callcIfRun(struct CallcIf *c_if);
void callcIfStop(struct CallcIf *c_if);
void callcIfPutStatus(struct CallcIf *c_if, char *msg);

#endif
