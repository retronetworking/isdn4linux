/* $Id$

 * diva.c     low level stuff for Eicon.Diehl Diva Family ISDN cards
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to Eicon Technology Diehl GmbH & Co. oHG for documents and informations
 *
 *
 * $Log$
 * Revision 1.1  1997/09/18 17:11:20  keil
 * first version
 *
 *
 */

#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include "diva.h"

extern const char *CardType[];

const char *Diva_revision = "$Revision$";

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
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char *data, int size)
{
	/* fifo write without cli because it's allready done  */
	byteout(ale, off);
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return(readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	readfifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0, data, size);
}

static void 
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	writefifo(cs->hw.diva.isac_adr, cs->hw.diva.isac, 0, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return(readreg(cs->hw.diva.hscx_adr,
		cs->hw.diva.hscx, offset + (hscx ? 0x40 : 0)));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.diva.hscx_adr,
		cs->hw.diva.hscx, offset + (hscx ? 0x40 : 0), value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.diva.hscx_adr, \
		cs->hw.diva.hscx, reg + (nr ? 0x40 : 0))
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.diva.hscx_adr, \
                cs->hw.diva.hscx, reg + (nr ? 0x40 : 0), data)
                
#define READHSCXFIFO(cs, nr, ptr, cnt) readfifo(cs->hw.diva.hscx_adr, \
		cs->hw.diva.hscx, (nr ? 0x40 : 0), ptr, cnt)
		
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) writefifo(cs->hw.diva.hscx_adr, \
		cs->hw.diva.hscx, (nr ? 0x40 : 0), ptr, cnt)
		
#include "hscx_irq.c"

static void
diva_interrupt(int intno, void *para, struct pt_regs *regs)
{
	struct IsdnCardState *cs = para;
	u_char val, sval, stat = 0;
	int cnt=8;

	if (!cs) {
		printk(KERN_WARNING "Diva: Spurious interrupt!\n");
		return;
	}
	while (((sval = bytein(cs->hw.diva.ctrl)) & DIVA_IRQ_REQ) && cnt) {
		val = readreg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_ISTA + 0x40);
		if (val) {
			hscx_int_main(cs, val);
			stat |= 1;
		}
		val = readreg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_ISTA);
		if (val) {
			isac_interrupt(cs, val);
			stat |= 2;
		}
		cnt--;
	}
	if (!cnt)
		printk(KERN_WARNING "Diva: IRQ LOOP\n");
	if (stat & 1) {
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK, 0xFF);
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK + 0x40, 0xFF);
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK, 0x0);
		writereg(cs->hw.diva.hscx_adr, cs->hw.diva.hscx, HSCX_MASK + 0x40, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.diva.isac_adr, cs->hw.diva.isac, ISAC_MASK, 0x0);
	}
}

void
release_io_diva(struct IsdnCard *card)
{
	int bytecnt;
	
	del_timer(&card->cs->hw.diva.tl);
	if (card->cs->subtyp == DIVA_ISA)
		bytecnt = 8;
	else
		bytecnt = 32;
	if (card->cs->hw.diva.cfg_reg) {
		byteout(card->cs->hw.diva.ctrl, 0); /* LED off, Reset */
		release_region(card->cs->hw.diva.cfg_reg, bytecnt);
	}
}

static void
reset_diva(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	sti();
	cs->hw.diva.ctrl_reg = 0;        /* Reset On */
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
	schedule();
	cs->hw.diva.ctrl_reg |= DIVA_RESET;  /* Reset Off */
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
	schedule();
	if (cs->subtyp == DIVA_ISA)
		cs->hw.diva.ctrl_reg |= DIVA_ISA_LED_A;
	else
		cs->hw.diva.ctrl_reg |= DIVA_PCI_LED_A;
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
}

int
initdiva(struct IsdnCardState *cs)
{
	int ret, irq_cnt, cnt = 3;
	long flags;

	irq_cnt = kstat.interrupts[cs->irq];
	printk(KERN_INFO "Diva: IRQ %d count %d\n", cs->irq, irq_cnt);
	ret = get_irq(cs->cardnr, &diva_interrupt);
	while (ret && cnt) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
		save_flags(flags);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + (20 * HZ) / 1000;		/* Timeout 110ms */
		schedule();
		restore_flags(flags);

		printk(KERN_INFO "Diva: IRQ %d count %d\n", cs->irq,
		       kstat.interrupts[cs->irq]);
		if (kstat.interrupts[cs->irq] == irq_cnt) {
			printk(KERN_WARNING
			       "Diva: IRQ(%d) getting no interrupts during init %d\n",
			       cs->irq, 4 - cnt);
			if (cnt == 1) {
				free_irq(cs->irq, NULL);
				return (0);
			} else {
				reset_diva(cs);
				cnt--;
			}
		} else {
			cnt = 0;
		}
	}
	return (ret);
}

#define DIVA_ASSIGN 1

