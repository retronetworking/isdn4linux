/* $Id$
 *
 *  German 1TR6 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 * Revision 1.3  1996/10/27 22:15:37  keil
 * bugfix reject handling
 *
 * Revision 1.2  1996/10/13 23:08:56  keil
 * added missing state for callback reject
 *
 * Revision 1.1  1996/10/13 20:04:55  keil
 * Initial revision
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "l3_1tr6.h"
#include "isdnl3.h"

#define MsgHead(ptr, cref, mty, dis) \
	*ptr++ = dis; \
	*ptr++ = 0x1; \
	*ptr++ = cref; \
	*ptr++ = mty

static void
l3_1TR6_message(struct PStack *st, byte mt, byte pd)
{
	struct BufHeader *dibh;
	byte           *p;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 18);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, mt, pd);

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);
}

static void
l3_1tr6_setup(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte           *p;
	char           *teln;

	st->l3.callref = st->pa->callref;
	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 19);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_N1_SETUP, PROTO_DIS_N1);

	if ('S' == (st->pa->called[0] & 0x5f)) {	/* SPV ??? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = st->pa->info; /* 0 for all Services */
		*p++ = st->pa->info2; /* 0 for all Services */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV (default) */
		*p++ = st->pa->info; /* 0 for all Services */
		*p++ = st->pa->info2; /* 0 for all Services */
	}
	if (st->pa->calling[0] != '\0') {
		*p++ = WE0_origAddr;
		*p++ = strlen(st->pa->calling) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		teln = st->pa->calling;
		while (*teln)
			*p++ = *teln++ & 0x7f;
	}
	*p++ = WE0_destAddr;
	teln = st->pa->called;
	if ('S' != (st->pa->called[0] & 0x5f)) {	/* Keine SPV ??? */
		*p++ = strlen(st->pa->called) + 1;
		st->pa->spv = 0;
	} else {		/* SPV */
		*p++ = strlen(st->pa->called);
		teln++;		/* skip S */
		st->pa->spv = 1;
	}
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
	while (*teln)
		*p++ = *teln++ & 0x7f;

	*p++ = WE_Shift_F6;
	/* Codesatz 6 fuer Service */
	*p++ = WE6_serviceInd;
	*p++ = 2;		/* len=2 info,info2 */
	*p++ = st->pa->info;
	*p++ = st->pa->info2;

	dibh->datasize = p - DATAPTR(dibh);

	newl3state(st, 1);
	st->l3.l3l2(st, DL_DATA, dibh);

}

static void
l3_1tr6_tu_setup(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	int		bcfound = 0;
	char		tmp[80];
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	p += st->l2.uihsize;
	st->pa->callref = getcallref(p);
	st->l3.callref = 0x80 + st->pa->callref;

	/* Channel Identification */
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_chanID, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		bcfound++;
	} else
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup without bchannel");

	p = DATAPTR(ibh);

	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize, WE6_serviceInd, 6))) {
		st->pa->info = p[2];
		st->pa->info2 = p[3];
	} else
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup without service indicator");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_destAddr, 0)))
		iecpy(st->pa->called, p, 1);
	else
		strcpy(st->pa->called, "");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_origAddr, 0))) {
		iecpy(st->pa->calling, p, 1);
	} else
		strcpy(st->pa->calling, "");

	p = DATAPTR(ibh);
	st->pa->spv = 0;
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			WE0_netSpecFac, 0))) {
		if ((FAC_SPV == p[3]) || (FAC_Activate == p[3]))
			st->pa->spv = 1;
	}
	BufPoolRelease(ibh);

        /* Signal all services, linklevel takes care of Service-Indicator */
        if (bcfound) {
		if ((st->pa->info != 7) && (st->l3.debug & L3_DEB_WARN)) {
                        sprintf(tmp, "non-digital call: %s -> %s",
                               st->pa->calling,
                               st->pa->called);
			l3_debug(st, tmp);
		}
        	newl3state(st, 6);
        	st->l3.l3l4(st, CC_SETUP_IND, NULL);
        }
}

