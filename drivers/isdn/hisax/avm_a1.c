/* $Id$

 * avm_a1.c     low level stuff for AVM A1 (Fritz) isdn cards
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 * Revision 2.1  1997/07/27 21:47:13  keil
 * new interface structures
 *
 * Revision 2.0  1997/06/26 11:02:48  keil
 * New Layer and card interface
 *
 * Revision 1.6  1997/04/13 19:54:07  keil
 * Change in IRQ check delay for SMP
 *
 * Revision 1.5  1997/04/06 22:54:10  keil
 * Using SKB's
 *
 * Revision 1.4  1997/01/27 15:50:21  keil
 * SMP proof,cosmetics
 *
 * Revision 1.3  1997/01/21 22:14:20  keil
 * cleanups
 *
 * Revision 1.2  1996/10/27 22:07:31  keil
 * cosmetic changes
 *
 * Revision 1.1  1996/10/13 20:04:49  keil
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "avm_a1.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];
const char *avm_revision = "$Revision$";

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
	return (readreg(cs->hw.avm.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.avm.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	read_fifo(cs->hw.avm.isacfifo, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	write_fifo(cs->hw.avm.isacfifo, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.avm.hscx[hscx], offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.avm.hscx[hscx], offset, value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.avm.hscx[nr], reg)
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.avm.hscx[nr], reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo(cs->hw.avm.hscxfifo[nr], ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo(cs->hw.avm.hscxfifo[nr], ptr, cnt)

#include "hscx_irq.c"

static void
avm_a1_interrupt(int intno, void *para, struct pt_regs *regs)
{
	struct IsdnCardState *cs = para;
	u_char val, sval, stat = 0;
	char tmp[32];

	if (!cs) {
		printk(KERN_WARNING "AVM A1: Spurious interrupt!\n");
		return;
	}
	while (((sval = bytein(cs->hw.avm.cfg_reg)) & 0xf) != 0x7) {
		if (!(sval & AVM_A1_STAT_TIMER)) {
			byteout(cs->hw.avm.cfg_reg, 0x14);
			byteout(cs->hw.avm.cfg_reg, 0x18);
			sval = bytein(cs->hw.avm.cfg_reg);
		} else if (cs->debug & L1_DEB_INTSTAT) {
			sprintf(tmp, "avm IntStatus %x", sval);
			debugl1(cs, tmp);
		}
		if (!(sval & AVM_A1_STAT_HSCX)) {
			val = readreg(cs->hw.avm.hscx[1], HSCX_ISTA);
			if (val) {
				hscx_int_main(cs, val);
				stat |= 1;
			}
		}
		if (!(sval & AVM_A1_STAT_ISAC)) {
			val = readreg(cs->hw.avm.isac, ISAC_ISTA);
			if (val) {
				isac_interrupt(cs, val);
				stat |= 2;
			}
		}
	}
	if (stat & 1) {
		writereg(cs->hw.avm.hscx[0], HSCX_MASK, 0xFF);
		writereg(cs->hw.avm.hscx[1], HSCX_MASK, 0xFF);
		writereg(cs->hw.avm.hscx[0], HSCX_MASK, 0x0);
		writereg(cs->hw.avm.hscx[1], HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.avm.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.avm.isac, ISAC_MASK, 0x0);
	}
}

inline static void
release_ioregs(struct IsdnCard *card, int mask)
{
	release_region(card->cs->hw.avm.cfg_reg, 8);
	if (mask & 1)
		release_region(card->cs->hw.avm.isac + 32, 32);
	if (mask & 2)
		release_region(card->cs->hw.avm.isacfifo, 1);
	if (mask & 4)
		release_region(card->cs->hw.avm.hscx[0] + 32, 32);
	if (mask & 8)
		release_region(card->cs->hw.avm.hscxfifo[0], 1);
	if (mask & 0x10)
		release_region(card->cs->hw.avm.hscx[1] + 32, 32);
	if (mask & 0x20)
		release_region(card->cs->hw.avm.hscxfifo[1], 1);
}

void
release_io_avm_a1(struct IsdnCard *card)
{
	release_ioregs(card, 0x3f);
}

static void
AVM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
}

int
initavm_a1(struct IsdnCardState *cs)
{
	int ret;
	int loop = 0;
	char tmp[40];

	cs->hw.avm.counter = kstat.interrupts[cs->irq];
	sprintf(tmp, "IRQ %d count %d", cs->irq, cs->hw.avm.counter);
	debugl1(cs, tmp);
	ret = get_irq(cs->cardnr, &avm_a1_interrupt);
	if (ret) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
		while (loop++ < 10) {
			/* At least 1-3 irqs must happen
			 * (one from HSCX A, one from HSCX B, 3rd from ISAC)
			 */
			if (kstat.interrupts[cs->irq] > cs->hw.avm.counter)
				break;
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + 1;
			schedule();
		}
		sprintf(tmp, "IRQ %d count %d", cs->irq,
			kstat.interrupts[cs->irq]);
		debugl1(cs, tmp);
		if (kstat.interrupts[cs->irq] == cs->hw.avm.counter) {
			printk(KERN_WARNING
			       "AVM A1: IRQ(%d) getting no interrupts during init\n",
			       cs->irq);
			free_irq(cs->irq, NULL);
			return (0);
		}
	}
	return (ret);
}

