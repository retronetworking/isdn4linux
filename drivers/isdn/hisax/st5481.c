/*
 *
 * HiSax ISDN driver - chip specific routines for ST5481 USB ISDN modem
 *
 * Author       Frode Isaksen (fisaksen@bewan.com)
 *
 *
 */ 

#include <linux/config.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/string.h>
#define __NO_VERSION__
#include "hisax.h"
#include "isdnl1.h"
#include "st5481_hdlc.h"
#include "st5481.h"
#include "st5481-debug.h"

#define ERR(format, arg...) \
printk(KERN_ERR __FILE__ ": " __FUNCTION__ ": " format "\n" , ## arg)

#define WARN(format, arg...) \
printk(KERN_WARNING __FILE__ ": " __FUNCTION__ ": " format "\n" , ## arg)

#define INFO(format, arg...) \
printk(KERN_INFO __FILE__ ": " __FUNCTION__ ": " format "\n" , ## arg)

static const char *st5481_revision = "$Revision$";

/* Internal event, make sure it does not overlap with the events in isdnl1.h!!! */
#define D_OUT_EVENT 10

/* Generic FIFO structure */ 
struct fifo {
	u_char r,w,count,size;
	spinlock_t lock;
};

#define MAX_EVT_FIFO 16
struct evt_fifo {
	struct fifo f;
	unsigned int data[MAX_EVT_FIFO];
};	

typedef void (*ctrl_complete_t)(void *);

typedef struct ctrl_msg {
	devrequest dr;
	ctrl_complete_t complete;
	void * context;
} ctrl_msg; 

#define MAX_EP0_MSG 16
struct ctrl_msg_fifo {
	struct fifo f;
	struct ctrl_msg data[MAX_EP0_MSG];
};	


/*
  FIFO utility functions.
*/

/*
  Get the index to the next entry to write to the FIFO without updating write index.
*/
static int
fifo_get(struct fifo *fifo)
{
	unsigned long flags;
	int index;

	if (!fifo) {
		return -1;
	}
      
	spin_lock_irqsave(&fifo->lock, flags);		
	if (fifo->count==fifo->size) {
		// FIFO full
		index = -1;
	} else {
		// Return index where to get the next data to add to the FIFO
		index = fifo->w & (fifo->size-1); 
	}
	spin_unlock_irqrestore(&fifo->lock, flags);

	return index;
}

/*
  Add an entry to the FIFO by incrementing write index.
*/
static void
fifo_add(struct fifo *fifo)
{
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	fifo->w++;
	fifo->count++;
	spin_unlock_irqrestore(&fifo->lock, flags);
}

/*
  Remove an entry from the FIFO with the index returned.
*/
static int
fifo_remove(struct fifo *fifo)
{
	unsigned long flags;
	int index;

	if (!fifo) {
		return -1;
	}

	spin_lock_irqsave(&fifo->lock, flags);		
	if (!fifo->count) {
		// FIFO empty
		index = -1;
	} else {
		// Return index where to get the next data from the FIFO
		index = fifo->r++ & (fifo->size-1); 
		fifo->count--;
	}
	spin_unlock_irqrestore(&fifo->lock, flags);

	return index;
}


/*
  USB utility functions.
*/

/*
  USB double buffering, return the URB index (0 or 1).
*/
static inline int 
get_buf_nr(struct urb *urbs[], struct urb *urb)
{
	return (urbs[0]==urb ? 0 : 1); 
}

/*
  Submit an URB with error reporting. This is a macro so
  the __FUNCTION__ returns the caller function name.
*/
#define submit_urb(urb) \
{ \
	int status; \
	if ((status = usb_submit_urb(urb)) < 0) { \
		WARN("usb_submit_urb failed,status=%d",status); \
	} \
}

static inline unsigned int
usb_b_sndisocpipe(struct usb_device *dev, int channel)
{
	return usb_sndisocpipe(dev, EP_B1_OUT + (channel*2));
}

static inline unsigned int
usb_b_rcvisocpipe(struct usb_device *dev, int channel)
{
	return usb_rcvisocpipe(dev, (EP_B1_IN + (channel*2)) | USB_DIR_IN);
}

/*
  Fill the ISOC URB.
*/
static void
fill_isoc_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe,
	      void *buf, int num_packets, int packet_size,
	      usb_complete_t complete, void *context) 
{
	int k;

	spin_lock_init(&urb->lock);
	urb->dev=dev;
	urb->pipe=pipe;
	urb->transfer_buffer=buf;
	urb->number_of_packets = num_packets;
	urb->transfer_buffer_length=num_packets*packet_size;
	urb->actual_length = 0;
	urb->complete=complete;
	urb->context=context;
	urb->transfer_flags=USB_ISO_ASAP;
	for (k = 0; k < num_packets; k++) {
		urb->iso_frame_desc[k].offset = packet_size * k;
		urb->iso_frame_desc[k].length = packet_size;
		urb->iso_frame_desc[k].actual_length = 0;
	}
}

/*
  Make the transfer_buffer contiguous by
  copying from the iso descriptors if necessary. 
*/
static int 
isoc_flatten(struct urb *urb)
{
	piso_packet_descriptor_t pipd,pend;
	unsigned char *src,*dst;
	unsigned int len;
	
	if (urb->status < 0) {
		return urb->status;
	}
	for (pipd = &urb->iso_frame_desc[0],
		     pend = &urb->iso_frame_desc[urb->number_of_packets],
		     dst = urb->transfer_buffer; 
	     pipd < pend; 
	     pipd++) {
		
		if (pipd->status < 0) {
			return (pipd->status);
		}
	
		len = pipd->actual_length;
		pipd->actual_length = 0;
		src = urb->transfer_buffer+pipd->offset;

		if (src != dst) {
			// Need to copy since isoc buffers not full
			while (len--) {
				*dst++ = *src++;
			}			
		} else {
			// No need to copy, just update destination buffer
			dst += len;
		}
	}
	// Return size of flattened buffer
	return (dst - (unsigned char *)urb->transfer_buffer);
}


/*
  USB control endpoint functions
*/

/*
  Send the next endpoint 0 request stored in the FIFO.
  Called either by the completion or by usb_ctrl_msg.
*/
static void 
usb_next_ctrl_msg(struct urb *urb, struct st5481_hw *hw)
{
	int r_index;

	if (test_and_set_bit(0,&hw->ctrl_busy)) {
		return;
	}

	if (hw->ctrl_msg_fifo == NULL) {
		test_and_clear_bit(0,&hw->ctrl_busy);
		return;
	}

	if ((r_index = fifo_remove(&hw->ctrl_msg_fifo->f)) < 0) {
		test_and_clear_bit(0,&hw->ctrl_busy);
		return;
	} 
	urb->setup_packet = 
		(unsigned char *)&hw->ctrl_msg_fifo->data[r_index];

	
	DBG(1,"request=0x%02x,value=0x%04x,index=%x",
	    ((struct ctrl_msg *)urb->setup_packet)->dr.request,
	    ((struct ctrl_msg *)urb->setup_packet)->dr.value,
	    ((struct ctrl_msg *)urb->setup_packet)->dr.index);

	// Prepare the URB
	urb->dev = hw->dev;

	submit_urb(urb);
}

/*
  The request on endpoint 0 has completed.
  Call the user provided completion routine and try
  to send the next request.
*/
static void 
usb_ctrl_complete(struct urb *urb)
{
	struct IsdnCardState *cs = (struct IsdnCardState *)urb->context;
	struct st5481_hw *hw = &cs->hw.st5481;
	struct ctrl_msg *ctrl_msg;
	
	if (urb->status < 0) {
		if (urb->status != USB_ST_URB_KILLED) {
			WARN("urb status %d",urb->status);
		} else {
			DBG(1,"urb killed");
			return; // Give up
		}
	}

	ctrl_msg = (struct ctrl_msg *)urb->setup_packet;
	
	if (ctrl_msg->dr.request == USB_REQ_CLEAR_FEATURE) {
	        /* Special case handling for pipe reset */
		le16_to_cpus(&ctrl_msg->dr.index);
		usb_endpoint_running(hw->dev,
				     ctrl_msg->dr.index & ~USB_DIR_IN, 
				     (ctrl_msg->dr.index & USB_DIR_IN) == 0);

		/* toggle is reset on clear */
		usb_settoggle(hw->dev, 
			      ctrl_msg->dr.index & ~USB_DIR_IN, 
			      (ctrl_msg->dr.index & USB_DIR_IN) == 0,
			      0);


	}
	
	if (ctrl_msg->complete) {
		ctrl_msg->complete(ctrl_msg->context);
	}

	test_and_clear_bit(0, &hw->ctrl_busy);
	
	// Try to send next control message
	usb_next_ctrl_msg(urb, hw);
	return;
}

/*
  Asynchronous endpoint 0 request (async version of usb_control_msg).
  The request will be queued up in a FIFO if the endpoint is busy.
*/
static void 
usb_ctrl_msg(struct IsdnCardState *cs,
	     __u8 request, __u8 requesttype, __u16 value, __u16 index,
	     ctrl_complete_t complete, void *context)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	int w_index;
	struct ctrl_msg *ctrl_msg;
	
	if (hw->ctrl_msg_fifo == NULL) {
		return;
	}

	if ((w_index = fifo_get(&hw->ctrl_msg_fifo->f)) < 0) {
		WARN("control msg FIFO full");
		return;
	}
	ctrl_msg = &hw->ctrl_msg_fifo->data[w_index]; 
   
	ctrl_msg->dr.requesttype = requesttype;
	ctrl_msg->dr.request = request;
	ctrl_msg->dr.value = cpu_to_le16p(&value);
	ctrl_msg->dr.index = cpu_to_le16p(&index);
	ctrl_msg->dr.length = 0;
	ctrl_msg->complete = complete;
	ctrl_msg->context = context;

	// Add this msg to the FIFO
	fifo_add(&hw->ctrl_msg_fifo->f);

	usb_next_ctrl_msg(hw->ctrl_urb, hw);
}

/*
  Asynchronous endpoint 0 device request.
*/
static void 
usb_device_ctrl_msg(struct IsdnCardState *cs,
		    __u8 request, __u16 value,
		    ctrl_complete_t complete,void *context)
{
	usb_ctrl_msg(cs,
		     request, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, value, 0,
		     complete, context);
}

/*
  Asynchronous pipe reset (async version of usb_clear_halt).
*/
static void 
usb_pipe_reset(struct IsdnCardState *cs,
	       u_char pipe,
	       ctrl_complete_t complete,void *context)
{
	DBG(1,"pipe=%02x",pipe);

	usb_ctrl_msg(cs,
		     USB_REQ_CLEAR_FEATURE, USB_DIR_OUT | USB_RECIP_ENDPOINT, 0, pipe,
		     complete, context);
}


/*
  B channel functions
*/

static void
st5481B_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
  Encode and transmit next frame.
*/
static void 
usb_b_out(struct BCState *bcs,int buf_nr)
{
	struct st5481B_hw *hw = &bcs->hw.st5481;
	struct urb *urb;
	unsigned int packet_size,offset;
	int len,buf_size,bytes_sent;
	int i;
	struct sk_buff *skb;
	
	if (test_and_set_bit(buf_nr,&hw->b_out_busy)) {
		DBG(4,"ep %d urb %d busy",(bcs->channel+1)*2,buf_nr);
		return;
	}
	urb = hw->b_out_urb[buf_nr];

	// Adjust isoc buffer size according to flow state
#define B_FLOW_ADJUST 2
	if(hw->b_flow_event & (OUT_DOWN | OUT_UNDERRUN)) {
		buf_size = NUM_ISO_PACKETS_B*8+B_FLOW_ADJUST;
		packet_size = 8+B_FLOW_ADJUST;
		DBG(4,"B%d,adjust flow,add %d bytes",bcs->channel+1,B_FLOW_ADJUST);
	} else if(hw->b_flow_event & OUT_UP){
		buf_size = NUM_ISO_PACKETS_B*8-B_FLOW_ADJUST;
		packet_size = 8-B_FLOW_ADJUST;
		DBG(4,"B%d,adjust flow,remove %d bytes",bcs->channel+1,B_FLOW_ADJUST);
	} else {
		buf_size = NUM_ISO_PACKETS_B*8;
		packet_size = 8;
	}
	hw->b_flow_event = 0;
#undef B_FLOW_ADJUST

	len = 0;
	while (len < buf_size) {
		if ((skb = bcs->tx_skb)) {
			DUMP_SKB(0x10, skb);
			DBG(4,"B%d,len=%d",bcs->channel+1,skb->len);
			
			if (bcs->mode == L1_MODE_TRANS) {	
				bytes_sent = (buf_size-len) > skb->len ? skb->len : (buf_size-len);

				memcpy(skb->data, urb->transfer_buffer+len, buf_size);
				
				len += bytes_sent;
			} else {
				len += hdlc_encode(hw->hdlc_state_out, 
						   skb->data, skb->len, &bytes_sent,
						   urb->transfer_buffer+len, buf_size-len);
			}

			skb_pull(skb, bytes_sent);
			
			if (!skb->len) {
				// Frame sent
				int tmp = bcs->tx_cnt;
				bcs->tx_cnt = 0;

				if (bcs->st->lli.l1writewakeup &&
				    (PACKET_NOACK != skb->pkt_type))
					bcs->st->lli.l1writewakeup(bcs->st, tmp);
				dev_kfree_skb_any(skb);
				if (!(bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
					st5481B_sched_event(bcs, B_XMTBUFREADY);
				}
			}
		} else {
			// Send flags
			len += hdlc_encode(hw->hdlc_state_out, 
					   NULL, 0, &bytes_sent,
					   urb->transfer_buffer+len, buf_size-len);
		}	
	}

	// Prepare the URB
	offset = 0;
	for(i=0,offset=0;i<NUM_ISO_PACKETS_B;i++){
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = packet_size;
		offset += packet_size;
		packet_size = 8;
		if (offset >= buf_size){
			packet_size = 0;
			offset = buf_size;
		}
	}
	urb->transfer_buffer_length = buf_size;
	urb->number_of_packets = NUM_ISO_PACKETS_B;
	urb->dev = bcs->cs->hw.st5481.dev;

	DUMP_ISO_PACKET(0x10,urb);

	submit_urb(urb);
}


static void 
usb_b_out_complete(struct urb *urb)
{
	struct BCState *bcs = (struct BCState *)urb->context;
	struct st5481B_hw *hw = &bcs->hw.st5481;
	int buf_nr;
	
	buf_nr = get_buf_nr(hw->b_out_urb, urb);
	test_and_clear_bit(buf_nr, &hw->b_out_busy);

	if (urb->status < 0) {
		if (urb->status != USB_ST_URB_KILLED) {
			WARN("urb status %d",urb->status);
			if (hw->b_out_busy == 0) {
				usb_pipe_reset(bcs->cs, (bcs->channel+1)*2 | USB_DIR_OUT, NULL, NULL);
			}
		} else {
			DBG(1,"urb killed"); 
			return; // Give up
		}
	}

	usb_b_out(bcs,buf_nr);
}

/*
  Start transfering (flags or data) on the B channel, since
  FIFO counters has been set to a non-zero value.
*/
static void 
st5481B_start_xfer(void *context)
{
	struct BCState *bcs = context;
	struct st5481B_hw *hw = &bcs->hw.st5481;
	unsigned int pipe;

	DBG(4,"B%d",bcs->channel+1);

	// Start receiving from B channel
	pipe = usb_b_rcvisocpipe(bcs->cs->hw.st5481.dev,bcs->channel);
	
	hw->b_in_urb[0]->pipe = pipe;
	hw->b_in_urb[0]->dev = bcs->cs->hw.st5481.dev;
	submit_urb(hw->b_in_urb[0]);

	hw->b_in_urb[1]->pipe = pipe;
	hw->b_in_urb[1]->dev = bcs->cs->hw.st5481.dev;
	submit_urb(hw->b_in_urb[1]);
		
	// Start transmitting (flags or data) on B channel
	pipe = usb_b_sndisocpipe(bcs->cs->hw.st5481.dev,bcs->channel);

	hw->b_out_urb[0]->pipe = pipe;	
	usb_b_out(bcs,0);

	hw->b_out_urb[1]->pipe = pipe;	
	usb_b_out(bcs,1);
}

/*
  Start or stop the transfer on the B channel.
*/
static void
st5481B_mode(struct BCState *bcs, int mode, int bc)
{
	struct st5481B_hw *hw = &bcs->hw.st5481;

	DBG(4,"B%d,mode=%d,bcs=%d",bc+1,mode,bcs==&bcs->cs->bcs[0] ? 0 : 1);

	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs, "ST5481 bchannel mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	
	if ((bcs->mode == mode) && (bcs->channel == bc)) {
		return;
	}

	bcs->mode = mode;
	bcs->channel = bc;

	// Cancel all USB transfers on this B channel
	usb_unlink_urb(hw->b_in_urb[0]);
	usb_unlink_urb(hw->b_in_urb[1]);
	usb_unlink_urb(hw->b_out_urb[0]);
	usb_unlink_urb(hw->b_out_urb[1]);
	hw->b_out_busy = 0;

	if (bcs->mode) {
		// Open the B channel
		if (bcs->mode != L1_MODE_TRANS) {
			hdlc_rcv_init(hw->hdlc_state_in, bcs->mode == L1_MODE_HDLC_56K);
			hdlc_out_init(hw->hdlc_state_out, 0, bcs->mode == L1_MODE_HDLC_56K);
		}
		usb_pipe_reset(bcs->cs, (bcs->channel+1)*2, NULL, NULL);
		usb_pipe_reset(bcs->cs, (bcs->channel+1)*2+1, NULL, NULL);
	
		// Enable B channel interrupts
		usb_device_ctrl_msg(bcs->cs, FFMSK_B1+(bcs->channel*2), 
				    OUT_UP+OUT_DOWN+OUT_UNDERRUN, NULL, NULL);

		// Enable B channel FIFOs
		usb_device_ctrl_msg(bcs->cs, OUT_B1_COUNTER+(bcs->channel*2), 32, NULL, NULL);
		usb_device_ctrl_msg(bcs->cs, IN_B1_COUNTER+(bcs->channel*2), 32, 
				    st5481B_start_xfer, bcs);
#if NUMBER_OF_LEDS == 4
		if (bc == 0) {
			bcs->cs->hw.st5481.leds |= B1_LED;
		} else {
			bcs->cs->hw.st5481.leds |= B2_LED;
		}
#endif
		
	} else {
		// Disble B channel interrupts
		usb_device_ctrl_msg(bcs->cs, FFMSK_B1+(bcs->channel*2), 0, NULL, NULL);

		// Disable B channel FIFOs
		usb_device_ctrl_msg(bcs->cs, OUT_B1_COUNTER+(bcs->channel*2), 0, NULL, NULL);
		usb_device_ctrl_msg(bcs->cs, IN_B1_COUNTER+(bcs->channel*2), 0, NULL, NULL);

#if NUMBER_OF_LEDS == 4
		if (bc == 0) {
			bcs->cs->hw.st5481.leds &= ~B1_LED;
		} else {
			bcs->cs->hw.st5481.leds &= ~B2_LED;
		}
#endif
	}
}

/*
 * st5481B_l2l1 is the entry point for upper layer routines that want to
 * transmit on the B channel.  PH_DATA | REQUEST is a normal packet that
 * we either start transmitting (if idle) or queue (if busy).
 * PH_PULL | REQUEST can be called to request a callback message
 * (PH_PULL | CONFIRM)
 * once the link is idle.  After a "pull" callback, the upper layer
 * routines can use PH_PULL | INDICATION to send data.
 */
static void
st5481B_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
	case (PH_DATA | REQUEST):
		save_flags(flags);
		cli();
		if (st->l1.bcs->tx_skb) {
			skb_queue_tail(&st->l1.bcs->squeue, skb);
		} else {
			st->l1.bcs->tx_skb = skb;
		}
		restore_flags(flags);
		break;
	case (PH_PULL | INDICATION):
		if (st->l1.bcs->tx_skb) {
			WARN("st->l1.bcs->tx_skb not NULL");
			break;
		}
		save_flags(flags);
		cli();
		st->l1.bcs->tx_skb = skb;
		restore_flags(flags);		
		break;
	case (PH_PULL | REQUEST):
		if (!st->l1.bcs->tx_skb) {
			test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		} else
			test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;
	case (PH_ACTIVATE | REQUEST):
		DBG(4,"B%d,PH_ACTIVATE_REQUEST",st->l1.bc+1);			
		test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
		st5481B_mode(st->l1.bcs, st->l1.mode, st->l1.bc);
		l1_msg_b(st, pr, arg);
		break;
	case (PH_DEACTIVATE | REQUEST):
		DBG(4,"B%d,PH_DEACTIVATE_REQUEST",st->l1.bc+1);			
		l1_msg_b(st, pr, arg);
		break;
	case (PH_DEACTIVATE | CONFIRM):
		DBG(4,"B%d,PH_DEACTIVATE_CONFIRM",st->l1.bc+1);			
		test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
		test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
		st5481B_mode(st->l1.bcs, 0, st->l1.bc);
		st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
		break;
	}
}

