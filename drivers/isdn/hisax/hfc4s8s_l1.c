/*************************************************************************/
/* $Id$        */
/* HFC-4S/8S low layer interface for Colognechip HFC-4S/8S isdn chips    */
/* The low layer (L1) is implemented as a loadable module for usage with */
/* the HiSax isdn driver for passive cards.                              */
/*                                                                       */
/* Author: Werner Cornelius                                              */
/* (C) 2003 Cornelius Consult (werner@cornelius-consult.de)              */
/*                                                                       */
/* This driver only works with chip revisions >= 1, older revision 0     */
/* engineering samples (only first manufacturer sample cards) will not   */
/* work and are rejected by the driver.                                  */
/*                                                                       */
/* This file distributed under the GNU GPL.                              */
/*                                                                       */
/* See Version History at the end of this file                           */
/*                                                                       */
/*************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include "hisax_if.h"
#include "hfc4s8s_l1.h"

static const char hfc4s8s_rev[] = "$Revision$";

/***************************************************************/
/* adjustable transparent mode fifo threshold                  */
/* The value defines the used fifo threshold with the equation */
/*                                                             */
/* notify number of bytes = 2 * 2 ^ TRANS_FIFO_THRES           */
/*                                                             */
/* The default value is 5 which results in a buffer size of 64 */
/* and an interrupt rate of 8ms.                               */
/* The maximum value is 7 due to fifo size restrictions.       */
/* Values below 3-4 are not recommended due to high interrupt  */
/* load of the processor. For non critical applications the    */
/* value should be raised to 7 to reduce any interrupt overhead*/
/***************************************************************/
#define TRANS_FIFO_THRES 5

/*************/
/* constants */
/*************/
#define MAX_HFC8_CARDS 4
#define HFC_MAX_ST 8
#define HFC_MAX_CHANNELS 32
#define MAX_D_FRAME_SIZE 270
#define MAX_B_FRAME_SIZE 1536
#define TRANS_TIMER_MODE (TRANS_FIFO_THRES & 0xf)
#define TRANS_FIFO_BYTES (2 << TRANS_FIFO_THRES)
#define MAX_F_CNT 0x0f

/******************/
/* types and vars */
/******************/
struct hfc4s8s_hw;

/* table entry in the PCI devices list */
typedef struct {
	int vendor_id;
	int device_id;
	int chip_id;
	int sub_vendor_id;
	int sub_device_id;
	char *vendor_name;
	char *card_name;
	int max_channels;
	int clock_mode;
} PCI_ENTRY;

/* static list of available card types */
static const PCI_ENTRY __devinitdata id_list[] = {
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_4S, CHIP_ID_4S, 0x1397, 0x8b4,
	 "Cologne Chip", "HFC-4S Eval", 4, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_8S, CHIP_ID_8S, 0x1397, 0x16b8,
	 "Cologne Chip", "HFC-8S Eval", 8, 0},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_4S, CHIP_ID_4S, 0x1397, 0xb520,
	 "Cologne Chip", "IOB4ST", 4, 1},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_8S, CHIP_ID_8S, 0x1397, 0xb522,
	 "Cologne Chip", "IOB8ST", 8, 1},
	{0, 0, 0, 0, 0, (char *) NULL, (char *) NULL, 0, 0},
};


/***********/
/* layer 1 */
/***********/
struct hfc4s8s_btype {
	struct hisax_b_if b_if;
	struct hfc4s8s_l1 *l1p;
	struct sk_buff_head tx_queue;
	struct sk_buff *tx_skb;
	struct sk_buff *rx_skb;
	__u8 *rx_ptr;
	int tx_cnt;
	int bchan;
	int mode;
};

struct hfc4s8s_l1 {
	struct hfc4s8s_hw *hw;	// pointer to hardware area
	int l1_state;		// actual l1 state
	struct timer_list l1_timer;	// layer 1 timer structure
	int nt_mode;		// set to nt mode
	int st_num;		// own index
	int enabled;		// interface is enabled
	struct sk_buff_head d_tx_queue;	// send queue
	int tx_cnt;		// bytes to send
	struct hisax_d_if d_if;	// D-channel interface
	struct hfc4s8s_btype b_ch[2];	// B-channel data
	struct hisax_b_if *b_table[2];
};

/**********************/
/* hardware structure */
/**********************/
struct hfc4s8s_hw {
	int ifnum;
	int iobase;
	int nt_mode;
	u_char *membase;
	u_char *hw_membase;
	u_char pci_bus;
	u_char pci_dev_fn;
	int max_st_ports;
	int max_fifo;
	int clock_mode;
	int irq;
	int fifo_sched_cnt;
	struct tq_struct tqueue;
	struct hfc4s8s_l1 l1[HFC_MAX_ST];
	char card_name[60];
	struct {
		u_char r_irq_ctrl;
		u_char r_ctrl0;
		volatile u_char r_irq_statech;	// active isdn l1 status
		u_char r_irqmsk_statchg;	// enabled isdn status ints
		u_char r_irq_fifo_blx[8];	// fifo status registers
		u_char fifo_rx_trans_enables[8];	// mask for enabled transparent rx fifos
		u_char fifo_slow_timer_service[8];	// mask for fifos needing slower timer service
		volatile u_char r_irq_oview;	// contents of overview register
		volatile u_char timer_irq;
		int timer_usg_cnt;	// number of channels using timer
	} mr;
};


/*************/
/* constants */
/*************/
#define CLKDEL_NT 0x6c
#define CLKDEL_TE 0xf
#define CTRL0_NT  4
#define CTRL0_TE  0
#define CHIP_ID_SHIFT	4

#define L1_TIMER_T4 2		// minimum in jiffies
#define L1_TIMER_T3 (7 * HZ)	// activation timeout
#define L1_TIMER_T1 ((120 * HZ) / 1000)	// NT mode deactivation timeout

/***************************/
/* inline function defines */
/***************************/
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM	/* inline functions mempry mapped */

/* memory write and dummy IO read to avoid PCI byte merge problems */
#define Write_hfc8(a,b,c) {(*((volatile u_char *)(a->membase+b)) = c); inb(a->iobase+4);}
/* memory write without dummy IO access for fifo data access */
#define fWrite_hfc8(a,b,c) (*((volatile u_char *)(a->membase+b)) = c)
#define Read_hfc8(a,b) (*((volatile u_char *)(a->membase+b)))
#define Write_hfc16(a,b,c) (*((volatile unsigned short *)(a->membase+b)) = c)
#define Read_hfc16(a,b) (*((volatile unsigned short *)(a->membase+b)))
#define Write_hfc32(a,b,c) (*((volatile unsigned long *)(a->membase+b)) = c)
#define Read_hfc32(a,b) (*((volatile unsigned long *)(a->membase+b)))
#define wait_busy(a) {while ((Read_hfc8(a, R_STATUS) & M_BUSY));}
#define PCI_ENA_MEMIO	0x03

#else

/* inline functions io mapped */
static inline void
SetRegAddr(struct hfc4s8s_hw *a, u_char b)
{
	outb(b, (a->iobase) + 4);
}

static inline u_char
GetRegAddr(struct hfc4s8s_hw *a)
{
	return (inb((volatile u_int) (a->iobase + 4)));
}


