/* $Id$

 */

#ifndef __ISDNL3_H__
#define __ISDNL3_H__

#include "stack.h"

#define SBIT(state) (1<<state)
#define ALL_STATES  0x03ffffff

#define PROTO_DIS_EURO	0x08

#define L3_DEB_WARN	0x01
#define L3_DEB_PROTERR	0x02
#define L3_DEB_STATE	0x04
#define L3_DEB_CHARGE	0x08
#define L3_DEB_CHECK	0x10
#define L3_DEB_SI	0x20

struct stateentry {
	int state;
	int primitive;
	void (*rout) (struct l3_process *, u_char, void *);
};

// =================================================================
//

struct setup_req_parm {
	unsigned char sending_complete[1];
	unsigned char bearer_capability[13];
	unsigned char channel_identification[3]; // BRI
	unsigned char facility[128];
	unsigned char progress_indicator[4];
	unsigned char network_specific_facilities[1]; // na
	unsigned char keypad_facility[34];
	unsigned char calling_party_number[24];
	unsigned char calling_party_subaddress[23];
	unsigned char called_party_number[23];
	unsigned char called_party_subaddress[23];
	unsigned char transit_network_selection[1]; // na
	unsigned char low_layer_compatibility[16];
	unsigned char high_layer_compatibility[4];
	unsigned char user_user[131];
};

struct alerting_req_parm {
	unsigned char facility[128];
	unsigned char progress_indicator[4];
	unsigned char user_user[131];
};

struct l3_process {
	int callref;
	int state;
	void (*l4l3)(struct l3_process *pc, int pr, void *arg);
	struct L3Timer timer;
	int N303;
	int debug;
	struct Param para;
	struct setup_req_parm setup_req;
	struct l4_process *l4pc;
	struct PStack *st;
	struct l3_process *next;
        ulong redir_result;

        /* protocol specific data fields */
        union 
	 { u_char uuuu; /* only when euro not defined, avoiding empty union */
#ifdef CONFIG_HISAX_EURO 
           dss1_proc_priv dss1; /* private dss1 data */
#endif CONFIG_HISAX_EURO            
	 } prot;
};

#define l3_debug(st, fmt, args...) HiSax_putstatus(st->l1.hardware, "l3 ", fmt, ## args)

extern void newl3state(struct l3_process *pc, int state);
extern void L3InitTimer(struct l3_process *pc, struct L3Timer *t);
extern void L3DelTimer(struct L3Timer *t);
extern int L3AddTimer(struct L3Timer *t, int millisec, int event);
extern void StopAllL3Timer(struct l3_process *pc);
extern struct sk_buff *l3_alloc_skb(int len);
extern struct l3_process *new_l3_process(struct PStack *st, int cr);
extern void release_l3_process(struct l3_process *p);
extern struct l3_process *getl3proc(struct PStack *st, int cr);
extern void l3_msg(struct PStack *st, int pr, void *arg);

#endif
