/* $Id$

 * dynalink.c     low level stuff for ASUSCOM NETWORK INC. ISDNLink cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to  ASUSCOM NETWORK INC. Taiwan and  Dynalink NL for informations
 *
 *
 * $Log$
 * Revision 1.5  1997/11/06 17:13:34  keil
 * New 2.1 init code
 *
 * Revision 1.4  1997/10/29 18:55:49  keil
 * changes for 2.1.60 (irq2dev_map)
 *
 * Revision 1.3  1997/08/01 11:16:33  keil
 * cosmetics
 *
 * Revision 1.2  1997/07/27 21:47:59  keil
 * new interface structures
 *
 * Revision 1.1  1997/06/26 11:21:41  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *Dynalink_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

#define DYNA_ISAC	0
#define DYNA_HSCX	1
#define DYNA_ADR	2
#define DYNA_CTRL_U7	3
#define DYNA_CTRL_POTS	5

/* CARD_ADR (Write) */
#define DYNA_RESET      0x80	/* Bit 7 Reset-Leitung */

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
	return (readreg(cs->hw.dyna.adr, cs->hw.dyna.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.dyna.adr, cs->hw.dyna.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	readfifo(cs->hw.dyna.adr, cs->hw.dyna.isac, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	writefifo(cs->hw.dyna.adr, cs->hw.dyna.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.dyna.adr,
			cs->hw.dyna.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.dyna.adr,
		 cs->hw.dyna.hscx, offset + (hscx ? 0x40 : 0), value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.dyna.adr, \
		cs->hw.dyna.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.dyna.adr, \
		cs->hw.dyna.hscx, reg + (nr ? 0x40 : 0), data)

#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.dyna.adr, \
		cs->hw.dyna.hscx, (nr ? 0x40 : 0), ptr, cnt)

#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.dyna.adr, \
		cs->hw.dyna.hscx, (nr ? 0x40 : 0), ptr, cnt)

#include "hscx_irq.c"

static void
dynalink_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, stat = 0;

	if (!cs) {
		printk(KERN_WARNING "ISDNLink: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.dyna.adr, cs->hw.dyna.hscx, HSCX_ISTA + 0x40);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readreg(cs->hw.dyna.adr, cs->hw.dyna.isac, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.dyna.adr, cs->hw.dyna.hscx, HSCX_ISTA + 0x40);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readreg(cs->hw.dyna.adr, cs->hw.dyna.isac, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writereg(cs->hw.dyna.adr, cs->hw.dyna.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.dyna.adr, cs->hw.dyna.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.dyna.adr, cs->hw.dyna.hscx, HSCX_MASK, 0x0);
		writereg(cs->hw.dyna.adr, cs->hw.dyna.hscx, HSCX_MASK + 0x40, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.dyna.adr, cs->hw.dyna.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.dyna.adr, cs->hw.dyna.isac, ISAC_MASK, 0x0);
	}
}

void
release_io_dynalink(struct IsdnCardState *cs)
{
	int bytecnt = 8;

	if (cs->hw.dyna.cfg_reg)
		release_region(cs->hw.dyna.cfg_reg, bytecnt);
}

static void
reset_dynalink(struct IsdnCardState *cs)
{
	long flags;

	byteout(cs->hw.dyna.adr, DYNA_RESET);	/* Reset On */
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 1;
	schedule();
	byteout(cs->hw.dyna.adr, 0);	/* Reset Off */
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 1;
	schedule();
	restore_flags(flags);
}

static int
Dyna_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_dynalink(cs);
			return(0);
		case CARD_RELEASE:
			release_io_dynalink(cs);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &dynalink_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			clear_pending_isac_ints(cs);
			clear_pending_hscx_ints(cs);
			initisac(cs);
			inithscx(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

__initfunc(int
setup_dynalink(struct IsdnCard *card))
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Dynalink_revision);
	printk(KERN_INFO "HiSax: ISDNLink driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_DYNALINK)
		return (0);

	bytecnt = 8;
	cs->hw.dyna.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	cs->hw.dyna.adr = cs->hw.dyna.cfg_reg + DYNA_ADR;
	cs->hw.dyna.isac = cs->hw.dyna.cfg_reg + DYNA_ISAC;
	cs->hw.dyna.hscx = cs->hw.dyna.cfg_reg + DYNA_HSCX;
	cs->hw.dyna.u7 = cs->hw.dyna.cfg_reg + DYNA_CTRL_U7;
	cs->hw.dyna.pots = cs->hw.dyna.cfg_reg + DYNA_CTRL_POTS;

	if (check_region((cs->hw.dyna.cfg_reg), bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.dyna.cfg_reg,
		       cs->hw.dyna.cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.dyna.cfg_reg, bytecnt, "dynalink isdn");
	}

	printk(KERN_INFO
	       "ISDNLink: defined at 0x%x IRQ %d\n",
	       cs->hw.dyna.cfg_reg,
	       cs->irq);
	printk(KERN_INFO "ISDNLink: resetting card\n");
	reset_dynalink(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Dyna_card_msg;
	ISACVersion(cs, "ISDNLink:");
	if (HscxVersion(cs, "ISDNLink:")) {
		printk(KERN_WARNING
		     "ISDNLink: wrong HSCX versions check IO address\n");
		release_io_dynalink(cs);
		return (0);
	}
	return (1);
}
