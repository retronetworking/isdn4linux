/* $Id$
 *
 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log$
 * Revision 1.2  1996/10/27 22:15:16  keil
 * bugfix reject handling
 *
 * Revision 1.1  1996/10/13 20:04:55  keil
 * Initial revision
 *
 *
 *
 */
 
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	*ptr++ = 0x1; \
	*ptr++ = cref; \
	*ptr++ = mty  

static void
l3_message(struct PStack *st, byte mt)
{
	struct BufHeader *dibh;
	byte             *p;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 18);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;
	
	MsgHead(p, st->l3.callref, mt);
	
	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);
}

static void
l3s3(struct PStack *st, byte pr, void *arg)
{
	l3_message(st, MT_RELEASE);
	newl3state(st, 19);
}

static void
l3s4(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3s4_1(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 19);
	l3_message(st, MT_RELEASE);
	st->l3.l3l4(st, CC_RELEASE_CNF, NULL);
}

static void
l3s5(struct PStack *st, byte pr,
     void *arg)
{
	struct BufHeader *dibh;
	byte           *p;
	char           *teln;

	st->l3.callref = st->pa->callref;
	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 19);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_SETUP);

	/*
         * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
         */
	*p++ = 0xa1;
	switch (st->pa->info) {
	  case 1:		/* Telephony                               */
		  *p++ = 0x4;	/* BC-IE-code                              */
		  *p++ = 0x3;	/* Length                                  */
		  *p++ = 0x90;	/* Coding Std. national, 3.1 kHz audio     */
		  *p++ = 0x90;	/* Circuit-Mode 64kbps                     */
		  *p++ = 0xa3;	/* A-Law Audio                             */
		  break;
	  case 5:		/* Datatransmission 64k, BTX               */
	  case 7:		/* Datatransmission 64k                    */
	  default:
		  *p++ = 0x4;	/* BC-IE-code                              */
		  *p++ = 0x2;	/* Length                                  */
		  *p++ = 0x88;	/* Coding Std. nat., unrestr. dig. Inform. */
		  *p++ = 0x90;	/* Packet-Mode 64kbps                      */
		  break;
	}
	/*
	 * What about info2? Mapping to High-Layer-Compatibility?
	 */
	if (st->pa->calling[0] != '\0') {
		*p++ = 0x6c;
		*p++ = strlen(st->pa->calling) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		teln = st->pa->calling;
		while (*teln)
			*p++ = *teln++ & 0x7f;
	}
	*p++ = 0x70;
	*p++ = strlen(st->pa->called) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */

	teln = st->pa->called;
	while (*teln)
		*p++ = *teln++ & 0x7f;


	dibh->datasize = p - DATAPTR(dibh);

	newl3state(st, 1);
	st->l3.l3l2(st, DL_DATA, dibh);

}

static void
l3s6(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.ihsize, ibh->datasize - st->l2.ihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
	} else 
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup answer without bchannel");
	
	BufPoolRelease(ibh);
	newl3state(st, 3);
	st->l3.l3l4(st, CC_PROCEEDING_IND, NULL);
}

static void
l3s7(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 12);
	st->l3.l3l4(st, CC_DISCONNECT_IND, NULL);
}

static void
l3s8(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	st->l3.l3l4(st, CC_SETUP_CNF, NULL);
	newl3state(st, 10);
}

static void
l3s11(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 4);
	st->l3.l3l4(st, CC_ALERTING_IND, NULL);
}

