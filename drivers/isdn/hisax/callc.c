/* $Id$
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log$
 * Revision 1.12  1996/11/26 20:20:03  keil
 * fixed warning while compile
 *
 * Revision 1.11  1996/11/26 18:43:17  keil
 * change ioctl 555 --> 55 (555 didn't work)
 *
 * Revision 1.10  1996/11/26 18:06:07  keil
 * fixed missing break statement,ioctl 555 reset modcount
 *
 * Revision 1.9  1996/11/18 20:23:19  keil
 * log writebuf channel not open changed
 *
 * Revision 1.8  1996/11/06 17:43:17  keil
 * more changes for 2.1.X;block fixed ST_PRO_W
 *
 * Revision 1.7  1996/11/06 15:13:51  keil
 * typo 0x64 --->64 in debug code
 *
 * Revision 1.6  1996/11/05 19:40:33  keil
 * X.75 windowsize
 *
 * Revision 1.5  1996/10/30 10:11:06  keil
 * debugging LOCK changed;ST_REL_W EV_HANGUP added
 *
 * Revision 1.4  1996/10/27 22:20:16  keil
 * alerting bugfixes
 * no static b-channel<->channel mapping
 *
 * Revision 1.2  1996/10/16 21:29:45  keil
 * compile bug as "not module"
 * Callback with euro
 *
 * Revision 1.1  1996/10/13 20:04:50  keil
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"

#ifdef MODULE
extern long mod_use_count_;
#endif MODULE

extern struct IsdnCard cards[];
extern int      nrcards;
extern int      drid;
extern isdn_if  iif;
extern void     HiSax_mod_dec_use_count(void);
extern void     HiSax_mod_inc_use_count(void);

static int      init_ds(int chan, int incoming);
static void     release_ds(int chan);
static char    *strcpyupto(char *dest, char *src, char upto);

static struct Fsm callcfsm =
{NULL, 0, 0},   lcfsm =
{NULL, 0, 0};

struct Channel *chanlist;
static int      chancount = 0;
unsigned int    debugflags = 0;

#define TMR_DCHAN_EST 2000

static void
stat_debug(struct Channel *chanp, char *s)
{
	char            tmp[100], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Channel %d HL->LL %s\n", tm, chanp->chan, s);
	HiSax_putstatus(tmp);
}


enum {
        ST_NULL,           /*  0 inactive                                               */
        ST_OUT,            /*  1 outgoing, awaiting SETUP confirm                       */
        ST_CLEAR,          /*  2 call release, awaiting RELEASE confirm                 */
        ST_OUT_W,          /*  3 outgoing, awaiting d-channel establishment             */
        ST_REL_W,          /*  4 awaiting d-channel release                             */
        ST_IN_W,           /*  5 incoming, awaiting d-channel establishment             */
        ST_IN,             /*  6 incoming call received                                 */
        ST_IN_SETUP,       /*  7 incoming, SETUP response sent                          */
        ST_IN_DACT,        /*  8 incoming connected, no b-channel prot.                 */
        ST_OUT_ESTB,       /* 10 outgoing connected, awaiting b-channel prot. estbl.    */
        ST_ACTIVE,         /* 11 active, b channel prot. established                    */
        ST_BC_HANGUP,      /* 12 call clear. (initiator), awaiting b channel prot. rel. */
        ST_PRO_W,          /* 13 call clear. (initiator), DISCONNECT req. sent          */
        ST_ANT_W,          /* 14 call clear. (receiver), awaiting DISCONNECT ind.       */
        ST_DISC_BC_HANGUP, /* 15 d channel gone, wait for b channel deactivation        */
	ST_OUT_W_HANGUP,   /* 16 Outgoing waiting for D-Channel hangup received         */
        ST_D_ERR,          /* 17 d channel released while active                        */
};

#define STATE_COUNT (ST_D_ERR+1)

static char    *strState[] =
{
        "ST_NULL",
        "ST_OUT",
        "ST_CLEAR",
        "ST_OUT_W",
        "ST_REL_W",
        "ST_IN_W",
        "ST_IN",
        "ST_IN_SETUP",
        "ST_IN_DACT",
        "ST_OUT_ESTB",
        "ST_ACTIVE",
        "ST_BC_HANGUP",
        "ST_PRO_W",
        "ST_ANT_W",
        "ST_DISC_BC_HANGUP",
        "ST_OUT_W_HANGUP",
        "ST_D_ERR",
};

enum {
        EV_DIAL,           /*  0 */
        EV_SETUP_CNF,      /*  1 */
        EV_ACCEPTB,        /*  2 */
        EV_DISCONNECT_CNF, /*  5 */
        EV_DISCONNECT_IND, /*  6 */
        EV_RELEASE_CNF,    /*  7 */
        EV_DLEST,          /*  8 */
        EV_DLRL,           /*  9 */
        EV_SETUP_IND,      /* 10 */
        EV_RELEASE_IND,    /* 11 */
        EV_ACCEPTD,        /* 12 */
        EV_SETUP_CMPL_IND, /* 13 */
        EV_BC_EST,         /* 14 */
        EV_WRITEBUF,       /* 15 */
        EV_DATAIN,         /* 16 */
        EV_HANGUP,         /* 17 */
        EV_BC_REL,         /* 18 */
        EV_CINF,           /* 19 */
        EV_SUSPEND,        /* 20 */
        EV_RESUME,         /* 21 */
        EV_ICALL_TIMER,    /* 22 */
};

#define EVENT_COUNT (EV_ICALL_TIMER+1)

static char    *strEvent[] =
{
        "EV_DIAL",
        "EV_SETUP_CNF",
        "EV_ACCEPTB",
        "EV_DISCONNECT_CNF",
        "EV_DISCONNECT_IND",
        "EV_RELEASE_CNF",
        "EV_DLEST",
        "EV_DLRL",
        "EV_SETUP_IND",
        "EV_RELEASE_IND",
        "EV_ACCEPTD",
        "EV_SETUP_CMPL_IND",
        "EV_BC_EST",
        "EV_WRITEBUF",
        "EV_DATAIN",
        "EV_HANGUP",
        "EV_BC_REL",
        "EV_CINF",
        "EV_SUSPEND",
        "EV_RESUME",
        "EV_ICALL_TIMER",
};