/*
  Called after B channel closed to release buffers.
*/
static void
BC_Close_st5481(struct BCState *bcs)
{
	struct st5481B_hw *hw = &bcs->hw.st5481;

	DBG(4,"B%d,bcs=%d",bcs->channel+1,bcs==&bcs->cs->bcs[0] ? 0 : 1);

	st5481B_mode(bcs, 0, bcs->channel);

	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (hw->rcvbuf) {
			kfree(hw->rcvbuf);
			hw->rcvbuf = NULL;
		}
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

/*
  Allocate receive buffers
*/
static int
st5481B_open(struct BCState *bcs)
{
	struct st5481B_hw *hw = &bcs->hw.st5481;

	DBG(4,"B%d",bcs->channel+1);

	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(hw->rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			WARN("No memory for rcvbuf");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	hw->rcvidx = 0;
	bcs->tx_cnt = 0;

	return (0);
}

static int
BC_SetStack_st5481(struct PStack *st, struct BCState *bcs)
{
	DBG(4,"B%d",st->l1.bc+1);

	bcs->channel = st->l1.bc;
	if (st5481B_open(bcs)) {
		return (-1);
	}
	st->l1.bcs = bcs;
	st->l2.l2l1 = st5481B_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

#if NUMBER_OF_LEDS==2
/*
  If the adapter has only 2 LEDs, the green
  LED will blink with a rate depending
  on the number of channels opened.
*/
static void
led_blink(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	u_char leds = hw->leds;

	// 50 frames/sec for each channel
	if (++hw->led_counter % 50) {
		return;
	}

	if (hw->led_counter % 100) {
		leds |= GREEN_LED;
	} else {
		leds &= ~GREEN_LED;
	}
	
	usb_device_ctrl_msg(cs, GPIO_OUT, leds, NULL, NULL);
}
#endif

/*
  Decode frames received on this B channel.
  Note that this function will be called continously
  with 64Kb/s of data and hence it will be called 50 times
  per second with 20 ISOC descriptors. 
  Called at interrupt.
*/
static void 
usb_b_in_complete(struct urb *urb)
{
	struct BCState *bcs = (struct BCState *)urb->context;
	struct st5481B_hw *hw = &bcs->hw.st5481;
	unsigned char *ptr;
	struct sk_buff *skb;
	int len,count;
	unsigned short status;

	if (urb->status < 0) {
		if (urb->status != USB_ST_URB_KILLED) {
			WARN("urb status %d",urb->status);
		} else {
			DBG(1,"urb killed");
			return; // Give up
		}
	}

	DUMP_ISO_PACKET(0x10,urb);

	len = isoc_flatten(urb);
	ptr = urb->transfer_buffer;
	while (len > 0) {
		if (bcs->mode == L1_MODE_TRANS) {
			memcpy(hw->rcvbuf, ptr, len);
			status =  HDLC_END_OF_FRAME | (unsigned short)len;
			len = 0;
		} else {
			status = hdlc_decode(hw->hdlc_state_in, ptr, len, &count,
					     hw->rcvbuf, HSCX_BUFMAX);
			ptr += count;
			len -= count;
		}
		
		if (status & HDLC_END_OF_FRAME) {
			// Good frame received
			count = status & 0x0FFF;
			DBG(4,"B%d,count=%d",bcs->channel+1,count);
			DUMP_PACKET(0x10, hw->rcvbuf, count);
			if (bcs->cs->debug & L1_DEB_HSCX_FIFO)
				debugl1(bcs->cs, "st5481 Bchan Frame %d", count);
			if (!(skb = dev_alloc_skb(count)))
				WARN("Bchan receive out of memory\n");
			else {
				memcpy(skb_put(skb, count), hw->rcvbuf, count);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			st5481B_sched_event(bcs, B_RCVBUFREADY);
		} else if (status & HDLC_CRC_ERROR) {
			WARN("CRC error");
#ifdef ERROR_STATISTIC
			++bcs->err_crc;
#endif		
		} else if (status & (HDLC_FRAMING_ERROR | HDLC_LENGTH_ERROR)) {
			WARN("framing/length error");
#ifdef ERROR_STATISTIC
			++bcs->err_inv;
#endif
		}
	}

	// Prepare URB for next transfer
	urb->dev = bcs->cs->hw.st5481.dev;
	urb->actual_length = 0;

	submit_urb(urb);

#if NUMBER_OF_LEDS==2
	led_blink(bcs->cs);
#endif
}


/*
  D channel functions
*/

static void
st5481_sched_event(struct IsdnCardState *cs, int event)
{
	test_and_set_bit(event, &cs->event);
	queue_task(&cs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
  Schedule the private D_OUT_EVENT.
  The actual event is stored in a FIFO.
*/
static void
st5481_sched_d_out_event(struct IsdnCardState *cs, unsigned int event)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	int w_index;
	
	DBG(2,"event=%s",D_EVENT_string(event));

	// Add this event to the FIFO
	if (hw->xmt_evt_fifo == NULL) {
		return;
	}
	
	if ((w_index = fifo_get(&hw->xmt_evt_fifo->f)) < 0) {
		WARN("D_OUT event FIFO full");
		return;
	}
	hw->xmt_evt_fifo->data[w_index] = event;
	fifo_add(&hw->xmt_evt_fifo->f);

	// Schedule to tell that an event has been added
	test_and_set_bit(D_OUT_EVENT, &cs->event);
	queue_task(&cs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
  The interrupt endpoint will be called when any
  of the 6 registers changes state (depending on masks).
  Decode the register values and schedule a private event.
  Called at interrupt.
*/
static void 
usb_int_complete(struct urb *urb)
{
	u_char *data = urb->transfer_buffer;
	u_char irqbyte;
	struct IsdnCardState *cs = (struct IsdnCardState *)urb->context;
	struct BCState *bcs;

	if (urb->status < 0) {
		if (urb->status != USB_ST_URB_KILLED) {
			WARN("urb status %d",urb->status);
			urb->actual_length = 0;
		} else {
			DBG(1,"urb killed");
			return; // Give up
		}
	}
	
	DUMP_PACKET(1, data, INT_PKT_SIZE);
		
	if (urb->actual_length == 0) {
		return;
	}

	irqbyte = data[MPINT];
	if (irqbyte & DEN_INT) {
		st5481_sched_d_out_event(cs, DEN_EVENT);
	}

	if (irqbyte & DCOLL_INT) {
		st5481_sched_d_out_event(cs, DCOLL_EVENT);
	}

	irqbyte = data[FFINT_D];
	if (irqbyte & (ANY_XMIT_INT)) {
		st5481_sched_d_out_event(cs, DUNDERRUN_EVENT);
	}

	irqbyte = data[MPINT];
	if (irqbyte & RXCI_INT) {
		DBG(8,"CI %s",ST5481_IND_string(data[CCIST] & 0x0f));
		cs->dc.st5481.ph_state = data[CCIST] & 0x0f;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ph_state change %x", cs->dc.st5481.ph_state);
		st5481_sched_event(cs, D_L1STATECHANGE);
	}

	for (bcs = &cs->bcs[0]; bcs <= &cs->bcs[1]; bcs++) {
		bcs->hw.st5481.b_flow_event |= data[FFINT_B1 + bcs->channel];
	}

	urb->actual_length = 0;
}

/*
  The D IN pipe has been reset.
*/
static void 
reset_fifoD_in_proc(void *context)
{
	struct IsdnCardState *cs = context;

	st5481_sched_d_out_event(cs, DNONE_EVENT);
}


/*
  Decode frames received on the D channel.
  Note that this function will be called continously
  with 16Kb/s of data and hence it will be called 50 times
  per second with 20 ISOC descriptors. 
  Called at interrupt.
*/
static void 
usb_d_in_complete(struct urb *urb)
{
	struct IsdnCardState *cs = (struct IsdnCardState *)urb->context;
	struct st5481_hw *hw = &cs->hw.st5481;
	unsigned char *ptr;
	struct sk_buff *skb;
	int len,count;
	unsigned short status;

	if (urb->status < 0) {
		if (urb->status != USB_ST_URB_KILLED) {
			WARN("urb status %d",urb->status);			
			usb_pipe_reset(cs, EP_D_IN, reset_fifoD_in_proc, cs);
		} else {
			DBG(1,"urb killed");
			return;  // Give up
		}
	}
	
	DUMP_ISO_PACKET(0x20,urb);

	len = isoc_flatten(urb);
	ptr = urb->transfer_buffer;
	while (len > 0) {
		status = hdlc_decode(hw->hdlc_state_in, ptr, len, &count,
				     cs->rcvbuf, MAX_DFRAME_LEN_L1);
		ptr += count;
		len -= count;
		
		if (status & HDLC_END_OF_FRAME) {
			count = status & 0x0FFF;
			DBG(2,"count=%d",count);
			DUMP_PACKET(0x10, cs->rcvbuf, count);
			// Good frame received
			if (!(skb = dev_alloc_skb(count)))
				WARN("D receive out of memory\n");
			else {
				memcpy(skb_put(skb, count), cs->rcvbuf, count);
				skb_queue_tail(&cs->rq, skb);
			}
			st5481_sched_event(cs, D_RCVBUFREADY);
		} else if (status & HDLC_CRC_ERROR) {
			WARN("CRC error");
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "st5481 CRC error");
#ifdef ERROR_STATISTIC
			cs->err_crc++;
#endif
		} else if (status & (HDLC_FRAMING_ERROR | HDLC_LENGTH_ERROR)) {
			WARN("framing/length error");
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "st5481 framing/length error");
#ifdef ERROR_STATISTIC
			cs->err_rx++;
#endif
		}
	}

	// Prepare URB for next transfer
	urb->dev = hw->dev;
	urb->actual_length = 0;

	submit_urb(urb);
}


/*
  D channel transmit and flow control functions:
*/

/*
  D OUT state machine:
  ====================

  Transmit short frame (< 16 bytes of encoded data):

  L1 FRAME    D_OUT_STATE           USB                  D CHANNEL
  --------    -----------           ---                  ---------
 
              INIT

 -> [xx..xx]  INIT_SHORT_FRAME      -> [7Exx..xxC1C27EFF]
              SHORT_WAIT_DEN        <> OUT_D_COUNTER=16 
                                                 
              END_OF_SHORT          <- DEN_EVENT         -> 7Exx
                                                          xxxx 
                                                          xxxx
							  xxxx 
							  xxxx
							  xxxx
							  C1C1 
							  7EFF 
              WAIT_FOR_RESET_IDLE   <- D_UNDERRUN        <- (8ms)                        
              IDLE                  <> Reset pipe

              

  Transmit long frame (>= 16 bytes of encoded data):

  L1 FRAME    D_OUT_STATE           USB                  D CHANNEL
  --------    -----------           ---                  ---------

 -> [xx...xx] IDLE
              WAIT_FOR_STOP         <> OUT_D_COUNTER=0
              WAIT_FOR_RESET        <> Reset pipe
	      STOP
	      INIT_LONG_FRAME       -> [7Exx..xx]
              WAIT_DEN              <> OUT_D_COUNTER=16 
              OUT_NORMAL            <- DEN_EVENT       -> 7Exx
              END_OF_FRAME_BUSY     -> [xxxx]             xxxx 
              END_OF_FRAME_NOT_BUSY -> [xxxx]             xxxx
				    -> [xxxx]		  xxxx 
				    -> [C1C2]		  xxxx
				    -> [7EFF]		  xxxx
							  xxxx 
							  xxxx 
                                                          ....
							  xxxx
							  C1C2
							  7EFF
	                 	    <- D_UNDERRUN      <- (> 8ms)                        
              WAIT_FOR_STOP         <> OUT_D_COUNTER=0
              WAIT_FOR_RESET        <> Reset pipe
	      STOP

*/          

#define d_out_set_state(hw,new_state) \
        DBG(2,"state=%s,new state=%s",D_STATE_string((hw)->d_out_state),D_STATE_string(new_state)); \
        (hw)->d_out_state = new_state;	

/*
  Start the transfer of a D channel frame.
*/
static void 
usb_d_out(struct IsdnCardState *cs,int buf_nr)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	struct urb *urb;
	unsigned int num_packets,packet_offset;
	int len,buf_size,bytes_sent;
	struct sk_buff *skb;

	if (test_and_set_bit(buf_nr,&hw->d_out_busy)) {
		DBG(2,"ep %d urb %d busy",EP_D_OUT,buf_nr);
		return;
	}
	urb = hw->d_out_urb[buf_nr];

	DBG(2,"state=%s",D_STATE_string(hw->d_out_state));

	skb = cs->tx_skb;
	switch(hw->d_out_state){
	case DOUT_INIT:
		if (skb) {
			DUMP_SKB(0x10, skb);
			len = hdlc_encode(hw->hdlc_state_out, 
					  skb->data, skb->len, &bytes_sent,
					  urb->transfer_buffer, 16);
			skb_pull(skb, bytes_sent);
		} else {
			// Send flags or idle
			len = hdlc_encode(hw->hdlc_state_out, 
					  NULL, 0, &bytes_sent,
					  urb->transfer_buffer, 16);
		}

		if(len < 16){
			d_out_set_state(hw, DOUT_INIT_SHORT_FRAME);
		} else {
			d_out_set_state(hw, DOUT_INIT_LONG_FRAME);
		}
		packet_offset = len;
		break;
	case DOUT_NORMAL:  
		buf_size = NUM_ISO_PACKETS_D*2;
		packet_offset = 2;
	
		if (skb) {
			len = hdlc_encode(hw->hdlc_state_out, 
					  skb->data, skb->len, &bytes_sent,
					  urb->transfer_buffer, buf_size);
			skb_pull(skb,bytes_sent);
		} else {
			// Send flags or idle
			len = hdlc_encode(hw->hdlc_state_out, 
					  NULL, 0, &bytes_sent,
					  urb->transfer_buffer, buf_size);
		}
		
		if(len < buf_size){
			st5481_sched_d_out_event(cs, DXSHORT_EVENT);
			if (len < 2) {
				packet_offset = len;
			}
		}
		break;
	default:
		return;
	}

	if (skb && !skb->len) {
		dev_kfree_skb_any(skb);
		if (!(cs->tx_skb = skb_dequeue(&cs->sq))) {
			st5481_sched_event(cs, D_XMTBUFREADY);
		}
	}

	// Prepare the URB
	urb->transfer_buffer_length = len;
	urb->actual_length = len;

	urb->iso_frame_desc[0].offset = 0;
	urb->iso_frame_desc[0].length = packet_offset;
	urb->iso_frame_desc[0].actual_length = packet_offset;

	num_packets = 1;
	if(len){
		while(packet_offset < len){
			urb->iso_frame_desc[num_packets].offset = packet_offset;
			urb->iso_frame_desc[num_packets].length = 2;
			urb->iso_frame_desc[num_packets].actual_length = 2;
			packet_offset += 2;
			num_packets++;
		}
	}
	urb->number_of_packets = num_packets;

	// Prepare the URB
	urb->dev = hw->dev;
	// Need to transmit the next buffer 8ms after the DEN_EVENT
	urb->transfer_flags = 0;
	urb->start_frame = usb_get_current_frame_number(hw->dev)+2;

	DUMP_ISO_PACKET(0x10,urb);

	if (usb_submit_urb(urb) < 0) {
		// There is another URB queued up
		urb->transfer_flags = USB_ISO_ASAP;
		submit_urb(urb);
	}	
}

static void 
reset_fifoD_out_proc(void *context)
{
	struct IsdnCardState *cs = context;

	d_out_set_state(&cs->hw.st5481, DOUT_STOP);
	st5481_sched_d_out_event(cs, DNONE_EVENT);
}

static void  
usb_d_out_complete(struct urb *urb)
{
	struct IsdnCardState *cs = urb->context;
	struct st5481_hw *hw = &cs->hw.st5481;
	int buf_nr;
	
	buf_nr = get_buf_nr(hw->d_out_urb, urb);
	test_and_clear_bit(buf_nr, &hw->d_out_busy);

	if (urb->status < 0) {
		if (urb->status != USB_ST_URB_KILLED) {
			WARN("urb status %d",urb->status);
			if(hw->d_out_busy==0) {
				usb_pipe_reset(cs, EP_D_OUT | USB_DIR_OUT, reset_fifoD_out_proc, cs);
			}
			return;
		} else {
			DBG(1,"urb killed"); 
			return; // Give up
		}
	}

	switch(hw->d_out_state){
	case DOUT_INIT_SHORT_FRAME:
	case DOUT_INIT_LONG_FRAME:
		st5481_sched_d_out_event(cs, DXMIT_INITED);
		break;
	case DOUT_NORMAL:
		usb_d_out(cs, buf_nr);
		break;
	case DOUT_END_OF_FRAME_BUSY:
	case DOUT_WAIT_FOR_NOT_BUSY:
		if(hw->d_out_busy==0)
			st5481_sched_d_out_event(cs, DXMIT_NOT_BUSY);
		break;
	default:
		break;
	}

}

static void 
reset_fifoD_out_proc_idle(void *context)
{
	struct IsdnCardState *cs = context;

	d_out_set_state(&cs->hw.st5481, DOUT_IDLE);
	st5481_sched_d_out_event(cs, DNONE_EVENT);
}

static void 
out_d_stop_event(void *context)
{
	struct IsdnCardState *cs = context;

	st5481_sched_d_out_event(cs, DXMIT_STOPPED);
}

static void 
out_d_start(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;

	if (!cs->tx_skb) {
		return;
	}

	DBG(2,"len=%d",cs->tx_skb->len);

	if(hw->d_out_state == DOUT_IDLE && cs->tx_skb->len > 4) {
		d_out_set_state(hw, DOUT_WAIT_FOR_STOP);
		usb_device_ctrl_msg(cs, OUT_D_COUNTER, 0, out_d_stop_event, cs);
		return;  
	}

	d_out_set_state(hw, DOUT_INIT);
	
	hdlc_out_init(hw->hdlc_state_out, 1, 0);
	usb_d_out(cs,0);					
}

static void 
out_d_short_fifoD(struct IsdnCardState *cs, unsigned int event)
{	
	switch(event){
	case DXMIT_INITED:
		usb_device_ctrl_msg(cs, OUT_D_COUNTER, 16, NULL, NULL);
		d_out_set_state(&cs->hw.st5481, DOUT_SHORT_WAIT_DEN);
		break;
	default:
		break;
	}
}

static void 
enable_fifoD_proc(void *context)
{
	struct IsdnCardState *cs = context;
    
	usb_device_ctrl_msg(cs, OUT_D_COUNTER, 16, NULL, NULL);
	d_out_set_state(&cs->hw.st5481, DOUT_WAIT_DEN);
}

static void 
out_d_long_fifoD(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DXMIT_INITED:
		usb_pipe_reset(cs, EP_D_OUT | USB_DIR_OUT, enable_fifoD_proc, cs);
		break;
	default:
		break;
	}
}

static void 
out_d_wait_den(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DEN_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_NORMAL);
		usb_d_out(cs,0);
		usb_d_out(cs,1);
		break;
	case DCOLL_EVENT:
	case DUNDERRUN_EVENT:
	case DXRESET_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_NOT_BUSY);
		break;
	default:
		break;
	}
}

