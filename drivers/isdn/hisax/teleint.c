/* $Id$

 * teleint.c     low level stuff for TeleInt isdn cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 * Revision 1.1  1997/09/11 17:32:32  keil
 * new
 *
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "teleint.h"
#include "isac.h"
#include "hfc_2bs0.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *TeleInt_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	register u_char ret;
	int max_delay = 2000;
	long flags;

	save_flags(flags);
	cli();
	byteout(ale, off);
	ret = HFC_BUSY & bytein(ale);
	while (ret && --max_delay)
		ret = HFC_BUSY & bytein(ale);
	if (!max_delay) {
		printk(KERN_WARNING "TeleInt Busy not inaktive\n");
		restore_flags(flags);
		return (0);
	}
	ret = bytein(adr);
	restore_flags(flags);
	return (ret);
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	register u_char ret;
	int max_delay = 2000;
	byteout(ale, off);

	ret = HFC_BUSY & bytein(ale);
	while (ret && --max_delay)
		ret = HFC_BUSY & bytein(ale);
	if (!max_delay) {
		printk(KERN_WARNING "TeleInt Busy not inaktive\n");
		return;
	}
	insb(adr, data, size);
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	register u_char ret;
	int max_delay = 2000;
	long flags;

	save_flags(flags);
	cli();
	byteout(ale, off);
	ret = HFC_BUSY & bytein(ale);
	while (ret && --max_delay)
		ret = HFC_BUSY & bytein(ale);
	if (!max_delay) {
		printk(KERN_WARNING "TeleInt Busy not inaktive\n");
		restore_flags(flags);
		return;
	}
	byteout(adr, data);
	restore_flags(flags);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	register u_char ret;
	int max_delay = 2000;

	/* fifo write without cli because it's allready done  */
	byteout(ale, off);
	ret = HFC_BUSY & bytein(ale);
	while (ret && --max_delay)
		ret = HFC_BUSY & bytein(ale);
	if (!max_delay) {
		printk(KERN_WARNING "TeleInt Busy not inaktive\n");
		return;
	}
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	cs->hw.hfc.cip = offset;
	return (readreg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	cs->hw.hfc.cip = offset;
	writereg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	cs->hw.hfc.cip = 0;
	readfifo(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, 0, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	cs->hw.hfc.cip = 0;
	writefifo(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, 0, data, size);
}

static u_char
ReadHFC(struct IsdnCardState *cs, int data, u_char reg)
{
	register u_char ret;

	if (data) {
		cs->hw.hfc.cip = reg;
		byteout(cs->hw.hfc.addr | 1, reg);
		ret = bytein(cs->hw.hfc.addr);
		if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2)) {
			char tmp[32];
			sprintf(tmp, "hfc RD %02x %02x", reg, ret);
			debugl1(cs, tmp);
		}
	} else
		ret = bytein(cs->hw.hfc.addr | 1);
	return (ret);
}

static void
WriteHFC(struct IsdnCardState *cs, int data, u_char reg, u_char value)
{
	byteout(cs->hw.hfc.addr | 1, reg);
	cs->hw.hfc.cip = reg;
	if (data)
		byteout(cs->hw.hfc.addr, value);
	if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2)) {
		char tmp[32];
		sprintf(tmp, "hfc W%c %02x %02x", data ? 'D' : 'C', reg, value);
		debugl1(cs, tmp);
	}
}

static void
TeleInt_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs;
	u_char val, stat = 0;

	cs = (struct IsdnCardState *) irq2dev_map[intno];

	if (!cs) {
		printk(KERN_WARNING "TeleInt: Spurious interrupt!\n");
		return;
	}
	val = readreg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	val = readreg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 2) {
		writereg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_MASK, 0xFF);
		writereg(cs->hw.hfc.addr | 1, cs->hw.hfc.addr, ISAC_MASK, 0x0);
	}
}

static void
TeleInt_Timer(struct IsdnCardState *cs)
{
	int stat = 0;

	if (cs->bcs[0].mode) {
		stat |= 1;
		main_irq_hfc(&cs->bcs[0]);
	}
	if (cs->bcs[1].mode) {
		stat |= 2;
		main_irq_hfc(&cs->bcs[1]);
	}
	cs->hw.hfc.timer.expires = jiffies + 1;
	add_timer(&cs->hw.hfc.timer);
}

