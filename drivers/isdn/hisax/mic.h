/* $Id$

 * mic.h  Header for mic ISDN cards
 *
 * $Log$
 * Revision 1.1.2.1  1997/10/17 22:10:55  keil
 * new files on 2.0
 *
 *
 */

#define MIC_ISAC	2
#define MIC_HSCX	1
#define MIC_ADR		7

/* CARD_ADR (Write) */
#define MIC_RESET      0x3	/* same as DOS driver */

extern void release_io_mic(struct IsdnCard *card);
extern int setup_mic(struct IsdnCard *card);
extern int initmic(struct IsdnCardState *cs);