static void 
out_d_short_wait_den(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DEN_EVENT:
	case DCOLL_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_END_OF_SHORT_FRAME);
		break;
	case DUNDERRUN_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_RESET_IDLE);
		usb_pipe_reset(cs, EP_D_OUT | USB_DIR_OUT, reset_fifoD_out_proc_idle, cs);
		break;
	case DXRESET_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_STOP);
		usb_device_ctrl_msg(cs, OUT_D_COUNTER, 0, out_d_stop_event, cs);
		break;
	default:
		break;
	}
}

static void 
out_d_end_of_short_frame(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DUNDERRUN_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_RESET_IDLE);
		usb_pipe_reset(cs, EP_D_OUT | USB_DIR_OUT, reset_fifoD_out_proc_idle, cs);
		break;
	case DCOLL_EVENT:
	case DXRESET_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_STOP);
		usb_device_ctrl_msg(cs, OUT_D_COUNTER, 0, out_d_stop_event, cs);
		break;
	default:
		break;
	}
}

static void 
out_d_end_of_frame_not_busy(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DCOLL_EVENT:
	case DUNDERRUN_EVENT:
	case DXRESET_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_STOP);
		usb_device_ctrl_msg(cs, OUT_D_COUNTER, 0, out_d_stop_event, cs);
		break;
	default:
		break;
	}
}

