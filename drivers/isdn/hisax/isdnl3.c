/* $Id$
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log$
 * Revision 1.1  1996/10/13 20:04:54  keil
 * Initial revision
 *
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl3.h"
#include <linux/config.h>

void
l3_debug(struct PStack *st, char *s)
{
        char            str[256], tm[32];

        jiftime(tm, jiffies);
        sprintf(str, "%s Channel %d l3 %s\n", tm, st->l3.channr, s);
        HiSax_putstatus(str);
}



void
newl3state(struct PStack *st, int state)
{
	char tmp[80];

	if (st->l3.debug & L3_DEB_STATE) {
		sprintf(tmp,"newstate  %d --> %d",st->l3.state, state);
		l3_debug(st, tmp);
	}
	st->l3.state = state;
}
static void
no_l3_proto(struct PStack *st, int pr, void *arg) {
	struct BufHeader *ibh = arg;
	
	l3_debug(st, "no protocol");
	if (ibh)
		BufPoolRelease(ibh);
}

#ifdef	CONFIG_HISAX_EURO
extern	void setstack_dss1(struct PStack *st);
#endif

#ifdef	CONFIG_HISAX_1TR6
extern	void setstack_1tr6(struct PStack *st);
#endif

void
setstack_isdnl3(struct PStack *st, int chan)
{
	char	tmp[64];


	st->l3.debug   = L3_DEB_WARN;
	st->l3.channr  = chan;

#ifdef	CONFIG_HISAX_EURO
	if (st->protocol == ISDN_PTYPE_EURO) {
		setstack_dss1(st);
	} else 
#endif
#ifdef	CONFIG_HISAX_1TR6
	if (st->protocol == ISDN_PTYPE_1TR6) {
		setstack_1tr6(st);
	} else 
#endif
	{
		sprintf(tmp,"protocol %s not supported",
			(st->protocol==ISDN_PTYPE_1TR6)?"1tr6":"euro");
		l3_debug(st,tmp);
		st->l4.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
		st->protocol = -1;
	}
	st->l3.state   = 0;
	st->l3.callref = 0;
}
