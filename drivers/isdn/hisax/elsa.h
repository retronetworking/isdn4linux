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
 * Revision 1.4  1997/01/21 22:21:05  keil
 * Elsa Quickstep support
 *
 * Revision 1.3  1996/12/08 19:47:38  keil
 * ARCOFI support
 *
 * Revision 1.2  1996/11/18 15:33:35  keil
 * PCC and PCFPro support
 *
 * Revision 1.1  1996/10/13 20:03:45  keil
 * Initial revision
 *
 *
*/

#define CARD_ISAC	0
#define CARD_ITAC	1
#define CARD_HSCX	2
#define CARD_ALE	3
#define CARD_CONTROL	4
#define CARD_CONFIG	5
#define CARD_START_TIMER 6
#define CARD_TRIG_IRQ	7

#define ELSA_PC      1
#define ELSA_PCC8    2
#define ELSA_PCC16   3
#define ELSA_PCF     4
#define ELSA_PCFPRO  5
#define ELSA_PCMCIA  6
#define ELSA_QS1000  7
#define ELSA_QS3000  8

/* ITAC Registeradressen (only Microlink PC) */
#define ITAC_SYS	0x34
#define ITAC_ISEN	0x48
#define ITAC_RFIE	0x4A
#define ITAC_XFIE	0x4C
#define ITAC_SCIE	0x4E
#define ITAC_STIE	0x46

/***                                                                    ***
 ***   Makros als Befehle fuer die Kartenregister                       ***
 ***   (mehrere Befehle werden durch Bit-Oderung kombiniert)            ***
 ***                                                                    ***/

/* Config-Register (Read) */
#define TIMER_RUN       0x02    /* Bit 1 des Config-Reg     */
#define TIMER_RUN_PCC8  0x01    /* Bit 0 des Config-Reg  bei PCC */
#define IRQ_INDEX       0x38    /* Bit 3,4,5 des Config-Reg */
#define IRQ_INDEX_PCC8  0x30    /* Bit 4,5 des Config-Reg */
#define IRQ_INDEX_PC    0x0c    /* Bit 2,3 des Config-Reg */

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