static void 
out_d_end_of_frame_busy(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DCOLL_EVENT:
	case DUNDERRUN_EVENT:
	case DXRESET_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_NOT_BUSY);
		break;
	case DXMIT_NOT_BUSY:
		d_out_set_state(&cs->hw.st5481, DOUT_END_OF_FRAME_NOT_BUSY);
		break;
	default:
		break;
	}
}

static void 
out_d_normal(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DCOLL_EVENT:
	case DUNDERRUN_EVENT:
	case DXRESET_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_NOT_BUSY);
		break;
	case DXSHORT_EVENT:
		d_out_set_state(&cs->hw.st5481, DOUT_END_OF_FRAME_BUSY);
		break;
	default:
		break;
	}
}

static void 
out_d_wait_for_not_busy(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DXMIT_NOT_BUSY:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_STOP);
		usb_device_ctrl_msg(cs, OUT_D_COUNTER, 0, out_d_stop_event, cs);
		break;
	default:
		break;
	}
}

static void 
out_d_wait_for_stop(struct IsdnCardState *cs, unsigned int event)
{
	switch(event){
	case DXMIT_STOPPED:
		d_out_set_state(&cs->hw.st5481, DOUT_WAIT_FOR_RESET);
		usb_pipe_reset(cs, EP_D_OUT | USB_DIR_OUT, reset_fifoD_out_proc, cs);
		break;
	default:
		break;
	}
}

