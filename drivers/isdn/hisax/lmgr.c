/* $Id$

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *  Layermanagement module
 *
 * $Log$
 * Revision 1.1.2.3  1998/03/07 23:15:37  tsbogend
 * made HiSax working on Linux/Alpha
 *
 * Revision 1.1.2.2  1997/11/15 18:54:19  keil
 * cosmetics
 *
 * Revision 1.1.2.1  1997/10/17 22:10:53  keil
 * new files on 2.0
 *
 * Revision 1.1  1997/06/26 11:17:25  keil
 * first version
 *
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
			st->l2.l2tei(st, MDL_ERROR | REQUEST, NULL);
			break;
	}
}

static void
hisax_manager(struct PStack *st, int pr, void *arg)
{
	char tm[32], str[256];
	long Code;

	switch (pr) {
		case (MDL_ERROR | INDICATION):
			Code = (long) arg;
			jiftime(tm, jiffies);
			sprintf(str, "%s manager: MDL_ERROR %c %s\n", tm,
				(char)Code, test_bit(FLG_LAPD, &st->l2.flag) ?
				"D-channel" : "B-channel");
			HiSax_putstatus(st->l1.hardware, str);
			if (test_bit(FLG_LAPD, &st->l2.flag))
				error_handling_dchan(st, Code);
			break;
	}
}

void
setstack_manager(struct PStack *st)
{
	st->ma.layer = hisax_manager;
}
