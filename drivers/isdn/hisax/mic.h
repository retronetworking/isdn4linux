/* $Id$

 * mic.h  Header for mic ISDN cards
 *
 * $Log$
 *
 */

#include <linux/config.h>

#define MIC_ISAC	2
#define MIC_HSCX	1
#define MIC_ADR		7

/* CARD_ADR (Write) */
#define MIC_RESET      0x3	/* same as DOS driver */

extern void release_io_mic(struct IsdnCard *card);
extern int setup_mic(struct IsdnCard *card);
extern int initmic(struct IsdnCardState *cs);
