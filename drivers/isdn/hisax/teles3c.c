/* $Id$

 * teles3c.c     low level stuff for teles 16.3c
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "hfc_2bds0.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *teles163c_revision = "$Revision$";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static void
dummyf(struct IsdnCardState *cs, u_char * data, int size)
{
}

static inline u_char
ReadReg(struct IsdnCardState *cs, int data, u_char reg)
{
	register u_char ret;

	if (data) {
		cs->hw.hfcD.cip = reg;
		byteout(cs->hw.hfcD.addr | 1, reg);
		ret = bytein(cs->hw.hfcD.addr);
		if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2)) {
			char tmp[32];
			sprintf(tmp, "t3c RD %02x %02x", reg, ret);
			debugl1(cs, tmp);
		}
	} else
		ret = bytein(cs->hw.hfcD.addr | 1);
	return (ret);
}

static inline void
WriteReg(struct IsdnCardState *cs, int data, u_char reg, u_char value)
{
	byteout(cs->hw.hfcD.addr | 1, reg);
	cs->hw.hfcD.cip = reg;
	if (data)
		byteout(cs->hw.hfcD.addr, value);
	if (cs->debug & L1_DEB_HSCX_FIFO && (data != HFCD_DATA_NODEB)) {
		char tmp[32];
		sprintf(tmp, "t3c W%c %02x %02x", data ? 'D' : 'C', reg, value);
		debugl1(cs, tmp);
	}
}

/* Interface functions */

static u_char
readreghfcd(struct IsdnCardState *cs, u_char offset)
{
	u_char val;
	val = ReadReg(cs, HFCD_DATA, offset);
	return(val);
}

static void
writereghfcd(struct IsdnCardState *cs, u_char offset, u_char value)
{
	WriteReg(cs, HFCD_DATA, offset, value);
}

static void
t163c_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, stat;
	char tmp[32];

	if (!cs) {
		printk(KERN_WARNING "teles3c: Spurious interrupt!\n");
		return;
	}
	if ((HFCD_ANYINT | HFCD_BUSY_NBUSY) & 
		(stat = ReadReg(cs, HFCD_DATA, HFCD_STAT))) {
		val = ReadReg(cs, HFCD_DATA, HFCD_INT_S1);
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "teles3c: stat(%02x) s1(%02x)", stat, val);
			debugl1(cs, tmp);
		}
		hfc2bds0_interrupt(cs, val);
	} else {
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "teles3c: irq_no_irq stat(%02x)", stat);
			debugl1(cs, tmp);
		}
	}
}

static void
t163c_Timer(struct IsdnCardState *cs)
{
	cs->hw.hfcD.timer.expires = jiffies + 75;
	/* WD RESET */
/*	WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt | 0x80);
	add_timer(&cs->hw.hfcD.timer);
*/
}

void
release_io_t163c(struct IsdnCardState *cs)
{
	release2bds0(cs);
	del_timer(&cs->hw.hfcD.timer);
	if (cs->hw.hfcD.addr)
		release_region(cs->hw.hfcD.addr, 2);
}

