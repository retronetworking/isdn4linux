/* $Id$

 * teleint.h   Header for Teleint ISDN cards
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * $Log$
 * Revision 1.1  1997/09/11 17:32:33  keil
 * new
 *
 * Revision 1.1  1997/06/26 11:21:41  keil
 * first version
 *
 *
 */
#include <linux/config.h>

extern void release_io_TeleInt(struct IsdnCard *card);
extern int setup_TeleInt(struct IsdnCard *card);
extern int initTeleInt(struct IsdnCardState *cs);
