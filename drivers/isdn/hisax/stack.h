#ifndef __STACK_H__
#define __STACK_H__

#include "fsm.h"

struct IsdnCardState;
struct Channel;

// =================================================================
// PStack

struct Layer1 {
	int mode; 
	struct IsdnCardState *hardware;
	struct BCState *bcs;
	struct PStack **stlistp;
	int Flags;
	struct FsmInst l1m;
	struct FsmTimer	timer;
	void (*l1l2) (struct PStack *, int, void *);
	void (*l1hw) (struct PStack *, int, void *);
	void (*l1tei) (struct PStack *, int, void *);
	int bc;
	int delay;
};

#define MAX_WINDOW	8

struct Layer2 {
	int mode;
	int tei;
	int sap;
	int AddressA;
	int AddressB;
	int maxlen;
	unsigned int flag;
	unsigned int vs, va, vr;
	int rc;
	unsigned int window;
	unsigned int sow;
	struct sk_buff *windowar[MAX_WINDOW];
	struct sk_buff_head i_queue;
	struct sk_buff_head ui_queue;
	void (*l2l1) (struct PStack *, int, void *);
	void (*l3l2) (struct PStack *, int, void *);
	void (*l2tei) (struct PStack *, int, void *);
	struct FsmInst l2m;
	struct FsmTimer t200, t203;
	int T200, N200, T203;
	int debug;
	char debug_id[16];
};

struct Layer3 {
	int mode;
        void (*l3ml3) (struct PStack *, int, void *);
	void (*l2l3) (struct PStack *, int, void *);
	void (*l4l3) (struct PStack *, int, void *);
        int  (*l4l3_proto) (struct PStack *, isdn_ctrl *);
	struct FsmInst l3m;
        struct FsmTimer l3m_timer;
	struct sk_buff_head squeue;
	struct l3_process *proc;
	struct l3_process *global;
	int N303;
	int debug;
	char debug_id[8];
};

struct Management {
	int	ri;
	struct FsmInst tei_m;
	struct FsmTimer t202;
	int T202, N202;
	void (*layer) (struct PStack *, int, void *);
	int debug;
};

struct PStack {
	struct PStack *next;
	struct Layer1 l1;
	struct Layer2 l2;
	struct Layer3 l3;
	struct Layer4 *l4;
	struct Management ma;

        /* protocol specific data fields */
        union
	 { u_char uuuu; /* only as dummy */
#ifdef CONFIG_HISAX_EURO
           dss1_stk_priv dss1; /* private dss1 data */
#endif CONFIG_HISAX_EURO              
	 } prot;
};

// =================================================================
//

#define CHANNEL_D  -1
#define CHANNEL_B1  0
#define CHANNEL_B2  1

#define B1_MODE_HDLC	  0
#define B1_MODE_TRANS	  1
#define B1_MODE_FAX	  4
#define B1_MODE_MODEM	  7
#define B1_MODE_EXTRN	  0x101
#define B1_MODE_NULL	  0x1000

#define B2_MODE_X75SLP    0
#define B2_MODE_TRANS     1
#define B2_MODE_LAPD_X31  4
#define B2_MODE_LAPD      12
#define B2_MODE_NULL      0x1000

#define B3_MODE_TRANS     0
#define B3_MODE_CC        (0x100) // isdnif values are shifted by this value
#define B3_MODE_1TR6      (B3_MODE_CC + ISDN_PTYPE_1TR6)
#define B3_MODE_DSS1      (B3_MODE_CC + ISDN_PTYPE_EURO)
#define B3_MODE_LEASED    (B3_MODE_CC + ISDN_PTYPE_LEASED)
#define B3_MODE_NI1       (B3_MODE_CC + ISDN_PTYPE_NI1)
#define B3_MODE_NULL      (0x1000)

struct StackParams {
	int b1_mode;
	int b2_mode;
	int b3_mode;
	unsigned int headroom;
};

int init_st(struct Layer4 *l4, struct IsdnCardState *cs, struct StackParams *sp, 
	    int bchannel);
void release_st(struct PStack *st);

#endif
