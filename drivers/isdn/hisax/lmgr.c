/* $Id$

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *  Layermanagement module
 *
 * $Log$
 *
 */

#define __NO_VERSION__
#include "hisax.h"

static void
error_handling_dchan(struct PStack *st, int Error)
{
	switch (Error) {
		case 'C':
		case 'D':
		case 'G':
		case 'H':
			st->l2.l2tei(st, MDL_VERIFY, NULL);
			break;
	}
}

static void
hisax_manager(struct PStack *st, int pr, void *arg)
{
	char tm[32], str[256];
	int Code;

	switch (pr) {
		case MDL_ERROR:
			Code = (int) arg;
			jiftime(tm, jiffies);
			sprintf(str, "%s manager: MDL_ERROR %c %s\n", tm,
				Code, (st->l2.flag & FLG_LAPD) ?
				"D-channel" : "B-channel");
			HiSax_putstatus(st->l1.hardware, str);
			if (st->l2.flag & FLG_LAPD)
				error_handling_dchan(st, Code);
			break;
	}
}

void
setstack_manager(struct PStack *st)
{
	st->ma.layer = hisax_manager;
}
