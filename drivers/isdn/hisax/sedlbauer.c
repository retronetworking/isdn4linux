/* $Id$

 * sedlbauer.c  low level stuff for Sedlbauer cards
 *              derived from the original file dynalink.c from Karsten Keil
 *
 * Copyright (C) 1997 Marcus Niemann (for the modifications to
 *                                    the original file dynalink.c)
 *
 * Author     Marcus Niemann (niemann@parallel.fh-bielefeld.de)
 *
 * Thanks to  Karsten Keil
 *            Sedlbauer AG for informations
 *            Edgar Toernig
 *
 * $Log$
 * Revision 1.1  1997/09/11 17:32:04  keil
 * new
 *
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "sedlbauer.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *Sedlbauer_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

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
	return (readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.sedl.adr, cs->hw.sedl.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.sedl.adr, cs->hw.sedl.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.sedl.adr,
			cs->hw.sedl.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.sedl.adr,
		 cs->hw.sedl.hscx, offset + (hscx ? 0x40 : 0), value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.sedl.adr, \
		cs->hw.sedl.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
sedlbauer_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs;
	u_char val, stat = 0;

	cs = (struct IsdnCardState *) irq2dev_map[intno];

	if (!cs) {
		printk(KERN_WARNING "Sedlbauer: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK, 0x0);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_MASK + 0x40, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_MASK, 0x0);
	}
}

void
release_io_sedlbauer(struct IsdnCard *card)
{
	int bytecnt = 8;

	if (card->cs->hw.sedl.cfg_reg)
		release_region(card->cs->hw.sedl.cfg_reg, bytecnt);
}

static void
reset_sedlbauer(struct IsdnCardState *cs)
{
	long flags;

	byteout(cs->hw.sedl.res_on, SEDL_RESET);	/* Reset On */
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 1;
	schedule();
	byteout(cs->hw.sedl.res_off, 0);	/* Reset Off */
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 1;
	schedule();
	restore_flags(flags);
}

int
initsedlbauer(struct IsdnCardState *cs)
{
	int ret, irq_cnt, cnt = 3;

	irq_cnt = kstat.interrupts[cs->irq];
	printk(KERN_INFO "Sedlbauer: IRQ %d count %d\n", cs->irq, irq_cnt);
	ret = get_irq(cs->cardnr, &sedlbauer_interrupt);
	while (ret && cnt) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
		printk(KERN_INFO "Sedlbauer: IRQ %d count %d\n", cs->irq,
		       kstat.interrupts[cs->irq]);
		if (kstat.interrupts[cs->irq] == irq_cnt) {
			printk(KERN_WARNING
			       "Sedlbauer: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (cnt == 1) {
				irq2dev_map[cs->irq] = NULL;
				free_irq(cs->irq, NULL);
				return (0);
			} else {
				reset_sedlbauer(cs);
				cnt--;
			}
		} else
			cnt = 0;
	}
	return (ret);
}

static void
Sedl_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
}

int
setup_sedlbauer(struct IsdnCard *card)
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Sedlbauer_revision);
	printk(KERN_NOTICE "HiSax: Sedlbauer driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_SEDLBAUER)
		return (0);

	bytecnt = 8;
	cs->hw.sedl.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	cs->hw.sedl.adr = cs->hw.sedl.cfg_reg + SEDL_ADR;
	cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_ISAC;
	cs->hw.sedl.hscx = cs->hw.sedl.cfg_reg + SEDL_HSCX;
	cs->hw.sedl.res_on = cs->hw.sedl.cfg_reg + SEDL_RES_ON;
	cs->hw.sedl.res_off = cs->hw.sedl.cfg_reg + SEDL_RES_OFF;

	if (check_region((cs->hw.sedl.cfg_reg), bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.sedl.cfg_reg,
		       cs->hw.sedl.cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.sedl.cfg_reg, bytecnt, "sedlbauer isdn");
	}

	printk(KERN_INFO
	       "Sedlbauer: defined at 0x%x IRQ %d\n",
	       cs->hw.sedl.cfg_reg,
	       cs->irq);
	printk(KERN_INFO "Sedlbauer: resetting card\n");
	reset_sedlbauer(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Sedl_card_msg;
	ISACVersion(cs, "Sedlbauer:");
	if (HscxVersion(cs, "Sedlbauer:")) {
		printk(KERN_WARNING
		    "Sedlbauer: wrong HSCX versions check IO address\n");
		release_io_sedlbauer(card);
		return (0);
	}
	return (1);
}