enum {
        ST_LC_NULL,
        ST_LC_ACTIVATE_WAIT,
        ST_LC_DELAY,
        ST_LC_ESTABLISH_WAIT,
        ST_LC_CONNECTED,
        ST_LC_FLUSH_WAIT,
        ST_LC_FLUSH_DELAY,
        ST_LC_RELEASE_WAIT,
};

#define LC_STATE_COUNT (ST_LC_RELEASE_WAIT+1)

static char    *strLcState[] =
{
        "ST_LC_NULL",
        "ST_LC_ACTIVATE_WAIT",
        "ST_LC_DELAY",
        "ST_LC_ESTABLISH_WAIT",
        "ST_LC_CONNECTED",
        "ST_LC_FLUSH_WAIT",
        "ST_LC_FLUSH_DELAY",
        "ST_LC_RELEASE_WAIT",
};

enum {
        EV_LC_ESTABLISH,
        EV_LC_PH_ACTIVATE,
        EV_LC_PH_DEACTIVATE,
        EV_LC_DL_ESTABLISH,
        EV_LC_TIMER,
        EV_LC_DL_FLUSH,
        EV_LC_DL_RELEASE,
        EV_LC_FLUSH,
        EV_LC_RELEASE,
};

#define LC_EVENT_COUNT (EV_LC_RELEASE+1)

static char    *strLcEvent[] =
{
        "EV_LC_ESTABLISH",
        "EV_LC_PH_ACTIVATE",
        "EV_LC_PH_DEACTIVATE",
        "EV_LC_DL_ESTABLISH",
        "EV_LC_TIMER",
        "EV_LC_DL_FLUSH",
        "EV_LC_DL_RELEASE",
        "EV_LC_FLUSH",
        "EV_LC_RELEASE",
};

#define LC_D  0
#define LC_B  1

static int
my_atoi(char *s)
{
        int             i, n;

        n = 0;
        if (!s)
                return -1;
        for (i = 0; *s >= '0' && *s <= '9'; i++, s++)
                n = 10 * n + (*s - '0');
        return n;
}

/*
 * Dial out
 */
static void
r1(struct FsmInst *fi, int event, void *arg)
{
        isdn_ctrl      *ic = arg;
        struct Channel *chanp = fi->userdata;
        char           *ptr;
        char            sis[3];

        /* Destination Phone-Number */
        ptr = strcpyupto(chanp->para.called, ic->num, ',');
        /* Source Phone-Number */
        ptr = strcpyupto(chanp->para.calling, ptr + 1, ',');
        if (!strcmp(chanp->para.calling, "0"))
                chanp->para.calling[0] = '\0';

        /* Service-Indicator 1 */
        ptr = strcpyupto(sis, ptr + 1, ',');
        chanp->para.info = my_atoi(sis);

        /* Service-Indicator 2 */
        ptr = strcpyupto(sis, ptr + 1, '\0');
        chanp->para.info2 = my_atoi(sis);

        chanp->l2_active_protocol = chanp->l2_protocol;
        chanp->incoming = 0;
        chanp->lc_b.l2_start = !0;

        switch (chanp->l2_active_protocol) {
          case (ISDN_PROTO_L2_X75I):
                  chanp->lc_b.l2_establish = !0;
                  break;
          case (ISDN_PROTO_L2_HDLC):
          case (ISDN_PROTO_L2_TRANS):
                  chanp->lc_b.l2_establish = 0;
                  break;
          default:
                  printk(KERN_WARNING "r1 unknown protocol\n");
                  break;
        }

        FsmChangeState(fi, ST_OUT_W);
        FsmEvent(&chanp->lc_d.lcfi, EV_LC_ESTABLISH, NULL);
}

static void
ll_hangup(struct Channel *chanp, int bchantoo)
{
        isdn_ctrl       ic;

        if (bchantoo) {
                if (chanp->debug & 1)
                        stat_debug(chanp, "STAT_BHUP");
                ic.driver = drid;
                ic.command = ISDN_STAT_BHUP;
                ic.arg = chanp->chan;
                iif.statcallb(&ic);
        }
        if (chanp->debug & 1)
                stat_debug(chanp, "STAT_DHUP");
        ic.driver = drid;
        ic.command = ISDN_STAT_DHUP;
        ic.arg = chanp->chan;
        iif.statcallb(&ic);
}

static void
r2(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->is.l4.l4l3(&chanp->is, CC_RELEASE_REQ, NULL);

        FsmChangeState(fi, ST_CLEAR);
        ll_hangup(chanp, 0);
}


static void
r2_1(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_OUT_W_HANGUP);
        chanp->is.l4.l4l3(&chanp->is, CC_REJECT_REQ, NULL);
}


static void
r2_2(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_REL_W);
        FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
        ll_hangup(chanp, 0);
}


static void
r2_3(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_REL_W);
        FsmEvent(&chanp->lc_d.lcfi, EV_LC_FLUSH, NULL);
        ll_hangup(chanp, 0);
}


static void
r2_4(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_OUT_W_HANGUP);
        chanp->is.l4.l4l3(&chanp->is, CC_DISCONNECT_REQ, NULL);
}


static void
r3(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
        FsmChangeState(fi, ST_REL_W);
}


static void
r3_1(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

	chanp->is.l4.l4l3(&chanp->is,CC_DLRL,NULL); 
	
        FsmChangeState(fi, ST_NULL);
        ll_hangup(chanp, 0);
}

static void
r4(struct FsmInst *fi, int event, void *arg)
{
	struct Channel *chanp=fi->userdata;  
  
        chanp->is.l4.l4l3(&chanp->is,CC_DLRL,NULL);                                  
        FsmChangeState(fi, ST_NULL);
}

static void
r5(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->para.callref = chanp->outcallref;

        chanp->outcallref++;
        if (chanp->outcallref == 128)
                chanp->outcallref = 64;

        chanp->is.l4.l4l3(&chanp->is, CC_SETUP_REQ, NULL);

        FsmChangeState(fi, ST_OUT);
}

static void
r6(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_IN_W);
        FsmEvent(&chanp->lc_d.lcfi, EV_LC_ESTABLISH, NULL);
}