/*
  OUT D state machine
*/
static void 
out_d(struct IsdnCardState *cs, unsigned int event)
{
	struct st5481_hw *hw = &cs->hw.st5481;

	DBG(2,"state=%s,event=%s",D_STATE_string(hw->d_out_state),D_EVENT_string(event));

	switch(hw->d_out_state){
	case DOUT_NONE:
	case DOUT_STOP:
	case DOUT_IDLE:
		if (event == DNONE_EVENT) {
			out_d_start(cs);
		}
		break;
	case DOUT_INIT_SHORT_FRAME:
		out_d_short_fifoD(cs, event);
		break;
	case DOUT_INIT_LONG_FRAME:
		out_d_long_fifoD(cs, event);
		break;
	case DOUT_WAIT_DEN:
		out_d_wait_den(cs, event);
		break;
	case DOUT_SHORT_WAIT_DEN:
		out_d_short_wait_den(cs, event);
		break;
	case DOUT_END_OF_SHORT_FRAME:
		out_d_end_of_short_frame(cs, event);
		break;
	case DOUT_END_OF_FRAME_BUSY:
		out_d_end_of_frame_busy(cs, event);
		break;
	case DOUT_END_OF_FRAME_NOT_BUSY:
		out_d_end_of_frame_not_busy(cs, event);
		break;
	case DOUT_NORMAL:
		out_d_normal(cs, event);
		break;
	case DOUT_WAIT_FOR_NOT_BUSY:
		out_d_wait_for_not_busy(cs, event);
		break;
	case DOUT_WAIT_FOR_STOP:
		out_d_wait_for_stop(cs, event);
		break;
	default:
		break;
	}
}

