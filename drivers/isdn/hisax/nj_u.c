#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "icc.h"
#include "isdnl1.h"
#include <linux/pci.h>
#ifndef COMPAT_HAS_NEW_PCI
#include <linux/bios32.h>
#endif
#include <linux/interrupt.h>
#include <linux/ppp_defs.h>
#include "netjet.h"

const char *NETjet_U_revision = "$Revision$";

static u_char dummyrr(struct IsdnCardState *cs, int chan, u_char off)
{
	return(5);
}

static void dummywr(struct IsdnCardState *cs, int chan, u_char off, u_char value)
{
}

static void
netjet_u_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, sval;
	long flags;

	if (!cs) {
		printk(KERN_WARNING "NETjet-U: Spurious interrupt!\n");
		return;
	}
	if (!((sval = bytein(cs->hw.njet.base + NETJET_IRQSTAT1)) &
		NETJET_ISACIRQ)) {
		val = NETjet_ReadIC(cs, ICC_ISTA);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "tiger: i1 %x %x", sval, val);
		if (val) {
			icc_interrupt(cs, val);
			NETjet_WriteIC(cs, ICC_MASK, 0xFF);
			NETjet_WriteIC(cs, ICC_MASK, 0x0);
		}
	}
	save_flags(flags);
	cli();
	if ((sval = bytein(cs->hw.njet.base + NETJET_IRQSTAT0))) {
		if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			restore_flags(flags);
			return;
		}
		cs->hw.njet.irqstat0 = sval;
		restore_flags(flags);
/*		debugl1(cs, "tiger: ist0 %x  %x %x  %x/%x  pulse=%d",
			sval, 
			bytein(cs->hw.njet.base + NETJET_DMACTRL),
			bytein(cs->hw.njet.base + NETJET_IRQMASK0),
			inl(cs->hw.njet.base + NETJET_DMA_READ_ADR),
			inl(cs->hw.njet.base + NETJET_DMA_WRITE_ADR),
			bytein(cs->hw.njet.base + NETJET_PULSE_CNT));
*/
/*		cs->hw.njet.irqmask0 = ((0x0f & cs->hw.njet.irqstat0) ^ 0x0f) | 0x30;
*/		byteout(cs->hw.njet.base + NETJET_IRQSTAT0, cs->hw.njet.irqstat0);
/*		byteout(cs->hw.njet.base + NETJET_IRQMASK0, cs->hw.njet.irqmask0);
*/		if (cs->hw.njet.irqstat0 & NETJET_IRQM0_READ)
			read_tiger(cs);
		if (cs->hw.njet.irqstat0 & NETJET_IRQM0_WRITE)
			write_tiger(cs);
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	} else
		restore_flags(flags);

/*	if (!testcnt--) {
		cs->hw.njet.dmactrl = 0;
		byteout(cs->hw.njet.base + NETJET_DMACTRL,
			cs->hw.njet.dmactrl);
		byteout(cs->hw.njet.base + NETJET_IRQMASK0, 0);
	}
*/
}

static void
reset_netjet_u(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	sti();
	cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
	cs->hw.njet.ctrl_reg = 0x00;  /* Reset Off and status read clear */
	byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */
	restore_flags(flags);
	cs->hw.njet.auxd = 0xC0;
	cs->hw.njet.dmactrl = 0;
	byteout(cs->hw.njet.auxa, 0);
	byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
	byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
	byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
}

static int
NETjet_U_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_netjet_u(cs);
			return(0);
		case CARD_RELEASE:
			release_io_netjet(cs);
			return(0);
		case CARD_INIT:
			inittiger(cs);
			clear_pending_icc_ints(cs);
			initicc(cs);
			/* Reenable all IRQ */
			cs->writeisac(cs, ICC_MASK, 0);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

#ifdef COMPAT_HAS_NEW_PCI
static 	struct pci_dev *dev_netjet __initdata = NULL;
#else
static  int pci_index __initdata = 0;
#endif

__initfunc(int
setup_netjet_u(struct IsdnCard *card))
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	long flags;
#if CONFIG_PCI
#ifndef COMPAT_HAS_NEW_PCI
	u_char pci_bus, pci_device_fn, pci_irq;
	u_int pci_ioaddr, found;