static inline void
Write_hfc8(struct hfc4s8s_hw *a, u_char b, u_char c)
{
	SetRegAddr(a, b);
	outb(c, a->iobase);
}

static inline void
fWrite_hfc8(struct hfc4s8s_hw *a, u_char c)
{
	outb(c, a->iobase);
}

static inline void
Write_hfc16(struct hfc4s8s_hw *a, u_char b, u_short c)
{
	SetRegAddr(a, b);
	outw(c, a->iobase);
}

static inline void
Write_hfc32(struct hfc4s8s_hw *a, u_char b, u_long c)
{
	SetRegAddr(a, b);
	outl(c, a->iobase);
}

static inline void
fWrite_hfc32(struct hfc4s8s_hw *a, u_long c)
{
	outl(c, a->iobase);
}

static inline u_char
Read_hfc8(struct hfc4s8s_hw *a, u_char b)
{
	SetRegAddr(a, b);
	return (inb((volatile u_int) a->iobase));
}

static inline u_char
fRead_hfc8(struct hfc4s8s_hw *a)
{
	return (inb((volatile u_int) a->iobase));
}


static inline u_short
Read_hfc16(struct hfc4s8s_hw *a, u_char b)
{
	SetRegAddr(a, b);
	return (inw((volatile u_int) a->iobase));
}

static inline u_long
Read_hfc32(struct hfc4s8s_hw *a, u_char b)
{
	SetRegAddr(a, b);
	return (inl((volatile u_int) a->iobase));
}

static inline u_long
fRead_hfc32(struct hfc4s8s_hw *a)
{
	return (inl((volatile u_int) a->iobase));
}

static inline void
wait_busy(struct hfc4s8s_hw *a)
{
	SetRegAddr(a, R_STATUS);
	while (inb((volatile u_int) a->iobase) & M_BUSY);
}

#define PCI_ENA_REGIO	0x01

#endif				/* CONFIG_HISAX_HFC4S8S_PCIMEM */


/*******************************************************************************************/
/* function to read critical counter registers that may be udpated by the chip during read */
/*******************************************************************************************/
static volatile u_char
Read_hfc8_stable(struct hfc4s8s_hw *hw, int reg)
{
	u_char ref8;
	u_char in8;
	ref8 = Read_hfc8(hw, reg);
	while (((in8 = Read_hfc8(hw, reg)) != ref8)) {
		ref8 = in8;
	}
	return in8;
}

static volatile int
Read_hfc16_stable(struct hfc4s8s_hw *hw, int reg)
{
	int ref16;
	int in16;

	ref16 = Read_hfc16(hw, reg);
	while (((in16 = Read_hfc16(hw, reg)) != ref16)) {
		ref16 = in16;
	}
	return in16;
}


/*************/
/* data area */
/*************/
static struct hfc4s8s_hw hfc4s8s[MAX_HFC8_CARDS];
static int card_cnt;

