/* $Id$

 * sedlbauer.c  low level stuff for Sedlbauer cards
 *              includes support for the Sedlbauer Speed Star 
 *              and support for the Sedlbauer ISDN-Controller PC/104
 *              derived from the original file asuscom.c from Karsten Keil
 *
 * Copyright (C) 1997,1998 Marcus Niemann (for the modifications to
 *                                         the original file asuscom.c)
 *
 * Author     Marcus Niemann (niemann@www-bib.fh-bielefeld.de)
 *
 * Thanks to  Karsten Keil
 *            Sedlbauer AG for informations
 *            Edgar Toernig
 *
 * $Log$
 * Revision 1.1.2.6  1998/09/12 18:44:06  niemann
 * Added new card: Sedlbauer ISDN-Controller PC/104
 *
 * Revision 1.1.2.5  1998/04/08 21:58:44  keil
 * New init code
 *
 * Revision 1.1.2.4  1998/02/09 11:21:17  keil
 * Sedlbauer PCMCIA support from Marcus Niemann
 *
 * Revision 1.1.2.3  1998/01/27 22:37:29  keil
 * fast io
 *
 * Revision 1.1.2.2  1997/11/15 18:50:56  keil
 * new common init function
 *
 * Revision 1.1.2.1  1997/10/17 22:10:56  keil
 * new files on 2.0
 *
 * Revision 1.1  1997/09/11 17:32:04  keil
 * new
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isar.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *Sedlbauer_revision = "$Revision$";

const char *Sedlbauer_Types[] =
{"None", "Speed Card", "Speed Win", "Speed Star", "Speed Fax+", "ISDN PC/104"};
 
#define SEDL_SPEED_CARD 1
#define SEDL_SPEED_WIN  2
#define SEDL_SPEED_STAR 3
#define SEDL_SPEED_FAX	4
#define SEDL_SPEED_PC104 5

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define SEDL_RESET_ON	0
#define SEDL_RESET_OFF	1
#define SEDL_ISAC	2
#define SEDL_HSCX	3
#define SEDL_ADR	4

#define SEDL_PCMCIA_RESET	0
#define SEDL_PCMCIA_ISAC	1
#define SEDL_PCMCIA_HSCX	2
#define SEDL_PCMCIA_ADR		4

#define SEDL_FAX_ISAC		4
#define SEDL_FAX_ISAR		6
#define SEDL_FAX_ADR		8
#define SEDL_FAX_ISAR_RESET_ON	10
#define SEDL_FAX_ISAR_RESET_OFF	12

#define SEDL_PC104_ADR 	0
#define SEDL_PC104_IPAC	2

#define SEDL_RESET      0x3	/* same as DOS driver */

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
ReadISAC_IPAC(struct IsdnCardState *cs, u_char offset)
{
        return (readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, offset|0x80));}

static void
WriteISAC_IPAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
        writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, offset|0x80, value);
}

static void
ReadISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
        readfifo(cs->hw.sedl.adr, cs->hw.sedl.isac, 0x80, data, size);
}

static void
WriteISACfifo_IPAC(struct IsdnCardState *cs, u_char * data, int size)
{
        writefifo(cs->hw.sedl.adr, cs->hw.sedl.isac, 0x80, data, size);
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

/* ISAR access routines
 * mode = 0 access with IRQ on
 * mode = 1 access with IRQ off
 * mode = 2 access with IRQ off and using last offset
 */
  
static u_char
ReadISAR(struct IsdnCardState *cs, int mode, u_char offset)
{	
	if (mode == 0)
		return (readreg(cs->hw.sedl.adr, cs->hw.sedl.isar, offset));
	else if (mode == 1)
		byteout(cs->hw.sedl.adr, offset);
	return(bytein(cs->hw.sedl.isar));
}

static void
WriteISAR(struct IsdnCardState *cs, int mode, u_char offset, u_char value)
{
	if (mode == 0)
		writereg(cs->hw.sedl.adr, cs->hw.sedl.isar, offset, value);
	else {
		if (mode == 1)
			byteout(cs->hw.sedl.adr, offset);
		byteout(cs->hw.sedl.isar, value);
	}
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
	struct IsdnCardState *cs = dev_id;
	u_char val, stat = 0;

	if (!cs) {
		printk(KERN_WARNING "Sedlbauer: Spurious interrupt!\n");
		return;
	}

        if ((cs->typ == ISDN_CTYPE_SEDLBAUER_PCMCIA) && (*cs->busy_flag == 1)) {
          /* The card tends to generate interrupts while being removed
             causing us to just crash the kernel. bad. */
          printk(KERN_WARNING "Sedlbauer: card not available!\n");
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

static void
sedlbauer_interrupt_ipac(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char ista, val, icnt = 20;
	char   tmp[64];

	if (!cs) {
		printk(KERN_WARNING "Sedlbauer: Spurious interrupt!\n");
		return;
	}
	ista = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_ISTA);
Start_IPAC:
	if (cs->debug & L1_DEB_IPAC) {
		sprintf(tmp, "IPAC ISTA %02X", ista);
		debugl1(cs, tmp);
	}
	if (ista & 0x0f) {
		val = readreg(cs->hw.sedl.adr, cs->hw.sedl.hscx, HSCX_ISTA + 0x40);
		if (ista & 0x01)
			val |= 0x01;
		if (ista & 0x04)
			val |= 0x02;
		if (ista & 0x08)
			val |= 0x04;
		if (val)
			hscx_int_main(cs, val);
	}
	if (ista & 0x20) {
		val = 0xfe & readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA | 0x80);
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "Sedlbauer IRQ LOOP\n");
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_MASK, 0xFF);
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_MASK, 0xC0);
}

