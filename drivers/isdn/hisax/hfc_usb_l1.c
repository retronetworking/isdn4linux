/* $Id$
 *
 *
 *
 * Author       (C) 2001 Werner Cornelius (werner@isdn-development.de)
 *              modular driver for Colognechip HFC-USB chip
 *              as plugin for HiSax isdn driver
 *
 * Copyright 2001  by Werner Cornelius (werner@isdn4linux.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/config.h>
#include <linux/isdn_compat.h>
#include <linux/init.h>
#include "hisax.h"
#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/tqueue.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include "hisax_loadable.h"

#define INCLUDE_INLINE_FUNCS

/***********/
/* defines */
/***********/
#define HFC_CTRL_TIMEOUT 5	/* 5ms timeout writing/reading regs */
#define HFC_TIMER_T3     7000	/* timeout for l1 activation timer */

#define HFCUSB_L1_STATECHANGE   0	/* L1 state changed */
#define HFCUSB_L1_DRX           1	/* D-frame received */
#define HFCUSB_L1_ERX           2	/* E-frame received */
#define HFCUSB_L1_DTX           4	/* D-frames completed */

#define MAX_BCH_SIZE        2048	/* allowed B-channel packet size */
#define MAX_DCH_SIZE        264	/* allowed D-channel packet size */

#define HFCUSB_RX_THRESHOLD 64	/* threshold for fifo report bit rx */
#define HFCUSB_TX_THRESHOLD 64	/* threshold for fifo report bit tx */

#define HFCUSB_CHIP_ID    0x16	/* Chip ID register index */
#define HFCUSB_CIRM       0x00	/* cirm register index */
#define HFCUSB_USB_SIZE   0x07	/* int length register */
#define HFCUSB_USB_SIZE_I 0x06	/* iso length register */
#define HFCUSB_F_CROSS    0x0b	/* bit order register */
#define HFCUSB_CLKDEL     0x37	/* bit delay register */
#define HFCUSB_CON_HDLC   0xfa	/* channel connect register */
#define HFCUSB_HDLC_PAR   0xfb
#define HFCUSB_SCTRL      0x31	/* S-bus control register (tx) */
#define HFCUSB_SCTRL_E    0x32	/* same for E and special funcs */
#define HFCUSB_SCTRL_R    0x33	/* S-bus control register (rx) */
#define HFCUSB_F_THRES    0x0c	/* threshold register */
#define HFCUSB_FIFO       0x0f	/* fifo select register */
#define HFCUSB_F_USAGE    0x1a	/* fifo usage register */
#define HFCUSB_MST_MODE0  0x14
#define HFCUSB_MST_MODE1  0x15
#define HFCUSB_INC_RES_F  0x0e
#define HFCUSB_STATES     0x30

#define HFCUSB_CHIPID 0x40	/* ID value of HFC-USB */

/******************/
/* fifo registers */
/******************/
#define HFCUSB_NUM_FIFOS   8	/* maximum number of fifos */
#define HFCUSB_B1_TX       0	/* index for B1 transmit bulk/int */
#define HFCUSB_B1_RX       1	/* index for B1 receive bulk/int */
#define HFCUSB_B2_TX       2
#define HFCUSB_B2_RX       3
#define HFCUSB_D_TX        4
#define HFCUSB_D_RX        5
#define HFCUSB_PCM_TX      6
#define HFCUSB_PCM_RX      7


/**********/
/* macros */
/**********/
#define Write_hfc(a,b,c) usb_control_msg((a)->dev,(a)->ctrl_out_pipe,0,0x40,(c),(b),0,0,HFC_CTRL_TIMEOUT)
#define Read_hfc(a,b,c) usb_control_msg((a)->dev,(a)->ctrl_in_pipe,1,0xC0,0,(b),(c),1,HFC_CTRL_TIMEOUT)

#ifdef COMPAT_HAS_USB_IDTAB
/****************************************/
/* data defining the devices to be used */
/****************************************/
static __devinitdata const struct usb_device_id hfc_usb_idtab[2] = {
	{USB_DEVICE(0x959, 0x2bd0)},	/* Colognechip ROM */
	{}			/* end with an all-zeroes entry */
};
#endif

/*************************************************/
/* entry and size of output/input control buffer */
/*************************************************/
#define HFC_CTRL_BUFSIZE 32
typedef struct {
	__u8 hfc_reg;		/* register number */
	__u8 reg_val;		/* value to be written (or read) */
} ctrl_buft;

/***************************************************************/
/* structure defining input+output fifos (interrupt/bulk mode) */
/***************************************************************/
struct hfcusb_data;		/* forward definition */
typedef struct {
	int fifonum;		/* fifo index attached to this structure */
	__u8 fifo_mask;		/* mask for this fifo */
	int active;		/* fifo is currently active */
	struct hfcusb_data *hfc;	/* pointer to main structure */
	int pipe;		/* address of endpoint */
	__u8 usb_maxlen;	/* maximum length for usb transfer */
	int max_size;		/* maximum size of receive/send packet */
	int transmode;		/* transparent mode selected */
	int framenum;		/* number of frame when last tx completed */
	int rx_offset;		/* offset inside rx buffer */
	int next_complete;	/* complete marker */
	__u8 *act_ptr;		/* pointer to next data */
	__u8 intervall;		/* interrupt interval */
	struct sk_buff *buff;	/* actual used buffer */
	urb_t urb;		/* transfer structure for usb routines */
	__u8 buffer[128];	/* buffer incoming/outgoing data */
} usb_fifo;

