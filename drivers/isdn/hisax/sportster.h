/* $Id$

 * sportster.h   Header for USR Sportster internal TA ISDN card
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 *
 */

#define	 SPORTSTER_ISAC		0xC000
#define	 SPORTSTER_HSCXA	0x0000
#define	 SPORTSTER_HSCXB	0x4000
#define	 SPORTSTER_RES_IRQ	0x8000
#define	 SPORTSTER_RESET	0x80
#define	 SPORTSTER_INTE		0x40

extern void release_io_sportster(struct IsdnCard *card);
extern int setup_sportster(struct IsdnCard *card);
extern int initsportster(struct IsdnCardState *cs);