static void
diva_led_handler(struct IsdnCardState *cs)
{
	int blink = 0;

	del_timer(&cs->hw.diva.tl);
	if (cs->hw.diva.status & DIVA_ASSIGN)
		cs->hw.diva.ctrl_reg |= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_A : DIVA_PCI_LED_A;
	else {
		cs->hw.diva.ctrl_reg ^= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_A : DIVA_PCI_LED_A;
		blink = 250;
	}
	if (cs->hw.diva.status & 0xf000)
		cs->hw.diva.ctrl_reg |= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B;
	else if (cs->hw.diva.status & 0x0f00) { 
		cs->hw.diva.ctrl_reg ^= (DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B;
		blink = 500;
	} else
		cs->hw.diva.ctrl_reg &= ~((DIVA_ISA == cs->subtyp) ?
			DIVA_ISA_LED_B : DIVA_PCI_LED_B);
	
	byteout(cs->hw.diva.ctrl, cs->hw.diva.ctrl_reg);
	if (blink) {
		init_timer(&cs->hw.diva.tl);
		cs->hw.diva.tl.expires = jiffies + ((blink * HZ) / 1000);
		add_timer(&cs->hw.diva.tl);
	}
}

static void
Diva_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
	  case MDL_REMOVE_REQ:
	  	cs->hw.diva.status = 0;
		break;
	  case MDL_ASSIGN_REQ:
		cs->hw.diva.status |= DIVA_ASSIGN;
		break;
	  case MDL_INFO_SETUP:
	  	if ((int)arg) 
			cs->hw.diva.status |=  0x0200;
		else
			cs->hw.diva.status |=  0x0100;
		break;
	  case MDL_INFO_CONN:
	  	if ((int)arg) 
			cs->hw.diva.status |=  0x2000;
		else
			cs->hw.diva.status |=  0x1000;
		break;
	  case MDL_INFO_REL:
	  	if ((int)arg) {
			cs->hw.diva.status &=  ~0x2000;
			cs->hw.diva.status &=  ~0x0200;
		} else {
			cs->hw.diva.status &=  ~0x1000;
			cs->hw.diva.status &=  ~0x0100;
		}
		break;
	}
	diva_led_handler(cs);
}



static 	int pci_index = 0;

int
setup_diva(struct IsdnCard *card)
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Diva_revision);
	printk(KERN_NOTICE "HiSax: Eicon.Diehl Diva driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_DIEHLDIVA)
		return(0);
	cs->hw.diva.status = 0;
	if (card->para[1]) {
		cs->subtyp = DIVA_ISA;
		cs->hw.diva.ctrl_reg = 0;
		cs->hw.diva.cfg_reg = card->para[1];
		cs->hw.diva.ctrl = card->para[1] + DIVA_ISA_CTRL;
		cs->hw.diva.isac = card->para[1] + DIVA_ISA_ISAC_DATA;
		cs->hw.diva.hscx = card->para[1] + DIVA_HSCX_DATA;
		cs->hw.diva.isac_adr = card->para[1] + DIVA_ISA_ISAC_ADR;
		cs->hw.diva.hscx_adr = card->para[1] + DIVA_HSCX_ADR;
		cs->irq = card->para[0];
		bytecnt = 8;
	} else {
#if CONFIG_PCI
		u_char pci_bus, pci_device_fn, pci_irq;
		u_int pci_ioaddr;

		cs->subtyp = 0;
		for (; pci_index < 0xff; pci_index++) {
			if (pcibios_find_device(PCI_VENDOR_EICON_DIEHL,
			   PCI_DIVA20_ID, pci_index, &pci_bus, &pci_device_fn)
			   == PCIBIOS_SUCCESSFUL)
				cs->subtyp = DIVA_PCI;
			else if (pcibios_find_device(PCI_VENDOR_EICON_DIEHL,
			   PCI_DIVA20_ID, pci_index, &pci_bus, &pci_device_fn)
			   == PCIBIOS_SUCCESSFUL)
			   	cs->subtyp = DIVA_PCI;
			else
				break;
			/* get IRQ */
			pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &pci_irq);

			/* get IO address */
			pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_2, &pci_ioaddr);
			if (cs->subtyp)
				break;
		}
		if (!cs->subtyp) {
			printk(KERN_WARNING "Diva: No PCI card found\n");
			return(0);
		}
		if (!pci_irq) {
			printk(KERN_WARNING "Diva: No IRQ for PCI card found\n");
			return(0);
		}

		if (!pci_ioaddr) {
			printk(KERN_WARNING "Diva: No IO-Adr for PCI card found\n");
			return(0);
		}
		pci_ioaddr &= ~3; /* remove io/mem flag */
		cs->hw.diva.cfg_reg = pci_ioaddr; 
		cs->hw.diva.ctrl = pci_ioaddr + DIVA_PCI_CTRL;
		cs->hw.diva.isac = pci_ioaddr + DIVA_PCI_ISAC_DATA;
		cs->hw.diva.hscx = pci_ioaddr + DIVA_HSCX_DATA;
		cs->hw.diva.isac_adr = pci_ioaddr + DIVA_PCI_ISAC_ADR;
		cs->hw.diva.hscx_adr = pci_ioaddr + DIVA_HSCX_ADR;
		cs->irq = pci_irq;
		bytecnt = 32;
#else
		printk(KERN_WARNING "Diva: cfgreg 0 and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Diva: unable to config DIVA PCI\n");
		return (0);
#endif /* CONFIG_PCI */
	}

	printk(KERN_INFO
		"Diva: %s card configured at 0x%x IRQ %d\n",
		(cs->subtyp == DIVA_ISA) ? "ISA" : "PCI",
		cs->hw.diva.cfg_reg, cs->irq);
	if (check_region(cs->hw.diva.cfg_reg, bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.diva.cfg_reg,
		       cs->hw.diva.cfg_reg + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.diva.cfg_reg, bytecnt, "diva isdn");
	}

	reset_diva(cs);
	cs->hw.diva.tl.function = (void *) diva_led_handler;
	cs->hw.diva.tl.data = (long) cs;
	init_timer(&cs->hw.diva.tl);
	cs->readisac  = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo  = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg  = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Diva_card_msg;

	ISACVersion(cs, "Diva:");
	if (HscxVersion(cs, "Diva:")) {
		printk(KERN_WARNING
		       "Diva: wrong HSCX versions check IO address\n");
		release_io_diva(card);
		return (0);
	}
	return (1);
}
