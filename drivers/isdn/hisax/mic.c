/* $Id$

 * mic.c  low level stuff for mic cards
 *
 * Copyright (C) 1997 
 *
 * Author  Stephan von Krawczynski <skraw@ithnet.com>
 *
 *
 * $Log$
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "mic.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *mic_revision = "$Revision$";

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
	return (readreg(cs->hw.mic.adr, cs->hw.mic.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.mic.adr, cs->hw.mic.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.mic.adr, cs->hw.mic.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.mic.adr, cs->hw.mic.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.mic.adr,
			cs->hw.mic.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.mic.adr,
		 cs->hw.mic.hscx, offset + (hscx ? 0x40 : 0), value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.mic.adr, \
		cs->hw.mic.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.mic.adr, \
		cs->hw.mic.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.mic.adr, \
		cs->hw.mic.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.mic.adr, \
		cs->hw.mic.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
mic_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs;
	u_char val, stat = 0;

	cs = (struct IsdnCardState *) irq2dev_map[intno];

	if (!cs) {
		printk(KERN_WARNING "mic: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.mic.adr, cs->hw.mic.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.mic.adr, cs->hw.mic.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.mic.adr, cs->hw.mic.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.mic.adr, cs->hw.mic.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writereg(cs->hw.mic.adr, cs->hw.mic.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.mic.adr, cs->hw.mic.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.mic.adr, cs->hw.mic.hscx, HSCX_MASK, 0x0);
		writereg(cs->hw.mic.adr, cs->hw.mic.hscx, HSCX_MASK + 0x40, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.mic.adr, cs->hw.mic.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.mic.adr, cs->hw.mic.isac, ISAC_MASK, 0x0);
	}
}

void
release_io_mic(struct IsdnCard *card)
{
	int bytecnt = 8;

	if (card->cs->hw.mic.cfg_reg)
		release_region(card->cs->hw.mic.cfg_reg, bytecnt);
}

static void
reset_mic(struct IsdnCardState *cs)
{
}

int
initmic(struct IsdnCardState *cs)
{
	int ret, irq_cnt, cnt = 3;

	irq_cnt = kstat.interrupts[cs->irq];
	printk(KERN_INFO "mic: IRQ %d count %d\n", cs->irq, irq_cnt);
	ret = get_irq(cs->cardnr, &mic_interrupt);
	while (ret && cnt) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
		printk(KERN_INFO "mic: IRQ %d count %d\n", cs->irq,
		       kstat.interrupts[cs->irq]);
		if (kstat.interrupts[cs->irq] == irq_cnt) {
			printk(KERN_WARNING
			       "mic: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (cnt == 1) {
				irq2dev_map[cs->irq] = NULL;
				free_irq(cs->irq, NULL);
				return (0);
			} else {
				reset_mic(cs);
				cnt--;
			}
		} else
			cnt = 0;
	}
	return (ret);
}

static void
mic_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
}

int
setup_mic(struct IsdnCard *card)
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, mic_revision);
	printk(KERN_NOTICE "HiSax: mic driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_MIC)
		return (0);

	bytecnt = 8;
	cs->hw.mic.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	cs->hw.mic.adr = cs->hw.mic.cfg_reg + MIC_ADR;
	cs->hw.mic.isac = cs->hw.mic.cfg_reg + MIC_ISAC;
	cs->hw.mic.hscx = cs->hw.mic.cfg_reg + MIC_HSCX;

	if (check_region((cs->hw.mic.cfg_reg), bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.mic.cfg_reg,
		       cs->hw.mic.cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.mic.cfg_reg, bytecnt, "mic isdn");
	}

	printk(KERN_INFO
	       "mic: defined at 0x%x IRQ %d\n",
	       cs->hw.mic.cfg_reg,
	       cs->irq);
	printk(KERN_INFO "mic: resetting card\n");
	reset_mic(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &mic_card_msg;
	ISACVersion(cs, "mic:");
	if (HscxVersion(cs, "mic:")) {
		printk(KERN_WARNING
		    "mic: wrong HSCX versions check IO address\n");
		release_io_mic(card);
		return (0);
	}
	return (1);
}