static void
r7(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;
        isdn_ctrl       ic;
        int		ret;
        char		txt[32];	       

        /*
         * Report incoming calls only once to linklevel, use CallFlags
         * which is set to 3 with each broadcast message in isdnl1.c
         * and resetted if a interface  answered the STAT_ICALL.
         */
        if ((chanp->sp) &&(chanp->sp->CallFlags==3)) {
                FsmChangeState(fi, ST_IN);
                if (chanp->debug & 1)
                        stat_debug(chanp, "STAT_ICALL");
                ic.driver = drid;
                ic.command = ISDN_STAT_ICALL;
                ic.arg = chanp->chan;
                /*
                 * No need to return "unknown" for calls without OAD,
                 * cause that's handled in linklevel now (replaced by '0')
                 */
                sprintf(ic.num, "%s,%d,0,%s", chanp->para.calling, chanp->para.info,
                        chanp->para.called);
                ret=iif.statcallb(&ic);
                if (chanp->debug & 1) {
                	sprintf(txt,"statcallb ret=%d",ret);
                	stat_debug(chanp, txt);
                }
                if (ret) /* if a interface knows this call, reset the CallFlag
			  * to avoid a second Call report to the linklevel */
                	chanp->sp->CallFlags &= ~(chanp->chan+1); 
                switch(ret) {
                  case 1: /* OK, anybody likes this call */
                  	chanp->is.l4.l4l3(&chanp->is, CC_ALERTING_REQ, NULL);
                	break;
                  case 2: /* Rejecting Call ,nothing to do here */
                  	break;
                  case 0: /* OK, nobody likes this call */
                  default: /* statcallb problems */
                	chanp->is.l4.l4l3(&chanp->is,CC_DLRL,NULL);
                	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
                	FsmChangeState(fi, ST_REL_W);
                	break;
                }
        } else {
                	chanp->is.l4.l4l3(&chanp->is,CC_DLRL,NULL);
                	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
                	FsmChangeState(fi, ST_REL_W);
        }
}

static void
r8(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_IN_SETUP);
        chanp->is.l4.l4l3(&chanp->is, CC_SETUP_RSP, NULL);

}

static void
r9(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_IN_DACT);

        chanp->l2_active_protocol = chanp->l2_protocol;
        chanp->incoming = !0;
        chanp->lc_b.l2_start = 0;

        switch (chanp->l2_active_protocol) {
          case (ISDN_PROTO_L2_X75I):
                  chanp->lc_b.l2_establish = !0;
                  break;
          case (ISDN_PROTO_L2_HDLC):
          case (ISDN_PROTO_L2_TRANS):
                  chanp->lc_b.l2_establish = 0;
                  break;
          default:
                  printk(KERN_WARNING "r9 unknown protocol\n");
                  break;
        }

        init_ds(chanp->chan, !0);

        FsmEvent(&chanp->lc_b.lcfi, EV_LC_ESTABLISH, NULL);
}

static void
r10(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_OUT_ESTB);

        init_ds(chanp->chan, 0);
        FsmEvent(&chanp->lc_b.lcfi, EV_LC_ESTABLISH, NULL);

}

static void
r12(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;
        isdn_ctrl       ic;

        FsmChangeState(fi, ST_ACTIVE);
        chanp->data_open = !0;

        if (chanp->debug & 1)
                stat_debug(chanp, "STAT_DCONN");
        ic.driver = drid;
        ic.command = ISDN_STAT_DCONN;
        ic.arg = chanp->chan;
        iif.statcallb(&ic);

        if (chanp->debug & 1)
                stat_debug(chanp, "STAT_BCONN");
        ic.driver = drid;
        ic.command = ISDN_STAT_BCONN;
        ic.arg = chanp->chan;
        iif.statcallb(&ic);
        
}

static void
r15(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->data_open = 0;
        FsmChangeState(fi, ST_BC_HANGUP);
        FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
}

static void
r16(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        release_ds(chanp->chan);

        FsmChangeState(fi, ST_PRO_W);
        chanp->is.l4.l4l3(&chanp->is, CC_DISCONNECT_REQ, NULL);
}

static void
r17(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->data_open = 0;
        release_ds(chanp->chan);

        FsmChangeState(fi, ST_ANT_W);
}


static void
r17_1(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->data_open = 0;
        release_ds(chanp->chan);

	chanp->is.l4.l4l3(&chanp->is,CC_DLRL,NULL); 
	
	FsmEvent(&chanp->lc_d.lcfi,EV_LC_RELEASE,NULL); 
	
        FsmChangeState(fi, ST_NULL);
        
        ll_hangup(chanp,!0); 
}

static void
r18(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_REL_W);
        FsmEvent(&chanp->lc_d.lcfi, EV_LC_FLUSH, NULL);

        ll_hangup(chanp, !0);
}

static void
r19(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        FsmChangeState(fi, ST_CLEAR);

        chanp->is.l4.l4l3(&chanp->is, CC_RELEASE_REQ, NULL);

        ll_hangup(chanp, !0);
}

static void
r20(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;
        
        chanp->is.l4.l4l3(&chanp->is,CC_DLRL,NULL); 
        
        FsmEvent(&chanp->lc_d.lcfi,EV_LC_RELEASE,NULL); 

        FsmChangeState(fi, ST_NULL);

        ll_hangup(chanp, 0);
}


static void
r21(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->data_open = 0;
        FsmChangeState(fi, ST_DISC_BC_HANGUP);
        FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
}

static void
r22(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        release_ds(chanp->chan);

        FsmChangeState(fi, ST_CLEAR);

        chanp->is.l4.l4l3(&chanp->is, CC_RELEASE_REQ, NULL);

        ll_hangup(chanp, !0);
}

static void
r23(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        release_ds(chanp->chan);

        FsmChangeState(fi, ST_PRO_W);
        chanp->is.l4.l4l3(&chanp->is, CC_DISCONNECT_REQ, NULL);
}

static void
r23_1(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        release_ds(chanp->chan);

	chanp->is.l4.l4l3(&chanp->is, CC_DLRL,NULL); 
	
	FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE,NULL); 
	
        FsmChangeState(fi, ST_NULL);
        
        ll_hangup(chanp,!0); 
}

static void
r24(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        chanp->data_open = 0;
        FsmChangeState(fi, ST_D_ERR);
        FsmEvent(&chanp->lc_b.lcfi, EV_LC_RELEASE, NULL);
}

static void
r25(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

        release_ds(chanp->chan);
        FsmChangeState(fi, ST_NULL);
        ll_hangup(chanp, !0);
}

static void
r26(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;
        isdn_ctrl       ic;

        ic.driver = drid;
        ic.command = ISDN_STAT_CINF;
        ic.arg = chanp->chan;
        sprintf(ic.num, "%d", chanp->para.chargeinfo);
        iif.statcallb(&ic);
}

