/* $Id$

 * dynalink.h   Header for Dynalink ISDN cards
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * $Log$
 *
 */
#include <linux/config.h>

#define DYNA_ISAC	0
#define DYNA_HSCX	1
#define DYNA_ADR	2
#define DYNA_CTRL_U7	3
#define DYNA_CTRL_POTS	5

/* CARD_ADR (Write) */
#define DYNA_RESET      0x80	/* Bit 7 Reset-Leitung */

extern void release_io_dynalink(struct IsdnCard *card);
extern int setup_dynalink(struct IsdnCard *card);
extern int initdynalink(struct IsdnCardState *cs);
