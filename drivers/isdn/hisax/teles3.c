/* $Id$

 * teles3.c     low level stuff for Teles 16.3 & PNP isdn cards
 *
 *              based on the teles driver from Jan den Ouden
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 * $Log$
 * Revision 2.0  1997/06/26 11:02:46  keil
 * New Layer and card interface
 *
 * Revision 1.11  1997/04/13 19:54:05  keil
 * Change in IRQ check delay for SMP
 *
 * Revision 1.10  1997/04/06 22:54:05  keil
 * Using SKB's
 *
 * Revision 1.9  1997/03/22 02:01:07  fritz
 * -Reworked toplevel Makefile. From now on, no different Makefiles
 *  for standalone- and in-kernel-compilation are needed any more.
 * -Added local Rules.make for above reason.
 * -Experimental changes in teles3.c for enhanced IRQ-checking with
 *  2.1.X and SMP kernels.
 * -Removed diffstd-script, same functionality is in stddiff -r.
 * -Enhanced scripts std2kern and stddiff.
 *
 * Revision 1.8  1997/02/23 18:43:55  fritz
 * Added support for Teles-Vision.
 *
 * Revision 1.7  1997/01/28 22:48:33  keil
 * fixes for Teles PCMCIA (Christof Petig)
 *
 * Revision 1.6  1997/01/27 15:52:55  keil
 * SMP proof,cosmetics, PCMCIA added
 *
 * removed old log info /KKe
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "teles3.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];
const char *teles3_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline u_char
readreg(unsigned int adr, u_char off)
{
	return (bytein(adr + off));
}

static inline void
writereg(unsigned int adr, u_char off, u_char data)
{
	byteout(adr + off, data);
}


static inline void
read_fifo(unsigned int adr, u_char * data, int size)
{
	insb(adr, data, size);
}

static void
write_fifo(unsigned int adr, u_char * data, int size)
{
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.teles3.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.teles3.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	read_fifo(cs->hw.teles3.isacfifo, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	write_fifo(cs->hw.teles3.isacfifo, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.teles3.hscx[hscx], offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.teles3.hscx[hscx], offset, value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.teles3.hscx[nr], reg)
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.teles3.hscx[nr], reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo(cs->hw.teles3.hscxfifo[nr], ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo(cs->hw.teles3.hscxfifo[nr], ptr, cnt)

#include "hscx_irq.c"

static void
teles3_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
#define MAXCOUNT 20
	struct IsdnCardState *cs;
	u_char val, stat = 0;
	int count = 0;

	cs = (struct IsdnCardState *) irq2dev_map[intno];

	if (!cs) {
		printk(KERN_WARNING "Teles: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.teles3.hscx[1], HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.teles3.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	count++;
	val = readreg(cs->hw.teles3.hscx[1], HSCX_ISTA);
	if (val && count < MAXCOUNT) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.teles3.isac, ISAC_ISTA);
	if (val && count < MAXCOUNT) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (count >= MAXCOUNT)
		printk(KERN_WARNING "Teles3: more than %d loops in teles3_interrupt\n", count);
	if (stat & 1) {
		writereg(cs->hw.teles3.hscx[0], HSCX_MASK, 0xFF);
		writereg(cs->hw.teles3.hscx[1], HSCX_MASK, 0xFF);
		writereg(cs->hw.teles3.hscx[0], HSCX_MASK, 0x0);
		writereg(cs->hw.teles3.hscx[1], HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.teles3.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.teles3.isac, ISAC_MASK, 0x0);
	}
}

inline static void
release_ioregs(struct IsdnCard *card, int mask)
{
	if (mask & 1)
		release_region(card->cs->hw.teles3.isac + 32, 32);
	if (mask & 2)
		release_region(card->cs->hw.teles3.hscx[0] + 32, 32);
	if (mask & 4)
		release_region(card->cs->hw.teles3.hscx[1] + 32, 32);
}

void
release_io_teles3(struct IsdnCard *card)
{
	if (card->cs->typ == ISDN_CTYPE_TELESPCMCIA)
		release_region(card->cs->hw.teles3.cfg_reg, 97);
	else {
		if (card->cs->hw.teles3.cfg_reg)
			release_region(card->cs->hw.teles3.cfg_reg, 8);
		release_ioregs(card, 0x7);
	}
}

static void
reset_teles(struct IsdnCardState *cs)
{
	long flags;
	u_char irqcfg;

	if (cs->typ != ISDN_CTYPE_TELESPCMCIA) {
		if (cs->hw.teles3.cfg_reg) {
			switch (cs->irq) {
				case 2:
					irqcfg = 0x00;
					break;
				case 3:
					irqcfg = 0x02;
					break;
				case 4:
					irqcfg = 0x04;
					break;
				case 5:
					irqcfg = 0x06;
					break;
				case 10:
					irqcfg = 0x08;
					break;
				case 11:
					irqcfg = 0x0A;
					break;
				case 12:
					irqcfg = 0x0C;
					break;
				case 15:
					irqcfg = 0x0E;
					break;
				default:
					irqcfg = 0x00;
					break;
			}
			save_flags(flags);
			byteout(cs->hw.teles3.cfg_reg + 4, irqcfg);
			sti();
			HZDELAY(HZ / 10 + 1);
			byteout(cs->hw.teles3.cfg_reg + 4, irqcfg | 1);
			HZDELAY(HZ / 10 + 1);
			restore_flags(flags);
		} else {
			/* Reset off for 16.3 PnP , thanks to Georg Acher */
			save_flags(flags);
			byteout(cs->hw.teles3.isac + 0x3c, 1);
			HZDELAY(2);
			restore_flags(flags);
		}
	}
}