/*****************************/
/* D-channel call from HiSax */
/*****************************/
static void
dch_l2l1(struct hisax_d_if *iface, int pr, void *arg)
{
	struct hfc4s8s_l1 *l1 = iface->ifc.priv;
	struct sk_buff *skb = (struct sk_buff *) arg;
	long flags;

	switch (pr) {

		case (PH_DATA | REQUEST):
			if (!l1->enabled) {
				dev_kfree_skb(skb);
				break;
			}
			skb_queue_tail(&l1->d_tx_queue, skb);
			save_flags(flags);
			cli();
			if ((skb_queue_len(&l1->d_tx_queue) == 1) &&
			    (l1->tx_cnt <= 0)) {
				l1->hw->mr.r_irq_fifo_blx[l1->st_num] |=
				    0x10;
				restore_flags(flags);
				queue_task(&l1->hw->tqueue, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
			} else
				restore_flags(flags);
			break;

		case (PH_ACTIVATE | REQUEST):
			if (!l1->enabled)
				break;
			if (!l1->nt_mode) {
				if (l1->l1_state < 6) {
					save_flags(flags);
					cli();

					Write_hfc8(l1->hw, R_ST_SEL,
						   l1->st_num);
					Write_hfc8(l1->hw, A_ST_WR_STA,
						   0x60);
					mod_timer(&l1->l1_timer,
						  jiffies + L1_TIMER_T3);
					restore_flags(flags);
				} else if (l1->l1_state == 7)
					l1->d_if.ifc.l1l2(&l1->d_if.ifc,
							  PH_ACTIVATE |
							  INDICATION,
							  NULL);
			} else {
				if (l1->l1_state != 3) {
					save_flags(flags);
					cli();
					Write_hfc8(l1->hw, R_ST_SEL,
						   l1->st_num);
					Write_hfc8(l1->hw, A_ST_WR_STA,
						   0x60);
					restore_flags(flags);
				} else if (l1->l1_state == 3)
					l1->d_if.ifc.l1l2(&l1->d_if.ifc,
							  PH_ACTIVATE |
							  INDICATION,
							  NULL);
			}
			break;

		default:
			printk(KERN_INFO
			       "HFC-4S/8S: Unknown D-chan cmd 0x%x received, ignored\n",
			       pr);
			break;
	}
	if (!l1->enabled)
		l1->d_if.ifc.l1l2(&l1->d_if.ifc,
				  PH_DEACTIVATE | INDICATION, NULL);
}				/* dch_l2l1 */

/*****************************/
/* B-channel call from HiSax */
/*****************************/
static void
bch_l2l1(struct hisax_if *ifc, int pr, void *arg)
{
	struct hfc4s8s_btype *bch = ifc->priv;
	struct hfc4s8s_l1 *l1 = bch->l1p;
	struct sk_buff *skb = (struct sk_buff *) arg;
	int mode = (int) arg;
	long flags;

	switch (pr) {

		case (PH_DATA | REQUEST):
			if (!l1->enabled || (bch->mode == L1_MODE_NULL)) {
				dev_kfree_skb(skb);
				break;
			}
			skb_queue_tail(&bch->tx_queue, skb);
			save_flags(flags);
			cli();
			if (!bch->tx_skb && (bch->tx_cnt <= 0)) {
				l1->hw->mr.r_irq_fifo_blx[l1->st_num] |=
				    ((bch->bchan == 1) ? 1 : 4);
				restore_flags(flags);
				queue_task(&l1->hw->tqueue, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
			} else
				restore_flags(flags);
			break;

		case (PH_ACTIVATE | REQUEST):
		case (PH_DEACTIVATE | REQUEST):
			if (!l1->enabled)
				break;
			if (pr == (PH_DEACTIVATE | REQUEST))
				mode = L1_MODE_NULL;

			switch (mode) {
				case L1_MODE_HDLC:
					save_flags(flags);
					cli();
					l1->hw->mr.timer_usg_cnt++;
					l1->hw->mr.
					    fifo_slow_timer_service[l1->
								    st_num]
					    |=
					    ((bch->bchan ==
					      1) ? 0x2 : 0x8);
					Write_hfc8(l1->hw, R_FIFO,
						   (l1->st_num * 8 +
						    ((bch->bchan ==
						      1) ? 0 : 2)));
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, A_CON_HDLC, 0xc);	// HDLC mode, flag fill, connect ST
					Write_hfc8(l1->hw, A_SUBCH_CFG, 0);	// 8 bits
					Write_hfc8(l1->hw, A_IRQ_MSK, 1);	// enable TX interrupts for hdlc
					Write_hfc8(l1->hw, A_INC_RES_FIFO, 2);	// reset fifo
					wait_busy(l1->hw);

					Write_hfc8(l1->hw, R_FIFO,
						   (l1->st_num * 8 +
						    ((bch->bchan ==
						      1) ? 1 : 3)));
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, A_CON_HDLC, 0xc);	// HDLC mode, flag fill, connect ST
					Write_hfc8(l1->hw, A_SUBCH_CFG, 0);	// 8 bits
					Write_hfc8(l1->hw, A_IRQ_MSK, 1);	// enable RX interrupts for hdlc
					Write_hfc8(l1->hw, A_INC_RES_FIFO, 2);	// reset fifo

					Write_hfc8(l1->hw, R_ST_SEL,
						   l1->st_num);
					l1->hw->mr.r_ctrl0 |=
					    (bch->bchan & 3);
					Write_hfc8(l1->hw, A_ST_CTRL0,
						   l1->hw->mr.r_ctrl0);
					bch->mode = L1_MODE_HDLC;
					restore_flags(flags);

					MOD_INC_USE_COUNT;

					bch->b_if.ifc.l1l2(&bch->b_if.ifc,
							   PH_ACTIVATE |
							   INDICATION,
							   NULL);
					break;

				case L1_MODE_TRANS:
					save_flags(flags);
					cli();
					l1->hw->mr.
					    fifo_rx_trans_enables[l1->
								  st_num]
					    |=
					    ((bch->bchan ==
					      1) ? 0x2 : 0x8);
					l1->hw->mr.timer_usg_cnt++;
					Write_hfc8(l1->hw, R_FIFO,
						   (l1->st_num * 8 +
						    ((bch->bchan ==
						      1) ? 0 : 2)));
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, A_CON_HDLC, 0xf);	// Transparent mode, 1 fill, connect ST
					Write_hfc8(l1->hw, A_SUBCH_CFG, 0);	// 8 bits
					Write_hfc8(l1->hw, A_IRQ_MSK, 0);	// disable TX interrupts
					Write_hfc8(l1->hw, A_INC_RES_FIFO, 2);	// reset fifo
					wait_busy(l1->hw);

					Write_hfc8(l1->hw, R_FIFO,
						   (l1->st_num * 8 +
						    ((bch->bchan ==
						      1) ? 1 : 3)));
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, A_CON_HDLC, 0xf);	// Transparent mode, 1 fill, connect ST
					Write_hfc8(l1->hw, A_SUBCH_CFG, 0);	// 8 bits
					Write_hfc8(l1->hw, A_IRQ_MSK, 0);	// disable RX interrupts
					Write_hfc8(l1->hw, A_INC_RES_FIFO, 2);	// reset fifo

					Write_hfc8(l1->hw, R_ST_SEL,
						   l1->st_num);
					l1->hw->mr.r_ctrl0 |=
					    (bch->bchan & 3);
					Write_hfc8(l1->hw, A_ST_CTRL0,
						   l1->hw->mr.r_ctrl0);
					bch->mode = L1_MODE_TRANS;
					restore_flags(flags);

					MOD_INC_USE_COUNT;

					bch->b_if.ifc.l1l2(&bch->b_if.ifc,
							   PH_ACTIVATE |
							   INDICATION,
							   NULL);
					break;

				default:
					if (bch->mode == L1_MODE_NULL)
						break;
					save_flags(flags);
					cli();
					l1->hw->mr.
					    fifo_slow_timer_service[l1->
								    st_num]
					    &=
					    ~((bch->bchan ==
					       1) ? 0x3 : 0xc);
					l1->hw->mr.
					    fifo_rx_trans_enables[l1->
								  st_num]
					    &=
					    ~((bch->bchan ==
					       1) ? 0x3 : 0xc);
					l1->hw->mr.timer_usg_cnt--;
					Write_hfc8(l1->hw, R_FIFO,
						   (l1->st_num * 8 +
						    ((bch->bchan ==
						      1) ? 0 : 2)));
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, A_IRQ_MSK, 0);	// disable TX interrupts
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, R_FIFO,
						   (l1->st_num * 8 +
						    ((bch->bchan ==
						      1) ? 1 : 3)));
					wait_busy(l1->hw);
					Write_hfc8(l1->hw, A_IRQ_MSK, 0);	// disable RX interrupts
					Write_hfc8(l1->hw, R_ST_SEL,
						   l1->st_num);
					l1->hw->mr.r_ctrl0 &=
					    ~(bch->bchan & 3);
					Write_hfc8(l1->hw, A_ST_CTRL0,
						   l1->hw->mr.r_ctrl0);
					restore_flags(flags);

					MOD_DEC_USE_COUNT;

					bch->mode = L1_MODE_NULL;
					bch->b_if.ifc.l1l2(&bch->b_if.ifc,
							   PH_DEACTIVATE |
							   INDICATION,
							   NULL);
					if (bch->tx_skb) {
						dev_kfree_skb(bch->tx_skb);
						bch->tx_skb = NULL;
					}
					if (bch->rx_skb) {
						dev_kfree_skb(bch->rx_skb);
						bch->rx_skb = NULL;
					}
					skb_queue_purge(&bch->tx_queue);
					bch->tx_cnt = 0;
					bch->rx_ptr = NULL;
					break;
			}

			// timer is only used when at least one b channel is set up to transparent mode
			if (l1->hw->mr.timer_usg_cnt) {
				Write_hfc8(l1->hw, R_IRQMSK_MISC,
					   M_TI_IRQMSK);
			} else {
				Write_hfc8(l1->hw, R_IRQMSK_MISC, 0);
			}

			break;

		default:
			printk(KERN_INFO
			       "HFC-4S/8S: Unknown B-chan cmd 0x%x received, ignored\n",
			       pr);
			break;
	}
	if (!l1->enabled)
		bch->b_if.ifc.l1l2(&bch->b_if.ifc,
				   PH_DEACTIVATE | INDICATION, NULL);

}				/* bch_l2l1 */

/**************************/
/* layer 1 timer function */
/**************************/
static void
hfc_l1_timer(struct hfc4s8s_l1 *l1)
{
	long flags;

	if (!l1->enabled)
		return;

	save_flags(flags);
	if (l1->nt_mode) {
		cli();
		l1->l1_state = 1;
		Write_hfc8(l1->hw, R_ST_SEL, l1->st_num);
		Write_hfc8(l1->hw, A_ST_WR_STA, 0x11);
		restore_flags(flags);
		l1->d_if.ifc.l1l2(&l1->d_if.ifc,
				  PH_DEACTIVATE | INDICATION, NULL);
		cli();
		l1->l1_state = 1;
		Write_hfc8(l1->hw, A_ST_WR_STA, 0x1);
		restore_flags(flags);
	} else {
		/* activation timed out */
		cli();
		Write_hfc8(l1->hw, R_ST_SEL, l1->st_num);
		Write_hfc8(l1->hw, A_ST_WR_STA, 0x13);
		restore_flags(flags);
		l1->d_if.ifc.l1l2(&l1->d_if.ifc,
				  PH_DEACTIVATE | INDICATION, NULL);
		cli();
		Write_hfc8(l1->hw, R_ST_SEL, l1->st_num);
		Write_hfc8(l1->hw, A_ST_WR_STA, 0x3);
		restore_flags(flags);
	}
}				/* hfc_l1_timer */