/*********************************************/
/* structure holding all data for one device */
/*********************************************/
typedef struct hfcusb_data {
	struct hisax_drvreg regd;	/* register data and callbacks */
	struct usb_device *dev;	/* our device */
	int if_used;		/* used interface number */
	int alt_used;		/* used alternate config */
	int ctrl_paksize;	/* control pipe packet size */
	int ctrl_in_pipe, ctrl_out_pipe;	/* handles for control pipe */

	/* control pipe background handling */
	ctrl_buft ctrl_buff[HFC_CTRL_BUFSIZE];	/* buffer holding queued data */
	volatile int ctrl_in_idx, ctrl_out_idx, ctrl_cnt;	/* input/output pointer + count */
	urb_t ctrl_urb;		/* transfer structure for control channel */
	devrequest ctrl_write;	/* buffer for control write request */
	devrequest ctrl_read;	/* same for read request */

	volatile __u8 dfifo_fill;	/* value read from tx d-fifo */
	volatile __u8 active_fifos;	/* fifos currently active as bit mask */
	volatile __u8 threshold_mask;	/* threshold actually reported */
	volatile __u8 service_request;	/* fifo needs service from task */
	volatile __u8 ctrl_fifo;	/* last selected fifo */
	volatile __u8 bch_enables;	/* or mask for sctrl_r and sctrl register values */
	usb_fifo fifos[HFCUSB_NUM_FIFOS];	/* structure holding all fifo data */

	/* layer 1 activation/deactivation handling */
	volatile __u8 l1_state;	/* actual l1 state */
	volatile ulong l1_event;	/* event mask */
	struct tq_struct l1_tq;	/* l1 bh structure */
	struct timer_list t3_timer;	/* timer for activation/deactivation */
} hfcusb_data;

static void
usb_dump_urb(purb_t purb)
{
	printk("urb                   :%p\n", purb);
	printk("next                  :%p\n", purb->next);
	printk("dev                   :%p\n", purb->dev);
	printk("pipe                  :%08X\n", purb->pipe);
	printk("status                :%d\n", purb->status);
	printk("transfer_flags        :%08X\n", purb->transfer_flags);
	printk("transfer_buffer       :%p\n", purb->transfer_buffer);
	printk("transfer_buffer_length:%d\n",
	       purb->transfer_buffer_length);
	printk("actual_length         :%d\n", purb->actual_length);
	printk("setup_packet          :%p\n", purb->setup_packet);
	printk("start_frame           :%d\n", purb->start_frame);
	printk("number_of_packets     :%d\n", purb->number_of_packets);
	printk("interval              :%d\n", purb->interval);
	printk("error_count           :%d\n", purb->error_count);
	printk("context               :%p\n", purb->context);
	printk("complete              :%p\n", purb->complete);
}

/*************************************************************************/
/* bottom half handler for L1 activation/deactiavtaion + D-chan + E-chan */
/*************************************************************************/
static void
usb_l1d_bh(hfcusb_data * hfc)
{

	while (hfc->l1_event) {
		if (test_and_clear_bit
		    (HFCUSB_L1_STATECHANGE, &hfc->l1_event)) {
			hfc->regd.dch_l1l2(hfc->regd.arg_hisax,
					   (hfc->l1_state ==
					    7) ? (PH_DEACTIVATE |
						  INDICATION)
					   : (PH_ACTIVATE | INDICATION),
					   NULL);
		}
		if (test_and_clear_bit(HFCUSB_L1_DRX, &hfc->l1_event)) {
			hfc->regd.dch_l1l2(hfc->regd.arg_hisax,
					   PH_DATA | INDICATION,
					   (void *) 0);
		}
		if (test_and_clear_bit(HFCUSB_L1_ERX, &hfc->l1_event)) {
			hfc->regd.dch_l1l2(hfc->regd.arg_hisax,
					   PH_DATA | INDICATION,
					   (void *) 1);
		}
		if (test_and_clear_bit(HFCUSB_L1_DTX, &hfc->l1_event)) {
			hfc->regd.dch_l1l2(hfc->regd.arg_hisax,
					   PH_DATA | CONFIRM, NULL);
		}
	}			/* while */
}				/* usb_l1d_bh */

/********************************/
/* called when timer t3 expires */
/********************************/
static void
timer_t3_expire(hfcusb_data * hfc)
{
	hfc->regd.dch_l1l2(hfc->regd.arg_hisax, PH_DEACTIVATE | INDICATION,
			   NULL);
}				/* timer_t3_expire */

/******************************************************/
/* start next background transfer for control channel */
/******************************************************/
static void
ctrl_start_transfer(hfcusb_data * hfc)
{

	if (hfc->ctrl_cnt) {
		switch (hfc->ctrl_buff[hfc->ctrl_out_idx].hfc_reg) {
			case HFCUSB_F_USAGE:
				hfc->ctrl_urb.pipe = hfc->ctrl_in_pipe;
				hfc->ctrl_urb.setup_packet =
				    (u_char *) & hfc->ctrl_read;
				hfc->ctrl_urb.transfer_buffer_length = 1;
				hfc->ctrl_read.index =
				    hfc->ctrl_buff[hfc->ctrl_out_idx].
				    hfc_reg;
				hfc->ctrl_urb.transfer_buffer =
				    (char *) &hfc->dfifo_fill;
				break;

			default:	/* write register */
				hfc->ctrl_urb.pipe = hfc->ctrl_out_pipe;
				hfc->ctrl_urb.setup_packet =
				    (u_char *) & hfc->ctrl_write;
				hfc->ctrl_urb.transfer_buffer = NULL;
				hfc->ctrl_urb.transfer_buffer_length = 0;
				hfc->ctrl_write.index =
				    hfc->ctrl_buff[hfc->ctrl_out_idx].
				    hfc_reg;
				hfc->ctrl_write.value =
				    hfc->ctrl_buff[hfc->ctrl_out_idx].
				    reg_val;
				break;
		}
		usb_submit_urb(&hfc->ctrl_urb);	/* start transfer */
	}
}				/* ctrl_start_transfer */