static void
r27(struct FsmInst *fi, int event, void *arg)
{
        struct Channel *chanp = fi->userdata;

         FsmEvent(&chanp->lc_d.lcfi, EV_LC_RELEASE, NULL);
         FsmChangeState(fi, ST_REL_W);
}

static struct FsmNode fnlist[] =
{
        {ST_NULL,             EV_DIAL,                r1},
        {ST_OUT_W,            EV_DLEST,               r5},
        {ST_OUT_W,            EV_DLRL,                r20},
        {ST_OUT_W,            EV_RELEASE_CNF,         r2_2 },   
        {ST_OUT,              EV_DISCONNECT_IND,      r2},
        {ST_OUT,              EV_SETUP_CNF,           r10},
        {ST_OUT,              EV_HANGUP,              r2_1},
        {ST_OUT,              EV_RELEASE_IND,         r20},
        {ST_OUT,              EV_RELEASE_CNF,         r20},
        {ST_OUT,              EV_DLRL,                r2_2},
        {ST_OUT_W_HANGUP,     EV_RELEASE_CNF,         r2_2},
        {ST_OUT_W_HANGUP,     EV_RELEASE_IND,         r2_3},
        {ST_OUT_W_HANGUP,     EV_DLRL,                r20},
        {ST_CLEAR,            EV_RELEASE_CNF,         r3},
        {ST_CLEAR,            EV_DLRL,                r20},
        {ST_REL_W,            EV_HANGUP,              r4},
        {ST_REL_W,            EV_DLRL,                r4},
        {ST_NULL,             EV_SETUP_IND,           r6},
        {ST_IN_W,             EV_DLEST,               r7},
        {ST_IN_W,             EV_DLRL,                r3_1},
        {ST_IN,               EV_DLRL,                r3_1},
        {ST_IN,               EV_HANGUP,              r2_1},
        {ST_IN,               EV_RELEASE_IND,         r2_3},
        {ST_IN,               EV_RELEASE_CNF,         r2_2},
        {ST_IN,               EV_ACCEPTD,             r8},
        {ST_IN,               EV_ICALL_TIMER,         r27},
        {ST_IN_SETUP,         EV_HANGUP,              r2_4},
        {ST_IN_SETUP,         EV_SETUP_CMPL_IND,      r9},
        {ST_IN_SETUP,         EV_RELEASE_IND,         r2_3},
        {ST_IN_SETUP,         EV_DISCONNECT_IND,      r2},
        {ST_IN_SETUP,         EV_DLRL,                r20},
        {ST_OUT_ESTB,         EV_BC_EST,              r12},
        {ST_OUT_ESTB,         EV_BC_REL,              r23},
        {ST_OUT_ESTB,         EV_DLRL,                r23_1},
        {ST_IN_DACT,          EV_BC_EST,              r12},
        {ST_IN_DACT,          EV_HANGUP,              r15},
        {ST_IN_DACT,          EV_DISCONNECT_IND,      r21}, 
        {ST_IN_DACT,          EV_BC_REL,              r17},
        {ST_IN_DACT,          EV_DLRL,                r17_1},
        {ST_ACTIVE,           EV_HANGUP,              r15},
        {ST_ACTIVE,           EV_BC_REL,              r17},
        {ST_ACTIVE,           EV_DISCONNECT_IND,      r21},
        {ST_ACTIVE,           EV_DLRL,                r24},
        {ST_ACTIVE,           EV_CINF,                r26},
        {ST_ACTIVE,           EV_RELEASE_IND,         r17},
        {ST_BC_HANGUP,        EV_BC_REL,              r16},
        {ST_BC_HANGUP,        EV_DISCONNECT_IND,      r21},
        {ST_PRO_W,            EV_RELEASE_IND,         r18},
        {ST_PRO_W,            EV_HANGUP,              r18},
        {ST_ANT_W,            EV_DISCONNECT_IND,      r19},
        {ST_DISC_BC_HANGUP,   EV_BC_REL,              r22},
        {ST_D_ERR,            EV_BC_REL,              r25},
};

#define FNCOUNT (sizeof(fnlist)/sizeof(struct FsmNode))

static void
lc_r1(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        FsmChangeState(fi, ST_LC_ACTIVATE_WAIT);
        FsmAddTimer(&lf->act_timer, 1000, EV_LC_TIMER, NULL, 50);
        lf->st->ma.manl1(lf->st, PH_ACTIVATE, NULL);

}

static void
lc_r6(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        FsmDelTimer(&lf->act_timer, 50);
        FsmChangeState(fi, ST_LC_DELAY);
        FsmAddTimer(&lf->act_timer, 40, EV_LC_TIMER, NULL, 51);
}

static void
lc_r2(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        if (lf->l2_establish) {
                FsmChangeState(fi, ST_LC_ESTABLISH_WAIT);
                if (lf->l2_start)
                        lf->st->ma.manl2(lf->st, DL_ESTABLISH, NULL);
        } else {
                FsmChangeState(fi, ST_LC_CONNECTED);
                lf->lccall(lf, LC_ESTABLISH, NULL);
        }
}

static void
lc_r3(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        FsmChangeState(fi, ST_LC_CONNECTED);
        lf->lccall(lf, LC_ESTABLISH, NULL);
}

static void
lc_r7(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        FsmChangeState(fi, ST_LC_FLUSH_WAIT);
	lf->st->ma.manl2(lf->st, DL_FLUSH, NULL);
}

static void
lc_r4(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        if (lf->l2_establish) {
                FsmChangeState(fi, ST_LC_RELEASE_WAIT);
                lf->st->ma.manl2(lf->st, DL_RELEASE, NULL);
        } else {
                FsmChangeState(fi, ST_LC_NULL);
                lf->st->ma.manl1(lf->st, PH_DEACTIVATE, NULL);
                lf->lccall(lf, LC_RELEASE, NULL);
        }
}

static void
lc_r4_1(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        FsmChangeState(fi, ST_LC_FLUSH_DELAY);
        FsmAddTimer(&lf->act_timer, 50, EV_LC_TIMER, NULL, 52);
}

static void
lc_r5(struct FsmInst *fi, int event, void *arg)
{
        struct LcFsm   *lf = fi->userdata;

        FsmChangeState(fi, ST_LC_NULL);
        lf->st->ma.manl1(lf->st, PH_DEACTIVATE, NULL);
        lf->lccall(lf, LC_RELEASE, NULL);
}