static void
l3s12(struct PStack *st, byte pr, void *arg)
{
	byte           *p;
	int		bcfound = 0;
	char		tmp[80];
	struct BufHeader *ibh = arg;

	p = DATAPTR(ibh);
	p += st->l2.uihsize;
	st->pa->callref = getcallref(p);
	st->l3.callref = 0x80 + st->pa->callref;

	/*
         * Channel Identification
         */
	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x18, 0))) {
		st->pa->bchannel = p[2] & 0x3;
		bcfound++ ;
	} else
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup without bchannel");

	p = DATAPTR(ibh);
	/*
        * Bearer Capabilities
        */
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize, 0x04, 0))) {
		switch (p[2] & 0x1f) {
		  case 0x00:
                	/* Speech */
		  case 0x10:
                        /* 3.1 Khz audio */
			st->pa->info = 1;
			break;
		  case 0x08:
                	/* Unrestricted digital information */
			st->pa->info = 7;
			break;
		  case 0x09:
                 	/* Restricted digital information */
			st->pa->info = 2;
			break;
		  case 0x11:
                 	/* Unrestr. digital information  with tones/announcements */
			st->pa->info = 3;
			break;
		  case 0x18:
                 	/* Video */
			st->pa->info = 4;
			break;
		  default:
			st->pa->info = 0;
		}
	} else
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "setup without bearer capabilities");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x70, 0)))
		iecpy(st->pa->called, p, 1);
	else
		strcpy(st->pa->called, "");

	p = DATAPTR(ibh);
	if ((p = findie(p + st->l2.uihsize, ibh->datasize - st->l2.uihsize,
			0x6c, 0))) 
		iecpy(st->pa->calling, p, 2);
	else
		strcpy(st->pa->calling, "");
	BufPoolRelease(ibh);

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
l3s13(struct PStack *st, byte pr, void *arg)
{
	newl3state(st, 0);
}

static void
l3s16(struct PStack *st, byte pr,
      void *arg)
{
	st->l3.callref = 0x80 + st->pa->callref;
	l3_message(st, MT_CONNECT);
	newl3state(st, 8);
}

static void
l3s17(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	st->l3.l3l4(st, CC_SETUP_COMPLETE_IND, NULL);
	newl3state(st, 10);
}

static void
l3s18(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte           *p;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 20);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_DISCONNECT);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x90;

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);

	newl3state(st, 11);
}

static void
l3s18_6(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh;
	byte           *p;

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 21);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_RELEASE_COMPLETE);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x95;  /* Call rejected */

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);
	newl3state(st, 0);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3s19(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *ibh = arg;

	BufPoolRelease(ibh);
	newl3state(st, 0);
	l3_message(st, MT_RELEASE_COMPLETE);
	st->l3.l3l4(st, CC_RELEASE_IND, NULL);
}

static void
l3s20(struct PStack *st, byte pr,
      void *arg)
{
	l3_message(st, MT_ALERTING);
	newl3state(st, 7);
}