/************************************/
/* queue a control transfer request */
/* return 0 on success.             */
/************************************/
static int
queue_control_request(hfcusb_data * hfc, __u8 reg, __u8 val)
{
	ctrl_buft *buf;

	if (hfc->ctrl_cnt >= HFC_CTRL_BUFSIZE)
		return (1);	/* no space left */
	buf = hfc->ctrl_buff + hfc->ctrl_in_idx;	/* pointer to new index */
	buf->hfc_reg = reg;
	buf->reg_val = val;
	if (++hfc->ctrl_in_idx >= HFC_CTRL_BUFSIZE)
		hfc->ctrl_in_idx = 0;	/* pointer wrap */
	if (++hfc->ctrl_cnt == 1)
		ctrl_start_transfer(hfc);
	return (0);
}				/* queue_control_request */

/**************************************************/
/* (re)fills a tx-fifo urb. Queuing is done later */
/**************************************************/
static void
fill_tx_urb(usb_fifo * fifo)
{
	struct sk_buff *skb;
	long flags;
	int i, ii = 0;

	if ((fifo->buff)
	    && (fifo->urb.transfer_buffer_length < fifo->usb_maxlen)) {
		switch (fifo->fifonum) {
			case HFCUSB_B1_TX:
			case HFCUSB_B2_TX:
				skb = fifo->buff;
				fifo->buff = NULL;
				fifo->hfc->regd.bch_l1l2(fifo->hfc->regd.
							 arg_hisax,
							 (fifo->fifonum ==
							  HFCUSB_B1_TX) ? 0
							 : 1,
							 (PH_DATA |
							  CONFIRM),
							 (void *) skb);
				fifo->hfc->service_request |=
				    fifo->fifo_mask;
				return;
			case HFCUSB_D_TX:
				dev_kfree_skb_any(fifo->buff);
				fifo->buff = NULL;
				save_flags(flags);
				cli();
				fifo->hfc->dfifo_fill = 0xff;	/* currently invalid data */
				queue_control_request(fifo->hfc,
						      HFCUSB_FIFO,
						      HFCUSB_D_TX);
				queue_control_request(fifo->hfc,
						      HFCUSB_F_USAGE, 0);
				restore_flags(flags);
				return;
			default:
				return;	/* error, invalid fifo */
		}
	}

	/* check if new buffer needed */
	if (!fifo->buff) {
		switch (fifo->fifonum) {
			case HFCUSB_B1_TX:
				if (fifo->hfc->regd.bsk[0])
					fifo->buff = *fifo->hfc->regd.bsk[0];	/* B1-channel tx buffer */
				break;
			case HFCUSB_B2_TX:
				if (fifo->hfc->regd.bsk[1])
					fifo->buff = *fifo->hfc->regd.bsk[1];	/* B2-channel tx buffer */
				break;
			case HFCUSB_D_TX:
				if (fifo->hfc->regd.dsq)
					fifo->buff = skb_dequeue(fifo->hfc->regd.dsq);	/* D-channel tx queue */
				break;
			default:
				return;	/* error, invalid fifo */
		}
		if (!fifo->buff) {
			fifo->active = 0;	/* we are inactive now */
			fifo->hfc->active_fifos &= ~fifo->fifo_mask;
			if (fifo->fifonum == HFCUSB_D_TX) {
				test_and_set_bit(HFCUSB_L1_DTX,
						 &fifo->hfc->l1_event);
				queue_task(&fifo->hfc->l1_tq,
					   &tq_immediate);
				mark_bh(IMMEDIATE_BH);
			}
			return;
		}
		fifo->act_ptr = fifo->buff->data;	/* start of data */
		fifo->active = 1;
		ii = 1;
		fifo->hfc->active_fifos |= fifo->fifo_mask;
		fifo->hfc->service_request &= ~fifo->fifo_mask;
	}
	/* fillup the send buffer */
	i = fifo->buff->len - (fifo->act_ptr - fifo->buff->data);	/* remaining length */
	fifo->buffer[0] = !fifo->transmode;	/* not eof */
	if (i > fifo->usb_maxlen - ii) {
		i = fifo->usb_maxlen - ii;
	}
	if (i)
		memcpy(fifo->buffer + ii, fifo->act_ptr, i);
	fifo->urb.transfer_buffer_length = i + ii;

}				/* fill_tx_urb */