static struct FsmNode LcFnList[] =
{
        {ST_LC_NULL,                  EV_LC_ESTABLISH,        lc_r1},
        {ST_LC_ACTIVATE_WAIT,         EV_LC_PH_ACTIVATE,      lc_r6},
        {ST_LC_DELAY,                 EV_LC_TIMER,            lc_r2},
        {ST_LC_DELAY,                 EV_LC_DL_ESTABLISH,     lc_r3},
        {ST_LC_ESTABLISH_WAIT,        EV_LC_DL_ESTABLISH,     lc_r3},
        {ST_LC_ESTABLISH_WAIT,        EV_LC_RELEASE,          lc_r5},
        {ST_LC_CONNECTED,             EV_LC_FLUSH,            lc_r7},
        {ST_LC_CONNECTED,             EV_LC_RELEASE,          lc_r4},
        {ST_LC_CONNECTED,             EV_LC_DL_RELEASE,       lc_r5},
        {ST_LC_FLUSH_WAIT,            EV_LC_DL_FLUSH,         lc_r4_1},
        {ST_LC_FLUSH_DELAY,           EV_LC_TIMER,            lc_r4},
        {ST_LC_RELEASE_WAIT,          EV_LC_DL_RELEASE,       lc_r5},
        {ST_LC_ACTIVATE_WAIT,         EV_LC_TIMER,            lc_r5},
        {ST_LC_ESTABLISH_WAIT,        EV_LC_DL_RELEASE,       lc_r5},
};

#define LC_FN_COUNT (sizeof(LcFnList)/sizeof(struct FsmNode))

void
CallcNew(void)
{
        callcfsm.state_count = STATE_COUNT;
        callcfsm.event_count = EVENT_COUNT;
        callcfsm.strEvent = strEvent;
        callcfsm.strState = strState;
        FsmNew(&callcfsm, fnlist, FNCOUNT);

        lcfsm.state_count = LC_STATE_COUNT;
        lcfsm.event_count = LC_EVENT_COUNT;
        lcfsm.strEvent = strLcEvent;
        lcfsm.strState = strLcState;
        FsmNew(&lcfsm, LcFnList, LC_FN_COUNT);
}

void
CallcFree(void)
{
        FsmFree(&lcfsm);
        FsmFree(&callcfsm);
}

static void
release_ds(int chan)
{
        struct PStack  *st = &chanlist[chan].ds;
        struct IsdnCardState *sp;
        struct HscxState *hsp;

        sp = st->l1.hardware;
        hsp = sp->hs + chanlist[chan].hscx;

        close_hscxstate(hsp);

        switch (chanlist[chan].l2_active_protocol) {
          case (ISDN_PROTO_L2_X75I):
                  releasestack_isdnl2(st);
                  break;
          case (ISDN_PROTO_L2_HDLC):
          case (ISDN_PROTO_L2_TRANS):
                  releasestack_transl2(st);
                  break;
        }
}

static void
cc_l1man(struct PStack *st, int pr, void *arg)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;

        switch (pr) {
          case (PH_ACTIVATE):
                  FsmEvent(&chanp->lc_d.lcfi, EV_LC_PH_ACTIVATE, NULL);
                  break;
          case (PH_DEACTIVATE):
                  FsmEvent(&chanp->lc_d.lcfi, EV_LC_PH_DEACTIVATE, NULL);
                  break;
        }
}

static void
cc_l2man(struct PStack *st, int pr, void *arg)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;

        switch (pr) {
          case (DL_ESTABLISH):
                  FsmEvent(&chanp->lc_d.lcfi, EV_LC_DL_ESTABLISH, NULL);
                  break;
          case (DL_RELEASE):
                  FsmEvent(&chanp->lc_d.lcfi, EV_LC_DL_RELEASE, NULL);
                  break;
          case (DL_FLUSH):
                  FsmEvent(&chanp->lc_d.lcfi, EV_LC_DL_FLUSH, NULL);
                  break;
        }
}

static void
dcc_l1man(struct PStack *st, int pr, void *arg)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;

        switch (pr) {
          case (PH_ACTIVATE):
                  FsmEvent(&chanp->lc_b.lcfi, EV_LC_PH_ACTIVATE, NULL);
                  break;
          case (PH_DEACTIVATE):
                  FsmEvent(&chanp->lc_b.lcfi, EV_LC_PH_DEACTIVATE, NULL);
                  break;
        }
}

static void
dcc_l2man(struct PStack *st, int pr, void *arg)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;

        switch (pr) {
          case (DL_ESTABLISH):
                  FsmEvent(&chanp->lc_b.lcfi, EV_LC_DL_ESTABLISH, NULL);
                  break;
          case (DL_RELEASE):
                  FsmEvent(&chanp->lc_b.lcfi, EV_LC_DL_RELEASE, NULL);
                  break;
        }
}

static void
ll_handler(struct PStack *st, int pr,
           struct BufHeader *ibh)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;

        switch (pr) {
          case (CC_DISCONNECT_IND):
                  FsmEvent(&chanp->fi, EV_DISCONNECT_IND, NULL);
                  break;
          case (CC_RELEASE_CNF):
                  FsmEvent(&chanp->fi, EV_RELEASE_CNF, NULL);
                  break;
          case (CC_SETUP_IND):
                  FsmEvent(&chanp->fi, EV_SETUP_IND, NULL);
                  break;
          case (CC_RELEASE_IND):
                  FsmEvent(&chanp->fi, EV_RELEASE_IND, NULL);
                  break;
          case (CC_SETUP_COMPLETE_IND):
                  FsmEvent(&chanp->fi, EV_SETUP_CMPL_IND, NULL);
                  break;
          case (CC_SETUP_CNF):
                  FsmEvent(&chanp->fi, EV_SETUP_CNF, NULL);
                  break;
          case (CC_INFO_CHARGE):
                  FsmEvent(&chanp->fi, EV_CINF, NULL);
                  break;
        }
}