/*
  Start transmitting D channel frame.
*/
static void 
st5481_fill_fifo(struct IsdnCardState *cs)
{
	out_d(cs, DNONE_EVENT);
}

/*
  Remove the event from the FIFO and call the OUT D
  state machine.
*/
static void
st5481_d_out_event(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	int r_index;
	unsigned int event;

	if (hw->xmt_evt_fifo == NULL) {
		return;
	}

	while ((r_index = fifo_remove(&hw->xmt_evt_fifo->f)) >= 0) {
		event = hw->xmt_evt_fifo->data[r_index];
		out_d(cs,event);
	}	

}

/*
  Physical level functions
*/

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	DBG(8,"command=%s",ST5481_CMD_string(command));

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ph_command %x", command);

	usb_device_ctrl_msg(cs, TXCI, command, NULL, NULL);
}

/*
  Start receiving on the D channel since entered state F7.
*/
static void
ph_connect(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;

	DBG(8,"");
		
	d_out_set_state(hw, DOUT_NONE);

	usb_device_ctrl_msg(cs, FFMSK_D, OUT_UNDERRUN, NULL, NULL);
	usb_device_ctrl_msg(cs, IN_D_COUNTER, 16, NULL, NULL);

#if LOOPBACK
	// Turn loopback on (data sent on B and D looped back)
	usb_device_ctrl_msg(cs, LBB, 0x04, NULL, NULL);
#endif

	hdlc_rcv_init(hw->hdlc_state_in, 0);
	
	usb_pipe_reset(cs, EP_D_OUT | USB_DIR_OUT, NULL, NULL);
	usb_pipe_reset(cs, EP_D_IN | USB_DIR_IN, NULL, NULL);

	// Turn on the green LED to tell that we are in state F7
	hw->leds |= GREEN_LED;
	usb_device_ctrl_msg(cs, GPIO_OUT, hw->leds, NULL, NULL);

	// Start receiving on the isoc endpoints
	hw->d_in_urb[0]->dev = hw->dev;
	submit_urb(hw->d_in_urb[0]);
	hw->d_in_urb[1]->dev = hw->dev;
	submit_urb(hw->d_in_urb[1]);
}

/*
  Stop receiving on the D channel since not in state F7.
*/
static void
ph_disconnect(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;

	DBG(8,"");

	// Stop receiving on the isoc endpoints
	usb_unlink_urb(hw->d_in_urb[0]);
	usb_unlink_urb(hw->d_in_urb[1]);

	// Turn off the green LED to tell that we left state F7
	hw->leds &= ~GREEN_LED;
	usb_device_ctrl_msg(cs, GPIO_OUT, hw->leds, NULL, NULL);
}

/*
  Reset the adapter to default values.
*/
static void 
ph_exit(struct IsdnCardState *cs)
{
	DBG(8,"");

	usb_device_ctrl_msg(cs, SET_DEFAULT, 0, NULL, NULL);
}

/*
  Initialize the adapter.
*/
static void
ph_init(struct IsdnCardState *cs)
{
	static const __u8 init_cmd_table[]={
		SET_DEFAULT,0,
		STT,0,
		SDA_MIN,0x0d,
		SDA_MAX,0x29,
		SDELAY_VALUE,0x14,
		GPIO_DIR,0x01,		
		GPIO_OUT,RED_LED,
		FFCTRL_OUT_B1,6,
		FFCTRH_OUT_B1,20,
		FFCTRL_OUT_B2,6,
		FFCTRH_OUT_B2,20,
		MPMSK,RXCI_INT+DEN_INT+DCOLL_INT,
		0
	};	
	struct st5481_hw *hw = &cs->hw.st5481;
	int i = 0;
	__u8 request,value;

	DBG(8,"");

	hw->leds = RED_LED; 

	cs->dc.st5481.ph_state = -1;	
	
	// Start receiving on the interrupt endpoint
	submit_urb(hw->int_urb); 

	while ((request = init_cmd_table[i++])) {
		value = init_cmd_table[i++];
		usb_device_ctrl_msg(cs, request, value, NULL, NULL);
	}
	ph_command(cs, ST5481_CMD_PUP);
}

static void
st5481_new_ph(struct IsdnCardState *cs)
{
	DBG(8,"state=%s",ST5481_IND_string(cs->dc.st5481.ph_state));

	switch (cs->dc.st5481.ph_state) {
	case ST5481_IND_DI:
		ph_disconnect(cs);
		l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
		break;
	case ST5481_IND_DP:
		ph_disconnect(cs);
		l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
		break;
	case ST5481_IND_RSY:
		ph_disconnect(cs);
		l1_msg(cs, HW_RSYNC | INDICATION, NULL);
		break;
	case ST5481_IND_AP:
		l1_msg(cs, HW_INFO2 | INDICATION, NULL);
		break;
	case ST5481_IND_AI8:
		l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
		break;
	case ST5481_IND_AI10:
		l1_msg(cs, HW_INFO4_P10 | INDICATION, NULL);
		break;
	default:
		WARN("unknown st5481.ph_state %x",cs->dc.st5481.ph_state);
		break;
	}
}


static void
st5481_bh(struct IsdnCardState *cs)
{
	struct PStack *stptr;

	if (!cs)
		return;
	
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		stptr = cs->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		st5481_new_ph(cs);
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event)) {
		DChannel_proc_rcv(cs);	
	}
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event)) {
		DChannel_proc_xmt(cs);
	}
	if (test_and_clear_bit(D_OUT_EVENT, &cs->event)) {
		st5481_d_out_event(cs);
	}

}

/*
  S/T state machine:

  Activation by TE:
  -----------------        
            F1

     PUP -> F2     INFO0 ->

  <- DP     F3  <- INFO0 
         
     AR8 -> F4     INFO1 ->
 
  <- AP     F6  <- INFO2
                   INFO3 ->
  <- AI8    F7  <- INFO4

  Activation by NT:
  -----------------
            F1     INFO0 ->
                <- INFO0
  
  <- AP	    F6  <- INFO2
                   INFO3 ->    
  <- AI8    F7  <- INFO4

  Deactivation by NT:
  -------------------
            F7  <- INFO4

  <- DP     F3  <- INFO0
  (  DR ->) 
 
  Loss of frame synchronisation by NT:
  -----------------------------
            F7  <- INFO4
                Loss of frame
   <- AP    F6  <- INFO2
                   INFO3 ->
   <- AI8   F7  <- INFO4

  Loss of frame synchronisation by TE:
  -----------------------------
            F7  <- INFO4
                Loss of frame
   <- RSY   F8     INFO0 ->
   <- AP    F6  <- INFO2   
                   INFO3 ->
   <- AI8   F7  <- INFO4
  
   Loss of power:
   --------------
            F7  <- INFO4
                
   <- DP    F1  Loss of power  
                   INFO0 ->
                <- INFO2
   <- DP    F2  Power recovered
                   INFO0 ->
   <- AP    F6  <- INFO2
                   INFO3 ->
 */