/************************************************/
/* transmit completion routine for all tx fifos */
/************************************************/
static void
tx_complete(purb_t urb)
{
	usb_fifo *fifo = (usb_fifo *) urb->context;	/* pointer to our fifo */

	fifo->hfc->service_request &= ~fifo->fifo_mask;	/* no further handling */
	fifo->framenum = usb_get_current_frame_number(fifo->urb.dev);

	/* check for deactivation or error */
	if ((!fifo->active) || (urb->status)) {
		fifo->hfc->active_fifos &= ~fifo->fifo_mask;	/* we are inactive */
		fifo->active = 0;
		if ((fifo->buff) && (fifo->fifonum == HFCUSB_D_TX)) {
			dev_kfree_skb_any(fifo->buff);
		}
		fifo->buff = NULL;
		return;
	}

	fifo->act_ptr += (urb->transfer_buffer_length - 1);	/* adjust pointer */
	fill_tx_urb(fifo);	/* refill the urb */
	fifo->hfc->threshold_mask |= fifo->fifo_mask;	/* assume threshold reached */
	if (fifo->buff)
		fifo->hfc->service_request |= fifo->fifo_mask;	/* need to restart */
}				/* tx_complete */

/***********************************************/
/* receive completion routine for all rx fifos */
/***********************************************/
static void
rx_complete(purb_t urb)
{
	usb_fifo *fifo = (usb_fifo *) urb->context;	/* pointer to our fifo */
	hfcusb_data *hfc = fifo->hfc;
	usb_fifo *txfifo;
	__u8 last_state;
	int i, ii, currcnt, hdlci;
	struct sk_buff *skb;

	if ((!fifo->active) || (urb->status)) {
		hfc->service_request &= ~fifo->fifo_mask;	/* no further handling */
		hfc->active_fifos &= ~fifo->fifo_mask;	/* we are inactive */
		fifo->urb.interval = 0;	/* cancel automatic rescheduling */
		if (fifo->buff) {
			dev_kfree_skb_any(fifo->buff);
			fifo->buff = NULL;
		}
		return;
	}

	/* first check for any status changes */
	if ((urb->actual_length < fifo->rx_offset)
	    || (urb->actual_length > fifo->usb_maxlen))
		return;		/* error condition */

	if (fifo->rx_offset) {
		hfc->threshold_mask = fifo->buffer[1];	/* update threshold status */
		fifo->next_complete = fifo->buffer[0] & 1;

		/* check if rescheduling needed */
		if ((i =
		     hfc->service_request & hfc->active_fifos & ~hfc->
		     threshold_mask)) {
			currcnt =
			    usb_get_current_frame_number(fifo->urb.dev);
			txfifo = hfc->fifos + HFCUSB_B1_TX;
			ii = 3;
			while (ii--) {
				if ((i & txfifo->fifo_mask)
				    && (currcnt != txfifo->framenum)) {
					hfc->service_request &=
					    ~txfifo->fifo_mask;
					if (!txfifo->buff)
						fill_tx_urb(txfifo);
					if (txfifo->buff)
						usb_submit_urb(&txfifo->
							       urb);
				}
				txfifo += 2;
			}
		}
		/* handle l1 events */
		if ((fifo->buffer[0] >> 4) != hfc->l1_state) {
			last_state = hfc->l1_state;
			hfc->l1_state = fifo->buffer[0] >> 4;	/* update status */
			if ((hfc->l1_state == 7) || (last_state == 7)) {
				if (timer_pending(&hfc->t3_timer)
				    && (hfc->l1_state == 7))
					del_timer(&hfc->t3_timer);	/* no longer needed */
				test_and_set_bit(HFCUSB_L1_STATECHANGE,
						 &hfc->l1_event);
				queue_task(&hfc->l1_tq, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
			}
		}
	}

	/* check the length for data and move if present */
	if (fifo->next_complete || (urb->actual_length > fifo->rx_offset)) {
		i = fifo->buff->len + urb->actual_length - fifo->rx_offset;	/* new total length */
		hdlci = (fifo->transmode) ? 0 : 3;
		if (i <= (fifo->max_size + hdlci)) {
			memcpy(fifo->act_ptr,
			       fifo->buffer + fifo->rx_offset,
			       urb->actual_length - fifo->rx_offset);
			fifo->act_ptr +=
			    (urb->actual_length - fifo->rx_offset);
			fifo->buff->len +=
			    (urb->actual_length - fifo->rx_offset);
		} else
			fifo->buff->len = fifo->max_size + 4;	/* mark frame as to long */
		if (((fifo->next_complete)
		     && (urb->actual_length < fifo->usb_maxlen)
		     && (!*(fifo->act_ptr - 1))) || fifo->transmode) {
			/* the frame is complete, hdlc with correct crc, check validity */
			fifo->next_complete = 0;
			if ((fifo->buff->len >= (hdlci + 1))
			    && (fifo->buff->len <=
				(fifo->max_size + hdlci))
			    &&
			    ((skb =
			      dev_alloc_skb(fifo->max_size + hdlci)) !=
			     NULL)) {
				fifo->buff->len -= hdlci;	/* adjust size */
				switch (fifo->fifonum) {
					case HFCUSB_D_RX:
						skb_queue_tail(hfc->regd.
							       drq,
							       fifo->buff);
						test_and_set_bit
						    (HFCUSB_L1_DRX,
						     &hfc->l1_event);
						queue_task(&hfc->l1_tq,
							   &tq_immediate);
						mark_bh(IMMEDIATE_BH);
						break;

					case HFCUSB_B1_RX:
						if (hfc->regd.brq[0]) {
							skb_queue_tail
							    (hfc->regd.
							     brq[0],
							     fifo->buff);
							hfc->regd.
							    bch_l1l2(hfc->
								     regd.
								     arg_hisax,
								     0,
								     PH_DATA
								     |
								     INDICATION,
								     (void
								      *)
								     fifo->
								     buff);
						} else
							dev_kfree_skb_any
							    (fifo->buff);
						break;

					case HFCUSB_B2_RX:
						if (hfc->regd.brq[1]) {
							skb_queue_tail
							    (hfc->regd.
							     brq[1],
							     fifo->buff);
							hfc->regd.
							    bch_l1l2(hfc->
								     regd.
								     arg_hisax,
								     1,
								     PH_DATA
								     |
								     INDICATION,
								     (void
								      *)
								     fifo->
								     buff);
						} else
							dev_kfree_skb_any
							    (fifo->buff);
						break;

					case HFCUSB_PCM_RX:
						skb_queue_tail(&hfc->regd.
							       erq,
							       fifo->buff);
						test_and_set_bit
						    (HFCUSB_L1_ERX,
						     &hfc->l1_event);
						queue_task(&hfc->l1_tq,
							   &tq_immediate);
						mark_bh(IMMEDIATE_BH);
						break;

					default:
						dev_kfree_skb_any(fifo->
								  buff);
						break;
				}
				fifo->buff = skb;
			}
			fifo->buff->len = 0;	/* reset counter */
			fifo->act_ptr = fifo->buff->data;	/* and pointer */
		}
	}
	fifo->rx_offset = (urb->actual_length < fifo->usb_maxlen) ? 2 : 0;
}				/* rx_complete */

/***************************************************/
/* start the interrupt transfer for the given fifo */
/***************************************************/
static void
start_rx_fifo(usb_fifo * fifo)
{
	if (fifo->buff)
		return;		/* still active */
	if (!
	    (fifo->buff =
	     dev_alloc_skb(fifo->max_size + (fifo->transmode ? 0 : 3))))
		return;
	fifo->act_ptr = fifo->buff->data;
	FILL_INT_URB(&fifo->urb, fifo->hfc->dev, fifo->pipe, fifo->buffer,
		     fifo->usb_maxlen, rx_complete, fifo, fifo->intervall);
	fifo->next_complete = 0;
	fifo->rx_offset = 2;
	fifo->active = 1;	/* must be marked active */
	fifo->hfc->active_fifos |= fifo->fifo_mask;
	if (usb_submit_urb(&fifo->urb)) {
		fifo->active = 0;
		fifo->hfc->active_fifos &= ~fifo->fifo_mask;
		dev_kfree_skb_any(fifo->buff);
		fifo->buff = NULL;
	}
}				/* start_rx_fifo */

/***************************************************************/
/* control completion routine handling background control cmds */
/***************************************************************/
static void
ctrl_complete(purb_t urb)
{
	hfcusb_data *hfc = (hfcusb_data *) urb->context;

	if (hfc->ctrl_cnt) {
		switch (hfc->ctrl_buff[hfc->ctrl_out_idx].hfc_reg) {
			case HFCUSB_FIFO:
				hfc->ctrl_fifo =
				    hfc->ctrl_buff[hfc->ctrl_out_idx].
				    reg_val;
				break;
			case HFCUSB_F_USAGE:
				if (!hfc->dfifo_fill) {
					fill_tx_urb(hfc->fifos +
						    HFCUSB_D_TX);
					if (hfc->fifos[HFCUSB_D_TX].buff)
						usb_submit_urb(&hfc->
							       fifos
							       [HFCUSB_D_TX].
							       urb);
				} else {
					queue_control_request(hfc,
							      HFCUSB_FIFO,
							      HFCUSB_D_TX);
					queue_control_request(hfc,
							      HFCUSB_F_USAGE,
							      0);
				}
				break;
			case HFCUSB_SCTRL_R:
				switch (hfc->ctrl_fifo) {
					case HFCUSB_B1_RX:
						if (hfc->bch_enables & 1)
							start_rx_fifo(hfc->
								      fifos
								      +
								      HFCUSB_B1_RX);
						break;
					case HFCUSB_B2_RX:
						if (hfc->bch_enables & 2)
							start_rx_fifo(hfc->
								      fifos
								      +
								      HFCUSB_B2_RX);
						break;
				}
				break;
		}
		hfc->ctrl_cnt--;	/* decrement actual count */
		if (++hfc->ctrl_out_idx >= HFC_CTRL_BUFSIZE)
			hfc->ctrl_out_idx = 0;	/* pointer wrap */
		ctrl_start_transfer(hfc);	/* start next transfer */
	}
}				/* ctrl_complete */

/*****************************************/
/* Layer 1 + D channel access from HiSax */
/*****************************************/
static void
hfcusb_l1_access(void *drvarg, int pr, void *arg)
{
	hfcusb_data *hfc = (hfcusb_data *) drvarg;

	switch (pr) {
		case (PH_DATA | REQUEST):
		case (PH_PULL | INDICATION):
			skb_queue_tail(hfc->regd.dsq,
				       (struct sk_buff *) arg);
			if (!hfc->fifos[HFCUSB_D_TX].active
			    && !hfc->dfifo_fill) {
				fill_tx_urb(hfc->fifos + HFCUSB_D_TX);
				hfc->active_fifos |=
				    hfc->fifos[HFCUSB_D_TX].fifo_mask;
				usb_submit_urb(&hfc->fifos[HFCUSB_D_TX].
					       urb);
			}
			break;
		case (PH_ACTIVATE | REQUEST):
			if (hfc->l1_state < 6) {
				queue_control_request(hfc, HFCUSB_STATES, 0x60);	/* start activation */
				hfc->t3_timer.expires =
				    jiffies + (HFC_TIMER_T3 * HZ) / 1000;
				if (!timer_pending(&hfc->t3_timer))
					add_timer(&hfc->t3_timer);
			} else
				hfc->regd.dch_l1l2(hfc->regd.arg_hisax,
						   (PH_ACTIVATE |
						    INDICATION), NULL);
			break;

		case (PH_DEACTIVATE | REQUEST):
			queue_control_request(hfc, HFCUSB_STATES, 0x40);	/* start deactivation */
			break;
		default:
			printk(KERN_INFO "unknown hfcusb l1_access 0x%x\n",
			       pr);
			break;
	}
}				/* hfcusb_l1_access */

/*******************************/
/* B channel access from HiSax */
/*******************************/
static void
hfcusb_bch_access(void *drvarg, int chan, int pr, void *arg)
{
	hfcusb_data *hfc = (hfcusb_data *) drvarg;
	usb_fifo *fifo = hfc->fifos + (chan ? HFCUSB_B2_TX : HFCUSB_B1_TX);
	long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
		case (PH_PULL | INDICATION):
			save_flags(flags);
			cli();
			if (!fifo->active) {
				fill_tx_urb(fifo);
				hfc->active_fifos |= fifo->fifo_mask;
				usb_submit_urb(&fifo->urb);
			}
			restore_flags(flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			if (!((int) arg)) {
				hfc->bch_enables &= ~(1 << chan);
				if (fifo->active) {
					fifo->active = 0;
					usb_unlink_urb(&fifo->urb);
				}
				save_flags(flags);
				cli();
				queue_control_request(hfc, HFCUSB_FIFO,
						      fifo->fifonum);
				queue_control_request(hfc,
						      HFCUSB_INC_RES_F, 2);
				queue_control_request(hfc, HFCUSB_CON_HDLC,
						      9);
				queue_control_request(hfc, HFCUSB_SCTRL,
						      0x40 +
						      hfc->bch_enables);
				queue_control_request(hfc, HFCUSB_SCTRL_R,
						      hfc->bch_enables);
				restore_flags(flags);
				fifo++;
				if (fifo->active) {
					fifo->active = 0;
					usb_unlink_urb(&fifo->urb);
				}
				return;	/* fifo deactivated */
			}
			fifo->transmode = ((int) arg == L1_MODE_TRANS);
			fifo->max_size =
			    ((fifo->transmode) ? fifo->
			     usb_maxlen : MAX_BCH_SIZE);
			(fifo + 1)->transmode = fifo->transmode;
			(fifo + 1)->max_size = fifo->max_size;
			hfc->bch_enables |= (1 << chan);
			save_flags(flags);
			cli();
			queue_control_request(hfc, HFCUSB_FIFO,
					      fifo->fifonum);
			queue_control_request(hfc, HFCUSB_CON_HDLC,
					      ((!fifo->
						transmode) ? 9 : 11));
			queue_control_request(hfc, HFCUSB_INC_RES_F, 2);
			queue_control_request(hfc, HFCUSB_SCTRL,
					      0x40 + hfc->bch_enables);
			if ((int) arg == L1_MODE_HDLC)
				queue_control_request(hfc, HFCUSB_CON_HDLC,
						      8);
			queue_control_request(hfc, HFCUSB_FIFO,
					      fifo->fifonum + 1);
			queue_control_request(hfc, HFCUSB_CON_HDLC,
					      ((!fifo->
						transmode) ? 8 : 10));
			queue_control_request(hfc, HFCUSB_INC_RES_F, 2);
			queue_control_request(hfc, HFCUSB_SCTRL_R,
					      hfc->bch_enables);
			restore_flags(flags);

			break;

		default:
			printk(KERN_INFO
			       "unknown hfcusb bch_access chan %d 0x%x\n",
			       chan, pr);
			break;
	}
}				/* hfcusb_bch_access */

/***************************************************************************/
/* usb_init is called once when a new matching device is detected to setup */
/* main parmeters. It registers the driver at the main hisax module.       */
/* on success 0 is returned.                                               */
/***************************************************************************/
static int
usb_init(hfcusb_data * hfc)
{
	usb_fifo *fifo;
	int i;
	u_char b;

	/* check the chip id */
	if ((Read_hfc(hfc, HFCUSB_CHIP_ID, &b) != 1) ||
	    (b != HFCUSB_CHIPID)) {
		printk(KERN_INFO "HFC-USB: Invalid chip id 0x%02x\n", b);
		return (1);
	}

	/* first set the needed config, interface and alternate */
	usb_set_configuration(hfc->dev, 1);
	usb_set_interface(hfc->dev, hfc->if_used, hfc->alt_used);

	/* now we initialise the chip */
	Write_hfc(hfc, HFCUSB_CIRM, 0x10);	/* aux = output, reset off */
	Write_hfc(hfc, HFCUSB_USB_SIZE,
		  (hfc->fifos[HFCUSB_B1_TX].usb_maxlen >> 3) |
		  ((hfc->fifos[HFCUSB_B1_RX].usb_maxlen >> 3) << 4));

	/* enable PCM/GCI master mode */
	Write_hfc(hfc, HFCUSB_MST_MODE1, 0);	/* set default values */
	Write_hfc(hfc, HFCUSB_MST_MODE0, 1);	/* enable master mode */

	/* init the fifos */
	Write_hfc(hfc, HFCUSB_F_THRES, (HFCUSB_TX_THRESHOLD >> 3) |
		  ((HFCUSB_RX_THRESHOLD >> 3) << 4));

	for (i = 0, fifo = hfc->fifos + i; i < HFCUSB_NUM_FIFOS;
	     i++, fifo++) {
		Write_hfc(hfc, HFCUSB_FIFO, i);	/* select the desired fifo */

		fifo->transmode = 0;	/* hdlc mode selected */
		fifo->buff = NULL;	/* init buffer pointer */
		fifo->max_size =
		    (i <= HFCUSB_B2_RX) ? MAX_BCH_SIZE : MAX_DCH_SIZE;
		Write_hfc(hfc, HFCUSB_HDLC_PAR, ((i <= HFCUSB_B2_RX) ? 0 : 2));	/* data length */
		Write_hfc(hfc, HFCUSB_CON_HDLC, ((i & 1) ? 0x08 : 0x09));	/* rx hdlc, tx fill 1 */
		Write_hfc(hfc, HFCUSB_INC_RES_F, 2);	/* reset the fifo */
	}

	Write_hfc(hfc, HFCUSB_CLKDEL, 0x0f);	/* clock delay value */
	Write_hfc(hfc, HFCUSB_STATES, 3 | 0x10);	/* set deactivated mode */
	Write_hfc(hfc, HFCUSB_STATES, 3);	/* enable state machine */

	Write_hfc(hfc, HFCUSB_SCTRL_R, 0);	/* disable both B receivers */
	Write_hfc(hfc, HFCUSB_SCTRL, 0x40);	/* disable B transmitters + cap mode */

	/* init the l1 timer */
	init_timer(&hfc->t3_timer);
	hfc->t3_timer.data = (long) hfc;
	hfc->t3_timer.function = (void *) timer_t3_expire;
	hfc->l1_tq.routine = (void *) (void *) usb_l1d_bh;
	hfc->l1_tq.sync = 0;
	hfc->l1_tq.data = hfc;

	/* init the background control machinery */
	hfc->ctrl_read.requesttype = 0xc0;
	hfc->ctrl_read.request = 1;
	hfc->ctrl_read.length = 1;
	hfc->ctrl_write.requesttype = 0x40;
	hfc->ctrl_write.request = 0;
	hfc->ctrl_write.length = 0;
	FILL_CONTROL_URB(&hfc->ctrl_urb, hfc->dev, hfc->ctrl_out_pipe,
			 (u_char *) & hfc->ctrl_write, NULL, 0,
			 ctrl_complete, hfc);

	/* init the TX-urbs */
	fifo = hfc->fifos + HFCUSB_D_TX;
	FILL_BULK_URB(&fifo->urb, hfc->dev, fifo->pipe,
		      (u_char *) fifo->buffer, 0, tx_complete, fifo);
	fifo = hfc->fifos + HFCUSB_B1_TX;
	FILL_BULK_URB(&fifo->urb, hfc->dev, fifo->pipe,
		      (u_char *) fifo->buffer, 0, tx_complete, fifo);
	fifo = hfc->fifos + HFCUSB_B2_TX;
	FILL_BULK_URB(&fifo->urb, hfc->dev, fifo->pipe,
		      (u_char *) fifo->buffer, 0, tx_complete, fifo);

	/* init the E-buffer */
	skb_queue_head_init(&hfc->regd.erq);

	/* now register ourself at hisax */
	hfc->regd.version = HISAX_LOAD_VERSION;	/* set our version */
	hfc->regd.cmd = HISAX_LOAD_REGISTER;	/* register command */
	hfc->regd.argl1 = (void *) hfc;	/* argument for our local routine */
	hfc->regd.dch_l2l1 = hfcusb_l1_access;
	hfc->regd.bch_l2l1 = hfcusb_bch_access;
	hfc->regd.drvname = "hfc_usb";
	if (hisax_register_hfcusb(&hfc->regd)) {
		printk(KERN_INFO "HFC-USB failed to register at hisax\n");
		Write_hfc(hfc, HFCUSB_CIRM, 0x08);	/* aux = input, reset on */
		return (1);
	}

	/* startup the D- and E-channel fifos */
	start_rx_fifo(hfc->fifos + HFCUSB_D_RX);	/* D-fifo */
	if (hfc->fifos[HFCUSB_PCM_RX].pipe)
		start_rx_fifo(hfc->fifos + HFCUSB_PCM_RX);	/* E-fifo */

	return (0);
}				/* usb_init */

/*************************************************/
/* function called to probe a new plugged device */
/*************************************************/
static void *
hfc_usb_probe(struct usb_device *dev, unsigned int interface
#ifdef COMPAT_HAS_USB_IDTAB
	      , const struct usb_device_id *id_table)
#else
    )