/****************************************/
/* a complete D-frame has been received */
/****************************************/

static void
rx_d_frame(struct hfc4s8s_l1 *l1p, int ech)
{
	int z1, z2;
	u_char f1, f2, df;
	struct sk_buff *skb;
	u_char *cp;


	if (!l1p->enabled)
		return;
	do {
		Write_hfc8(l1p->hw, R_FIFO, (l1p->st_num * 8 + ((ech) ? 7 : 5)));	// E/D RX fifo
		wait_busy(l1p->hw);

		f1 = Read_hfc8_stable(l1p->hw, A_F1);
		f2 = Read_hfc8(l1p->hw, A_F2);
		df = f1 - f2;
		if ((f1 - f2) < 0)
			df = f1 - f2 + MAX_F_CNT + 1;


		if (!df) {
			return;	// no complete frame in fifo
		}

		z1 = Read_hfc16_stable(l1p->hw, A_Z1);
		z2 = Read_hfc16(l1p->hw, A_Z2);

		z1 = z1 - z2 + 1;
		if (z1 < 0)
			z1 += 384;


		if (!(skb = dev_alloc_skb(MAX_D_FRAME_SIZE))) {
			printk(KERN_INFO
			       "HFC-4S/8S: Could not allocate D/E channel receive buffer");
			Write_hfc8(l1p->hw, A_INC_RES_FIFO, 2);
			wait_busy(l1p->hw);
			return;
		}

		if (((z1 < 4) || (z1 > MAX_D_FRAME_SIZE))) {
			if (skb)
				dev_kfree_skb(skb);
			/* remove errornous D frame */
			if (df == 1) {
				/* reset fifo */
				Write_hfc8(l1p->hw, A_INC_RES_FIFO, 2);
				wait_busy(l1p->hw);
				return;
			} else {
				/* read errornous D frame */

#ifndef CONFIG_HISAX_HFC4S8S_PCIMEM
				SetRegAddr(l1p->hw, A_FIFO_DATA0);
#endif

				while (z1 >= 4) {
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
					Read_hfc32(l1p->hw, A_FIFO_DATA0);
#else
					fRead_hfc32(l1p->hw);
#endif
					z1 -= 4;
				}

				while (z1--)
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
					Read_hfc8(l1p->hw, A_FIFO_DATA0);
#else
					fRead_hfc8(l1p->hw);
#endif

				Write_hfc8(l1p->hw, A_INC_RES_FIFO, 1);
				wait_busy(l1p->hw);
				return;
			}
		}

		cp = skb->data;

#ifndef CONFIG_HISAX_HFC4S8S_PCIMEM
		SetRegAddr(l1p->hw, A_FIFO_DATA0);
#endif

		while (z1 >= 4) {
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			*((unsigned long *) cp) =
			    Read_hfc32(l1p->hw, A_FIFO_DATA0);
#else
			*((unsigned long *) cp) = fRead_hfc32(l1p->hw);
#endif
			cp += 4;
			z1 -= 4;
		}

		while (z1--)
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			*cp++ = Read_hfc8(l1p->hw, A_FIFO_DATA0);
#else
			*cp++ = fRead_hfc8(l1p->hw);
#endif

		Write_hfc8(l1p->hw, A_INC_RES_FIFO, 1);	/* increment f counter */
		wait_busy(l1p->hw);

		if (*(--cp)) {
			dev_kfree_skb(skb);
		} else {
			skb->len = (cp - skb->data) - 2;
			if (ech)
				l1p->d_if.ifc.l1l2(&l1p->d_if.ifc,
						   PH_DATA_E | INDICATION,
						   skb);
			else
				l1p->d_if.ifc.l1l2(&l1p->d_if.ifc,
						   PH_DATA | INDICATION,
						   skb);
		}

	} while (1);
}				/* rx_d_frame */


/*************************************************************/
/* a B-frame has been received (perhaps not fully completed) */
/*************************************************************/
static void
rx_b_frame(struct hfc4s8s_btype *bch)
{
	int z1, z2, hdlc_complete;
	u_char f1, f2;
	struct hfc4s8s_l1 *l1 = bch->l1p;
	struct sk_buff *skb;

	if (!l1->enabled || (bch->mode == L1_MODE_NULL))
		return;

	do {
		Write_hfc8(l1->hw, R_FIFO, (l1->st_num * 8 + ((bch->bchan == 1) ? 1 : 3)));	// RX fifo
		wait_busy(l1->hw);

		if (bch->mode == L1_MODE_HDLC) {
			f1 = Read_hfc8_stable(l1->hw, A_F1);
			f2 = Read_hfc8(l1->hw, A_F2);
			hdlc_complete = ((f1 ^ f2) & MAX_F_CNT);
		} else
			hdlc_complete = 0;
		z1 = Read_hfc16_stable(l1->hw, A_Z1);
		z2 = Read_hfc16(l1->hw, A_Z2);
		z1 = (z1 - z2);
		if (hdlc_complete)
			z1++;
		if (z1 < 0)
			z1 += 384;

		if (!z1)
			break;

		if (!(skb = bch->rx_skb)) {
			if (!
			    (skb =
			     dev_alloc_skb((bch->mode ==
					    L1_MODE_TRANS) ? z1
					   : (MAX_B_FRAME_SIZE + 3)))) {
				printk(KERN_ERR
				       "HFC-4S/8S: Could not allocate B channel receive buffer");
				return;
			}
			bch->rx_ptr = skb->data;
			bch->rx_skb = skb;
		}

		skb->len = (bch->rx_ptr - skb->data) + z1;

		// HDLC length check
		if ((bch->mode == L1_MODE_HDLC) &&
		    ((hdlc_complete && (skb->len < 4)) ||
		     (skb->len > (MAX_B_FRAME_SIZE + 3)))) {

			skb->len = 0;
			bch->rx_ptr = skb->data;
			Write_hfc8(l1->hw, A_INC_RES_FIFO, 2);	// reset fifo
			wait_busy(l1->hw);
			return;
		}
#ifndef CONFIG_HISAX_HFC4S8S_PCIMEM
		SetRegAddr(l1->hw, A_FIFO_DATA0);
#endif

		while (z1 >= 4) {
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			*((unsigned long *) bch->rx_ptr) =
			    Read_hfc32(l1->hw, A_FIFO_DATA0);
#else
			*((unsigned long *) bch->rx_ptr) =
			    fRead_hfc32(l1->hw);
#endif
			bch->rx_ptr += 4;
			z1 -= 4;
		}
		
		while (z1--)
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			*(bch->rx_ptr++) = Read_hfc8(l1->hw, A_FIFO_DATA0);
#else
			*(bch->rx_ptr++) = fRead_hfc8(l1->hw);
#endif

		if (hdlc_complete) {
			Write_hfc8(l1->hw, A_INC_RES_FIFO, 1);	// increment f counter
			wait_busy(l1->hw);

			// hdlc crc check
			bch->rx_ptr--;
			if (*bch->rx_ptr) {
				skb->len = 0;
				bch->rx_ptr = skb->data;
				continue;
			}
			skb->len -= 3;
		}
		if (hdlc_complete || (bch->mode == L1_MODE_TRANS)) {
			bch->rx_skb = NULL;
			bch->rx_ptr = NULL;
			bch->b_if.ifc.l1l2(&bch->b_if.ifc,
					   PH_DATA | INDICATION, skb);
		}

	} while (1);
}				/* rx_b_frame */