static void
st5481_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;

	switch (pr) {
	case (PH_DATA |REQUEST):
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 0);
		if (cs->tx_skb) {
			skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
		} else {
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				st5481_fill_fifo(cs);
		}
		break;
	case (PH_PULL |INDICATION):
		if (cs->tx_skb) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
			skb_queue_tail(&cs->sq, skb);
			break;
		}
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 0);
		cs->tx_skb = skb;
		cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
		st5481_fill_fifo(cs);
		break;
	case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			debugl1(cs, "-> PH_REQUEST_PULL");
#endif
		if (!cs->tx_skb) {
			test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		} else
			test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;
	case (HW_RESET | REQUEST):
		DBG(8,"HW_RESET_REQUEST,state=%s",ST5481_IND_string(cs->dc.st5481.ph_state));
		ph_command(cs, ST5481_CMD_PUP);
		l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
		break;
	case (HW_ENABLE | REQUEST):
		DBG(8,"HW_ENABLE_REQUEST,state=%s",ST5481_IND_string(cs->dc.st5481.ph_state));
		ph_command(cs, ST5481_CMD_DR);
		ph_command(cs, ST5481_CMD_PUP);
		break;
	case (HW_INFO3 | REQUEST):
		DBG(8,"HW_INFO_3_REQUEST,state=%s",ST5481_IND_string(cs->dc.st5481.ph_state));
		if (cs->dc.st5481.ph_state == ST5481_IND_AI8) {
			ph_command(cs, ST5481_CMD_AR8);
			ph_connect(cs);
		} else if (cs->dc.st5481.ph_state == ST5481_IND_AI10) {
			ph_command(cs, ST5481_CMD_AR10);
			ph_connect(cs);
		} else {
			ph_command(cs, ST5481_CMD_AR8);
		}
		break;
	case (HW_TESTLOOP | REQUEST):
		DBG(8,"HW_TESTLOOP_REQUEST,state=%s",ST5481_IND_string(cs->dc.st5481.ph_state));
		switch ((int)arg) {
		case 0:
			// Off
			usb_device_ctrl_msg(cs, LBA, 0x2, NULL, NULL);
			break;			
		case 1:
			// B1 channel
			usb_device_ctrl_msg(cs, LBA, 0x42 , NULL, NULL);
			break;
		case 2:
			// B2 channel
			usb_device_ctrl_msg(cs, LBA, 0x22 , NULL, NULL);
			break;
		case 4:
			// LOOP4 on
			usb_device_ctrl_msg(cs, LBA, 0x0e, NULL, NULL);
			break;
		}
		break;
	case (HW_DEACTIVATE | RESPONSE):
		DBG(8,"HW_DEACTIVATE_RESPONSE,state=%s",ST5481_IND_string(cs->dc.st5481.ph_state));
		//ph_command(cs, ST5481_CMD_DR);		
		skb_queue_purge(&cs->rq);
		skb_queue_purge(&cs->sq);
		if (cs->tx_skb) {
			dev_kfree_skb_any(cs->tx_skb);
			cs->tx_skb = NULL;
		}
		if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			st5481_sched_event(cs, D_CLEARBUSY);
		break;
	default:
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, __FUNCTION__ "unknown %04x", pr);
		break;
	}
}


/* 
   Initialisation of USB, D and B channels
*/

static void
setstack_d_st5481(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = st5481_l1hw;
}


static void 
DC_Close_st5481(struct IsdnCardState *cs) 
{
}

static int 
init_st5481(struct IsdnCardState *cs)
{
	int i;

	DBG(1,"");
	
	// B channels
	for (i = 0; i < 2; i++) {
		cs->bcs[i].BC_SetStack = BC_SetStack_st5481;
		cs->bcs[i].BC_Close = BC_Close_st5481;
	}

	// D channel
	cs->tqueue.routine = (void *) (void *) st5481_bh;
	cs->setstack_d = setstack_d_st5481;
	cs->DC_Close = DC_Close_st5481;

	// Physical layer
	ph_init(cs);

	return (0);
}

/*
  Release buffers and URBs for the B channels
*/
static void
release_b(struct BCState *bcs)
{
	struct st5481B_hw *hw = &bcs->hw.st5481;
	struct urb *urb;
	int j;

	DBG(1,"");

	for (j = 0; j < 2; j++) {
		if ((urb = hw->b_out_urb[j])) {
			usb_unlink_urb(urb);
			if (urb->transfer_buffer)
				kfree(urb->transfer_buffer);
			usb_free_urb(urb);
			hw->b_out_urb[j] = NULL;
		}
	}
	for (j = 0; j < 2; j++) {
		if ((urb = hw->b_in_urb[j])) {
			usb_unlink_urb(urb);
			if (urb->transfer_buffer)
				kfree(urb->transfer_buffer);
			usb_free_urb(urb);
			hw->b_in_urb[j] = NULL;
		}
	}
	if (hw->hdlc_state_out) {
		kfree(hw->hdlc_state_out);
		hw->hdlc_state_out = NULL;
	}
	if (hw->hdlc_state_in) {
		kfree(hw->hdlc_state_in);
		hw->hdlc_state_in = NULL;
	}
}

/*
  Release buffers and URBs for the D channel
*/
static void
release_d(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	struct urb *urb;
	int j;

	DBG(1,"");

	for (j = 0; j < 2; j++) {
		if ((urb = hw->d_out_urb[j])) {
			usb_unlink_urb(urb);
			if (urb->transfer_buffer)
				kfree(urb->transfer_buffer);			
			usb_free_urb(urb);
			hw->d_out_urb[j] = NULL;
		}
	}
	for (j = 0; j < 2; j++) {
		if ((urb = hw->d_in_urb[j])) {
			usb_unlink_urb(urb);
			if (urb->transfer_buffer)
				kfree(urb->transfer_buffer);
			usb_free_urb(urb);
			hw->d_in_urb[j] = NULL;
		}
	}
	if (hw->hdlc_state_out) {
		kfree(hw->hdlc_state_out);
		hw->hdlc_state_out = NULL;
	}
	if (hw->hdlc_state_in) {
		kfree(hw->hdlc_state_in);
		hw->hdlc_state_in = NULL;
	}
}

/*
  Release buffers and URBs for the interrupt and control
  endpoint.
*/
static void
release_usb(struct IsdnCardState *cs)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	struct urb *urb;

	DBG(1,"");

	// Stop and free Control and Interrupt URBs
	if ((urb = hw->int_urb)) {
		usb_unlink_urb(urb);
		if (urb->transfer_buffer)
			kfree(urb->transfer_buffer);
		usb_free_urb(urb);
		hw->int_urb = NULL;
	}
	if ((urb = hw->ctrl_urb)) {
		usb_unlink_urb(urb);
		if (urb->transfer_buffer)
			kfree(urb->transfer_buffer);
		usb_free_urb(urb);
		hw->ctrl_urb = NULL;
	}

	// Release memory for the FIFOs
	if (hw->ctrl_msg_fifo) {
		kfree(hw->ctrl_msg_fifo);
		hw->ctrl_msg_fifo = NULL;
	}
	if (hw->xmt_evt_fifo) {
		kfree(hw->xmt_evt_fifo);
		hw->xmt_evt_fifo = NULL;
	}
}

static void
release_st5481(struct IsdnCardState *cs)
{
	int i;

	INFO("cardnr=%d",cs->cardnr);

	ph_exit(cs);
	release_d(cs);
	for (i=0; i < 2; i++) {
		release_b(&cs->bcs[i]);
	}
	release_usb(cs);
}

static int
st5481_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	DBG(1,"mt=%x",mt);

	switch (mt) {
		case CARD_RESET:
			return(0);
		case CARD_RELEASE:
			release_st5481(cs);
			return(0);
		case CARD_INIT:
			return(init_st5481(cs));
		case CARD_TEST:
			return(0);
	}
	return(0);
}

