/* $Id$

 * hscx_irq.c     low level b-channel stuff for Siemens HSCX
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 * This is an include file for fast inline IRQ stuff
 *
 * $Log$
 * Revision 1.14.2.1  2000/03/03 13:11:32  kai
 * changed L1_MODE_... to B1_MODE_... using constants defined in CAPI
 *
 * Revision 1.14  2000/02/26 00:35:13  keil
 * Fix skb freeing in interrupt context
 *
 * Revision 1.13  1999/10/14 20:25:28  keil
 * add a statistic for error monitoring
 *
 * Revision 1.12  1999/07/01 08:11:42  keil
 * Common HiSax version for 2.0, 2.1, 2.2 and 2.3 kernel
 *
 * Revision 1.11  1998/11/15 23:54:49  keil
 * changes from 2.0
 *
 * Revision 1.10  1998/08/13 23:36:35  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 1.9  1998/06/24 14:44:51  keil
 * Fix recovery of TX IRQ loss
 *
 * Revision 1.8  1998/04/10 10:35:22  paul
 * fixed (silly?) warnings from egcs on Alpha.
 *
 * Revision 1.7  1998/02/12 23:07:37  keil
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.6  1997/10/29 19:01:07  keil
 * changes for 2.1
 *
 * Revision 1.5  1997/10/01 09:21:35  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.4  1997/08/15 17:48:02  keil
 * cosmetic
 *
 * Revision 1.3  1997/07/27 21:38:36  keil
 * new B-channel interface
 *
 * Revision 1.2  1997/06/26 11:16:19  keil
 * first version
 *
 *
 */


static inline void
waitforCEC(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((READHSCX(cs, hscx, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforCEC timeout\n");
}


static inline void
waitforXFW(struct IsdnCardState *cs, int hscx)
{
	int to = 50;

	while ((!(READHSCX(cs, hscx, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: waitforXFW timeout\n");
}

static inline void
WriteHSCXCMDR(struct IsdnCardState *cs, int hscx, u_char data)
{
	long flags;

	save_flags(flags);
	cli();
	waitforCEC(cs, hscx);
	WRITEHSCX(cs, hscx, HSCX_CMDR, data);
	restore_flags(flags);
}



static void
hscx_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct IsdnCardState *cs = bcs->cs;
	long flags;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_empty_fifo");

	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hscx_empty_fifo: incoming packet too large");
		WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x80);
		bcs->hw.hscx.rcvidx = 0;
		return;
	}
	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	bcs->hw.hscx.rcvidx += count;
	save_flags(flags);
	cli();
	READHSCXFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x80);
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_empty_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static void
hscx_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	u_char *ptr;
	long flags;


	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hscx_fill_fifo");

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	more = (bcs->mode == B1_MODE_TRANS) ? 1 : 0;
	if (bcs->tx_skb->len > fifo_size) {
		more = !0;
		count = fifo_size;
	} else
		count = bcs->tx_skb->len;

	waitforXFW(cs, bcs->hw.hscx.hscx);
	save_flags(flags);
	cli();
	ptr = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hscx.count += count;
	WRITEHSCXFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static inline void
hscx_interrupt(struct IsdnCardState *cs, u_char val, u_char hscx)
{
	u_char r;
	struct BCState *bcs = cs->bcs + hscx;
	struct sk_buff *skb;
	int fifo_size = test_bit(HW_IPAC, &cs->HW_Flags)? 64: 32;
	int count;

	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = READHSCX(cs, hscx, HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX invalid frame");
#ifdef ERROR_STATISTIC
				bcs->err_inv++;
#endif
			}
			if ((r & 0x40) && (bcs->mode != B1_MODE_NULL)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX RDO mode=%d",
						bcs->mode);
#ifdef ERROR_STATISTIC
				bcs->err_rdo++;
#endif
			}
			if (!(r & 0x20)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX CRC error");
#ifdef ERROR_STATISTIC
				bcs->err_crc++;
#endif
			}
			WriteHSCXCMDR(cs, hscx, 0x80);
		} else {
			count = READHSCX(cs, hscx, HSCX_RBCL) & (
				test_bit(HW_IPAC, &cs->HW_Flags)? 0x3f: 0x1f);
			if (count == 0)
				count = fifo_size;
			hscx_empty_fifo(bcs, count);
			if ((count = bcs->hw.hscx.rcvidx - 1) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO)
					debugl1(cs, "HX Frame %d", count);
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "HSCX: receive out of memory\n");
				else {
					SET_SKB_FREE(skb);
					memcpy(skb_put(skb, count), bcs->hw.hscx.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.hscx.rcvidx = 0;
		hscx_sched_event(bcs, B_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		hscx_empty_fifo(bcs, fifo_size);
		if (bcs->mode == B1_MODE_TRANS) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(fifo_size)))
				printk(KERN_WARNING "HiSax: receive out of memory\n");
			else {
				SET_SKB_FREE(skb);
				memcpy(skb_put(skb, fifo_size), bcs->hw.hscx.rcvbuf, fifo_size);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.hscx.rcvidx = 0;
			hscx_sched_event(bcs, B_RCVBUFREADY);
		}
	}
	if (val & 0x10) {	/* XPR */
		if (bcs->tx_skb) {
			if (bcs->tx_skb->len) {
				hscx_fill_fifo(bcs);
				return;
			} else {
				bcs->st->l1.l1l2(bcs->st, PH_DATA | CONFIRM, bcs->tx_skb);
				idev_kfree_skb_irq(bcs->tx_skb, FREE_WRITE);
				bcs->hw.hscx.count = 0; 
				bcs->tx_skb = NULL;
			}
		}
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hscx.count = 0;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			hscx_fill_fifo(bcs);
		} else {
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			hscx_sched_event(bcs, B_XMTBUFREADY);
		}
	}
}

static inline void
hscx_int_main(struct IsdnCardState *cs, u_char val)
{

	u_char exval;
	struct BCState *bcs;

	if (val & 0x01) {
		bcs = cs->bcs + 1;
		exval = READHSCX(cs, 1, HSCX_EXIR);
		if (exval & 0x40) {
			if (bcs->mode == B1_MODE_TRANS)
				hscx_fill_fifo(bcs);
			else {
#ifdef ERROR_STATISTIC
				bcs->err_tx++;
#endif
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				if (bcs->tx_skb) {
					skb_push(bcs->tx_skb, bcs->hw.hscx.count);
					bcs->tx_cnt += bcs->hw.hscx.count;
					bcs->hw.hscx.count = 0;
				}
				WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x01);
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX B EXIR %x Lost TX", exval);
			}
		} else if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B EXIR %x", exval);
	}
	if (val & 0xf8) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX B interrupt %x", val);
		hscx_interrupt(cs, val, 1);
	}
	if (val & 0x02) {
		bcs = cs->bcs;
		exval = READHSCX(cs, 0, HSCX_EXIR);
		if (exval & 0x40) {
			if (bcs->mode == B1_MODE_TRANS)
				hscx_fill_fifo(bcs);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
#ifdef ERROR_STATISTIC
				bcs->err_tx++;
#endif
				if (bcs->tx_skb) {
					skb_push(bcs->tx_skb, bcs->hw.hscx.count);
					bcs->tx_cnt += bcs->hw.hscx.count;
					bcs->hw.hscx.count = 0;
				}
				WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x01);
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "HSCX A EXIR %x Lost TX", exval);
			}
		} else if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A EXIR %x", exval);
	}
	if (val & 0x04) {
		exval = READHSCX(cs, 0, HSCX_ISTA);
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX A interrupt %x", exval);
		hscx_interrupt(cs, exval, 0);
	}
}
