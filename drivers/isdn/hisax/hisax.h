/* $Id$

 *   Basic declarations, defines and prototypes
 *
 * $Log$
 * Revision 2.5  1997/08/03 14:36:31  keil
 * Implement RESTART procedure
 *
 * Revision 2.4  1997/07/31 19:25:20  keil
 * PTP_DATA_LINK support
 *
 * Revision 2.3  1997/07/31 11:50:17  keil
 * ONE TEI and FIXED TEI handling
 *
 * Revision 2.2  1997/07/30 17:13:02  keil
 * more changes for 'One TEI per card'
 *
 * Revision 2.1  1997/07/27 21:45:13  keil
 * new main structures
 *
 * Revision 2.0  1997/06/26 11:06:27  keil
 * New card and L1 interface.
 * Eicon.Diehl Diva and Dynalink IS64PH support
 *
 * Revision 1.13  1997/04/06 22:54:12  keil
 * Using SKB's
 *
 * old changes removed KKe
 *
 */
#include <linux/module.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/isdnif.h>
#include <linux/tty.h>

#define PH_ACTIVATE	1
#define PH_DATA		2
#define PH_DEACTIVATE	3
#define PH_TEST_LOOP	4
#define MDL_ASSIGN	5
#define DL_UNIT_DATA	6
#define CC_ESTABLISH	7
#define DL_ESTABLISH	8
#define DL_DATA		9

#define CC_CONNECT	15
#define DL_RELEASE	20
#define DL_FLUSH	21

#define CC_REJECT	23

#define CC_SETUP_REQ	24
#define CC_SETUP_CNF	25
#define CC_SETUP_IND	26
#define CC_SETUP_RSP	27
#define CC_SETUP_COMPLETE_IND	28

#define CC_DISCONNECT_REQ	29
#define CC_DISCONNECT_IND	30

#define CC_RELEASE_CNF	31
#define CC_RELEASE_IND	32
#define CC_RELEASE_REQ	33

#define CC_REJECT_REQ	34

#define CC_PROCEEDING_IND	35

#define CC_DLRL		36
#define CC_DLEST	37

#define CC_ALERTING_REQ	38
#define CC_ALERTING_IND	39

#define DL_STOP		40
#define DL_START	41

#define MDL_INFO_SETUP	42
#define MDL_INFO_CONN	43
#define MDL_INFO_REL	44
#define MDL_NOTEIPROC	46

#define LC_ESTABLISH	47
#define LC_RELEASE	48

#define PH_REQUEST_PULL	49
#define PH_PULL_ACK	50
#define	PH_DATA_PULLED	51
#define CC_INFO_CHARGE	52

#define CC_MORE_INFO	53
#define CC_IGNORE	54
#define CC_RESTART	55

#define MDL_REMOVE	56
#define MDL_VERIFY	57
#define MDL_ERROR	58

#define CC_T303		60
#define CC_T304		61
#define CC_T305		62
#define CC_T308_1	64
#define CC_T308_2	65
#define CC_T310		66
#define CC_T313		67
#define CC_T318		68
#define CC_T319		69

#define CC_NOSETUP_RSP_ERR	70
#define CC_SETUP_ERR		71
#define CC_CONNECT_ERR		72
#define CC_RELEASE_ERR		73

#ifdef __KERNEL__

#define MAX_DFRAME_LEN	 260
#define HSCX_BUFMAX	4096
#define MAX_DATA_SIZE	(HSCX_BUFMAX - 4)
#define MAX_DATA_MEM    (HSCX_BUFMAX * 2)
#define MAX_HEADER_LEN	4
#define MAX_WINDOW	8

/*
 * Statemachine
 */
struct Fsm {
	int *jumpmatrix;
	int state_count, event_count;
	char **strEvent, **strState;
};

struct FsmInst {
	struct Fsm *fsm;
	int state;
	int debug;
	void *userdata;
	int userint;
	void (*printdebug) (struct FsmInst *, char *);
};

struct FsmNode {
	int state, event;
	void (*routine) (struct FsmInst *, int, void *);
};

struct FsmTimer {
	struct FsmInst *fi;
	struct timer_list tl;
	int event;
	void *arg;
};