static void
init_is(int chan, unsigned int ces)
{
        struct PStack  *st = &(chanlist[chan].is);
        struct IsdnCardState *sp = chanlist[chan].sp;
        char            tmp[128];

        setstack_HiSax(st, sp);

        st->l2.sap = 0;

        st->l2.tei = 255;

        st->l2.ces = ces;
        st->l2.extended = !0;
        st->l2.laptype = LAPD;
        st->l2.window = 1;
        st->l2.orig = !0;
        st->l2.t200 = 1000;               /* 1000 milliseconds  */
        if (st->protocol == ISDN_PTYPE_1TR6) {
                st->l2.n200 = 3;          /* try 3 times        */
                st->l2.t203 = 10000;      /* 10000 milliseconds */
        } else {
                st->l2.n200 = 4;          /* try 4 times        */
                st->l2.t203 = 5000;       /* 5000 milliseconds  */
        }

        sprintf(tmp, "Channel %d q.921", chan);
        setstack_isdnl2(st, tmp);
        setstack_isdnl3(st, chan);
        st->l2.debug = 0xff;
        st->l4.userdata = chanlist + chan;
        st->l4.l2writewakeup = NULL;

        st->l3.l3l4 = ll_handler;
        st->l1.l1man = cc_l1man;
        st->l2.l2man = cc_l2man;

        st->pa = &chanlist[chan].para;
        HiSax_addlist(sp, st);
}

static void
callc_debug(struct FsmInst *fi, char *s)
{
        char            str[80], tm[32];
        struct Channel *chanp = fi->userdata;

        jiftime(tm, jiffies);
        sprintf(str, "%s Channel %d callc %s\n", tm, chanp->chan, s);
        HiSax_putstatus(str);
}

static void
lc_debug(struct FsmInst *fi, char *s)
{
        char            str[256], tm[32];
        struct LcFsm   *lf = fi->userdata;

        jiftime(tm, jiffies);
        sprintf(str, "%s Channel %d lc %s\n", tm, lf->ch->chan, s);
        HiSax_putstatus(str);
}

static void
dlc_debug(struct FsmInst *fi, char *s)
{
        char            str[256], tm[32];
        struct LcFsm   *lf = fi->userdata;

        jiftime(tm, jiffies);
        sprintf(str, "%s Channel %d dlc %s\n", tm, lf->ch->chan, s);
        HiSax_putstatus(str);
}

static void
lccall_d(struct LcFsm *lf, int pr, void *arg)
{
        struct Channel *chanp = lf->ch;

        switch (pr) {
          case (LC_ESTABLISH):
                  FsmEvent(&chanp->fi, EV_DLEST, NULL);
                  break;
          case (LC_RELEASE):
                  FsmEvent(&chanp->fi, EV_DLRL, NULL);
                  break;
        }
}

static void
lccall_b(struct LcFsm *lf, int pr, void *arg)
{
        struct Channel *chanp = lf->ch;

        switch (pr) {
          case (LC_ESTABLISH):
                  FsmEvent(&chanp->fi, EV_BC_EST, NULL);
                  break;
          case (LC_RELEASE):
                  FsmEvent(&chanp->fi, EV_BC_REL, NULL);
                  break;
        }
}

static void
init_chan(int chan, int cardnr, int hscx,
          unsigned int ces)
{
        struct IsdnCard *card = cards + cardnr;
        struct Channel *chanp = chanlist + chan;

        chanp->sp = card->sp;
        chanp->hscx = hscx;
        chanp->chan = chan;
        chanp->incoming = 0;
        chanp->debug = 0;
	
        init_is(chan, ces);

        chanp->fi.fsm = &callcfsm;
        chanp->fi.state = ST_NULL;
        chanp->fi.debug = 0;
        chanp->fi.userdata = chanp;
        chanp->fi.printdebug = callc_debug;

        chanp->lc_d.lcfi.fsm = &lcfsm;
        chanp->lc_d.lcfi.state = ST_LC_NULL;
        chanp->lc_d.lcfi.debug = 0;
        chanp->lc_d.lcfi.userdata = &chanp->lc_d;
        chanp->lc_d.lcfi.printdebug = lc_debug;
        chanp->lc_d.type = LC_D;
        chanp->lc_d.ch = chanp;
        chanp->lc_d.st = &chanp->is;
        chanp->lc_d.l2_establish = !0;
        chanp->lc_d.l2_start = !0;
        chanp->lc_d.lccall = lccall_d;
        FsmInitTimer(&chanp->lc_d.lcfi, &chanp->lc_d.act_timer);

        chanp->lc_b.lcfi.fsm = &lcfsm;
        chanp->lc_b.lcfi.state = ST_LC_NULL;
        chanp->lc_b.lcfi.debug = 0;
        chanp->lc_b.lcfi.userdata = &chanp->lc_b;
        chanp->lc_b.lcfi.printdebug = dlc_debug;
        chanp->lc_b.type = LC_B;
        chanp->lc_b.ch = chanp;
        chanp->lc_b.st = &chanp->ds;
        chanp->lc_b.l2_establish = !0;
        chanp->lc_b.l2_start = !0;
        chanp->lc_b.lccall = lccall_b;
        FsmInitTimer(&chanp->lc_b.lcfi, &chanp->lc_b.act_timer);

        chanp->outcallref = 64;
        chanp->data_open = 0;
}

int
CallcNewChan(void)
{
        int             i, ces, c;

        chancount = 0;
        for (i = 0; i < nrcards; i++)
                if (cards[i].sp)
                        chancount += 2;

        chanlist = (struct Channel *) Smalloc(sizeof(struct Channel) *
                                      chancount, GFP_KERNEL, "chanlist");

        c = 0;
        ces = randomces();
        for (i = 0; i < nrcards; i++)
                if (cards[i].sp) {
                        init_chan(c++, i, 1, ces++);
                        ces %= 0xffff;
                        init_chan(c++, i, 0, ces++);
                        ces %= 0xffff;
                }
        printk(KERN_INFO "HiSax: %d channels available\n", chancount);
        return (chancount);

}

static void
release_is(int chan)
{
        struct PStack  *st = &chanlist[chan].is;

        releasestack_isdnl2(st);
        HiSax_rmlist(st->l1.hardware, st);
        BufQueueRelease(&st->l2.i_queue);
}

void
CallcFreeChan(void)
{
        int             i;

        for (i = 0; i < chancount; i++)
                release_is(i);
        Sfree((void *) chanlist);
}

static void
lldata_handler(struct PStack *st, int pr,
               void *arg)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;
        byte           *ptr;
        int             size;
        struct BufHeader *ibh = arg;

        switch (pr) {
          case (DL_DATA):
                  if (chanp->data_open) {
                          ptr = DATAPTR(ibh);
                          ptr += chanp->ds.l2.ihsize;
                          size = ibh->datasize - chanp->ds.l2.ihsize;
                          iif.rcvcallb(drid, chanp->chan, ptr, size);
                  }
                  BufPoolRelease(ibh);
                  break;
          default:
                  printk(KERN_WARNING "lldata_handler unknown primitive %d\n",
                  	pr);
                  break;
        }
}