#endif
#endif
	strcpy(tmp, NETjet_U_revision);
	printk(KERN_INFO "HiSax: Traverse Tech. NETjet-U driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_NETJET_U)
		return(0);
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);

	for ( ;; )
	{

#if CONFIG_PCI
#ifdef COMPAT_HAS_NEW_PCI

		if (!pci_present()) {
			printk(KERN_ERR "Netjet: no PCI bus present\n");
			return(0);
		}
		if ((dev_netjet = pci_find_device(PCI_VENDOR_TRAVERSE_TECH,
			PCI_NETJET_ID,  dev_netjet))) {
			cs->irq = dev_netjet->irq;
			if (!cs->irq) {
				printk(KERN_WARNING "NETjet-U: No IRQ for PCI card found\n");
				return(0);
			}
			cs->hw.njet.base = get_pcibase(dev_netjet, 0)
				& PCI_BASE_ADDRESS_IO_MASK; 
			if (!cs->hw.njet.base) {
				printk(KERN_WARNING "NETjet-U: No IO-Adr for PCI card found\n");
				return(0);
			}
		} else {
			printk(KERN_WARNING "NETjet-U: No PCI card found\n");
			return(0);
		}
#else
		found = 0;
		for (; pci_index < 0xff; pci_index++) {
			if (pcibios_find_device(PCI_VENDOR_TRAVERSE_TECH,
				PCI_NETJET_ID, pci_index, &pci_bus, &pci_device_fn)
				== PCIBIOS_SUCCESSFUL)
				found = 1;
			else
				continue;
			/* get IRQ */
			pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &pci_irq);

			/* get IO address */
			pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_0, &pci_ioaddr);
			if (found)
				break;
		}
		if (!found) {
			printk(KERN_WARNING "NETjet-U: No PCI card found\n");
			return(0);
		}
		pci_index++;
		if (!pci_irq) {
			printk(KERN_WARNING "NETjet-U: No IRQ for PCI card found\n");
			return(0);
		}
		if (!pci_ioaddr) {
			printk(KERN_WARNING "NETjet-U: No IO-Adr for PCI card found\n");
			return(0);
		}
		cs->hw.njet.base = pci_ioaddr & PCI_BASE_ADDRESS_IO_MASK; 
		cs->irq = pci_irq;

#endif /* COMPAT_HAS_NEW_PCI */

		cs->hw.njet.auxa = cs->hw.njet.base + NETJET_AUXDATA;
		cs->hw.njet.isac = cs->hw.njet.base | NETJET_ISAC_OFF;

		save_flags(flags);
		sti();

		cs->hw.njet.ctrl_reg = 0xff;  /* Reset On */
		byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */

		cs->hw.njet.ctrl_reg = 0x00;  /* Reset Off and status read clear */
		byteout(cs->hw.njet.base + NETJET_CTRL, cs->hw.njet.ctrl_reg);

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000);	/* Timeout 10ms */

		restore_flags(flags);

		cs->hw.njet.auxd = 0xC0;
		cs->hw.njet.dmactrl = 0;

		byteout(cs->hw.njet.auxa, 0);
		byteout(cs->hw.njet.base + NETJET_AUXCTRL, ~NETJET_ISACIRQ);
		byteout(cs->hw.njet.base + NETJET_IRQMASK1, NETJET_ISACIRQ);
		byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);

		switch ( ( ( NETjet_ReadIC( cs, ICC_RBCH ) >> 5 ) & 3 ) )
		{
			case 3 :
				break;

			case 0 :
				printk( KERN_WARNING "NETspider-U: NETjet-S PCI card found\n" );
				continue;

			default :
				printk( KERN_WARNING "NETspider-U: No PCI card found\n" );
				return 0;
                }
                break;
	}
#else

	printk(KERN_WARNING "NETjet-U: NO_PCI_BIOS\n");
	printk(KERN_WARNING "NETjet-U: unable to config NETJET-U PCI\n");
	return (0);

#endif /* CONFIG_PCI */

	bytecnt = 256;

	printk(KERN_INFO
		"NETjet-U: PCI card configured at 0x%x IRQ %d\n",
		cs->hw.njet.base, cs->irq);
	if (check_region(cs->hw.njet.base, bytecnt)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.njet.base,
		       cs->hw.njet.base + bytecnt);
		return (0);
	} else {
		request_region(cs->hw.njet.base, bytecnt, "netjet-u isdn");
	}
	reset_netjet_u(cs);
	cs->readisac  = &NETjet_ReadIC;
	cs->writeisac = &NETjet_WriteIC;
	cs->readisacfifo  = &NETjet_ReadICfifo;
	cs->writeisacfifo = &NETjet_WriteICfifo;
	cs->BC_Read_Reg  = &dummyrr;
	cs->BC_Write_Reg = &dummywr;
	cs->BC_Send_Data = &netjet_fill_dma;
	cs->cardmsg = &NETjet_U_card_msg;
	cs->irq_func = &netjet_u_interrupt;
	cs->irq_flags |= SA_SHIRQ;
	ICCVersion(cs, "NETjet-U:");
	return (1);
}
