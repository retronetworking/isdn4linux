/* $Id$

 * teles0.c     low level stuff for Teles Memory IO isdn cards
 *              based on the teles driver from Jan den Ouden
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 * $Log$
 * Revision 2.1  1997/07/27 21:47:10  keil
 * new interface structures
 *
 * Revision 2.0  1997/06/26 11:02:43  keil
 * New Layer and card interface
 *
 * Revision 1.8  1997/04/13 19:54:04  keil
 * Change in IRQ check delay for SMP
 *
 * Revision 1.7  1997/04/06 22:54:04  keil
 * Using SKB's
 *
 * removed old log info /KKe
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "teles0.h"
#include "isdnl1.h"
#include "isac.h"
#include "hscx.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];

const char *teles0_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline u_char
readisac(unsigned int adr, u_char off)
{
	return readb(adr + ((off & 1) ? 0x2ff : 0x100) + off);
}

static inline void
writeisac(unsigned int adr, u_char off, u_char data)
{
	writeb(data, adr + ((off & 1) ? 0x2ff : 0x100) + off);
}


static inline u_char
readhscx(unsigned int adr, int hscx, u_char off)
{
	return readb(adr + (hscx ? 0x1c0 : 0x180) +
		     ((off & 1) ? 0x1ff : 0) + off);
}

static inline void
writehscx(unsigned int adr, int hscx, u_char off, u_char data)
{
	writeb(data, adr + (hscx ? 0x1c0 : 0x180) +
	       ((off & 1) ? 0x1ff : 0) + off);
}

static inline void
read_fifo_isac(unsigned int adr, u_char * data, int size)
{
	register int i;
	register u_char *ad = (u_char *) (adr + 0x100);
	for (i = 0; i < size; i++)
		data[i] = readb(ad);
}

static inline void
write_fifo_isac(unsigned int adr, u_char * data, int size)
{
	register int i;
	register u_char *ad = (u_char *) (adr + 0x100);
	for (i = 0; i < size; i++)
		writeb(data[i], ad);
}

static inline void
read_fifo_hscx(unsigned int adr, int hscx, u_char * data, int size)
{
	register int i;
	register u_char *ad = (u_char *) (adr + (hscx ? 0x1c0 : 0x180));
	for (i = 0; i < size; i++)
		data[i] = readb(ad);
}

static inline void
write_fifo_hscx(unsigned int adr, int hscx, u_char * data, int size)
{
	int i;
	register u_char *ad = (u_char *) (adr + (hscx ? 0x1c0 : 0x180));
	for (i = 0; i < size; i++)
		writeb(data[i], ad);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readisac(cs->hw.teles0.membase, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writeisac(cs->hw.teles0.membase, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	read_fifo_isac(cs->hw.teles0.membase, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	write_fifo_isac(cs->hw.teles0.membase, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readhscx(cs->hw.teles0.membase, hscx, offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writehscx(cs->hw.teles0.membase, hscx, offset, value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readhscx(cs->hw.teles0.membase, nr, reg)
#define WRITEHSCX(cs, nr, reg, data) writehscx(cs->hw.teles0.membase, nr, reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo_hscx(cs->hw.teles0.membase, nr, ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo_hscx(cs->hw.teles0.membase, nr, ptr, cnt)

#include "hscx_irq.c"

static void
telesS0_interrupt(int intno, void *para, struct pt_regs *regs)
{
	struct IsdnCardState *cs = para;
	u_char val, stat = 0;
	int count = 0;

	if (!cs) {
		printk(KERN_WARNING "Teles0: Spurious interrupt!\n");
		return;
	}
	val = readhscx(cs->hw.teles0.membase, 1, HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
		stat |= 1;
	}
	val = readisac(cs->hw.teles0.membase, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
		stat |= 2;
	}
	count++;
	val = readhscx(cs->hw.teles0.membase, 1, HSCX_ISTA);
	if (val && count < 20) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = readisac(cs->hw.teles0.membase, ISAC_ISTA);
	if (val && count < 20) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	if (stat & 1) {
		writehscx(cs->hw.teles0.membase, 0, HSCX_MASK, 0xFF);
		writehscx(cs->hw.teles0.membase, 1, HSCX_MASK, 0xFF);
		writehscx(cs->hw.teles0.membase, 0, HSCX_MASK, 0x0);
		writehscx(cs->hw.teles0.membase, 1, HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writeisac(cs->hw.teles0.membase, ISAC_MASK, 0xFF);
		writeisac(cs->hw.teles0.membase, ISAC_MASK, 0x0);
	}
}

void
release_io_teles0(struct IsdnCard *card)
{
	if (card->cs->hw.teles0.cfg_reg)
		release_region(card->cs->hw.teles0.cfg_reg, 8);
}

static void
reset_teles(struct IsdnCardState *cs)
{
	u_char cfval;
	long flags;

	save_flags(flags);
	sti();
	if (cs->hw.teles0.cfg_reg) {
		switch (cs->irq) {
			case 2:
				cfval = 0x00;
				break;
			case 3:
				cfval = 0x02;
				break;
			case 4:
				cfval = 0x04;
				break;
			case 5:
				cfval = 0x06;
				break;
			case 10:
				cfval = 0x08;
				break;
			case 11:
				cfval = 0x0A;
				break;
			case 12:
				cfval = 0x0C;
				break;
			case 15:
				cfval = 0x0E;
				break;
			default:
				cfval = 0x00;
				break;
		}
		cfval |= ((cs->hw.teles0.membase >> 9) & 0xF0);
		byteout(cs->hw.teles0.cfg_reg + 4, cfval);
		HZDELAY(HZ / 10 + 1);
		byteout(cs->hw.teles0.cfg_reg + 4, cfval | 1);
		HZDELAY(HZ / 10 + 1);
	}
	writeb(0, cs->hw.teles0.membase + 0x80);
	HZDELAY(HZ / 5 + 1);
	writeb(1, cs->hw.teles0.membase + 0x80);
	HZDELAY(HZ / 5 + 1);
	restore_flags(flags);
}

int
initteles0(struct IsdnCardState *cs)
{
	int ret;
	int loop, counter, cnt = 3;
	char tmp[40];

	counter = kstat.interrupts[cs->irq];
	sprintf(tmp, "IRQ %d count %d", cs->irq, counter);
	debugl1(cs, tmp);
	ret = get_irq(cs->cardnr, &telesS0_interrupt);
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
		sprintf(tmp, "IRQ %d count %d", cs->irq,
			kstat.interrupts[cs->irq]);
		debugl1(cs, tmp);
		if (kstat.interrupts[cs->irq] == counter) {
			printk(KERN_WARNING
			       "Teles0: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (!(--cnt)) {
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
setup_teles0(struct IsdnCard *card)
{
	u_char val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, teles0_revision);
	printk(KERN_NOTICE "HiSax: Teles 8.0/16.0 driver Rev. %s\n", HiSax_getrev(tmp));
	if ((cs->typ != ISDN_CTYPE_16_0) && (cs->typ != ISDN_CTYPE_8_0))
		return (0);

	if (cs->typ == ISDN_CTYPE_16_0)
		cs->hw.teles0.cfg_reg = card->para[2];
	else			/* 8.0 */
		cs->hw.teles0.cfg_reg = 0;

	if (card->para[1] < 0x10000) {
		card->para[1] <<= 4;
		printk(KERN_INFO
		   "Teles0: membase configured DOSish, assuming 0x%lx\n",
		       (unsigned long) card->para[1]);
	}
	cs->hw.teles0.membase = card->para[1];
	cs->irq = card->para[0];
	if (cs->hw.teles0.cfg_reg) {
		if (check_region((cs->hw.teles0.cfg_reg), 8)) {
			printk(KERN_WARNING
			  "HiSax: %s config port %x-%x already in use\n",
			       CardType[card->typ],
			       cs->hw.teles0.cfg_reg,
			       cs->hw.teles0.cfg_reg + 8);
			return (0);
		} else {
			request_region(cs->hw.teles0.cfg_reg, 8, "teles cfg");
		}
	}
	if (cs->hw.teles0.cfg_reg) {
		if ((val = bytein(cs->hw.teles0.cfg_reg + 0)) != 0x51) {
			printk(KERN_WARNING "Teles0: 16.0 Byte at %x is %x\n",
			       cs->hw.teles0.cfg_reg + 0, val);
			release_region(cs->hw.teles0.cfg_reg, 8);
			return (0);
		}
		if ((val = bytein(cs->hw.teles0.cfg_reg + 1)) != 0x93) {
			printk(KERN_WARNING "Teles0: 16.0 Byte at %x is %x\n",
			       cs->hw.teles0.cfg_reg + 1, val);
			release_region(cs->hw.teles0.cfg_reg, 8);
			return (0);
		}
		val = bytein(cs->hw.teles0.cfg_reg + 2);	/* 0x1e=without AB
								   * 0x1f=with AB
								   * 0x1c 16.3 ???
								 */
		if (val != 0x1e && val != 0x1f) {
			printk(KERN_WARNING "Teles0: 16.0 Byte at %x is %x\n",
			       cs->hw.teles0.cfg_reg + 2, val);
			release_region(cs->hw.teles0.cfg_reg, 8);
			return (0);
		}
	}
	/* 16.0 and 8.0 designed for IOM1 */
	test_and_set_bit(HW_IOM1, &cs->HW_Flags);
	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d mem:%x cfg:%x\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles0.membase, cs->hw.teles0.cfg_reg);
	reset_teles(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Teles_card_msg;
	ISACVersion(cs, "Teles0:");
	if (HscxVersion(cs, "Teles0:")) {
		printk(KERN_WARNING
		 "Teles0: wrong HSCX versions check IO/MEM addresses\n");
		release_io_teles0(card);
		return (0);
	}
	return (1);
}