static void
lltrans_handler(struct PStack *st, int pr,
                struct BufHeader *ibh)
{
        struct Channel *chanp = (struct Channel *) st->l4.userdata;
        byte           *ptr;

        switch (pr) {
          case (PH_DATA):
                  if (chanp->data_open) {
                          ptr = DATAPTR(ibh);
                          iif.rcvcallb(drid, chanp->chan, ptr, ibh->datasize);
                  }
                  BufPoolRelease(ibh);
                  break;
          default:
                  printk(KERN_WARNING "lltrans_handler unknown primitive %d\n",
                  	pr);
                  break;
        }
}

static void
ll_writewakeup(struct PStack *st)
{
        struct Channel *chanp = st->l4.userdata;
        isdn_ctrl       ic;

        ic.driver = drid;
        ic.command = ISDN_STAT_BSENT;
        ic.arg = chanp->chan;
        iif.statcallb(&ic);
}

static int
init_ds(int chan, int incoming)
{
        struct PStack  *st = &(chanlist[chan].ds);
        struct IsdnCardState *sp = (struct IsdnCardState *)
        				chanlist[chan].is.l1.hardware;
        struct HscxState *hsp = sp->hs + chanlist[chan].hscx;
        char            tmp[128];

        st->l1.hardware = sp;

        hsp->mode = 2;
        hsp->transbufsize = 4000;

        if (setstack_hscx(st, hsp))
                return (-1);

        st->l2.extended = 0;
        st->l2.laptype = LAPB;
        st->l2.orig = !incoming;
        st->l2.t200 = 1000;        /* 1000 milliseconds */
        st->l2.window = 7;
        st->l2.n200 = 4;           /* try 4 times       */
        st->l2.t203 = 5000;        /* 5000 milliseconds */

        st->l2.debug = 0xff;
        st->l3.debug = 0xff;
        switch (chanlist[chan].l2_active_protocol) {
          case (ISDN_PROTO_L2_X75I):
                  sprintf(tmp, "Channel %d x.75", chan);
                  setstack_isdnl2(st, tmp);
                  st->l2.l2l3 = lldata_handler;
                  st->l1.l1man = dcc_l1man;
                  st->l2.l2man = dcc_l2man;
                  st->l4.userdata = chanlist + chan;
                  st->l4.l1writewakeup = NULL;
                  st->l4.l2writewakeup = ll_writewakeup;
                  st->l2.l2m.debug = debugflags & 16;
                  st->ma.manl2(st, MDL_NOTEIPROC, NULL);
                  st->l1.hscxmode = 2;        /* Packet-Mode ? */
                  st->l1.hscxchannel = chanlist[chan].para.bchannel - 1;
                  break;
          case (ISDN_PROTO_L2_HDLC):
                  st->l1.l1l2 = lltrans_handler;
                  st->l1.l1man = dcc_l1man;
                  st->l4.userdata = chanlist + chan;
                  st->l4.l1writewakeup = ll_writewakeup;
                  st->l1.hscxmode = 2;
                  st->l1.hscxchannel = chanlist[chan].para.bchannel - 1;
                  break;
          case (ISDN_PROTO_L2_TRANS):
                  st->l1.l1l2 = lltrans_handler;
                  st->l1.l1man = dcc_l1man;
                  st->l4.userdata = chanlist + chan;
                  st->l4.l1writewakeup = ll_writewakeup;
                  st->l1.hscxmode = 1;
                  st->l1.hscxchannel = chanlist[chan].para.bchannel - 1;
                  break;
        }

        return (0);

}

static void
channel_report(int i)
{
}

static void
command_debug(struct Channel *chanp, char *s)
{
        char            tmp[64], tm[32];

        jiftime(tm, jiffies);
        sprintf(tmp, "%s Channel %d LL->HL %s\n", tm, chanp->chan, s);
        HiSax_putstatus(tmp);
}

static void
distr_debug(void)
{
        int             i;

        for (i = 0; i < chancount; i++) {  
                chanlist[i].debug = debugflags & 1;
                chanlist[i].fi.debug = debugflags & 2;
                chanlist[i].is.l2.l2m.debug = debugflags & 8;
                chanlist[i].ds.l2.l2m.debug = debugflags & 16;
                chanlist[i].lc_d.lcfi.debug = debugflags & 128;
                chanlist[i].lc_b.lcfi.debug = debugflags & 256;
        }
        for (i = 0; i < nrcards; i++)
                if (cards[i].sp) {
                        cards[i].sp->dlogflag = debugflags & 4;
                        cards[i].sp->teistack->l2.l2m.debug = debugflags & 512;
                }
}