struct L3Timer {
	struct l3_process *pc;
	struct timer_list tl;
	int event;
};

struct Layer1 {
	void *hardware;
	struct BCState *bcs;
	struct PStack **stlistp;
	int act_state;
	void (*l1l2) (struct PStack *, int, void *);
	void (*l1man) (struct PStack *, int, void *);
	void (*l1tei) (struct PStack *, int, void *);
	int mode, bc, requestpull;
};

#define GROUP_TEI	127
#define TEI_SAPI	63
#define CTRL_SAPI	0

/* Layer2 Flags */

#define FLG_LAPB	0x0001
#define FLG_LAPD	0x0002
#define FLG_ORIG	0x0004
#define FLG_MOD128	0x0008
#define FLG_PEND_REL	0x0010
#define FLG_L3_INIT	0x0020 
#define FLG_T200_RUN	0x0040 
#define FLG_ACK_PEND	0x0100
#define FLG_REJEXC	0x0200
#define FLG_OWN_BUSY	0x0400
#define FLG_PEER_BUSY	0x0800

struct Layer2 {
	int tei;
	int tei_wanted;
	int sap;
	unsigned int flag;
	int vs, va, vr;
	int rc;
	int window;
	int sow;
	struct sk_buff *windowar[MAX_WINDOW];
	struct sk_buff_head i_queue;
	struct sk_buff_head ui_queue;
	void (*l2l1) (struct PStack *, int, void *);
	void (*l2man) (struct PStack *, int, void *);
	void (*l2l3) (struct PStack *, int, void *);
	void (*l2tei) (struct PStack *, int, void *);
	struct FsmInst l2m;
	struct FsmTimer t200, t203;
	int T200, N200, T203;
	int debug;
	char debug_id[32];
};

struct Layer3 {
	void (*l3l4) (struct l3_process *, int, void *);
	void (*l3l2) (struct PStack *, int, void *);
	struct l3_process *proc;
	struct l3_process *global;
	int N303;
	int debug;
};

struct LLInterface {
	void (*l4l3) (struct PStack *, int, void *);
	void *userdata;
	void (*l1writewakeup) (struct PStack *);
	void (*l2writewakeup) (struct PStack *);
};


struct Management {
	int	ri;
	struct FsmInst tei_m;
	struct FsmTimer t202;
	int T202, N202, debug;
	void (*layer) (struct PStack *, int, void *);
	void (*manl1) (struct PStack *, int, void *);
	void (*manl2) (struct PStack *, int, void *);
};


struct Param {
	int cause;
	int loc;
	int bchannel;
	setup_parm setup;	/* from isdnif.h numbers and Serviceindicator */
	int chargeinfo;		/* Charge Info - only for 1tr6 in
				 * the moment
				 */
	int spv;		/* SPV Flag */
};


struct PStack {
	struct PStack *next;
	struct Layer1 l1;
	struct Layer2 l2;
	struct Layer3 l3;
	struct LLInterface lli; 
	struct Management ma;
	int protocol;		/* EDSS1 or 1TR6 */
};

struct l3_process {
	int callref;
	int state;
	struct L3Timer timer;
	int N303;
	int debug;
	struct Param para;
	struct Channel *chan;
	struct PStack *st;
	struct l3_process *next;
};

struct hscx_hw {
	int rcvidx;
	int count;              /* Current skb sent count */
	u_char *rcvbuf;         /* B-Channel receive Buffer */
	struct sk_buff *tx_skb; /* B-Channel transmit Buffer */
};

struct hfcB_hw {
	unsigned int *send;
	int f1;
	int f2;
	struct sk_buff *tx_skb; /* B-Channel transmit Buffer */
};

#define BC_FLG_INIT	0x01
#define BC_FLG_ACTIV	0x02
#define BC_FLG_BUSY	0x04

#define L1_MODE_NULL     0
#define L1_MODE_TRANS    1
#define L1_MODE_HDLC     2