/********************************************/
/* a D-frame has been/should be transmitted */
/********************************************/
static void
tx_d_frame(struct hfc4s8s_l1 *l1p)
{
	struct sk_buff *skb;
	u_char f1, f2;
	u_char *cp;
	int cnt;

	if (l1p->l1_state != 7)
		return;

	Write_hfc8(l1p->hw, R_FIFO, (l1p->st_num * 8 + 4));	// TX fifo
	wait_busy(l1p->hw);

	f1 = Read_hfc8(l1p->hw, A_F1);
	f2 = Read_hfc8_stable(l1p->hw, A_F2);

	if ((f1 ^ f2) & MAX_F_CNT)
		return;		// fifo is still filled

	if (l1p->tx_cnt > 0) {
		cnt = l1p->tx_cnt;
		l1p->tx_cnt = 0;
		l1p->d_if.ifc.l1l2(&l1p->d_if.ifc, PH_DATA | CONFIRM,
				   (void *) cnt);
	}

	if ((skb = skb_dequeue(&l1p->d_tx_queue))) {

		cp = skb->data;
		cnt = skb->len;
#ifndef CONFIG_HISAX_HFC4S8S_PCIMEM
		SetRegAddr(l1p->hw, A_FIFO_DATA0);
#endif

		while (cnt >= 4) {
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			fWrite_hfc32(l1p->hw, A_FIFO_DATA0,
				     *(unsigned long *) cp);
#else
			SetRegAddr(l1p->hw, A_FIFO_DATA0);
			fWrite_hfc32(l1p->hw, *(unsigned long *) cp);
#endif
			cp += 4;
			cnt -= 4;
		}

#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
		while (cnt--)
			fWrite_hfc8(l1p->hw, A_FIFO_DATA0, *cp++);
#else
		while (cnt--)
			fWrite_hfc8(l1p->hw, *cp++);
#endif
		l1p->tx_cnt = skb->truesize;
		Write_hfc8(l1p->hw, A_INC_RES_FIFO, 1);	// increment f counter
		wait_busy(l1p->hw);

		dev_kfree_skb(skb);
	}
}				/* tx_d_frame */

/******************************************************/
/* a B-frame may be transmitted (or is not completed) */
/******************************************************/
static void
tx_b_frame(struct hfc4s8s_btype *bch)
{
	struct sk_buff *skb;
	struct hfc4s8s_l1 *l1 = bch->l1p;
	u_char *cp;
	int cnt, max, hdlc_num, ack_len = 0;

	if (!l1->enabled || (bch->mode == L1_MODE_NULL))
		return;

	Write_hfc8(l1->hw, R_FIFO, (l1->st_num * 8 + ((bch->bchan == 1) ? 0 : 2)));	// TX fifo
	wait_busy(l1->hw);
	do {

		if (bch->mode == L1_MODE_HDLC) {
			hdlc_num = Read_hfc8(l1->hw, A_F1) & MAX_F_CNT;
			hdlc_num -=
			    (Read_hfc8_stable(l1->hw, A_F2) & MAX_F_CNT);
			if (hdlc_num < 0)
				hdlc_num += 16;
			if (hdlc_num >= 15)
				break;	// fifo still filled up with hdlc frames
		} else
			hdlc_num = 0;

		if (!(skb = bch->tx_skb)) {
			if (!(skb = skb_dequeue(&bch->tx_queue))) {
				l1->hw->mr.fifo_slow_timer_service[l1->
								   st_num]
				    &= ~((bch->bchan == 1) ? 1 : 4);
				break;	// list empty
			}
			bch->tx_skb = skb;
			bch->tx_cnt = 0;
		}

		if (!hdlc_num)
			l1->hw->mr.fifo_slow_timer_service[l1->st_num] |=
			    ((bch->bchan == 1) ? 1 : 4);
		else
			l1->hw->mr.fifo_slow_timer_service[l1->st_num] &=
			    ~((bch->bchan == 1) ? 1 : 4);

		max = Read_hfc16_stable(l1->hw, A_Z2);
		max -= Read_hfc16(l1->hw, A_Z1);
		if (max <= 0)
			max += 384;
		max--;

		if (max < 16)
			break;	// don't write to small amounts of bytes

		cnt = skb->len - bch->tx_cnt;
		if (cnt > max)
			cnt = max;
		cp = skb->data + bch->tx_cnt;
		bch->tx_cnt += cnt;

#ifndef CONFIG_HISAX_HFC4S8S_PCIMEM
		SetRegAddr(l1->hw, A_FIFO_DATA0);
#endif
		while (cnt >= 4) {
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			fWrite_hfc32(l1->hw, A_FIFO_DATA0,
				     *(unsigned long *) cp);
#else
			fWrite_hfc32(l1->hw, *(unsigned long *) cp);
#endif
			cp += 4;
			cnt -= 4;
		}

		while (cnt--)
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
			fWrite_hfc8(l1->hw, A_FIFO_DATA0, *cp++);
#else
			fWrite_hfc8(l1->hw, *cp++);
#endif

		if (bch->tx_cnt >= skb->len) {
			if (bch->mode == L1_MODE_HDLC) {
				Write_hfc8(l1->hw, A_INC_RES_FIFO, 1);	// increment f counter
			}
			ack_len += skb->truesize;
			bch->tx_skb = 0;
			bch->tx_cnt = 0;
			dev_kfree_skb(skb);
		} else
			Write_hfc8(l1->hw, R_FIFO, (l1->st_num * 8 + ((bch->bchan == 1) ? 0 : 2)));	// Re-Select
		wait_busy(l1->hw);

	} while (1);

	if (ack_len)
		bch->b_if.ifc.l1l2((struct hisax_if *) &bch->b_if,
				   PH_DATA | CONFIRM, (void *) ack_len);
}				/* tx_b_frame */