int
initteles3(struct IsdnCardState *cs)
{
	int ret;
	int loop, counter, cnt = 3;
	char tmp[40];

	counter = kstat.interrupts[cs->irq];
	sprintf(tmp, "IRQ %d count %d", cs->irq, counter);
	debugl1(cs, tmp);
	ret = get_irq(cs->cardnr, &teles3_interrupt);
	while (ret && cnt) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
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
		sprintf(tmp, "IRQ %d count %d loop %d", cs->irq,
			kstat.interrupts[cs->irq], loop);
		debugl1(cs, tmp);
		if (kstat.interrupts[cs->irq] <= counter) {
			printk(KERN_WARNING
			       "Teles3: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (!(--cnt)) {
				irq2dev_map[cs->irq] = NULL;
				free_irq(cs->irq, NULL);
				return (0);
			} else
				reset_teles(cs);
		} else
			cnt = 0;
	}
	return (ret);
}

static void
Teles_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
}

int
setup_teles3(struct IsdnCard *card)
{
	u_char val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, teles3_revision);
	printk(KERN_NOTICE "HiSax: Teles IO driver Rev. %s\n", HiSax_getrev(tmp));
	if ((cs->typ != ISDN_CTYPE_16_3) && (cs->typ != ISDN_CTYPE_PNP)
	    && (cs->typ != ISDN_CTYPE_TELESPCMCIA))
		return (0);

	if (cs->typ == ISDN_CTYPE_16_3) {
		cs->hw.teles3.cfg_reg = card->para[1];
		switch (cs->hw.teles3.cfg_reg) {
			case 0x180:
			case 0x280:
			case 0x380:
				cs->hw.teles3.cfg_reg |= 0xc00;
				break;
		}
		cs->hw.teles3.isac = cs->hw.teles3.cfg_reg - 0x420;
		cs->hw.teles3.hscx[0] = cs->hw.teles3.cfg_reg - 0xc20;
		cs->hw.teles3.hscx[1] = cs->hw.teles3.cfg_reg - 0x820;
	} else if (cs->typ == ISDN_CTYPE_TELESPCMCIA) {
		cs->hw.teles3.cfg_reg = card->para[1];
		cs->hw.teles3.hscx[0] = card->para[1] - 0x20;
		cs->hw.teles3.hscx[1] = card->para[1];
		cs->hw.teles3.isac = card->para[1] + 0x20;
	} else {		/* PNP */
		cs->hw.teles3.cfg_reg = 0;
		cs->hw.teles3.isac = card->para[1] - 32;
		cs->hw.teles3.hscx[0] = card->para[2] - 32;
		cs->hw.teles3.hscx[1] = card->para[2];
	}
	cs->irq = card->para[0];
	cs->hw.teles3.isacfifo = cs->hw.teles3.isac + 0x3e;
	cs->hw.teles3.hscxfifo[0] = cs->hw.teles3.hscx[0] + 0x3e;
	cs->hw.teles3.hscxfifo[1] = cs->hw.teles3.hscx[1] + 0x3e;
	if (cs->typ == ISDN_CTYPE_TELESPCMCIA) {
		if (check_region((cs->hw.teles3.cfg_reg), 97)) {
			printk(KERN_WARNING
			       "HiSax: %s ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.cfg_reg,
			       cs->hw.teles3.cfg_reg + 96);
			return (0);
		} else
			request_region(cs->hw.teles3.hscx[0], 97, "HiSax Teles PCMCIA");
	} else {
		if (cs->hw.teles3.cfg_reg) {
			if (check_region((cs->hw.teles3.cfg_reg), 8)) {
				printk(KERN_WARNING
				       "HiSax: %s config port %x-%x already in use\n",
				       CardType[card->typ],
				       cs->hw.teles3.cfg_reg,
				       cs->hw.teles3.cfg_reg + 8);
				return (0);
			} else {
				request_region(cs->hw.teles3.cfg_reg, 8, "teles3 cfg");
			}
		}
		if (check_region((cs->hw.teles3.isac + 32), 32)) {
			printk(KERN_WARNING
			   "HiSax: %s isac ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.isac + 32,
			       cs->hw.teles3.isac + 64);
			if (cs->hw.teles3.cfg_reg) {
				release_region(cs->hw.teles3.cfg_reg, 8);
			}
			return (0);
		} else {
			request_region(cs->hw.teles3.isac + 32, 32, "HiSax isac");
		}
		if (check_region((cs->hw.teles3.hscx[0] + 32), 32)) {
			printk(KERN_WARNING
			 "HiSax: %s hscx A ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.hscx[0] + 32,
			       cs->hw.teles3.hscx[0] + 64);
			if (cs->hw.teles3.cfg_reg) {
				release_region(cs->hw.teles3.cfg_reg, 8);
			}
			release_ioregs(card, 1);
			return (0);
		} else {
			request_region(cs->hw.teles3.hscx[0] + 32, 32, "HiSax hscx A");
		}
		if (check_region((cs->hw.teles3.hscx[1] + 32), 32)) {
			printk(KERN_WARNING
			 "HiSax: %s hscx B ports %x-%x already in use\n",
			       CardType[cs->typ],
			       cs->hw.teles3.hscx[1] + 32,
			       cs->hw.teles3.hscx[1] + 64);
			if (cs->hw.teles3.cfg_reg) {
				release_region(cs->hw.teles3.cfg_reg, 8);
			}
			release_ioregs(card, 3);
			return (0);
		} else {
			request_region(cs->hw.teles3.hscx[1] + 32, 32, "HiSax hscx B");
		}
	}
	if (cs->hw.teles3.cfg_reg) {
		if ((val = bytein(cs->hw.teles3.cfg_reg + 0)) != 0x51) {
			printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
			       cs->hw.teles3.cfg_reg + 0, val);
			release_io_teles3(card);
			return (0);
		}
		if ((val = bytein(cs->hw.teles3.cfg_reg + 1)) != 0x93) {
			printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
			       cs->hw.teles3.cfg_reg + 1, val);
			release_io_teles3(card);
			return (0);
		}
		val = bytein(cs->hw.teles3.cfg_reg + 2);	/* 0x1e=without AB
								   * 0x1f=with AB
								   * 0x1c 16.3 ???
								   * 0x46 16.3 with AB + Video (Teles-Vision)
								 */
		if (val != 0x46 && val != 0x1c && val != 0x1e && val != 0x1f) {
			printk(KERN_WARNING "Teles: 16.3 Byte at %x is %x\n",
			       cs->hw.teles3.cfg_reg + 2, val);
			release_io_teles3(card);
			return (0);
		}
	}
	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d isac:%x  cfg:%x\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles3.isac + 32, cs->hw.teles3.cfg_reg);
	printk(KERN_NOTICE
	       "HiSax: hscx A:%x  hscx B:%x\n",
	       cs->hw.teles3.hscx[0] + 32, cs->hw.teles3.hscx[1] + 32);

	reset_teles(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Teles_card_msg;
	ISACVersion(cs, "Teles3:");
	if (HscxVersion(cs, "Teles3:")) {
		printk(KERN_WARNING
		       "Teles3: wrong HSCX versions check IO address\n");
		release_io_teles3(card);
		return (0);
	}
	return (1);
}
