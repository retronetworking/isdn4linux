/* $Id$

 * hscx_irq.c     low level b-channel stuff for Siemens HSCX
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 * This is an include file for fast inline IRQ stuff
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
		debugl1(L1_DEB_HSCX, cs, "hscx_empty_fifo");

	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		debugl1(L1_DEB_WARN, cs, "hscx_empty_fifo: incoming packet too large");
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
		debugl1(L1_DEB_HSCX_FIFO, cs, bcs->blog);
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
		debugl1(L1_DEB_HSCX, cs, "hscx_fill_fifo");

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
	bcs->hw.hscx.count += count;
	WRITEHSCXFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(L1_DEB_HSCX_FIFO, cs, bcs->blog);
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
				debugl1(L1_DEB_WARN, cs, "HSCX invalid frame");
#ifdef ERROR_STATISTIC
				bcs->err_inv++;
#endif
			}
			if ((r & 0x40) && (bcs->mode != B1_MODE_NULL)) {
				debugl1(L1_DEB_WARN, cs, "HSCX RDO mode=%d", bcs->mode);
#ifdef ERROR_STATISTIC
				bcs->err_rdo++;
#endif
			}
			if (!(r & 0x20)) {
				debugl1(L1_DEB_WARN, cs, "HSCX CRC error");
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
				debugl1(L1_DEB_HSCX_FIFO, cs, "HX Frame %d", count);
				if (!(skb = dev_alloc_skb_headroom(count, bcs->headroom)))
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
			if (!(skb = dev_alloc_skb_headroom(fifo_size, bcs->headroom)))
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
					bcs->hw.hscx.count = 0;
				}
				WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x01);
				debugl1(L1_DEB_WARN, cs, "HSCX B EXIR %x Lost TX", exval);
			}
		} else
			debugl1(L1_DEB_HSCX, cs, "HSCX B EXIR %x", exval);
	}
	if (val & 0xf8) {
		debugl1(L1_DEB_HSCX, cs, "HSCX B interrupt %x", val);
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
					bcs->hw.hscx.count = 0;
				}
				WriteHSCXCMDR(cs, bcs->hw.hscx.hscx, 0x01);
				debugl1(L1_DEB_WARN, cs, "HSCX A EXIR %x Lost TX", exval);
			}
		} else
			debugl1(L1_DEB_HSCX, cs, "HSCX A EXIR %x", exval);
	}
	if (val & 0x04) {
		exval = READHSCX(cs, 0, HSCX_ISTA);
		debugl1(L1_DEB_HSCX, cs, "HSCX A interrupt %x", exval);
		hscx_interrupt(cs, exval, 0);
	}
}