/*************************************/
/* bottom half handler for interrupt */
/*************************************/
static void
hfc4s8s_bh(struct hfc4s8s_hw *hw)
{
	u_char b;
	struct hfc4s8s_l1 *l1p;
	volatile u_char *fifo_stat;
	int idx;

	// handle layer 1 state changes
	b = 1;
	l1p = hw->l1;
	while (b) {
		if ((b & hw->mr.r_irq_statech)) {
			hw->mr.r_irq_statech &= ~b;	// reset l1 event
			if (l1p->enabled) {
				if (l1p->nt_mode) {
					u_char oldstate = l1p->l1_state;	// save old l1 state

					Write_hfc8(l1p->hw, R_ST_SEL,
						   l1p->st_num);
					l1p->l1_state = Read_hfc8(l1p->hw, A_ST_RD_STA) & 0xf;	// new state

					if ((oldstate == 3)
					    && (l1p->l1_state != 3))
						l1p->d_if.ifc.l1l2(&l1p->
								   d_if.
								   ifc,
								   PH_DEACTIVATE
								   |
								   INDICATION,
								   NULL);

					if (l1p->l1_state != 2) {
						del_timer(&l1p->l1_timer);
						if (l1p->l1_state == 3) {
							l1p->d_if.ifc.
							    l1l2(&l1p->
								 d_if.ifc,
								 PH_ACTIVATE
								 |
								 INDICATION,
								 NULL);
						}
					} else {
						Write_hfc8(hw, A_ST_WR_STA, M_SET_G2_G3);	// allow transition
						mod_timer(&l1p->l1_timer,
							  jiffies +
							  L1_TIMER_T1);
					}
					printk(KERN_INFO
					       "HFC-4S/8S: NT ch %d l1 state %d -> %d\n",
					       l1p->st_num, oldstate,
					       l1p->l1_state);
				} else {
					u_char oldstate = l1p->l1_state;	// save old l1 state

					Write_hfc8(l1p->hw, R_ST_SEL,
						   l1p->st_num);
					l1p->l1_state = Read_hfc8(l1p->hw, A_ST_RD_STA) & 0xf;	// new state

					if (((l1p->l1_state == 3) &&
					     ((oldstate == 7) ||
					      (oldstate == 8))) ||
					    ((timer_pending
					      (&l1p->l1_timer))
					     && (l1p->l1_state == 8))) {
						mod_timer(&l1p->l1_timer,
							  L1_TIMER_T4 +
							  jiffies);
					} else {
						if (l1p->l1_state == 7) {
							del_timer(&l1p->l1_timer);	/* no longer needed */
							l1p->d_if.ifc.
							    l1l2(&l1p->
								 d_if.ifc,
								 PH_ACTIVATE
								 |
								 INDICATION,
								 NULL);
							tx_d_frame(l1p);
						}
						if (l1p->l1_state == 3) {
							if (oldstate != 3)
								l1p->d_if.
								    ifc.
								    l1l2
								    (&l1p->
								     d_if.
								     ifc,
								     PH_DEACTIVATE
								     |
								     INDICATION,
								     NULL);
						}
					}
					printk(KERN_INFO
					       "HFC-4S/8S: TE %d ch %d l1 state %d -> %d\n",
					       l1p->hw - hfc4s8s,
					       l1p->st_num, oldstate,
					       l1p->l1_state);
				}
			}
		}
		b <<= 1;
		l1p++;
	}

	// now handle the fifos
	idx = 0;
	fifo_stat = hw->mr.r_irq_fifo_blx;
	l1p = hw->l1;
	while (idx < hw->max_st_ports) {

		if (hw->mr.timer_irq) {
			*fifo_stat |= hw->mr.fifo_rx_trans_enables[idx];
			if (hw->fifo_sched_cnt <= 0) {
				*fifo_stat |=
				    hw->mr.fifo_slow_timer_service[l1p->
								   st_num];
			}
		}
		// ignore fifo 6 (TX E fifo)
		*fifo_stat &= 0xff - 0x40;

		while (*fifo_stat) {

			if (!l1p->nt_mode) {
				// RX Fifo has data to read
				if ((*fifo_stat & 0x20)) {
					*fifo_stat &= ~0x20;
					rx_d_frame(l1p, 0);
				}
				// E Fifo has data to read
				if ((*fifo_stat & 0x80)) {
					*fifo_stat &= ~0x80;
					rx_d_frame(l1p, 1);
				}
				// TX Fifo completed send
				if ((*fifo_stat & 0x10)) {
					*fifo_stat &= ~0x10;
					tx_d_frame(l1p);
				}
			}

			// B1 RX Fifo has data to read
			if ((*fifo_stat & 0x2)) {
				*fifo_stat &= ~0x2;
				rx_b_frame(l1p->b_ch);
			}
			// B1 TX Fifo has send completed
			if ((*fifo_stat & 0x1)) {
				*fifo_stat &= ~0x1;
				tx_b_frame(l1p->b_ch);
			}
			// B2 RX Fifo has data to read
			if ((*fifo_stat & 0x8)) {
				*fifo_stat &= ~0x8;
				rx_b_frame(l1p->b_ch + 1);
			}
			// B2 TX Fifo has send completed
			if ((*fifo_stat & 0x4)) {
				*fifo_stat &= ~0x4;
				tx_b_frame(l1p->b_ch + 1);
			}


		}
		fifo_stat++;
		l1p++;
		idx++;
	}

	if (hw->fifo_sched_cnt <= 0)
		hw->fifo_sched_cnt += (1 << (7 - TRANS_TIMER_MODE));
	hw->mr.timer_irq = 0;	// clear requested timer irq
}				/* hfc4s8s_bh */

/*********************/
/* interrupt handler */
/*********************/
static void
hfc4s8s_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct hfc4s8s_hw *hw = dev_id;
	u_char b, ovr;
	volatile u_char *ovp;
	int idx;
#ifndef	CONFIG_HISAX_HFC4S8S_PCIMEM
	u_char old_ioreg;
#endif

	if (!hw || !(hw->mr.r_irq_ctrl & M_GLOB_IRQ_EN))
		return;

#ifndef	CONFIG_HISAX_HFC4S8S_PCIMEM
	/* read current selected regsister */
	old_ioreg = GetRegAddr(hw);
#endif

	/* Layer 1 State change */
	hw->mr.r_irq_statech |=
	    (Read_hfc8(hw, R_SCI) & hw->mr.r_irqmsk_statchg);
	if (!
	    (b = (Read_hfc8(hw, R_STATUS) & (M_MISC_IRQSTA | M_FR_IRQSTA)))
	    && !hw->mr.r_irq_statech) {
#ifndef	CONFIG_HISAX_HFC4S8S_PCIMEM
		SetRegAddr(hw, old_ioreg);
#endif
		return;
	}

	/* timer event */
	if (Read_hfc8(hw, R_IRQ_MISC) & M_TI_IRQ) {
		hw->mr.timer_irq = 1;
		hw->fifo_sched_cnt--;
	}

	/* FIFO event */
	if ((ovr = Read_hfc8(hw, R_IRQ_OVIEW))) {
		hw->mr.r_irq_oview |= ovr;
		idx = R_IRQ_FIFO_BL0;
		ovp = hw->mr.r_irq_fifo_blx;
		while (ovr) {
			if ((ovr & 1)) {
				*ovp |= Read_hfc8(hw, idx);
			}
			ovp++;
			idx++;
			ovr >>= 1;
		}
	}

	/* queue the request to allow other cards to interrupt */
	queue_task(&hw->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

#ifndef	CONFIG_HISAX_HFC4S8S_PCIMEM
	SetRegAddr(hw, old_ioreg);
#endif
}				/* hfc4s8s_interrupt */