static void
l3_1tr6_tu_setup_ack(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE0_chanID, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup answer without bchannel");

	BufPoolRelease(ibh);
	newl3state(st, 2);
}

static void
l3_1tr6_tu_call_sent(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE0_chanID, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup answer without bchannel");

	BufPoolRelease(ibh);
	newl3state(st, 3);
	st->l3.l3l4(st, CC_PROCEEDING_IND, NULL);
}

static void
l3_1tr6_tu_alert(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 4);
	st->l3.l3l4(st, CC_ALERTING_IND, NULL);
}

static void
l3_1tr6_tu_info(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	int             i,tmpcharge=0;
	char            a_charge[8], tmp[32];
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
                for (i = 0; i < strlen (a_charge); i++) {
     	                tmpcharge *= 10;
     	                tmpcharge += a_charge[i] & 0xf;
     	        }
                if (tmpcharge > st->pa->chargeinfo) {
     	                st->pa->chargeinfo = tmpcharge;
     	                st->l3.l3l4 (st, CC_INFO_CHARGE, NULL);
     	        }
		if (st->l3.debug & L3_DEB_CHARGE) {
			sprintf(tmp, "charging info %d", st->pa->chargeinfo);
			l3_debug(st, tmp);
		}
	} else if (st->l3.debug & L3_DEB_CHARGE) 
		l3_debug(st, "charging info not found");

	BufPoolRelease(ibh);
}

static void
l3_1tr6_tu_info_s2(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
}

static void
l3_1tr6_tu_connect(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

        st->pa->chargeinfo=0;
	BufPoolRelease(ibh);
	st->l3.l3l4(st, CC_SETUP_CNF, NULL);
	newl3state(st, 10);
}

static void
l3_1tr6_tu_rel(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	l3_1TR6_message(st, MT_N1_REL_ACK, PROTO_DIS_N1);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
	newl3state(st, 0);
}

static void
l3_1tr6_tu_rel_ack(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3_1tr6_tu_disc(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;
	byte           *p;
	int             i,tmpcharge=0;
	char            a_charge[8], tmp[32];

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
                for (i = 0; i < strlen (a_charge); i++) {
     	                tmpcharge *= 10;
     	                tmpcharge += a_charge[i] & 0xf;
     	        }
                if (tmpcharge > st->pa->chargeinfo) {
     	                st->pa->chargeinfo = tmpcharge;
     	                st->l3.l3l4 (st, CC_INFO_CHARGE, NULL);
     	        }
		if (st->l3.debug & L3_DEB_CHARGE) {
			sprintf(tmp, "charging info %d", st->pa->chargeinfo);
			l3_debug(st, tmp);
		}
	} else if (st->l3.debug & L3_DEB_CHARGE) 
		l3_debug(st, "charging info not found");


	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			WE0_cause, 0))) {
		if (p[1] > 0) {
			st->pa->cause = p[2];
		} else {
			st->pa->cause = 0;
		}
	} else if (st->l3.debug & L3_DEB_WARN)
		l3_debug(st, "cause not found");

	BufPoolRelease(ibh);
	newl3state(st, 12);
	st->l3.l3l4(st, CC_DISCONNECT_IND, NULL);
}


static void
l3_1tr6_tu_connect_ack(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	st->pa->chargeinfo = 0;
	st->l3.l3l4(st, CC_SETUP_COMPLETE_IND, NULL);
	newl3state(st, 10);
}

static void
l3_1tr6_alert(struct PStack *st, byte pr,
	      void *arg)
{
	l3_1TR6_message(st, MT_N1_ALERT, PROTO_DIS_N1);
	newl3state(st, 7);
}