static void
reset_t163c(struct IsdnCardState *cs)
{
	long flags;

	printk(KERN_INFO "teles3c: resetting card\n");
	cs->hw.hfcD.cirm = HFCD_RESET | HFCD_MEM8K;
	WriteReg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* Reset On */
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 3;
	schedule();
	cs->hw.hfcD.cirm = HFCD_MEM8K;
	WriteReg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* Reset Off */
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 1;
	schedule();
	cs->hw.hfcD.cirm |= HFCD_INTB;
	WriteReg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* INT B */
	WriteReg(cs, HFCD_DATA, HFCD_CLKDEL, 0x0e);
	WriteReg(cs, HFCD_DATA, HFCD_TEST, HFCD_AUTO_AWAKE); /* S/T Auto awake */
	cs->hw.hfcD.ctmt = HFCD_TIM25 | HFCD_AUTO_TIMER;
	WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt);
	cs->hw.hfcD.int_m2 = HFCD_IRQ_ENABLE;
	cs->hw.hfcD.int_m1 = 0xff;
	WriteReg(cs, HFCD_DATA, HFCD_INT_M1, cs->hw.hfcD.int_m1);
	WriteReg(cs, HFCD_DATA, HFCD_INT_M2, cs->hw.hfcD.int_m2);
	WriteReg(cs, HFCD_DATA, HFCD_STATES, HFCD_LOAD_STATE | 2); /* HFC ST 2 */
	udelay(10);
	WriteReg(cs, HFCD_DATA, HFCD_STATES, 2); /* HFC ST 2 */
	cs->hw.hfcD.mst_m = 0;
	WriteReg(cs, HFCD_DATA, HFCD_MST_MODE, HFCD_MASTER); /* HFC Master */
	cs->hw.hfcD.sctrl = 0;
	WriteReg(cs, HFCD_DATA, HFCD_SCTRL, cs->hw.hfcD.sctrl);
	restore_flags(flags);
}

static int
t163c_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	long flags;
	u_char reg,val;
	char tmp[32];

	if (cs->debug & L1_DEB_ISAC) {
		
		sprintf(tmp, "teles3c: card_msg %x", mt);
		debugl1(cs, tmp);
	}
	switch (mt) {
		case CARD_RESET:
			reset_t163c(cs);
			return(0);
		case CARD_RELEASE:
			release_io_t163c(cs);
			return(0);
		case CARD_SETIRQ:
			cs->hw.hfcD.timer.expires = jiffies + 75;
			add_timer(&cs->hw.hfcD.timer);
			return(request_irq(cs->irq, &t163c_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			init2bds0(cs);
			save_flags(flags);
			sti();
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + (80*HZ)/1000;
			schedule();
			cs->hw.hfcD.ctmt |= HFCD_TIM800;
			WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt); 
			WriteReg(cs, HFCD_DATA, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			restore_flags(flags);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

__initfunc(int
setup_t163c(struct IsdnCard *card))
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, teles163c_revision);
	printk(KERN_INFO "HiSax: Teles 16.3c driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_TELES3C)
		return (0);
	cs->debug = 0xff;
	cs->hw.hfcD.addr = card->para[1] & 0xfffe;
	cs->irq = card->para[0];
	cs->hw.hfcD.cip = 0;
	cs->hw.hfcD.send = NULL;
	cs->bcs[0].hw.hfc.send = NULL;
	cs->bcs[1].hw.hfc.send = NULL;
	cs->hw.hfcD.bfifosize = 1024 + 512;
	cs->hw.hfcD.dfifosize = 512;
	cs->ph_state = 0;
	if (check_region((cs->hw.hfcD.addr), 2)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.hfcD.addr,
		       cs->hw.hfcD.addr + 2);
		return (0);
	} else {
		request_region(cs->hw.hfcD.addr, 2, "teles3c isdn");
	}
	/* Teles 16.3c IO ADR is 0x200 | YY0U (YY Bit 15/14 address) */
	byteout(cs->hw.hfcD.addr, 0x0);
	byteout(cs->hw.hfcD.addr | 1, 0x56);
	printk(KERN_INFO
	       "teles3c: defined at 0x%x IRQ %d HZ %d\n",
	       cs->hw.hfcD.addr,
	       cs->irq, HZ);

	reset_t163c(cs);
	cs->hw.hfcD.timer.function = (void *) t163c_Timer;
	cs->hw.hfcD.timer.data = (long) cs;
	init_timer(&cs->hw.hfcD.timer);
	cs->readisac = &readreghfcd;
	cs->writeisac = &writereghfcd;
	cs->readisacfifo = &dummyf;
	cs->writeisacfifo = &dummyf;
	cs->BC_Read_Reg = &ReadReg;
	cs->BC_Write_Reg = &WriteReg;
	cs->cardmsg = &t163c_card_msg;
	return (1);
}
