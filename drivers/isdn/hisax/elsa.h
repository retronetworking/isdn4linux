/* $Id$
 *
 * elsa.h   Header for Elsa ISDN cards
 *
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 * 
 * Thanks to    Elsa GmbH for documents and informations
 *
 *
 * $Log$
 *
*/

#define CARD_ISAC	0
#define CARD_HSCX	2
#define CARD_ALE	3
#define CARD_CONTROL	4
#define CARD_CONFIG	5
#define CARD_START_TIMER 6
#define CARD_TRIG_IRQ	7

/***                                                                    ***
 ***   Makros als Befehle fuer die Kartenregister                       ***
 ***   (mehrere Befehle werden durch Bit-Oderung kombiniert)            ***
 ***                                                                    ***/

/* Config-Register (Read) */
#define TIMER_RUN       0x02    /* Bit 1 des Config-Reg     */
#define TOGGLE          0x04    /* Bit 2 Config-Reg toggelt bei Zugriffen */    
#define IRQ_INDEX       0x38    /* Bit 3,4,5 des Config-Reg */

/* Control-Register (Write) */
#define LINE_LED        0x02    /* Bit 1 Gelbe LED */
#define STAT_LED        0x08    /* Bit 3 Gruene LED */
#define ISDN_RESET      0x20    /* Bit 5 Reset-Leitung */
#define ENABLE_TIM_INT  0x80    /* Bit 7 Freigabe Timer Interrupt */

/* ALE-Register (Read) */
#define HW_RELEASE      0x07    /* Bit 0-2 Hardwarerkennung */
#define S0_POWER_BAD    0x08    /* Bit 3 S0-Bus Spannung fehlt */ 

extern	void elsa_report(struct IsdnCardState *sp);
extern  void release_io_elsa(struct IsdnCard *card);
extern	int  setup_elsa(struct IsdnCard *card);
extern  int  initelsa(struct IsdnCardState *sp);