struct BCState {
	int channel;
	int mode;
	int Flag;
	struct IsdnCardState *cs;
	int tx_cnt;		/* B-Channel transmit counter */
	struct sk_buff_head rqueue;	/* B-Channel receive Queue */
	struct sk_buff_head squeue;	/* B-Channel send Queue */
	struct PStack *st;
	struct tq_struct tqueue;
	int event;
	int  (*BC_SetStack) (struct PStack *, struct BCState *);
	void (*BC_Close) (struct BCState *);
	union {
		struct hscx_hw hscx;
		struct hfcB_hw  hfc;
	} hw;
};

struct LcFsm {
	int type;
	int delay;
	struct FsmInst lcfi;
	struct Channel *ch;
	void (*lccall) (struct LcFsm *, int, void *);
	struct PStack *st;
	int l2_establish;
	int l2_start;
	struct FsmTimer act_timer;
	char debug_id[32];
};

struct Channel {
	struct PStack *b_st, *d_st;
	struct IsdnCardState *cs;
	struct BCState *bcs;
	int chan;
	int incoming;
	struct FsmInst fi;
	struct LcFsm *lc_d;
	struct LcFsm *lc_b;
	struct FsmTimer drel_timer, dial_timer;
	int debug;
	int l2_protocol, l2_active_protocol;
	int data_open;
	struct l3_process *proc;
	setup_parm setup;	/* from isdnif.h numbers and Serviceindicator */
	int Flags;		/* for remembering action done in l4 */
	int leased;
};

struct elsa_hw {
	unsigned int base;
	unsigned int cfg;
	unsigned int ctrl;
	unsigned int ale;
	unsigned int isac;
	unsigned int itac;
	unsigned int hscx;
	unsigned int trig;
	unsigned int timer;
	unsigned int counter;
	unsigned int status;
	struct timer_list tl;
	u_char ctrl_reg;
};	

struct teles3_hw {
	unsigned int cfg_reg;
	unsigned int isac;
	unsigned int hscx[2];
	unsigned int isacfifo;
	unsigned int hscxfifo[2];
};	

struct teles0_hw {
	unsigned int cfg_reg;
	unsigned int membase;
};	

struct avm_hw {
	unsigned int cfg_reg;
	unsigned int isac;
	unsigned int hscx[2];
	unsigned int isacfifo;
	unsigned int hscxfifo[2];
	unsigned int counter;
};	

struct ix1_hw {
	unsigned int cfg_reg;
	unsigned int isac_ale;
	unsigned int isac;
	unsigned int hscx_ale;
	unsigned int hscx;
};

struct diva_hw {
	unsigned int cfg_reg;
	unsigned int ctrl;
	unsigned int isac_adr;
	unsigned int isac;
	unsigned int hscx_adr;
	unsigned int hscx;
	unsigned int status;
	struct timer_list tl;
	u_char ctrl_reg;
};	

struct dyna_hw {
	unsigned int cfg_reg;
	unsigned int adr;
	unsigned int isac;
	unsigned int hscx;
	unsigned int u7;
	unsigned int pots;
};

struct hfc_hw {
	unsigned int addr;
	unsigned int fifosize;
	unsigned char cirm;
	unsigned char ctmt;
	unsigned char cip;
	u_char isac_spcr;
	struct timer_list timer;
};

struct sedl_hw {
	unsigned int cfg_reg;
	unsigned int adr;
	unsigned int isac;
	unsigned int hscx;
	unsigned int res_on;
	unsigned int res_off;
};


#define HW_IOM1	1
#define FLG_TWO_DCHAN 0x10

#define FLG_L1TIMER       0xf00
#define FLG_L1TIMER_ACT   0x100
#define FLG_L1TIMER_DEACT 0x200
#define FLG_L1TIMER_DBUSY 0x400