int
setup_avm_a1(struct IsdnCard *card)
{
	u_char val;
	struct IsdnCardState *cs = card->cs;
	long flags;
	char tmp[64];

	strcpy(tmp, avm_revision);
	printk(KERN_NOTICE "HiSax: AVM driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_A1)
		return (0);

	cs->hw.avm.cfg_reg = card->para[1] + 0x1800;
	cs->hw.avm.isac = card->para[1] + 0x1400 - 0x20;
	cs->hw.avm.hscx[0] = card->para[1] + 0x400 - 0x20;
	cs->hw.avm.hscx[1] = card->para[1] + 0xc00 - 0x20;
	cs->hw.avm.isacfifo = card->para[1] + 0x1000;
	cs->hw.avm.hscxfifo[0] = card->para[1];
	cs->hw.avm.hscxfifo[1] = card->para[1] + 0x800;
	cs->irq = card->para[0];
	if (check_region((cs->hw.avm.cfg_reg), 8)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.avm.cfg_reg,
		       cs->hw.avm.cfg_reg + 8);
		return (0);
	} else {
		request_region(cs->hw.avm.cfg_reg, 8, "avm cfg");
	}
	if (check_region((cs->hw.avm.isac + 32), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s isac ports %x-%x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.isac + 32,
		       cs->hw.avm.isac + 64);
		release_ioregs(card, 0);
		return (0);
	} else {
		request_region(cs->hw.avm.isac + 32, 32, "HiSax isac");
	}
	if (check_region((cs->hw.avm.isacfifo), 1)) {
		printk(KERN_WARNING
		       "HiSax: %s isac fifo port %x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.isacfifo);
		release_ioregs(card, 1);
		return (0);
	} else {
		request_region(cs->hw.avm.isacfifo, 1, "HiSax isac fifo");
	}
	if (check_region((cs->hw.avm.hscx[0]) + 32, 32)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx A ports %x-%x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscx[0] + 32,
		       cs->hw.avm.hscx[0] + 64);
		release_ioregs(card, 3);
		return (0);
	} else {
		request_region(cs->hw.avm.hscx[0] + 32, 32, "HiSax hscx A");
	}
	if (check_region(cs->hw.avm.hscxfifo[0], 1)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx A fifo port %x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscxfifo[0]);
		release_ioregs(card, 7);
		return (0);
	} else {
		request_region(cs->hw.avm.hscxfifo[0], 1, "HiSax hscx A fifo");
	}
	if (check_region(cs->hw.avm.hscx[1] + 32, 32)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx B ports %x-%x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscx[1] + 32,
		       cs->hw.avm.hscx[1] + 64);
		release_ioregs(card, 0xf);
		return (0);
	} else {
		request_region(cs->hw.avm.hscx[1] + 32, 32, "HiSax hscx B");
	}
	if (check_region(cs->hw.avm.hscxfifo[1], 1)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx B fifo port %x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscxfifo[1]);
		release_ioregs(card, 0x1f);
		return (0);
	} else {
		request_region(cs->hw.avm.hscxfifo[1], 1, "HiSax hscx B fifo");
	}
	save_flags(flags);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	sti();
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x1);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	val = cs->irq;
	if (val == 9)
		val = 2;
	byteout(cs->hw.avm.cfg_reg + 1, val);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	restore_flags(flags);

	val = bytein(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg, val);
	val = bytein(cs->hw.avm.cfg_reg + 3);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg + 3, val);
	val = bytein(cs->hw.avm.cfg_reg + 2);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg + 2, val);
	byteout(cs->hw.avm.cfg_reg, 0x14);
	byteout(cs->hw.avm.cfg_reg, 0x18);
	val = bytein(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg, val);

	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d cfg:%x\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.avm.cfg_reg);
	printk(KERN_NOTICE
	       "HiSax: isac:%x/%x\n",
	       cs->hw.avm.isac + 32, cs->hw.avm.isacfifo);
	printk(KERN_NOTICE
	       "HiSax: hscx A:%x/%x  hscx B:%x/%x\n",
	       cs->hw.avm.hscx[0] + 32, cs->hw.avm.hscxfifo[0],
	       cs->hw.avm.hscx[1] + 32, cs->hw.avm.hscxfifo[1]);

	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &AVM_card_msg;
	ISACVersion(cs, "AVM A1:");
	if (HscxVersion(cs, "AVM A1:")) {
		printk(KERN_WARNING
		       "AVM A1: wrong HSCX versions check IO address\n");
		release_io_avm_a1(card);
		return (0);
	}
	return (1);
}