/***********************************************************************/
/* reset the complete chip, don't release the chips irq but disable it */
/***********************************************************************/
static void
chipreset(struct hfc4s8s_hw *hw)
{
	long flags;

	save_flags(flags);
	cli();
	Write_hfc8(hw, R_CTRL, 0);	// use internal RAM
	Write_hfc8(hw, R_RAM_MISC, 0);	// 32k*8 RAM
	Write_hfc8(hw, R_FIFO_MD, 0);	// fifo mode 386 byte/fifo simple mode
	Write_hfc8(hw, R_CIRM, M_SRES);	// reset chip
	hw->mr.r_irq_ctrl = 0;	// interrupt is inactive
	restore_flags(flags);

	udelay(3);
	Write_hfc8(hw, R_CIRM, 0);	// disable reset
	wait_busy(hw);

	Write_hfc8(hw, R_PCM_MD0, M_PCM_MD);	// master mode
	Write_hfc8(hw, R_RAM_MISC, M_FZ_MD);	// transmit fifo option
	if (hw->clock_mode == 1)
		Write_hfc8(hw, R_BRG_PCM_CFG, M_PCM_CLK);	// PCM clk / 2
	Write_hfc8(hw, R_TI_WD, TRANS_TIMER_MODE);	// timer interval

	memset(&hw->mr, 0, sizeof(hw->mr));

}				/* chipreset */

/********************************************/
/* disable/enable hardware in nt or te mode */
/********************************************/
void
hfc_hardware_enable(struct hfc4s8s_hw *hw, int enable, int nt_mode)
{
	long flags;
	char if_name[40];
	int i;

	if (enable) {

		// save system vars
		hw->nt_mode = nt_mode;

		// enable fifo and state irqs, but not global irq enable
		hw->mr.r_irq_ctrl = M_FIFO_IRQ;
		Write_hfc8(hw, R_IRQ_CTRL, hw->mr.r_irq_ctrl);
		hw->mr.r_irqmsk_statchg = 0;
		Write_hfc8(hw, R_SCI_MSK, hw->mr.r_irqmsk_statchg);
		Write_hfc8(hw, R_PWM_MD, 0x80);
		Write_hfc8(hw, R_PWM1, 26);
		if (!nt_mode)
			Write_hfc8(hw, R_ST_SYNC, M_AUTO_SYNC);

		// enable the line interfaces and fifos
		for (i = 0; i < hw->max_st_ports; i++) {
			hw->mr.r_irqmsk_statchg |= (1 << i);
			Write_hfc8(hw, R_SCI_MSK,
				   hw->mr.r_irqmsk_statchg);
			Write_hfc8(hw, R_ST_SEL, i);
			Write_hfc8(hw, A_ST_CLK_DLY,
				   ((nt_mode) ? CLKDEL_NT : CLKDEL_TE));
			hw->mr.r_ctrl0 = ((nt_mode) ? CTRL0_NT : CTRL0_TE);
			Write_hfc8(hw, A_ST_CTRL0, hw->mr.r_ctrl0);
			Write_hfc8(hw, A_ST_CTRL2, 3);
			Write_hfc8(hw, A_ST_WR_STA, 0);	// enable state machine

			hw->l1[i].enabled = 1;
			hw->l1[i].nt_mode = nt_mode;

			if (!nt_mode) {
				// setup E-fifo
				Write_hfc8(hw, R_FIFO, i * 8 + 7);	// E fifo
				wait_busy(hw);
				Write_hfc8(hw, A_CON_HDLC, 0x11);	// HDLC mode, 1 fill, connect ST
				Write_hfc8(hw, A_SUBCH_CFG, 2);	// only 2 bits
				Write_hfc8(hw, A_IRQ_MSK, 1);	// enable interrupt
				Write_hfc8(hw, A_INC_RES_FIFO, 2);	// reset fifo
				wait_busy(hw);

				// setup D RX-fifo
				Write_hfc8(hw, R_FIFO, i * 8 + 5);	// RX fifo
				wait_busy(hw);
				Write_hfc8(hw, A_CON_HDLC, 0x11);	// HDLC mode, 1 fill, connect ST
				Write_hfc8(hw, A_SUBCH_CFG, 2);	// only 2 bits
				Write_hfc8(hw, A_IRQ_MSK, 1);	// enable interrupt
				Write_hfc8(hw, A_INC_RES_FIFO, 2);	// reset fifo
				wait_busy(hw);

				// setup D TX-fifo
				Write_hfc8(hw, R_FIFO, i * 8 + 4);	// TX fifo
				wait_busy(hw);
				Write_hfc8(hw, A_CON_HDLC, 0x11);	// HDLC mode, 1 fill, connect ST
				Write_hfc8(hw, A_SUBCH_CFG, 2);	// only 2 bits
				Write_hfc8(hw, A_IRQ_MSK, 1);	// enable interrupt
				Write_hfc8(hw, A_INC_RES_FIFO, 2);	// reset fifo
				wait_busy(hw);
			}


			sprintf(if_name, "hfc4s8s_%d%d_", hw - hfc4s8s, i);
			if (hisax_register
			    (&hw->l1[i].d_if, hw->l1[i].b_table, if_name,
			     ((nt_mode) ? 3 : 2))) {

				hw->l1[i].enabled = 0;
				hw->mr.r_irqmsk_statchg &= ~(1 << i);
				Write_hfc8(hw, R_SCI_MSK,
					   hw->mr.r_irqmsk_statchg);
				printk(KERN_INFO
				       "HFC-4S/8S: Unable to register S/T device %s, break\n",
				       if_name);
				break;
			}
		}

		save_flags(flags);
		cli();
		hw->mr.r_irq_ctrl |= M_GLOB_IRQ_EN;
		Write_hfc8(hw, R_IRQ_CTRL, hw->mr.r_irq_ctrl);
		restore_flags(flags);

	} else {
		save_flags(flags);
		cli();
		hw->mr.r_irq_ctrl &= ~M_GLOB_IRQ_EN;
		Write_hfc8(hw, R_IRQ_CTRL, hw->mr.r_irq_ctrl);
		restore_flags(flags);

		for (i = hw->max_st_ports - 1; i >= 0; i--) {
			hw->l1[i].enabled = 0;
			hisax_unregister(&hw->l1[i].d_if);
			del_timer(&hw->l1[i].l1_timer);
			skb_queue_purge(&hw->l1[i].d_tx_queue);
			skb_queue_purge(&hw->l1[i].b_ch[0].tx_queue);
			skb_queue_purge(&hw->l1[i].b_ch[1].tx_queue);
		}

		chipreset(hw);
	}
}				/* hfc_hardware_enable */