static void
sedlbauer_interrupt_isar(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val;
	int cnt = 20;

	if (!cs) {
		printk(KERN_WARNING "Sedlbauer: Spurious interrupt!\n");
		return;
	}

	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isar, ISAR_IRQBIT);
      Start_ISAR:
	if (val & ISAR_IRQSTA)
		isar_int_main(cs);
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isar, ISAR_IRQBIT);
	if ((val & ISAR_IRQSTA) && --cnt) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "ISAR IntStat after IntRoutine");
		goto Start_ISAR;
	}
	val = readreg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_ISTA);
	if (val && --cnt) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (!cnt)
		printk(KERN_WARNING "Sedlbauer IRQ LOOP\n");

	writereg(cs->hw.sedl.adr, cs->hw.sedl.isar, ISAR_IRQBIT, 0);
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_MASK, 0xFF);
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, ISAC_MASK, 0x0);
	writereg(cs->hw.sedl.adr, cs->hw.sedl.isar, ISAR_IRQBIT, ISAR_IRQMSK);
}

void
release_io_sedlbauer(struct IsdnCardState *cs)
{
	int bytecnt = (cs->subtyp == SEDL_SPEED_FAX) ? 16 : 8;

	if (cs->hw.sedl.cfg_reg)
		release_region(cs->hw.sedl.cfg_reg, bytecnt);
}

static void
reset_sedlbauer(struct IsdnCardState *cs)
{
	long flags;

	if (cs->subtyp != SEDL_SPEED_STAR) {
		if (cs->subtyp == SEDL_SPEED_PC104)
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_POTA2, 0x20);
		else
			byteout(cs->hw.sedl.reset_on, SEDL_RESET);	/* Reset On */
		save_flags(flags);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + 1;
		schedule();
		if (cs->subtyp == SEDL_SPEED_PC104)
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_POTA2, 0x0);
		else
			byteout(cs->hw.sedl.reset_off, 0);	/* Reset Off */
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + 1;
		schedule();
		if (cs->subtyp == SEDL_SPEED_PC104) {
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_CONF, 0x0);
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_ACFG, 0xff);
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_AOE, 0x0);
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_MASK, 0xc0);
			writereg(cs->hw.sedl.adr, cs->hw.sedl.isac, IPAC_PCFG, 0x12);
		}

		restore_flags(flags);
	}
}

static int
Sedl_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_sedlbauer(cs);
			return(0);
		case CARD_RELEASE:
			release_io_sedlbauer(cs);
			return(0);
		case CARD_SETIRQ:
			if (cs->subtyp == SEDL_SPEED_FAX) {
				return(request_irq(cs->irq, &sedlbauer_interrupt_isar,
					I4L_IRQ_FLAG, "HiSax", cs));
			} else if (cs->subtyp == SEDL_SPEED_PC104) {
				return(request_irq(cs->irq, &sedlbauer_interrupt_ipac,
					I4L_IRQ_FLAG, "HiSax", cs));
			} else {
				return(request_irq(cs->irq, &sedlbauer_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
			}
		case CARD_INIT:
			if (cs->subtyp == SEDL_SPEED_FAX) {
				clear_pending_isac_ints(cs);
				writereg(cs->hw.sedl.adr, cs->hw.sedl.isar,
					ISAR_IRQBIT, 0);
				initisac(cs);
				inithscx(cs);
				/* Reenable all IRQ */
				cs->writeisac(cs, ISAC_MASK, 0);
				// writereg(cs->hw.sedl.adr, cs->hw.sedl.isar,
				//	ISAR_IRQBIT, ISAR_IRQSTA);
				/* RESET Receiver and Transmitter */
				cs->writeisac(cs, ISAC_CMDR, 0x41);
			} else {
				inithscxisac(cs, 3);
			}
			return(0);
		case CARD_TEST:
			return(0);
		case CARD_LOAD_FIRM:
			if (cs->subtyp == SEDL_SPEED_FAX)
				return(isar_load_firmware(cs, arg));
			else
				return(0);
	}
	return(0);
}