/* Status enquire answer */
static void
l3s21(struct PStack *st, byte pr, void *arg)
{
	struct BufHeader *dibh = arg;
	byte           *p;

	BufPoolRelease(dibh);

	BufPoolGet(&dibh, st->l1.sbufpool, GFP_ATOMIC, (void *) st, 22);
	p = DATAPTR(dibh);
	p += st->l2.ihsize;

	MsgHead(p, st->l3.callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80;
	*p++ = 0x9E; /* answer status enquire */

	*p++ = 0x14; /* CallState */
	*p++ = 0x1;
	*p++ = st->l3.state & 0x3f;

	dibh->datasize = p - DATAPTR(dibh);
	st->l3.l3l2(st, DL_DATA, dibh);

}

static struct stateentry downstatelist[] =
{
        {SBIT(0),
        	CC_SETUP_REQ,l3s5},
        {SBIT(1)| SBIT(3)| SBIT(4)| SBIT(7)| SBIT(8)| SBIT(10),
        	CC_DISCONNECT_REQ,l3s18},
        {SBIT(1)| SBIT(3)| SBIT(4)| SBIT(6)| SBIT(7)| SBIT(8)| SBIT(10)| 
        	SBIT(11)| SBIT(12),
        	CC_RELEASE_REQ,l3s3},
        {SBIT(1)| SBIT(3)| SBIT(4)| SBIT(6)| SBIT(7)| SBIT(8)| SBIT(10)|
        	SBIT(19),
        	CC_DLRL,l3s13},
        {SBIT(6),
        	CC_DISCONNECT_REQ,l3s18_6},
        {SBIT(6),
        	CC_ALERTING_REQ,l3s20},
        {SBIT(6)| SBIT(7),
        	CC_SETUP_RSP,l3s16},
};

static int      downsllen = sizeof(downstatelist) /
sizeof(struct stateentry);

static struct stateentry datastatelist[] =
{
        {ALL_STATES,
        	MT_STATUS_ENQUIRY,l3s21},
        {SBIT(0)| SBIT(6),
        	MT_SETUP,l3s12},
        {SBIT(1),
        	MT_CALL_PROCEEDING,l3s6},
        {SBIT(1),
        	MT_SETUP_ACKNOWLEDGE,l3s6},
        {SBIT(1)| SBIT(3)| SBIT(4)| SBIT(11)| SBIT(19),
        	MT_RELEASE_COMPLETE,l3s4},
        {SBIT(1)| SBIT(3)| SBIT(4)| SBIT(7)| SBIT(8)| SBIT(10)| SBIT(11),
        	MT_RELEASE,l3s19},
        {SBIT(1)| SBIT(3)| SBIT(4)| SBIT(7)| SBIT(8)| SBIT(10),
        	MT_DISCONNECT,l3s7},
        {SBIT(3)| SBIT(4),
        	MT_CONNECT,l3s8},
        {SBIT(3),
        	MT_ALERTING,l3s11},
        {SBIT(7)| SBIT(8)| SBIT(10),
        	MT_RELEASE_COMPLETE,l3s4_1},
        {SBIT(8),
        	MT_CONNECT_ACKNOWLEDGE,l3s17},
};

static int      datasllen = sizeof(datastatelist) /
sizeof(struct stateentry);

static void
dss1up(struct PStack *st,
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
        		sprintf(tmp, "dss1up unknown data typ %d state %d",
        		 	pr, st->l3.state);
			l3_debug(st, tmp);
		}
		BufPoolRelease(ibh);
		return;
	}
	if (ptr[0] != PROTO_DIS_EURO) {
        	if (st->l3.debug & L3_DEB_PROTERR) {
        		sprintf(tmp, "dss1up%sunexpected discriminator %x message len %d state %d",
        			(pr==DL_DATA)?" ":"(broadcast) ",
        			ptr[0], size, st->l3.state);
			l3_debug(st, tmp);
		}
		BufPoolRelease(ibh);
		return;
	}
	mt = ptr[3];
	for (i = 0; i < datasllen; i++)
		if ((mt == datastatelist[i].primitive) &&
			((1<< st->l3.state) & datastatelist[i].state))
			break;
	if (i == datasllen) {
		BufPoolRelease(ibh);
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"dss1up%sstate %d mt %x unhandled",
        			(pr==DL_DATA)?" ":"(broadcast) ",
        			st->l3.state, mt);
			l3_debug(st, tmp);
		}
		return;
	} else {
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"dss1up%sstate %d mt %x",
        			(pr==DL_DATA)?" ":"(broadcast) ",
        			st->l3.state, mt);
			l3_debug(st, tmp);
		}
		datastatelist[i].rout(st, pr, ibh);
	}
}

static void
dss1down(struct PStack *st,
       int pr, void *arg)
{
	int             i;
	struct BufHeader *ibh = arg;
	char	tmp[80];
        
	for (i = 0; i < downsllen; i++)
		if ((pr == downstatelist[i].primitive)&&
			((1<< st->l3.state) & downstatelist[i].state))
			break;
	if (i == downsllen) {
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"dss1down state %d prim %d unhandled",
        			st->l3.state, pr);
			l3_debug(st, tmp);
		}
	} else {
        	if (st->l3.debug & L3_DEB_STATE) {
        		sprintf(tmp,"dss1down state %d prim %d",
        			st->l3.state, pr);
			l3_debug(st, tmp);
		}
		downstatelist[i].rout(st, pr, ibh);
	}
}

void
setstack_dss1(struct PStack *st)
{
	st->l4.l4l3 = dss1down;
	st->l2.l2l3 = dss1up;
}