#endif
{
	hfcusb_data *context;
	struct usb_interface *ifp = dev->actconfig->interface + interface;
	struct usb_interface_descriptor *ifdp =
	    ifp->altsetting + ifp->act_altsetting;
	struct usb_endpoint_descriptor *epd;
	int i, idx, ep_msk;

#ifdef COMPAT_HAS_USB_IDTAB
	if (id_table && (dev->descriptor.idVendor == id_table->idVendor) &&
	    (dev->descriptor.idProduct == id_table->idProduct) &&
#else
	if ((dev->descriptor.idVendor == 0x959) &&
	    (dev->descriptor.idProduct == 0x2bd0) &&
#endif
	    (ifdp->bNumEndpoints >= 6) && (ifdp->bNumEndpoints <= 16)) {

		if (!(context = kmalloc(sizeof(hfcusb_data), GFP_KERNEL))) {
			return (NULL);	/* got no mem */
		};
		memset(context, 0, sizeof(hfcusb_data));	/* clear the structure */
		i = ifdp->bNumEndpoints;	/* get number of endpoints */
		ep_msk = 0;	/* none found */
		epd = ifdp->endpoint;	/* first endpoint descriptor */
		while (i-- && ((ep_msk & 0xcf) != 0xcf)) {

			idx = (((epd->bEndpointAddress & 0x7f) - 1) << 1);	/* get endpoint base */
			if (idx < 7) {
				switch (epd->bmAttributes) {
					case USB_ENDPOINT_XFER_INT:
						if (!
						    (epd->
						     bEndpointAddress &
						     0x80))
							break;	/* only interrupt in allowed */
						idx++;	/* input index is odd */
						context->fifos[idx].pipe =
						    usb_rcvintpipe(dev,
								   epd->
								   bEndpointAddress);
						break;

					case USB_ENDPOINT_XFER_BULK:
						if (epd->
						    bEndpointAddress &
						    0x80)
							break;	/* only bulk out allowed */
						context->fifos[idx].pipe =
						    usb_sndbulkpipe(dev,
								    epd->
								    bEndpointAddress);
						break;
					default:
						context->fifos[idx].pipe = 0;	/* reset data */
				}	/* switch attribute */

				if (context->fifos[idx].pipe) {
					context->fifos[idx].fifonum = idx;
					context->fifos[idx].fifo_mask =
					    1 << idx;
					context->fifos[idx].hfc = context;
					context->fifos[idx].usb_maxlen =
					    epd->wMaxPacketSize;
					context->fifos[idx].intervall =
					    epd->bInterval;
					ep_msk |= (1 << idx);
				} else
					ep_msk &= ~(1 << idx);
			}	/* idx < 7 */
			epd++;
		}

		if ((ep_msk & 0x3f) != 0x3f) {
			kfree(context);
			return (NULL);
		}
		MOD_INC_USE_COUNT;	/* lock our module */
		context->dev = dev;	/* save device */
		context->if_used = interface;	/* save used interface */
		context->alt_used = ifp->act_altsetting;	/* and alternate config */
		context->ctrl_paksize = dev->descriptor.bMaxPacketSize0;	/* control size */

		/* create the control pipes needed for register access */
		context->ctrl_in_pipe = usb_rcvctrlpipe(context->dev, 0);
		context->ctrl_out_pipe = usb_sndctrlpipe(context->dev, 0);

		/* init the chip and register the driver */
		if (usb_init(context)) {
			kfree(context);
			MOD_DEC_USE_COUNT;
			return (NULL);
		}

		printk(KERN_INFO
		       "HFC-USB: New device if=%d alt=%d registered\n",
		       context->if_used, context->alt_used);
		return (context);
	}

	return (NULL);		/* no matching entry */
}				/* hfc_usb_probe */

/****************************************************/
/* function called when an active device is removed */
/****************************************************/
static void
hfc_usb_disconnect(struct usb_device *usbdev, void *drv_context)
{
	hfcusb_data *context = drv_context;
	int i;
	struct sk_buff *skb;

	/* tell all fifos to terminate */
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++)
		if (context->fifos[i].active) {
			context->fifos[i].active = 0;
			usb_unlink_urb(&context->fifos[i].urb);
		}
	while (context->active_fifos) {
		set_current_state(TASK_INTERRUPTIBLE);
		/* Timeout 10ms */
		schedule_timeout((10 * HZ) / 1000);
	}
	if (timer_pending(&context->t3_timer))
		del_timer(&context->t3_timer);
	context->regd.release_driver(context->regd.arg_hisax);
	while ((skb = skb_dequeue(&context->regd.erq)) != NULL)
		dev_kfree_skb_any(skb);

	kfree(context);		/* free our structure again */
	MOD_DEC_USE_COUNT;	/* and decrement the usage counter */
}				/* hfc_usb_disconnect */

/************************************/
/* our driver information structure */
/************************************/
static struct usb_driver hfc_drv = {
	name:"hfc_usb",
#ifdef COMPAT_HAS_USB_IDTAB
	id_table:hfc_usb_idtab,
#endif
	probe:hfc_usb_probe,
	disconnect:hfc_usb_disconnect,
};

static void __exit
hfc_usb_exit(void)
{

	usb_deregister(&hfc_drv);	/* release our driver */
	printk(KERN_INFO "HFC-USB module removed\n");
}

static int __init
hfc_usb_init(void)
{
	struct hisax_drvreg drv;

	drv.version = HISAX_LOAD_VERSION;	/* set our version */
	drv.cmd = HISAX_LOAD_CHKVER;	/* check command only */
	if (hisax_register_hfcusb(&drv)) {
		printk(KERN_INFO "HFC-USB <-> hisax version conflict\n");
		return (-1);	/* unable to register */
	}
	if (usb_register(&hfc_drv)) {
		printk(KERN_INFO
		       "Unable to register HFC-USB module at usb stack\n");
		return (-1);	/* unable to register */
	}

	printk(KERN_INFO "HFC-USB module loaded\n");
	return (0);
}

module_init(hfc_usb_init);
module_exit(hfc_usb_exit);
