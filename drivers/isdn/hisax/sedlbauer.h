/* $Id$

 * sedlbauer.h  Header for Sedlbauer ISDN cards
 *              derived from the original file dynalink.h from Karsten Keil
 *
 * Copyright (C) 1997 Marcus Niemann (for the modifications to
 *                                    the original file teles.c)
 *
 * Author       Marcus Niemann (niemann@parallel.fh-bielefeld.de)
 *
 * Thanks to    Karsten Keil
 *              Sedlbauer AG for informations
 *              Edgar Toernig
 *
 * $Log$
 *
 */

#include <linux/config.h>

#define SEDL_RES_ON	0
#define SEDL_RES_OFF	1
#define SEDL_ISAC	2
#define SEDL_HSCX	3
#define SEDL_ADR	4

/* CARD_ADR (Write) */
#define SEDL_RESET      0x3	/* same as DOS driver */

extern void release_io_sedlbauer(struct IsdnCard *card);
extern int setup_sedlbauer(struct IsdnCard *card);
extern int initsedlbauer(struct IsdnCardState *cs);