/*************************************/
/* initialise the HFC-4s/8s hardware */
/* return 0 on success.              */
/*************************************/
static int __init
hfc4s8s_init_hw(void)
{
	struct pci_dev *tmp_hfc4s8s = NULL;
	PCI_ENTRY *list = (PCI_ENTRY *) id_list;
	struct hfc4s8s_hw *hw = hfc4s8s;
	long flags;
	int i;

	printk(KERN_INFO
	       "Layer 1 driver module for HFC4S/8S isdn chips, %s\n",
	       hfc4s8s_rev);
	printk(KERN_INFO
	       "(C) 2003 Cornelius Consult, www.cornelius-consult.de\n");

	do {

		tmp_hfc4s8s =
		    pci_find_device(list->vendor_id, list->device_id,
				    tmp_hfc4s8s);

		if (!tmp_hfc4s8s) {
			list++;	// next PCI-ID
			if (!list->vendor_id)
				break;	// end of list
			continue;	// search PCI base again
		}

		if (list->sub_vendor_id
		    && (list->sub_vendor_id !=
			tmp_hfc4s8s->subsystem_vendor))
			continue;	// sub_vendor defined and not matching

		if (list->sub_device_id
		    && (list->sub_device_id !=
			tmp_hfc4s8s->subsystem_device))
			continue;	// sub_device defined and not matching

		// init Layer 1 structure
		if (tmp_hfc4s8s->irq <= 0) {
			printk(KERN_INFO
			       "HFC4S/8S: found PCI card without assigned IRQ, card ignored\n");
			continue;
		}

		if (pci_enable_device(tmp_hfc4s8s)) {
			printk(KERN_INFO
			       "HFC4S/8S: Error enabling PCI card, card ignored\n");
			continue;
		}

		hw->irq = tmp_hfc4s8s->irq;
		hw->max_st_ports = list->max_channels;
		hw->max_fifo = list->max_channels * 4;
		hw->clock_mode = list->clock_mode;

		for (i = 0; i < HFC_MAX_ST; i++) {
			struct hfc4s8s_l1 *l1p;

			l1p = hw->l1 + i;
			l1p->hw = hw;
			l1p->l1_timer.function = (void *) hfc_l1_timer;
			l1p->l1_timer.data = (long) (l1p);
			init_timer(&l1p->l1_timer);
			l1p->st_num = i;
			skb_queue_head_init(&l1p->d_tx_queue);
			l1p->d_if.ifc.priv = hw->l1 + i;
			l1p->d_if.ifc.l2l1 = (void *) dch_l2l1;
			l1p->b_ch[0].b_if.ifc.l2l1 = (void *) bch_l2l1;
			l1p->b_ch[0].b_if.ifc.priv =
			    (void *) &l1p->b_ch[0];
			l1p->b_ch[0].l1p = hw->l1 + i;
			l1p->b_ch[0].bchan = 1;
			l1p->b_table[0] = &l1p->b_ch[0].b_if;
			skb_queue_head_init(&l1p->b_ch[0].tx_queue);
			l1p->b_ch[1].b_if.ifc.l2l1 = (void *) bch_l2l1;
			l1p->b_ch[1].b_if.ifc.priv =
			    (void *) &l1p->b_ch[1];
			l1p->b_ch[1].l1p = hw->l1 + i;
			l1p->b_ch[1].bchan = 2;
			l1p->b_table[1] = &l1p->b_ch[1].b_if;
			skb_queue_head_init(&l1p->b_ch[1].tx_queue);
		}

		hw->iobase =
		    tmp_hfc4s8s->resource[0].
		    start & PCI_BASE_ADDRESS_IO_MASK;

#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
		hw->hw_membase = (u_char *) tmp_hfc4s8s->resource[1].start;
		hw->membase = ioremap((ulong) hw->hw_membase, 256);
#else
		if (!request_region(hw->iobase, 8, hw->card_name)) {
			printk(KERN_INFO
			       "HFC-4S/8S: failed to request address space at 0x%04x\n",
			       hw->iobase);
			continue;
		}
#endif
		hw->pci_bus = tmp_hfc4s8s->bus->number;
		hw->pci_dev_fn = tmp_hfc4s8s->devfn;

#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
		/* now enable memory mapped ports */
		pcibios_write_config_word(hw->pci_bus,
					  hw->pci_dev_fn,
					  PCI_COMMAND, PCI_ENA_MEMIO);
#else
		/* now enable IO mapped ports */
		pcibios_write_config_word(hw->pci_bus,
					  hw->pci_dev_fn,
					  PCI_COMMAND, PCI_ENA_REGIO);
#endif

		chipreset(hw);
		i = Read_hfc8(hw, R_CHIP_ID) >> CHIP_ID_SHIFT;

		if (i != list->chip_id) {
			printk(KERN_INFO
			       "HFC-4S/8S: invalid chip id 0x%x instead of 0x%x, card ignored\n",
			       i, list->chip_id);
			pcibios_write_config_word(hw->pci_bus, hw->pci_dev_fn, PCI_COMMAND, 0);	/* disable memory mapped ports */
			vfree(hw->membase);
			continue;
		}

		i = Read_hfc8(hw, R_CHIP_RV) & 0xf;

		if (!i) {
			printk(KERN_INFO
			       "HFC-4S/8S: chip revision 0 not supported, card ignored\n");
			pcibios_write_config_word(hw->pci_bus, hw->pci_dev_fn, PCI_COMMAND, 0);	/* disable memory mapped ports */
			vfree(hw->membase);
			continue;
		}

		hw->tqueue.sync = 0;
		hw->tqueue.routine = (void *) (void *) hfc4s8s_bh;
		hw->tqueue.data = hw;

		sprintf(hw->card_name, "hfc4s8s_%d", hw - hfc4s8s);
		save_flags(flags);
		cli();
		if (request_irq
		    (hw->irq, hfc4s8s_interrupt, SA_SHIRQ, hw->card_name,
		     hw)) {

			vfree(hw->membase);
			restore_flags(flags);
			printk(KERN_INFO
			       "HFC-4S/8S: unable to alloc irq %d, card ignored\n",
			       hw->irq);
			hw->irq = 0;
			continue;
		}
		restore_flags(flags);

#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
		printk(KERN_INFO
		       "HFC-4S/8S: found PCI card at membase 0x%p, irq %d\n",
		       hw->hw_membase, hw->irq);
#else
		printk(KERN_INFO
		       "HFC-4S/8S: found PCI card at iobase 0x%x, irq %d\n",
		       hw->iobase, hw->irq);
#endif
		hfc_hardware_enable(hw, 1, 0);
		card_cnt++;
		hw++;

	} while ((card_cnt < MAX_HFC8_CARDS) && list->vendor_id);

	printk(KERN_INFO
	       "HFC-4S/8S: nominal transparent fifo transfer size is %d bytes\n",
	       TRANS_FIFO_BYTES);
	printk(KERN_INFO
	       "HFC-4S/8S: module successfully installed, %d cards found\n",
	       card_cnt);

	return (0);
}				/* hfc4s8s_init_hw */

/*************************************/
/* release the HFC-4s/8s hardware    */
/*************************************/
static void
hfc4s8s_release_hw(void)
{
	struct hfc4s8s_hw *hw = hfc4s8s + card_cnt - 1;

	while (card_cnt) {
		hfc_hardware_enable(hw, 0, 0);
		if (hw->irq)
			free_irq(hw->irq, hw);
		hw->irq = 0;

		pcibios_write_config_word(hw->pci_bus,
		                          hw->pci_dev_fn,
		                          PCI_COMMAND, 0);
#ifdef CONFIG_HISAX_HFC4S8S_PCIMEM
		if (hw->membase) {
			iounmap((void *) hw->membase);
			vfree(hw->membase);
		}
#else
		if (hw->iobase)
			release_region(hw->iobase, 8);
#endif

		hw--;
		card_cnt--;
	}

	printk(KERN_INFO "HFC-4S/8S: module removed successfully\n");
}				/* hfc4s8s_release_hw */


MODULE_AUTHOR("Werner Cornelius, werner@cornelius-consult.de");
MODULE_DESCRIPTION("ISDN layer 1 for Colognechip HFC4S/8S chips");
MODULE_LICENSE("GPL");
module_init(hfc4s8s_init_hw);
module_exit(hfc4s8s_release_hw);