static void
l3_1tr6_conn(struct PStack *st, byte pr,
	     void *arg)
{
	struct BufHeader *dibh;
	byte *p;


	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_N1_CONN, PROTO_DIS_N1);

	if (st->pa->spv) {	/* SPV ??? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = st->pa->info;
		*p++ = st->pa->info2;
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV */
		*p++ = st->pa->info;
		*p++ = st->pa->info2;
	}
	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);

	newl3state(st, 8);
}

static void
l3_1tr6_reset(struct PStack *st, byte pr, void *arg)
{
	newl3state(st, 0);
}

static void
l3_1tr6_disconn_req(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte             *p;
        byte             rejflg;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 21);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_N1_DISC, PROTO_DIS_N1);

        if ((st->l3.state & 0xfe) == 6) {
                rejflg = 1;
                *p++ = WE0_cause;       /* Anruf abweisen                */
                *p++ = 0x01;            /* Laenge = 1                    */
                *p++ = CAUSE_CallRejected;
        } else {
                rejflg = 0;
                *p++ = WE0_cause;
                *p++ = 0x0;             /* Laenge = 0 normales Ausloesen */
        }

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);

        newl3state(st, 11);
}

static void
l3_1tr6_rel_req(struct PStack *st, byte pr, void *arg)
{
	l3_1TR6_message(st, MT_N1_REL, PROTO_DIS_N1);
	newl3state(st, 19);
}

static struct stateentry downstl[] =
{
	{SBIT(0),
		CC_SETUP_REQ, l3_1tr6_setup},
	{SBIT(1)| SBIT(2)| SBIT(3)| SBIT(4)| SBIT(6)| SBIT(7)| SBIT(8)|
		SBIT(10),
		CC_DISCONNECT_REQ, l3_1tr6_disconn_req},
	{SBIT(1)| SBIT(2)| SBIT(3)| SBIT(4)| SBIT(6)| SBIT(7)| SBIT(8)|
		SBIT(10)| SBIT(12),
		CC_RELEASE_REQ, l3_1tr6_rel_req},
        {SBIT(1)| SBIT(2)| SBIT(3)| SBIT(4)| SBIT(6)| SBIT(7)| SBIT(8)|
         	SBIT(10)| SBIT(12)| SBIT(19),
        	CC_DLRL, l3_1tr6_reset},
	{SBIT(6),
		CC_REJECT_REQ, l3_1tr6_reset},
	{SBIT(6)|SBIT(7),
		CC_SETUP_RSP, l3_1tr6_conn},
	{SBIT(6),
		CC_ALERTING_REQ, l3_1tr6_alert},
};

static int      downstl_len = sizeof(downstl) /
sizeof(struct stateentry);

static struct stateentry datastln1[] =
{
	{SBIT(0),
		MT_N1_SETUP, l3_1tr6_tu_setup},
	{SBIT(0)| SBIT(1)| SBIT(2)| SBIT(3)| SBIT(4)| SBIT(7)| SBIT(8)|
		SBIT(10)| SBIT(11)| SBIT(12),
		MT_N1_REL, l3_1tr6_tu_rel},
	{SBIT(1),
		MT_N1_SETUP_ACK, l3_1tr6_tu_setup_ack},
	{SBIT(1),
		MT_N1_CALL_SENT, l3_1tr6_tu_call_sent},
	{SBIT(1)| SBIT(2)| SBIT(3)| SBIT(4)| SBIT(7)| SBIT(8)| SBIT(10),
		MT_N1_DISC, l3_1tr6_tu_disc},
	{SBIT(2), MT_N1_CALL_SENT, l3_1tr6_tu_call_sent},
	{SBIT(2)| SBIT(3)| SBIT(4),
		MT_N1_ALERT, l3_1tr6_tu_alert},
	{SBIT(2)| SBIT(3)| SBIT(4),
		MT_N1_CONN, l3_1tr6_tu_connect},
	{SBIT(2),
		MT_N1_INFO, l3_1tr6_tu_info_s2},
	{SBIT(8),
		MT_N1_CONN_ACK, l3_1tr6_tu_connect_ack},
	{SBIT(10), 
		MT_N1_INFO, l3_1tr6_tu_info},
	{SBIT(19),
		MT_N1_REL_ACK, l3_1tr6_tu_rel_ack}
};