struct IsdnCardState {
	unsigned char typ;
	unsigned char subtyp;
	int protocol;
	unsigned int irq;
	int HW_Flags; 
	union {
		struct elsa_hw elsa;
		struct teles0_hw teles0;
		struct teles3_hw teles3;
		struct avm_hw avm;
		struct ix1_hw ix1;
		struct diva_hw diva;
		struct dyna_hw dyna;
		struct hfc_hw hfc;
		struct sedl_hw sedl;
	} hw;
	int myid;
	isdn_if iif;
	u_char *status_buf;
	u_char *status_read;
	u_char *status_write;
	u_char *status_end;
	u_char (*readisac) (struct IsdnCardState *, u_char);
	void   (*writeisac) (struct IsdnCardState *, u_char, u_char);
	void   (*readisacfifo) (struct IsdnCardState *, u_char *, int);
	void   (*writeisacfifo) (struct IsdnCardState *, u_char *, int);
	u_char (*BC_Read_Reg) (struct IsdnCardState *, int, u_char);
	void   (*BC_Write_Reg) (struct IsdnCardState *, int, u_char, u_char);
	void   (*BC_Send_Data) (struct BCState *);
	void   (*cardmsg) (struct IsdnCardState *, int, void *);
	struct Channel channel[2];
	struct BCState bcs[2];
	struct PStack *stlist;
	u_char *rcvbuf;
	int rcvidx;
	struct sk_buff *tx_skb;
	int tx_cnt;
	int event;
	struct tq_struct tqueue;
	struct timer_list t3;
	struct timer_list l1timer;
	int ph_active;
	struct sk_buff_head rq, sq; /* D-channel queues */
	int cardnr;
	int ph_state;
	int dlogflag;
	char *dlogspace;
	int debug;
	unsigned int CallFlags;
};

#define  MON0_RX	1
#define  MON1_RX	2
#define  MON0_TX	4
#define  MON1_TX	8

#define  ISDN_CTYPE_16_0	1
#define  ISDN_CTYPE_8_0		2
#define  ISDN_CTYPE_16_3	3
#define  ISDN_CTYPE_PNP		4
#define  ISDN_CTYPE_A1		5
#define  ISDN_CTYPE_ELSA	6
#define  ISDN_CTYPE_ELSA_PNP	7
#define  ISDN_CTYPE_TELESPCMCIA	8
#define  ISDN_CTYPE_IX1MICROR2	9
#define  ISDN_CTYPE_ELSA_PCMCIA	10
#define  ISDN_CTYPE_DIEHLDIVA   11
#define  ISDN_CTYPE_DYNALINK    12
#define  ISDN_CTYPE_TELEINT	13
#define  ISDN_CTYPE_16_3C	14
#define  ISDN_CTYPE_SEDLBAUER	15
#define  ISDN_CTYPE_SPORTSTER	16
#define  ISDN_CTYPE_MIC		17

#define  ISDN_CTYPE_COUNT	17

#ifdef	CONFIG_HISAX_16_0
#define  CARD_TELES0 (1<< ISDN_CTYPE_16_0) | (1<< ISDN_CTYPE_8_0)
#else
#define  CARD_TELES0  0
#endif

#ifdef	CONFIG_HISAX_16_3
#define  CARD_TELES3 (1<< ISDN_CTYPE_16_3) | (1<< ISDN_CTYPE_PNP) | \
		     (1<< ISDN_CTYPE_TELESPCMCIA)
#else
#define  CARD_TELES3  0
#endif

#ifdef	CONFIG_HISAX_AVM_A1
#define  CARD_AVM_A1 (1<< ISDN_CTYPE_A1)
#else
#define  CARD_AVM_A1  0
#endif

#ifdef	CONFIG_HISAX_ELSA
#define  CARD_ELSA (1<< ISDN_CTYPE_ELSA) | (1<< ISDN_CTYPE_ELSA_PNP) | \
		   (1<< ISDN_CTYPE_ELSA_PCMCIA)
#else
#define  CARD_ELSA  0
#endif


#ifdef	CONFIG_HISAX_IX1MICROR2
#define	CARD_IX1MICROR2 (1 << ISDN_CTYPE_IX1MICROR2)
#else
#define CARD_IX1MICROR2 0
#endif

#ifdef  CONFIG_HISAX_DIEHLDIVA
#define CARD_DIEHLDIVA (1 << ISDN_CTYPE_DIEHLDIVA)
#else
#define CARD_DIEHLDIVA 0
#endif

#ifdef  CONFIG_HISAX_DYNALINK
#define CARD_DYNALINK (1 << ISDN_CTYPE_DYNALINK)
#else
#define CARD_DYNALINK 0
#endif