__initfunc(int
setup_sedlbauer(struct IsdnCard *card))
{
	int bytecnt, ver, val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Sedlbauer_revision);
	printk(KERN_INFO "HiSax: Sedlbauer driver Rev. %s\n", HiSax_getrev(tmp));
 	if (cs->typ == ISDN_CTYPE_SEDLBAUER) {
 		cs->subtyp = SEDL_SPEED_CARD;
 	} else if (cs->typ == ISDN_CTYPE_SEDLBAUER_PCMCIA) {	
 		cs->subtyp = SEDL_SPEED_STAR;
 	} else if (cs->typ == ISDN_CTYPE_SEDLBAUER_FAX) {	
 		cs->subtyp = SEDL_SPEED_FAX;
 	} else
		return (0);

	bytecnt = 8;
	cs->hw.sedl.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	if (cs->subtyp == SEDL_SPEED_STAR) {
		cs->hw.sedl.adr = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_ADR;
		cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_ISAC;
		cs->hw.sedl.hscx = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_HSCX;
		cs->hw.sedl.reset_on = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_RESET;
		cs->hw.sedl.reset_off = cs->hw.sedl.cfg_reg + SEDL_PCMCIA_RESET;
	} else if (cs->subtyp == SEDL_SPEED_FAX) {
		cs->hw.sedl.adr = cs->hw.sedl.cfg_reg + SEDL_FAX_ADR;
		cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_FAX_ISAC;
		cs->hw.sedl.isar = cs->hw.sedl.cfg_reg + SEDL_FAX_ISAR;
		cs->hw.sedl.reset_on = cs->hw.sedl.cfg_reg + SEDL_FAX_ISAR_RESET_ON;
		cs->hw.sedl.reset_off = cs->hw.sedl.cfg_reg + SEDL_FAX_ISAR_RESET_OFF;
		test_and_set_bit(HW_ISAR, &cs->HW_Flags);
		bytecnt = 16;
	} else {
		cs->hw.sedl.adr = cs->hw.sedl.cfg_reg + SEDL_ADR;
		cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_ISAC;
		cs->hw.sedl.hscx = cs->hw.sedl.cfg_reg + SEDL_HSCX;
		cs->hw.sedl.reset_on = cs->hw.sedl.cfg_reg + SEDL_RESET_ON;
		cs->hw.sedl.reset_off = cs->hw.sedl.cfg_reg + SEDL_RESET_OFF;
	}
        
	/* In case of the sedlbauer pcmcia card, this region is in use,
           reserved for us by the card manager. So we do not check it
           here, it would fail. */
	if ((cs->typ != ISDN_CTYPE_SEDLBAUER_PCMCIA) &&
		check_region((cs->hw.sedl.cfg_reg), bytecnt)) {
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
	       "Sedlbauer: defined at 0x%x-0x%x IRQ %d\n",
	       cs->hw.sedl.cfg_reg,
	       cs->hw.sedl.cfg_reg + bytecnt,
	       cs->irq);

	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Sedl_card_msg;
	
	val = readreg(cs->hw.sedl.cfg_reg + SEDL_PC104_ADR,
                cs->hw.sedl.cfg_reg + SEDL_PC104_IPAC, IPAC_ID);
        if (val == 1) {
                cs->subtyp = SEDL_SPEED_PC104;
                cs->hw.sedl.adr  = cs->hw.sedl.cfg_reg + SEDL_PC104_ADR;
                cs->hw.sedl.isac = cs->hw.sedl.cfg_reg + SEDL_PC104_IPAC;
                cs->hw.sedl.hscx = cs->hw.sedl.cfg_reg + SEDL_PC104_IPAC;
                test_and_set_bit(HW_IPAC, &cs->HW_Flags);
                cs->readisac = &ReadISAC_IPAC;
                cs->writeisac = &WriteISAC_IPAC;
                cs->readisacfifo = &ReadISACfifo_IPAC;
                cs->writeisacfifo = &WriteISACfifo_IPAC;
                printk(KERN_INFO "Sedlbauer %s: IPAC version %x\n",
			Sedlbauer_Types[cs->subtyp], val);
	} else {
		cs->readisac = &ReadISAC;
		cs->writeisac = &WriteISAC;
		cs->readisacfifo = &ReadISACfifo;
		cs->writeisacfifo = &WriteISACfifo;
		ISACVersion(cs, "Sedlbauer:");
		if (cs->subtyp == SEDL_SPEED_FAX) {
			cs->BC_Read_Reg = &ReadISAR;
			cs->BC_Write_Reg = &WriteISAR;
			cs->BC_Send_Data = &hscx_fill_fifo;
			ver = ISARVersion(cs, "Sedlbauer:");
			if (ver < 0) {
				printk(KERN_WARNING
					"Sedlbauer: wrong ISAR version (ret = %d)\n", ver);
				release_io_sedlbauer(cs);
				return (0);
			}
		} else {
			if (HscxVersion(cs, "Sedlbauer:")) {
				printk(KERN_WARNING
					"Sedlbauer %s: wrong HSCX versions check IO address\n",
					Sedlbauer_Types[cs->subtyp]);
				release_io_sedlbauer(cs);
				return (0);
			}
		}
	}
	printk(KERN_INFO "Sedlbauer %s: resetting card\n",
			Sedlbauer_Types[cs->subtyp]);
	reset_sedlbauer(cs);
	return (1);
}