static int      datastln1_len = sizeof(datastln1) /
sizeof(struct stateentry);

static void
up1tr6(struct PStack *st,
     int pr, void *arg)
{
	int             i, mt, size;
	byte           *ptr;
	struct BufHeader *ibh = arg;
	char	tmp[80];
        
	if (pr == DL_DATA) {
		ptr = DATAPTR(ibh);
		ptr += st->l2.ihsize;
		size = ibh->datasize - st->l2.ihsize;
	} else if (pr == DL_UNIT_DATA) {
		ptr = DATAPTR(ibh);
		ptr += st->l2.uihsize;
		size = ibh->datasize - st->l2.uihsize;
	} else {
        	if (st->l3.debug & L3_DEB_WARN) {
        		sprintf(tmp, "up1tr6 unknown data typ %d state %d",
        		 	pr, st->l3.state);
			l3_debug(st, tmp);
		}
		BufPoolRelease(ibh);
		return;
	}
	if ((ptr[0] & 0xfe) != PROTO_DIS_N0) {
        	if (st->l3.debug & L3_DEB_PROTERR) {
        		sprintf(tmp, "up1tr6%sunexpected discriminator %x message len %d state %d",
        			(pr==DL_DATA)?" ":"(broadcast) ",
        			ptr[0], size, st->l3.state);
			l3_debug(st, tmp);
		}
		BufPoolRelease(ibh);
		return;
	}
	mt = ptr[3];
	
	if (ptr[0]  == PROTO_DIS_N0) {
		BufPoolRelease(ibh); 
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"up1tr6%s N0 state %d mt %x unhandled",
        			(pr==DL_DATA)?" ":"(broadcast) ",
        			st->l3.state, mt);
			l3_debug(st, tmp);
		}
	} else if (ptr[0]  == PROTO_DIS_N1) {
		for (i = 0; i < datastln1_len; i++)
			if ((mt == datastln1[i].primitive) &&
				((1<< st->l3.state) & datastln1[i].state))
				break;
		if (i == datastln1_len) {
			BufPoolRelease(ibh); 
        		if (st->l3.debug & L3_DEB_STATE) {
        			sprintf(tmp,"up1tr6%sstate %d mt %x unhandled",
        				(pr==DL_DATA)?" ":"(broadcast) ",
        				st->l3.state, mt);
				l3_debug(st, tmp);
			}
			return;
		} else {
        		if (st->l3.debug & L3_DEB_STATE) {
        			sprintf(tmp,"up1tr6%sstate %d mt %x",
        				(pr==DL_DATA)?" ":"(broadcast) ",
        				st->l3.state, mt);
				l3_debug(st, tmp);
			}
			datastln1[i].rout(st, pr, ibh);
		}
	}
}

static void
down1tr6(struct PStack *st,
       int pr, void *arg)
{
	int             i;
	struct BufHeader *ibh = arg;
	char	tmp[80];
        
	for (i = 0; i < downstl_len; i++)
		if ((pr == downstl[i].primitive)&&
			((1<< st->l3.state) & downstl[i].state))
			break;
	if (i == downstl_len) {
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"down1tr6 state %d prim %d unhandled",
        			st->l3.state, pr);
			l3_debug(st, tmp);
		}
	} else {
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"down1tr6 state %d prim %d",
        			st->l3.state, pr);
			l3_debug(st, tmp);
		}
		downstl[i].rout(st, pr, ibh);
	}
}

void
setstack_1tr6(struct PStack *st)
{
	st->l4.l4l3 = down1tr6;
	st->l2.l2l3 = up1tr6;
}
 