int
HiSax_command(isdn_ctrl * ic)
{
        struct Channel *chanp;
        char            tmp[64];
        int             i;
        unsigned int    num;

        switch (ic->command) {
          case (ISDN_CMD_SETEAZ):
                  chanp = chanlist + ic->arg;
                  if (chanp->debug & 1)
                          command_debug(chanp, "SETEAZ");
                  return (0);
          case (ISDN_CMD_DIAL):
                  chanp = chanlist + (ic->arg & 0xff);
                  if (chanp->debug & 1) {
                          sprintf(tmp, "DIAL %s", ic->num);
                          command_debug(chanp, tmp);
                  }
                  FsmEvent(&chanp->fi, EV_DIAL, ic);
                  return (0);
          case (ISDN_CMD_ACCEPTB):
                  chanp = chanlist + ic->arg;
                  if (chanp->debug & 1)
                          command_debug(chanp, "ACCEPTB");
                  FsmEvent(&chanp->fi, EV_ACCEPTB, NULL);
                  break;
          case (ISDN_CMD_ACCEPTD):
                  chanp = chanlist + ic->arg;
                  if (chanp->debug & 1)
                          command_debug(chanp, "ACCEPTD");
                  FsmEvent(&chanp->fi, EV_ACCEPTD, NULL);
                  break;
          case (ISDN_CMD_HANGUP):
                  chanp = chanlist + ic->arg;
                  if (chanp->debug & 1)
                          command_debug(chanp, "HANGUP");
                  FsmEvent(&chanp->fi, EV_HANGUP, NULL);
                  break;
          case (ISDN_CMD_SUSPEND):
                  chanp = chanlist + ic->arg;
                  if (chanp->debug & 1) {
                          sprintf(tmp, "SUSPEND %s", ic->num);
                          command_debug(chanp, tmp);
                  }
                  FsmEvent(&chanp->fi, EV_SUSPEND, ic);
                  break;
          case (ISDN_CMD_RESUME):
                  chanp = chanlist + ic->arg;
                  if (chanp->debug & 1) {
                          sprintf(tmp, "RESUME %s", ic->num);
                          command_debug(chanp, tmp);
                  }
                  FsmEvent(&chanp->fi, EV_RESUME, ic);
                  break;
          case (ISDN_CMD_LOCK):
                  HiSax_mod_inc_use_count();
#ifdef MODULE
		  if (debugflags & 64) {
		  	jiftime(tmp, jiffies);
		  	i=strlen(tmp);
                  	sprintf(tmp+i, "   LOCK modcnt %lx\n",mod_use_count_);
		  	HiSax_putstatus(tmp);
		  }
#endif MODULE
                  break;
          case (ISDN_CMD_UNLOCK):
                  HiSax_mod_dec_use_count();
#ifdef MODULE
		  if (debugflags & 64) {
		  	jiftime(tmp, jiffies);
		  	i=strlen(tmp);
                  	sprintf(tmp+i, " UNLOCK modcnt %lx\n",mod_use_count_);
		  	HiSax_putstatus(tmp);
		  }
#endif MODULE
                  break;
          case (ISDN_CMD_IOCTL):
                  switch (ic->arg) {
                    case (0):
                            for (i = 0; i < nrcards; i++)
                                    if (cards[i].sp)
                                            HiSax_reportcard(i);
                            for (i = 0; i < chancount; i++)
                                    channel_report(i);
                            break;
                    case (1):
                            debugflags = *(unsigned int *) ic->num;
                            distr_debug();
                            sprintf(tmp, "debugging flags set to %x\n", debugflags);
                            HiSax_putstatus(tmp);
			    printk(KERN_DEBUG "HiSax: %s", tmp);
                            break;
                    case (2):
                            num = *(unsigned int *) ic->num;
                            i = num >> 8;
                            if (i >= chancount)
                                    break;
                            chanp = chanlist + i;
                            chanp->impair = num & 0xff;
                            if (chanp->debug & 1) {
                                    sprintf(tmp, "IMPAIR %x", chanp->impair);
                                    command_debug(chanp, tmp);
                            }
                            break;
                    case (3):
                    	    for (i = 0; i < *(unsigned int *)ic->num; i++)
                 	    	HiSax_mod_dec_use_count();
                 	    break;
                    case (4):
                    	    for (i = 0; i < *(unsigned int *)ic->num; i++)
                 	    	HiSax_mod_inc_use_count();
                 	    break;
#ifdef MODULE
                    case (55):
                  	    mod_use_count_ &= 0xF0000000;
                  	    HiSax_mod_inc_use_count();
                  	    break;
#endif MODULE
                    case (11):
                    	    num=0;
        		    for (i = 0; i < nrcards; i++)
                	    	if (cards[i].sp) {
                        		cards[i].sp->debug = *(unsigned int *) ic->num;
                        		num++;
                       	    	}
                       	    if (num) {
                            	sprintf(tmp, "l1 debugging flags set to %x\n", 
                            		*(unsigned int *) ic->num);
                                HiSax_putstatus(tmp);
			        printk(KERN_DEBUG "HiSax: %s", tmp);
                       	    }
                            break;
                    case (12):
                    	    num=0;
        		    for (i = 0; i < chancount; i++) {  
                		chanlist[i].is.l3.debug = *(unsigned int *)ic->num;
                		chanlist[i].ds.l3.debug = *(unsigned int *)ic->num;
                		num++;
        		    }
                       	    if (num) {
                            	sprintf(tmp, "l3 debugging flags set to %x\n", 
                            		*(unsigned int *) ic->num);
                                HiSax_putstatus(tmp);
			        printk(KERN_DEBUG "HiSax: %s", tmp);
                       	    }
                       	    break;
                    default:  
                    	    printk(KERN_DEBUG "HiSax: invalid ioclt %d\n",
                    	    	(int)ic->arg);
                    	    return (-EINVAL);
                  }
                  break;
          case (ISDN_CMD_SETL2):
                  chanp = chanlist + (ic->arg & 0xff);
                  if (chanp->debug & 1) {
                          sprintf(tmp, "SETL2 %ld", ic->arg >> 8);
                          command_debug(chanp, tmp);
                  }
                  chanp->l2_protocol = ic->arg >> 8;
                  break;
          default:
                  break;
        }

        return (0);
}

int
HiSax_writebuf(int id, int chan, const u_char * buf, int count, int user)
{
        struct Channel *chanp = chanlist + chan;
        struct PStack  *st = &chanp->ds;
        struct BufHeader *ibh;
        int             err, i;
        byte           *ptr;

	if (!chanp->data_open) {
		command_debug(chanp, "writebuf: channel not open");
		return -EIO;
	}
	
        err = BufPoolGet(&ibh, st->l1.sbufpool, GFP_ATOMIC, st, 21);
        if (err)
                /* Must return 0 here, since this is not an error
                 * but a temporary lack of resources.
                 */
                return 0;

        ptr = DATAPTR(ibh);
        if (chanp->lc_b.l2_establish)
                i = st->l2.ihsize;
        else
                i = 0;

        if ((count+i) > BUFFER_SIZE(HSCX_SBUF_ORDER, HSCX_SBUF_BPPS)) {
                printk(KERN_WARNING "HiSax_writebuf: packet too large!\n");
                return (-EINVAL);
        }

        ptr += i;

        if (user)
                copy_from_user(ptr, buf, count);
        else
                memcpy(ptr, buf, count);
        ibh->datasize = count + i;

        if (chanp->data_open) {
                if (chanp->lc_b.l2_establish)
                        chanp->ds.l3.l3l2(&chanp->ds, DL_DATA, ibh);
                else
                        chanp->ds.l2.l2l1(&chanp->ds, PH_DATA, ibh);
                return (count);
        } else {
                BufPoolRelease(ibh);
                return (0);
        }

}

static char    *
strcpyupto(char *dest, char *src, char upto)
{
        while (*src && (*src != upto) && (*src != '\0'))
                *dest++ = *src++;
        *dest = '\0';
        return (src);
}
