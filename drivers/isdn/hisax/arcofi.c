/* $Id$

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *
 * $Log$
 *
 */
 
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl1.h"
#include "isac.h"

int
send_arcofi(struct IsdnCardState *cs, const u_char *msg) {
	u_char val;
	char tmp[32];
	long flags;
	int cnt=2;
	
	cs->mon_txp = 0;
	cs->mon_txc = msg[0];
	memcpy(cs->mon_tx, &msg[1], cs->mon_txc);
	cs->mocr &= 0x0f;
	cs->mocr |= 0xa0;
	test_and_clear_bit(HW_MON1_TX_END, &cs->HW_Flags);
	cs->writeisac(cs, ISAC_MOCR, cs->mocr);
	val = cs->readisac(cs, ISAC_MOSR);
	cs->writeisac(cs, ISAC_MOX1, cs->mon_tx[cs->mon_txp++]);
	cs->mocr |= 0x10;
	cs->writeisac(cs, ISAC_MOCR, cs->mocr);
	save_flags(flags);
	sti();
	while (cnt && !test_bit(HW_MON1_TX_END, &cs->HW_Flags)) {
		cnt--;
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
		schedule();
	}
	restore_flags(flags);
	sprintf(tmp, "arcofi tout %d", cnt);
	debugl1(cs, tmp);
	return(cnt);	
}