#ifdef  CONFIG_HISAX_TELEINT
#define CARD_TELEINT (1 << ISDN_CTYPE_TELEINT)
#else
#define CARD_TELEINT 0
#endif

#ifdef  CONFIG_HISAX_SEDLBAUER
#define CARD_SEDLBAUER (1 << ISDN_CTYPE_SEDLBAUER)
#else
#define CARD_SEDLBAUER 0
#endif



#define  SUPORTED_CARDS  (CARD_TELES0 | CARD_TELES3 | CARD_AVM_A1 | CARD_ELSA \
			 | CARD_IX1MICROR2 | CARD_DIEHLDIVA | CARD_DYNALINK \
			 | CARD_TELEINT | CARD_SEDLBAUER)

#define TEI_PER_CARD 0

#ifdef CONFIG_HISAX_1TR6
#undef TEI_PER_CARD
#define TEI_PER_CARD 1
#endif

#ifdef CONFIG_HISAX_EURO
#undef TEI_PER_CARD
#define TEI_PER_CARD 1
#endif

#if TEI_PER_CARD
#undef TEI_FIXED
#endif

#undef PTP_DATA_LINK

#ifdef PTP_DATA_LINK
#undef TEI_FIXED
#define TEI_FIXED 0
#define LAYER2_WATCHING
#endif

struct IsdnCard {
	int typ;
	int protocol;		/* EDSS1 or 1TR6 */
	unsigned int para[3];
	struct IsdnCardState *cs;
};

void setstack_isdnl2(struct PStack *st, char *debug_id);
int HiSax_inithardware(void);
void HiSax_closehardware(void);

void setstack_HiSax(struct PStack *st, struct IsdnCardState *sp);
unsigned int random_ri(void);
void setstack_isdnl3(struct PStack *st, struct Channel *chanp);
void HiSax_addlist(struct IsdnCardState *sp, struct PStack *st);
void releasestack_isdnl2(struct PStack *st);
void releasestack_isdnl3(struct PStack *st);
void HiSax_rmlist(struct IsdnCardState *sp, struct PStack *st);

u_char *findie(u_char * p, int size, u_char ie, int wanted_set);
int getcallref(u_char * p);
int newcallref(void);

void FsmNew(struct Fsm *fsm, struct FsmNode *fnlist, int fncount);
void FsmFree(struct Fsm *fsm);
int FsmEvent(struct FsmInst *fi, int event, void *arg);
void FsmChangeState(struct FsmInst *fi, int newstate);
void FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft);
int FsmAddTimer(struct FsmTimer *ft, int millisec, int event,
	void *arg, int where);
void FsmRestartTimer(struct FsmTimer *ft, int millisec, int event,
	void *arg, int where);
void FsmDelTimer(struct FsmTimer *ft, int where);
void jiftime(char *s, long mark);

int HiSax_command(isdn_ctrl * ic);
int HiSax_writebuf_skb(int id, int chan, struct sk_buff *skb);
void HiSax_putstatus(struct IsdnCardState *csta, char *buf);
void HiSax_reportcard(int cardnr);
int QuickHex(char *txt, u_char * p, int cnt);
void LogFrame(struct IsdnCardState *sp, u_char * p, int size);
void dlogframe(struct IsdnCardState *sp, u_char * p, int size, char *comment);
void iecpy(u_char * dest, u_char * iestart, int ieoffset);
void setstack_transl2(struct PStack *st);
void releasestack_transl2(struct PStack *st);
void setstack_tei(struct PStack *st);
void setstack_manager(struct PStack *st);

#endif				/* __KERNEL__ */

#define HZDELAY(jiffs) {int tout = jiffs; while (tout--) udelay(1000000/HZ);}

int ll_run(struct IsdnCardState *csta);
void ll_stop(struct IsdnCardState *csta);
void CallcNew(void);
void CallcFree(void);
int CallcNewChan(struct IsdnCardState *csta);
void CallcFreeChan(struct IsdnCardState *csta);
void Isdnl2New(void);
void Isdnl2Free(void);
void init_tei(struct IsdnCardState *sp, int protocol);
void release_tei(struct IsdnCardState *sp);
char *HiSax_getrev(const char *revision);
void TeiNew(void);
void TeiFree(void);
