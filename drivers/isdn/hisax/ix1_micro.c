/* $Id$

 * ix1_micro.c  low level stuff for ITK ix1-micro Rev.2 isdn cards
 *              derived from the original file teles3.c from Karsten Keil
 *
 * Copyright (C) 1997 Klaus-Peter Nischke (ITK AG) (for the modifications to
 *                                                  the original file teles.c)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 * $Log$
 * Revision 1.3  1997/04/13 19:54:02  keil
 * Change in IRQ check delay for SMP
 *
 * Revision 1.2  1997/04/06 22:54:21  keil
 * Using SKB's
 *
 * Revision 1.1  1997/01/27 15:43:10  keil
 * first version
 *
 *
 */

/*
   For the modification done by the author the following terms and conditions
   apply (GNU PUBLIC LICENSE)


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


   You may contact Klaus-Peter Nischke by email: klaus@nischke.do.eunet.de
   or by conventional mail:

   Klaus-Peter Nischke
   Deusener Str. 287
   44369 Dortmund
   Germany
 */


#define __NO_VERSION__
#include "hisax.h"
#include "ix1_micro.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];
const char *ix1_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

#define SPECIAL_PORT_OFFSET 3

#define ISAC_COMMAND_OFFSET 2
#define ISAC_DATA_OFFSET 0
#define HSCX_COMMAND_OFFSET 2
#define HSCX_DATA_OFFSET 1

#define TIMEOUT 50

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;
	long flags;

	save_flags(flags);
	cli();
	byteout(ale, off);
	ret = bytein(adr);
	restore_flags(flags);
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	/* fifo read without cli because it's allready done  */

	byteout(ale, off);
	insb(adr, data, size);
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(ale, off);
	byteout(adr, data);
	restore_flags(flags);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	/* fifo write without cli because it's allready done  */
	byteout(ale, off);
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.ix1.hscx_ale,
			cs->hw.ix1.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.ix1.hscx_ale,
		 cs->hw.ix1.hscx, offset + (hscx ? 0x40 : 0), value);
}

#define READHSCX(cs, nr, reg) readreg(cs->hw.ix1.hscx_ale, \
		cs->hw.ix1.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.ix1.hscx_ale, \
		cs->hw.ix1.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.ix1.hscx_ale, \
		cs->hw.ix1.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.ix1.hscx_ale, \
		cs->hw.ix1.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
ix1micro_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs;
	u_char val, stat = 0;

	cs = (struct IsdnCardState *) irq2dev_map[intno];

	if (!cs) {
		printk(KERN_WARNING "IX1: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.ix1.hscx_ale, cs->hw.ix1.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.ix1.hscx_ale, cs->hw.ix1.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writereg(cs->hw.ix1.hscx_ale, cs->hw.ix1.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.ix1.hscx_ale, cs->hw.ix1.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.ix1.hscx_ale, cs->hw.ix1.hscx, HSCX_MASK, 0);
		writereg(cs->hw.ix1.hscx_ale, cs->hw.ix1.hscx, HSCX_MASK + 0x40, 0);
	}
	if (stat & 2) {
		writereg(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.ix1.isac_ale, cs->hw.ix1.isac, ISAC_MASK, 0);
	}
}

void
release_io_ix1micro(struct IsdnCard *card)
{
	if (card->sp->hw.ix1.cfg_reg)
		release_region(card->sp->hw.ix1.cfg_reg, 4);
}

static void
ix1_reset(struct IsdnCardState *cs)
{
	long flags;
	int cnt;

	/* reset isac */
	save_flags(flags);
	cnt = 3 * (HZ / 10) + 1;
	sti();
	while (cnt--) {
		byteout(cs->hw.ix1.cfg_reg + SPECIAL_PORT_OFFSET, 1);
		HZDELAY(1);	/* wait >=10 ms */
	}
	byteout(cs->hw.ix1.cfg_reg + SPECIAL_PORT_OFFSET, 0);
	restore_flags(flags);
}

int
initix1micro(struct IsdnCardState *cs)
{
	int ret;
	int loop, counter, cnt = 3;
	char tmp[40];

	counter = kstat.interrupts[cs->irq];
	sprintf(tmp, "IRQ %d count %d", cs->irq, counter);
	debugl1(cs, tmp);
	ret = get_irq(cs->cardnr, &ix1micro_interrupt);
	while (ret && cnt) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		modehscx(cs->hs, 0, 0);
		modehscx(cs->hs + 1, 0, 0);
		loop = 0;
		while (loop++ < 10) {
			/* At least 1-3 irqs must happen
			 * (one from HSCX A, one from HSCX B, 3rd from ISAC)
			 */
			if (kstat.interrupts[cs->irq] > counter)
				break;
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + 1;
			schedule();
		}
		sprintf(tmp, "IRQ %d count %d", cs->irq,
			kstat.interrupts[cs->irq]);
		debugl1(cs, tmp);
		if (kstat.interrupts[cs->irq] == counter) {
			printk(KERN_WARNING
			       "ix1-Micro: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (!(--cnt)) {
				irq2dev_map[cs->irq] = NULL;
				free_irq(cs->irq, NULL);
				return (0);
			} else
				ix1_reset(cs);
		} else
			cnt = 0;
	}
	return (ret);
}

static void
ix1_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
}


int
setup_ix1micro(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->sp;
	char tmp[64];

	strcpy(tmp, ix1_revision);
	printk(KERN_NOTICE "HiSax: ITK IX1 driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_IX1MICROR2)
		return (0);

	/* IO-Ports */
	cs->hw.ix1.isac_ale = card->para[1] + ISAC_COMMAND_OFFSET;
	cs->hw.ix1.hscx_ale = card->para[1] + HSCX_COMMAND_OFFSET;
	cs->hw.ix1.isac = card->para[1] + ISAC_DATA_OFFSET;
	cs->hw.ix1.hscx = card->para[1] + HSCX_DATA_OFFSET;
	cs->hw.ix1.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	if (cs->hw.ix1.cfg_reg) {
		if (check_region((cs->hw.ix1.cfg_reg), 4)) {
			printk(KERN_WARNING
			  "HiSax: %s config port %x-%x already in use\n",
			       CardType[card->typ],
			       cs->hw.ix1.cfg_reg,
			       cs->hw.ix1.cfg_reg + 4);
			return (0);
		} else
			request_region(cs->hw.ix1.cfg_reg, 4, "ix1micro cfg");
	}
	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d io:0x%x\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.ix1.cfg_reg);
	ix1_reset(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->readhscx = &ReadHSCX;
	cs->writehscx = &WriteHSCX;
	cs->hscx_fill_fifo = &hscx_fill_fifo;
	cs->cardmsg = &ix1_card_msg;
	ISACVersion(cs, "Diva:");
	if (HscxVersion(cs, "Diva:")) {
		printk(KERN_WARNING
		    "ix1-Micro: wrong HSCX versions check IO address\n");
		release_io_ix1micro(card);
		return (0);
	}
	return (1);
}
