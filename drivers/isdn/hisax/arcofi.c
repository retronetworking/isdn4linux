/* $Id$

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *
 * $Log$
 * Revision 1.2  1998/04/15 16:47:16  keil
 * new interface
 *
 * Revision 1.1  1997/10/29 18:51:20  keil
 * New files
 *
 */
 
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl1.h"
#include "isac.h"

int
send_arcofi(struct IsdnCardState *cs, const u_char *msg, int bc, int receive) {
	u_char val;
	char tmp[32];
	long flags;
	int cnt=50;
	
	cs->mon_txp = 0;
	cs->mon_txc = msg[0];
	memcpy(cs->mon_tx, &msg[1], cs->mon_txc);
	switch(bc) {
		case 0: break;
		case 1: cs->mon_tx[1] |= 0x40;
			break;
		default: break;
	}
	cs->mocr &= 0x0f;
	cs->mocr |= 0xa0;
	test_and_clear_bit(HW_MON1_TX_END, &cs->HW_Flags);
	if (receive)
		test_and_clear_bit(HW_MON1_RX_END, &cs->HW_Flags);
	cs->writeisac(cs, ISAC_MOCR, cs->mocr);
	val = cs->readisac(cs, ISAC_MOSR);
	cs->writeisac(cs, ISAC_MOX1, cs->mon_tx[cs->mon_txp++]);
	cs->mocr |= 0x10;
	cs->writeisac(cs, ISAC_MOCR, cs->mocr);
	save_flags(flags);
	sti();
	while (cnt && !test_bit(HW_MON1_TX_END, &cs->HW_Flags)) {
		cnt--;
		udelay(500);
#if 0
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
		schedule();
#endif
	}
	if (receive) {
		while (cnt && !test_bit(HW_MON1_RX_END, &cs->HW_Flags)) {
			cnt--;
			udelay(500);
		}
	}
	restore_flags(flags);
	sprintf(tmp, "arcofi tout %d", cnt);
	debugl1(cs, tmp);
	return(cnt);	
}