/* 
   Set the USB configuration and allocate memory
   and URBs for the interrupt and control endpoints.
*/
static int __devinit
setup_usb(struct IsdnCardState *cs, struct usb_device *dev)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	struct usb_interface_descriptor *altsetting;
	struct usb_endpoint_descriptor *endpoint;
	int status;
	urb_t *urb;
	u_char *buf;
	
	DBG(1,"");
	
	memset(hw,0,sizeof(*hw));
	
	if ((status = usb_set_configuration (dev,dev->config[0].bConfigurationValue)) < 0) {
		WARN("set_configuration failed,status=%d",status);
		return (status);
	}

	
	altsetting = &(dev->config->interface[0].altsetting[3]);	

	// Check if the config is sane
	if ( altsetting->bNumEndpoints != (7) ) {
		WARN("expecting %d got %d endpoints!", 7, altsetting->bNumEndpoints);
		return (-EINVAL);
	}

	// The descriptor is wrong for some early samples of the ST5481 chip
	altsetting->endpoint[3].wMaxPacketSize = SIZE_ISO_PACKETS_B;
	altsetting->endpoint[4].wMaxPacketSize = SIZE_ISO_PACKETS_B;

	// Use alternative setting 3 on interface 0 to have 2B+D
	if ((status = usb_set_interface (dev, 0, 3)) < 0) {
		WARN("usb_set_interface failed,status=%d",status);
		return (status);
	}

	// Good device
	hw->dev = dev;
	
	// Allocate URB for control endpoint
	urb = usb_alloc_urb(0);
	if (!urb) {
		return (-ENOMEM);
	}
	hw->ctrl_urb = urb;
	
	// Fill the control URB
	FILL_CONTROL_URB (urb, dev, 
			  usb_sndctrlpipe(dev, 0),
			  NULL, NULL, 0, usb_ctrl_complete, cs);

		
	// Allocate URBs and buffers for interrupt endpoint
	urb = usb_alloc_urb(0);
	if (!urb) { 
		return (-ENOMEM);
	}
	hw->int_urb = urb;
	
	buf = kmalloc(INT_PKT_SIZE, GFP_KERNEL);
	if (!buf) {
		return (-ENOMEM);
	}

	endpoint = &altsetting->endpoint[EP_INT-1];
				
	// Fill the interrupt URB
	FILL_INT_URB(urb, dev,
		     usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		     buf, INT_PKT_SIZE,
		     usb_int_complete, cs,
		     endpoint->bInterval);
		
	// Allocate memory for the FIFOs
	if (!(hw->ctrl_msg_fifo = kmalloc(sizeof(*hw->ctrl_msg_fifo), GFP_KERNEL))) {
		return (-ENOMEM);
	}
	hw->ctrl_msg_fifo->f.r = hw->ctrl_msg_fifo->f.w = hw->ctrl_msg_fifo->f.count = 0;
	hw->ctrl_msg_fifo->f.size = sizeof(hw->ctrl_msg_fifo->data)/sizeof(hw->ctrl_msg_fifo->data[0]);
	spin_lock_init(hw->ctrl_msg_fifo->f.lock);
	
	if (!(hw->xmt_evt_fifo = kmalloc(sizeof(*hw->xmt_evt_fifo), GFP_KERNEL))) {
		return (-ENOMEM);
	}
	hw->xmt_evt_fifo->f.r = hw->xmt_evt_fifo->f.w = hw->xmt_evt_fifo->f.count = 0;
	hw->xmt_evt_fifo->f.size = sizeof(hw->xmt_evt_fifo->data)/sizeof(hw->xmt_evt_fifo->data[0]);
	spin_lock_init(hw->xmt_evt_fifo->f.lock);

	return (0);
}

/*
  Allocate buffers and URBs for the D channel endpoints.
*/
static int __devinit
setup_d(struct IsdnCardState *cs,struct usb_device *dev)
{
	struct st5481_hw *hw = &cs->hw.st5481;
	struct usb_interface_descriptor *altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct urb *urb;
	unsigned char *buf;
	int j;

	DBG(2,"");

	if (!(hw->hdlc_state_in = kmalloc(sizeof(struct hdlc_vars), GFP_KERNEL))) {
		return (-ENOMEM);
	}

	if (!(hw->hdlc_state_out = kmalloc(sizeof(struct hdlc_vars), GFP_KERNEL))) {
		return (-ENOMEM);
	}

	altsetting = &(dev->config->interface[0].altsetting[3]);
	
	// Allocate URBs and buffers for the D channel out
	endpoint = &altsetting->endpoint[EP_D_OUT-1];

	DBG(1,"endpoint address=%02x,packet size=%d",
	    endpoint->bEndpointAddress,endpoint->wMaxPacketSize);

	for (j=0; j < 2; j++) {
		urb = usb_alloc_urb(NUM_ISO_PACKETS_D);
		if (!urb) {
			return -ENOMEM;
		}

		hw->d_out_urb[j] = urb;

		// Allocate memory for 2000bytes/sec (16Kb/s)
		buf = kmalloc(NUM_ISO_PACKETS_D * 2, GFP_KERNEL);
		if (!buf) { 
			return -ENOMEM;
		}
			
		// Fill the isochronous URB
		fill_isoc_urb(urb, dev,
			      usb_sndisocpipe(dev, endpoint->bEndpointAddress),
			      buf, NUM_ISO_PACKETS_D, 2,
			      usb_d_out_complete, cs);
	}
	
	// Allocate URBs and buffers for the D channel in
	endpoint = &altsetting->endpoint[EP_D_IN-1];

	DBG(1,"endpoint address=%02x,packet size=%d",
	    endpoint->bEndpointAddress,endpoint->wMaxPacketSize);

	for (j=0; j < 2; j++) {
		urb = usb_alloc_urb(NUM_ISO_PACKETS_D);
		if (!urb) {
			return (-ENOMEM);
		}

		hw->d_in_urb[j] = urb;

		buf = kmalloc(NUM_ISO_PACKETS_D * SIZE_ISO_PACKETS_D, GFP_KERNEL);
		if (!buf) {
			return (-ENOMEM);
		}
			
		// Fill the isochronous URB
		fill_isoc_urb(urb, dev,
			      usb_rcvisocpipe(dev, endpoint->bEndpointAddress),
			      buf, NUM_ISO_PACKETS_D, SIZE_ISO_PACKETS_D,
			      usb_d_in_complete, cs);
	}
	
	return (0);
}

/*
  Allocate buffers and URBs for the B channel endpoints.
*/
static int __devinit
setup_b(struct BCState *bcs,struct usb_device *dev)

{
	struct st5481B_hw *hw = &bcs->hw.st5481;
	struct urb *urb;
	unsigned char *buf;
	int j;

	DBG(4,"");

	memset(hw,0,sizeof(*hw));

	if (!(hw->hdlc_state_in = kmalloc(sizeof(struct hdlc_vars), GFP_KERNEL))) {
		return (-ENOMEM);
	}

	if (!(hw->hdlc_state_out = kmalloc(sizeof(struct hdlc_vars), GFP_KERNEL))) {
		return (-ENOMEM);
	}

	// Allocate URBs and buffers for the B channel out
	for (j=0; j < 2; j++) {
		urb = usb_alloc_urb(NUM_ISO_PACKETS_B);
		if (!urb) 
			return (-ENOMEM);

		hw->b_out_urb[j] = urb;

		// Allocate memory for 8000bytes/sec + extra bytes if underrun
		buf = kmalloc((NUM_ISO_PACKETS_B*8)+8, GFP_KERNEL);
		if (!buf) {
			return (-ENOMEM);
		}
			
		// Fill the isochronous URB
		fill_isoc_urb(urb, dev,
			      0, // Unknown B channel
			      buf, NUM_ISO_PACKETS_B, 8,
			      usb_b_out_complete, bcs);
	}
	
	// Allocate URBs and buffers for the B channel in
	for (j=0; j < 2; j++) {
		urb = usb_alloc_urb(NUM_ISO_PACKETS_B);
		if (!urb) 
			return (-ENOMEM);

		hw->b_in_urb[j] = urb;

		buf = kmalloc(NUM_ISO_PACKETS_B * SIZE_ISO_PACKETS_B, GFP_KERNEL);
		if (!buf) { 
			return (-ENOMEM);
		}
			
		// Fill the isochronous URB
		fill_isoc_urb(urb, dev,
			      0,  // Unknown B channel
			      buf, NUM_ISO_PACKETS_B, SIZE_ISO_PACKETS_B,
			      usb_b_in_complete, bcs);
	}
	
	return (0);
}

/*
  This function will be called when the adapter is plugged
  into the USB bus (via init_usb_st5481).
*/
int  __devinit
setup_st5481(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	struct usb_device *dev;
	int i;
	
	if (cs->typ != ISDN_CTYPE_ST5481) {
		return (0);
	}
	
	dev = (struct usb_device *)card->para[1]; // FIXME: broken for sizeof(void *) != sizeof(int)
	if (!dev) {
		ERR("no usb_device");
		return (0);
	}

	strcpy(tmp, st5481_revision);
	INFO("Rev. %s",HiSax_getrev(tmp));
	INFO("adapter with VendorId %04x,ProductId %04x,LEDs %d",
	     dev->descriptor.idVendor,dev->descriptor.idProduct,NUMBER_OF_LEDS);
	
	if (setup_usb(cs, dev) < 0) {
		WARN("setup_usb failed");
		return (0);
	}
	
	if (setup_d(cs, dev) < 0) {
		WARN("setup_d failed");
		return (0);
	}
	
	for (i=0; i < 2; i++) {
		if (setup_b(&cs->bcs[i], dev) < 0) { 
			WARN("setup_b failed");
			return (0);
		}
	}
	
 	cs->cardmsg = &st5481_card_msg;
	
	return (1);
}