void
release_io_TeleInt(struct IsdnCard *card)
{
	del_timer(&card->cs->hw.hfc.timer);
	releasehfc(card->cs);
	if (card->cs->hw.hfc.addr)
		release_region(card->cs->hw.hfc.addr, 2);
}

static void
reset_TeleInt(struct IsdnCardState *cs)
{
	long flags;

	printk(KERN_INFO "TeleInt: resetting card\n");
	cs->hw.hfc.cirm |= HFC_RESET;
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.cirm);	/* Reset On */
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 3;
	schedule();
	cs->hw.hfc.cirm &= ~HFC_RESET;
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.cirm);	/* Reset Off */
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 1;
	schedule();
	restore_flags(flags);
}

int
initTeleInt(struct IsdnCardState *cs)
{
	int ret, irq_cnt, cnt = 3;

	inithfc(cs);
	irq_cnt = kstat.interrupts[cs->irq];
	printk(KERN_INFO "TeleInt: IRQ %d count %d\n", cs->irq, irq_cnt);
	ret = get_irq(cs->cardnr, &TeleInt_interrupt);
	while (ret && cnt) {
		clear_pending_isac_ints(cs);
		initisac(cs);
		cs->writeisac(cs, ISAC_SPCR, cs->hw.hfc.isac_spcr);
		printk(KERN_INFO "TeleInt: IRQ %d count %d\n", cs->irq,
		       kstat.interrupts[cs->irq]);
		if (kstat.interrupts[cs->irq] == irq_cnt) {
			printk(KERN_WARNING
			       "TeleInt: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (cnt == 1) {
				irq2dev_map[cs->irq] = NULL;
				free_irq(cs->irq, NULL);
				return (0);
			} else {
				reset_TeleInt(cs);
				cnt--;
			}
		} else {
			cnt = 0;
			cs->hw.hfc.timer.expires = jiffies + 1;
			add_timer(&cs->hw.hfc.timer);
		}
	}
	return (ret);
}

static void
TeleInt_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
}

int
setup_TeleInt(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, TeleInt_revision);
	printk(KERN_NOTICE "HiSax: TeleInt driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_TELEINT)
		return (0);

	cs->hw.hfc.addr = card->para[1] & 0x3fe;
	cs->irq = card->para[0];
	cs->hw.hfc.cirm = HFC_CIRM;
	cs->hw.hfc.isac_spcr = 0x00;
	cs->hw.hfc.cip = 0;
	cs->hw.hfc.ctmt = HFC_CTMT | HFC_CLTIMER;
	cs->bcs[0].hw.hfc.send = NULL;
	cs->bcs[1].hw.hfc.send = NULL;
	cs->hw.hfc.fifosize = 7 * 1024 + 512;
	cs->hw.hfc.timer.function = (void *) TeleInt_Timer;
	cs->hw.hfc.timer.data = (long) cs;
	init_timer(&cs->hw.hfc.timer);
	if (check_region((cs->hw.hfc.addr), 2)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.hfc.addr,
		       cs->hw.hfc.addr + 2);
		return (0);
	} else {
		request_region(cs->hw.hfc.addr, 2, "TeleInt isdn");
	}
	/* HW IO = IO */
	byteout(cs->hw.hfc.addr, cs->hw.hfc.addr & 0xff);
	byteout(cs->hw.hfc.addr | 1, ((cs->hw.hfc.addr & 0x300) >> 8) | 0x54);
	switch (cs->irq) {
		case 3:
			cs->hw.hfc.cirm |= HFC_INTA;
			break;
		case 4:
			cs->hw.hfc.cirm |= HFC_INTB;
			break;
		case 5:
			cs->hw.hfc.cirm |= HFC_INTC;
			break;
		case 7:
			cs->hw.hfc.cirm |= HFC_INTD;
			break;
		case 10:
			cs->hw.hfc.cirm |= HFC_INTE;
			break;
		case 11:
			cs->hw.hfc.cirm |= HFC_INTF;
			break;
		default:
			release_io_TeleInt(card);
			return (0);
	}
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.cirm);
	byteout(cs->hw.hfc.addr | 1, cs->hw.hfc.ctmt);

	printk(KERN_INFO
	       "TeleInt: defined at 0x%x IRQ %d\n",
	       cs->hw.hfc.addr,
	       cs->irq);

	reset_TeleInt(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHFC;
	cs->BC_Write_Reg = &WriteHFC;
	cs->cardmsg = &TeleInt_card_msg;
	ISACVersion(cs, "TeleInt:");
	return (1);
}
