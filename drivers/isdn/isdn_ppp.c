/* $Id$
 *
 * Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * $Log$
 */

/* User setable options now have gone into isdnconfig.h */

#include <isdn.h>
#include "isdn_common.h"
#include "isdn_ppp.h"
#include "isdn_net.h"

/* Prototypes */
static int isdn_ppp_fill_rq(char *buf, int len, int minor);
static int isdn_ppp_hangup(int);
static int isdn_ppp_fill_mpqueue(isdn_net_dev *, u_char ** buf, int *len, int b,
			     int BEbyte, int *sqno, int min_sqno);
static void isdn_ppp_mask_queue(isdn_net_dev * dev, long mask);
static void isdn_ppp_cleanup_queue(isdn_net_dev * dev, long min);
static void isdn_ppp_push_higher(isdn_net_dev * net_dev, isdn_net_local * lp,
			     char *buf, int b, int proto, int pkt_len);
static int isdn_ppp_if_get_unit(char **namebuf);
static int isdn_ppp_bundle(int, int);

static char *isdn_ppp_revision = "$Revision$";
static struct ippp_struct *ippp_table = (struct ippp_struct *) 0;

int isdn_ppp_free(isdn_net_local * lp)
{
	if (lp->ppp_minor < 0)
		return 0;
	isdn_ppp_hangup(lp->ppp_minor);
#if 0
	printk(KERN_DEBUG "isdn_ppp_free %d %lx %lx\n", lp->ppp_minor, (long) lp, ippp_table[lp->ppp_minor].lp);
#endif
	ippp_table[lp->ppp_minor].lp = NULL;
	return 0;
}

int isdn_ppp_bind(isdn_net_local * lp)
{
	int i;
	int unit = 0;
	char *name;
	long flags;

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return 0;

	save_flags(flags);
	cli();
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if (ippp_table[i].state == IPPP_OPEN) {		/* OPEN, but not connected! */
			printk(KERN_DEBUG "find_minor, %d lp: %08lx\n", i, (long) lp);
			break;
		}
	}

	if (i >= ISDN_MAX_CHANNELS) {
		restore_flags(flags);
		printk(KERN_WARNING "isdn_ppp_bind: Can't find usable ippp device.\n");
		return -1;
	}
	lp->ppp_minor = i;
	ippp_table[lp->ppp_minor].lp = lp;

	name = lp->name;
	unit = isdn_ppp_if_get_unit(&name);
	ippp_table[lp->ppp_minor].unit = unit;

	ippp_table[lp->ppp_minor].state = IPPP_OPEN | IPPP_CONNECT | IPPP_NOBLOCK;

	restore_flags(flags);

	if (ippp_table[lp->ppp_minor].wq)
		wake_up_interruptible(&ippp_table[lp->ppp_minor].wq);

	return lp->ppp_minor;
}

static int isdn_ppp_hangup(int minor)
{
	if (minor < 0 || minor >= ISDN_MAX_CHANNELS)
		return 0;

	if (ippp_table[minor].state && ippp_table[minor].wq)
		wake_up_interruptible(&ippp_table[minor].wq);

	ippp_table[minor].state = IPPP_CLOSEWAIT;
	return 1;
}

/*
 * isdn_ppp_open 
 */

int isdn_ppp_open(int minor, struct file *file)
{
#if 0
	printk(KERN_DEBUG "ippp, open, minor: %d\n", minor);
#endif

	if (ippp_table[minor].state)
		return -EBUSY;

	ippp_table[minor].lp = 0;
	ippp_table[minor].mp_seqno = 0;
	ippp_table[minor].pppcfg = 0;
	ippp_table[minor].mpppcfg = 0;
	ippp_table[minor].range = 0x1000000;	/* 24 bit range */
	ippp_table[minor].last_link_seqno = -1;		/* maybe set to Bundle-MIN, when joining a bundle ?? */
	ippp_table[minor].unit = -1;	/* set, when we have our interface */
	ippp_table[minor].mru = 1524;	/* MRU, default 1524 */
	ippp_table[minor].maxcid = 16;	/* VJ: maxcid */
	ippp_table[minor].tk = current;
	ippp_table[minor].wq = NULL;
	ippp_table[minor].wq1 = NULL;
	ippp_table[minor].first = ippp_table[minor].rq + NUM_RCV_BUFFS - 1;
	ippp_table[minor].last = ippp_table[minor].rq;
#ifdef CONFIG_ISDN_PPP_VJ
	ippp_table[minor].cbuf = kmalloc(ippp_table[minor].mru + PPP_HARD_HDR_LEN + 2, GFP_KERNEL);

	if (ippp_table[minor].cbuf == NULL) {
		printk(KERN_DEBUG "ippp: Can't allocate memory buffer for VJ compression.\n");
		return -ENOMEM;
	}
	ippp_table[minor].slcomp = slhc_init(16, 16);	/* not necessary for 2. link in bundle */
#endif

	ippp_table[minor].state = IPPP_OPEN;

	return 0;
}

void ippp_release(int minor, struct file *file)
{
	int i;

	if (minor < 0 || minor >= ISDN_MAX_CHANNELS)
		return;

#if 0
	printk(KERN_DEBUG "ippp: release, minor: %d %lx\n", minor, (long) ippp_table[minor].lp);
#endif

	if (ippp_table[minor].lp) {	/* a lp address says: this link is still up */
		isdn_net_dev *p = ippp_table[minor].lp->netdev;
		ippp_table[minor].lp->ppp_minor = -1;
		isdn_net_hangup(&p->dev);

#if 0
		for (; p;) {
			isdn_net_local *lp = p->queue;
			p = p->next;
			for (;;) {
				if (lp == ippp_table[minor].lp) {
					printk(KERN_DEBUG "ippp_release, hangup\n");
					isdn_net_hangup(lp);
					isdn_net_clean_queue();
					p = NULL;
					break;
				}
				lp = lp->next;
				if (lp == p->queue)
					break;
			}
		}
#endif
		ippp_table[minor].lp = NULL;
	}
	for (i = 0; i < NUM_RCV_BUFFS; i++) {
		if (ippp_table[minor].rq[i].buf)
			kfree(ippp_table[minor].rq[i].buf);
	}

#ifdef CONFIG_ISDN_PPP_VJ
	slhc_free(ippp_table[minor].slcomp);
	kfree(ippp_table[minor].cbuf);
#endif

	ippp_table[minor].state = 0;
}

static int get_arg(void *b, unsigned long *val)
{
	int r;
	if ((r = verify_area(VERIFY_READ, (void *) b, sizeof(unsigned long))))
		 return r;
	memcpy_fromfs((void *) val, b, sizeof(unsigned long));
	return 0;
}

static int set_arg(void *b, unsigned long val)
{
	int r;
	if ((r = verify_area(VERIFY_WRITE, b, sizeof(unsigned long))))
		 return r;
	memcpy_tofs(b, (void *) &val, sizeof(unsigned long));
	return 0;
}

int ippp_ioctl(int minor, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int r;

/*  printk(KERN_DEBUG "ippp, ioctl, minor: %d %x %x\n",minor,cmd,ippp_table[minor].state); */

	if (!(ippp_table[minor].state & IPPP_OPEN))
		return -EINVAL;

	switch (cmd) {
#if 0
	case PPPIOCSINPSIG:	/* set input ready signal */
		/* usual: sig = SIGIO *//* we always deliver a SIGIO */
		break;
#endif
	case PPPIOCBUNDLE:
		printk(KERN_DEBUG "PPPIOCBUNDLE\n");
		if ((r = get_arg((void *) arg, &val)))
			return r;
		printk(KERN_DEBUG "isdnPPP-bundle: minor: %d unit0: %d unit1: %d\n", (int) minor, (int) ippp_table[minor].unit, (int) val);
		return isdn_ppp_bundle(minor, val);
		break;
	case PPPIOCGUNIT:	/* get ppp/isdn unit number */
		if ((r = set_arg((void *) arg, ippp_table[minor].unit)))
			return r;
		break;
	case PPPIOCGMPFLAGS:	/* get configuration flags */
		if ((r = set_arg((void *) arg, ippp_table[minor].mpppcfg)))
			return r;
		break;
	case PPPIOCSMPFLAGS:	/* set configuration flags */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		ippp_table[minor].mpppcfg = val;
		break;
	case PPPIOCGFLAGS:	/* get configuration flags */
		if ((r = set_arg((void *) arg, ippp_table[minor].pppcfg)))
			return r;
		break;
	case PPPIOCSFLAGS:	/* set configuration flags */
		if ((r = get_arg((void *) arg, &val))) {
			return r;
		}
		if (val & SC_ENABLE_IP && !(ippp_table[minor].pppcfg & SC_ENABLE_IP)) {
			ippp_table[minor].lp->netdev->dev.tbusy = 0;
			mark_bh(NET_BH);
		}
		ippp_table[minor].pppcfg = val;
		break;
#if 0
	case PPPIOCGSTAT:	/* read PPP statistic information */
		break;
	case PPPIOCGTIME:	/* read time delta information */
		break;
#endif
	case PPPIOCSMRU:	/* set receive unit size for PPP */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		ippp_table[minor].mru = val;
		break;
	case PPPIOCSMPMRU:
		break;
	case PPPIOCSMPMTU:
		break;
	case PPPIOCSMAXCID:	/* set the maximum compression slot id */
		if ((r = get_arg((void *) arg, &val)))
			return r;
		ippp_table[minor].maxcid = val;
		break;
	case PPPIOCGDEBUG:
		break;
	case PPPIOCSDEBUG:
		break;
	default:
		break;
	}
	return 0;
}

int ippp_select(int minor, struct file *file, int type, select_table * st)
{
	struct ippp_buf_queue *bf, *bl;
	unsigned long flags;

	if (!(ippp_table[minor].state & IPPP_OPEN))
		return -EINVAL;

	switch (type) {
	case SEL_IN:
		save_flags(flags);
		cli();
		bl = ippp_table[minor].last;
		bf = ippp_table[minor].first;
		if (bf->next == bl && !(ippp_table[minor].state & IPPP_NOBLOCK)) {
			select_wait(&ippp_table[minor].wq, st);
			restore_flags(flags);
			return 0;
		}
		ippp_table[minor].state &= ~IPPP_NOBLOCK;
		restore_flags(flags);
		return 1;
	case SEL_OUT:
		return 1;
	case SEL_EX:
		select_wait(&ippp_table[minor].wq1, st);
		return 0;
	}
	return 1;
}

/*
 *  fill up isdn_ppp_read() queue .. send SIGIO to process .. 
 */

static int isdn_ppp_fill_rq(char *buf, int len, int minor)
{
	struct ippp_buf_queue *bf, *bl;
	unsigned long flags;

	if (minor < 0 || minor >= ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "ippp: illegal minor.\n");
		return 0;
	}
	if (!(ippp_table[minor].state & IPPP_CONNECT)) {
		printk(KERN_DEBUG "ippp: device not activated.\n");
		return 0;
	}
	save_flags(flags);
	cli();

	bf = ippp_table[minor].first;
	bl = ippp_table[minor].last;

	if (bf == bl) {
		printk(KERN_WARNING "ippp: Queue is full; discarding first buffer\n");
		bf = bf->next;
		kfree(bf->buf);
		ippp_table[minor].first = bf;
	}
	bl->buf = (char *) kmalloc(len, GFP_ATOMIC);
	if (!bl->buf) {
		printk(KERN_WARNING "ippp: Can't alloc buf\n");
		restore_flags(flags);
		return 0;
	}
	bl->len = len;

	memcpy(bl->buf, buf, len);

	ippp_table[minor].last = bl->next;
	restore_flags(flags);

	if (ippp_table[minor].wq)
		wake_up_interruptible(&ippp_table[minor].wq);

	return len;
}

/*
 * read() .. pppd calls it only after receiption of an interrupt 
 */

int isdn_ppp_read(int minor, struct file *file, char *buf, int count)
{
	struct ippp_struct *c = &ippp_table[minor];
	struct ippp_buf_queue *b;
	int r;
	unsigned long flags;

	if (!(ippp_table[minor].state & IPPP_OPEN))
		return 0;

	if ((r = verify_area(VERIFY_WRITE, (void *) buf, count)))
		return r;

	save_flags(flags);
	cli();

	b = c->first->next;
	if (!b->buf) {
		restore_flags(flags);
		return -EAGAIN;
	}
	if (b->len < count)
		count = b->len;
	memcpy_tofs(buf, b->buf, count);
	kfree(b->buf);
	b->buf = NULL;
	c->first = b;
	restore_flags(flags);

	return count;
}

int isdn_ppp_write(int minor, struct file *file, FOPS_CONST char *buf, int count)
{
	isdn_net_local *lp;

	if (!(ippp_table[minor].state & IPPP_CONNECT))
		return 0;

	lp = ippp_table[minor].lp;

	/* -> push it directly to the lowlevel interface */

	if (!lp)
		printk(KERN_DEBUG "isdn_ppp_write: lp == NULL\n");
	else {
		if (lp->isdn_device < 0 || lp->isdn_channel < 0)
			return 0;

		if (dev->drv[lp->isdn_device]->running && lp->dialstate == 0 &&
		    (lp->flags & ISDN_NET_CONNECTED))
			dev->drv[lp->isdn_device]->interface->writebuf(lp->isdn_channel, buf, count, 1);
	}

	return count;
}

int isdn_ppp_init(void)
{
	int i, j;

	if (!(ippp_table = (struct ippp_struct *)
	      kmalloc(sizeof(struct ippp_struct) * ISDN_MAX_CHANNELS, GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_ppp_init: Could not alloc ippp_table\n");
		return -1;
	}
	memset((char *) ippp_table, 0, sizeof(struct ippp_struct) * ISDN_MAX_CHANNELS);
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		ippp_table[i].state = 0;
		ippp_table[i].first = ippp_table[i].rq + NUM_RCV_BUFFS - 1;
		ippp_table[i].last = ippp_table[i].rq;

		for (j = 0; j < NUM_RCV_BUFFS; j++) {
			ippp_table[i].rq[j].buf = NULL;
			ippp_table[i].rq[j].last = ippp_table[i].rq +
			    (NUM_RCV_BUFFS + j - 1) % NUM_RCV_BUFFS;
			ippp_table[i].rq[j].next = ippp_table[i].rq + (j + 1) % NUM_RCV_BUFFS;
		}
	}
	return 0;
}

void isdn_ppp_cleanup(void)
{
	kfree(ippp_table);
}

/*
 * handler for incoming packets on a syncPPP interface
 */

void isdn_ppp_receive(isdn_net_dev * net_dev, isdn_net_local * lp, u_char * buf, int pkt_len)
{
	int proto, b, freebuf = 0;
	int sqno_end;

	b = (buf[0] == 0xff && buf[1] == 0x03) ? 2 : 0;
	if (!b && (ippp_table[lp->ppp_minor].pppcfg & SC_REJ_COMP_AC))
		return;		/* discard it silently */

	if (!(ippp_table[lp->ppp_minor].mpppcfg & SC_REJ_MP_PROT)) {
		if (buf[b] & 0x1) {
			proto = (unsigned char) buf[b];
			b++;	/* protocol ID is only 8 bit */
		} else {
			proto = ((int) (unsigned char) buf[b] << 8) + buf[b + 1];
			b += 2;
		}
		if (proto == PPP_MP) {
			isdn_net_local *lpq;
			int sqno, min_sqno, tseq;
			u_char BEbyte = buf[b];
#if 0
			printk(KERN_DEBUG " %d/%d -> %02x %02x %02x %02x %02x %02x\n", lp->ppp_minor,
			       pkt_len, (int) buf[b], (int) buf[b + 1], (int) buf[b + 2], (int) buf[b + 3],
			       (int) buf[b + 4], (int) buf[b + 5]);
#endif
			if (!(ippp_table[lp->ppp_minor].mpppcfg & SC_IN_SHORT_SEQ)) {
				sqno = ((int) buf[b + 1] << 16) + ((int) buf[b + 2] << 8) + (int) buf[b + 3];
				b += 4;
			} else {
				sqno = (((int) buf[b] & 0xf) << 8) + (int) buf[b + 1];
				b += 2;
			}

			if ((tseq = ippp_table[lp->ppp_minor].last_link_seqno) >= sqno) {
				int range = ippp_table[lp->ppp_minor].range;
				if (tseq + 512 < range || sqno > 512)
					printk(KERN_WARNING "isdn_net_receive: MP: detected overflow with sqno: %d, last: %d !!!\n", sqno, tseq);
				else {
					sqno += range;
					ippp_table[lp->ppp_minor].last_link_seqno = sqno;
				}
			} else
				ippp_table[lp->ppp_minor].last_link_seqno = sqno;

			for (min_sqno = 0, lpq = net_dev->queue;;) {
				if (ippp_table[lpq->ppp_minor].last_link_seqno > min_sqno)
					min_sqno = ippp_table[lpq->ppp_minor].last_link_seqno;
				lpq = lpq->next;
				if (lpq == net_dev->queue)
					break;
			}
			if (min_sqno >= ippp_table[lpq->ppp_minor].range) {	/* OK, every link overflowed */
				int mask = ippp_table[lpq->ppp_minor].range - 1;	/* range is a power of 2 */
				isdn_ppp_cleanup_queue(net_dev, min_sqno);
				isdn_ppp_mask_queue(net_dev, mask);
				net_dev->ib.next_num &= mask;
				{
					struct sqqueue *q = net_dev->ib.sq;
					while (q) {
						q->sqno_start &= mask;
						q->sqno_end &= mask;
					}
				}
				min_sqno &= mask;
				for (lpq = net_dev->queue;;) {
					ippp_table[lpq->ppp_minor].last_link_seqno &= mask;
					lpq = lpq->next;
					if (lpq == net_dev->queue)
						break;
				}
			}
			if ((BEbyte & (MP_BEGIN_FRAG | MP_END_FRAG)) != (MP_BEGIN_FRAG | MP_END_FRAG)) {
				printk(KERN_DEBUG "ippp: trying ;) to fill mp_queue %d .. UNTESTED!!\n", lp->ppp_minor);
				if ((sqno_end = isdn_ppp_fill_mpqueue(net_dev, &buf, &pkt_len, b, BEbyte, &sqno, min_sqno)) < 0)
					return;		/* no packet complete */
				freebuf = 1;
			} else
				sqno_end = sqno;

			net_dev->ib.modify = 1;		/* block timeout-timer */
			if (net_dev->ib.next_num != sqno) {
				struct sqqueue *q;

				q = (struct sqqueue *) kmalloc(sizeof(struct sqqueue), GFP_ATOMIC);
				if (!q) {
					printk(KERN_WARNING "ippp: err, no memory !!\n");
					net_dev->ib.modify = 0;
					return;		/* discard */
				}
				if (!freebuf) {
					q->buf = (char *) kmalloc(pkt_len, GFP_ATOMIC);
					if (!q->buf) {
						kfree(q);
						printk(KERN_WARNING "ippp: err, no memory !!\n");
						net_dev->ib.modify = 0;
						return;		/* discard */
					}
					memcpy(q->buf, buf, pkt_len);
				} else
					q->buf = buf;
				q->freebuf = 1;
				q->pkt_len = pkt_len;
				q->b = b;
				q->sqno_end = sqno_end;
				q->sqno_start = sqno;
				q->timer = jiffies + (ISDN_TIMER_1SEC) * 5;	/* timeout after 5 seconds */

				if (!net_dev->ib.sq) {
					net_dev->ib.sq = q;
					q->next = NULL;
				} else {
					struct sqqueue *ql = net_dev->ib.sq;
					if (ql->sqno_start > q->sqno_start) {
						q->next = ql;
						net_dev->ib.sq = q;
					} else {
						while (ql->next && ql->next->sqno_start < q->sqno_start)
							ql = ql->next;
						q->next = ql->next;
						ql->next = q;
					}
				}
				net_dev->ib.modify = 0;
				return;
			} else {
				struct sqqueue *q;

				net_dev->ib.next_num = sqno_end + 1;
				isdn_ppp_push_higher(net_dev, lp, buf, b, -1, pkt_len);

				while ((q = net_dev->ib.sq) && q->sqno_start == net_dev->ib.next_num) {
					isdn_ppp_push_higher(net_dev, lp, q->buf, q->b, -1, q->pkt_len);
					if (q->freebuf)
						kfree(q->buf);
					net_dev->ib.sq = q->next;
					net_dev->ib.next_num = q->sqno_end + 1;
					kfree(q);
				}
			}
			net_dev->ib.modify = 0;

		} else
			isdn_ppp_push_higher(net_dev, lp, buf, b, proto, pkt_len);
	} else
		isdn_ppp_push_higher(net_dev, lp, buf, b, -1, pkt_len);

	if (freebuf)
		kfree(buf);
}


static void isdn_ppp_push_higher(isdn_net_dev * net_dev, isdn_net_local * lp, char *buf,
			     int b, int proto, int pkt_len)
{
	struct device *dev = &net_dev->dev;
	int skb_still_allocated = 0;
	struct sk_buff *skb = NULL;
	int pidcomp = 0;
	int bo = 0, mp = 0;

	if (proto < 0) {	/* MP, oder normales Paket bei REJ_MP, MP Pakete gehen bei REJ zum pppd */
		mp = 1;
		if (buf[b] & 0x01) {	/* is it odd? */
			proto = buf[b];
			b++;	/* protocol ID is only 8 bit */
			pidcomp = 1;
		} else {
			proto = ((int) buf[b] << 8) + buf[b + 1];
			b += 2;
		}
	}
#ifdef LINUX_1_2_X
	bo = 14;
#else
	bo = 0;
#endif

#if 0
	printk(KERN_DEBUG "isdn: proto %04x\n", proto);
#endif

	switch (proto) {
	case PPP_VJC_UNCOMP:
#ifdef CONFIG_ISDN_PPP_VJ
		slhc_remember(ippp_table[net_dev->local.ppp_minor].slcomp, buf + b, pkt_len - b);
#endif
#if 0
	case PPP_IPX:
#endif
	case PPP_IP:
		buf += b;
		pkt_len -= b;
		break;
	case PPP_VJC_COMP:
#ifdef CONFIG_ISDN_PPP_VJ
		{
#ifdef LINUX_1_2_X
			skb = alloc_skb(pkt_len + 14 + 35, GFP_ATOMIC);		/* 35 bytes max. expand */
#else
			skb = dev_alloc_skb(pkt_len + 35);
#endif
			if (!skb) {
				printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
				net_dev->local.stats.rx_dropped++;
				return;
			}
			skb->dev = dev;

#ifdef LINUX_1_2_X
			memcpy(&skb->data[14], buf + b, pkt_len - b);
			pkt_len = slhc_uncompress(ippp_table[net_dev->local.ppp_minor].slcomp,
					    &skb->data[14], pkt_len - b);
			skb->len = pkt_len + 14;
#else
			skb->mac.raw = skb->data;
			memcpy(skb->data, buf + b, pkt_len - b);
			pkt_len = slhc_uncompress(ippp_table[net_dev->local.ppp_minor].slcomp,
						  skb->data, pkt_len - b);
			skb_put(skb, pkt_len);
			skb->protocol = htons(ETH_P_IP);
#endif

			skb_still_allocated = 1;
		}
#else
		printk(KERN_INFO "isdn: Ooopsa .. VJ-Compression support not compiled into isdn driver.\n");
		lp->stats.rx_dropped++;
		return;
#endif
		break;
	default:
		if (mp) {
			buf[b - 4] = 0xff;
			buf[b - 3] = 0x03;
			if (pidcomp)
				buf[b - 2] = 0x00;
			isdn_ppp_fill_rq(buf + b - 4, pkt_len - b + 4, lp->ppp_minor);	/* push data to pppd device */
		} else
			isdn_ppp_fill_rq(buf, pkt_len, lp->ppp_minor);	/* push data to pppd device */
		return;
	}


	if (!skb_still_allocated) {
#ifdef LINUX_1_2_X
		skb = alloc_skb(pkt_len + bo, GFP_ATOMIC);	/*, GFP_ATOMIC); */
#else
		skb = dev_alloc_skb(pkt_len + bo + 2);	/*, GFP_ATOMIC); */
#endif
		if (skb == NULL) {
			printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
			net_dev->local.stats.rx_dropped++;
			return;
		}
		skb->dev = dev;
#ifdef LINUX_1_2_X
		skb->len = pkt_len + bo;
#else
		skb_reserve(skb, 2);
		skb_put(skb, pkt_len + bo);
#endif
		memcpy(&skb->data[bo], buf, pkt_len);
	}
	if (lp->p_encap) {
#ifdef LINUX_1_2_X
		struct ethhdr *eth = (struct ethhdr *) skb->data;
		int i;

		eth->h_proto = htons(ETH_P_IP);
/* IP: Insert fake MAC Adresses */
		for (i = 0; i < ETH_ALEN; i++)
			eth->h_source[i] = 0xfc;
		memcpy(&(eth->h_dest[0]), dev->dev_addr, ETH_ALEN);
#else
		skb->mac.raw = skb->data;
		skb->protocol = htons(ETH_P_IP);
#endif
	}
	netif_rx(skb);
	net_dev->local.stats.rx_packets++;
	/* Reset hangup-timer */
	lp->huptimer = 0;

	return;
}

int isdn_ppp_xmit(struct sk_buff *skb, struct device *dev)
{
	isdn_net_dev *nd = ((isdn_net_local *) dev->priv)->netdev;
	isdn_net_local *lp = nd->queue;
	int proto = PPP_IP;	/* 0x21 */
	struct ippp_struct *ipt = ippp_table + lp->ppp_minor;
	struct ippp_struct *ipts = ippp_table + lp->netdev->local.ppp_minor;
	u_char *buf = skb->data;
	int pktlen = skb->len;
	int bo;

	if (ipt->pppcfg & SC_COMP_TCP) {
		buf += 14;
		pktlen = slhc_compress(ipts->slcomp, buf, pktlen - 14, ipts->cbuf + PPP_HARD_HDR_LEN,
				 &buf, !(ipts->pppcfg & SC_NO_TCP_CCID));
		pktlen += 14;
		if (buf[0] & SL_TYPE_COMPRESSED_TCP) {	/* cslip? style -> PPP */
			proto = PPP_VJC_COMP;
			buf[0] ^= SL_TYPE_COMPRESSED_TCP;
		} else {
			if (buf[0] >= SL_TYPE_UNCOMPRESSED_TCP)
				proto = PPP_VJC_UNCOMP;
			buf[0] = (buf[0] & 0x0f) | 0x40;
		}
		buf -= 14;
	}
	if (!(ipt->mpppcfg & SC_MP_PROT)) {
		bo = 14 - PPP_HARD_HDR_LEN;
		buf[bo] = 0xff;	/* PPP header, raw IP with no compression */
		buf[bo + 1] = 0x03;
		buf[bo + 2] = 0x00;
		buf[bo + 3] = proto;
	} else {
		/* we get mp_seqno from static isdn_net_local */
		long mp_seqno = ipts->mp_seqno;
		ipts->mp_seqno++;
		if (ipt->mpppcfg & SC_OUT_SHORT_SEQ) {
			mp_seqno &= 0xfff;
			bo = 14 - (PPP_HARD_HDR_LEN - 1) - 4;	/* 4 = short seq. MP header */
			buf[bo + 4] = MP_BEGIN_FRAG | MP_END_FRAG | (mp_seqno >> 8);	/* (B)egin & (E)ndbit .. */
			buf[bo + 5] = (mp_seqno >> 8) & 0xff;
			buf[bo + 6] = proto;	/* PID compression */
		} else {
			bo = 14 - (PPP_HARD_HDR_LEN - 1) - 6;	/* 6 = long seq. MP header */
			buf[bo + 4] = MP_BEGIN_FRAG | MP_END_FRAG;	/* (B)egin & (E)ndbit .. */
			buf[bo + 5] = (mp_seqno >> 16) & 0xff;	/* sequence nubmer: 24bit */
			buf[bo + 6] = (mp_seqno >> 8) & 0xff;
			buf[bo + 7] = (mp_seqno >> 0) & 0xff;
			buf[bo + 8] = proto;	/* PID compression */
		}
		buf[bo] = 0xff;	/* PPP header, raw IP with no compression */
		buf[bo + 1] = 0x03;
		buf[bo + 2] = PPP_MP >> 8;	/* MP Protocol, 0x003d */
		buf[bo + 3] = PPP_MP;
	}
	lp->huptimer = 0;
	if (!(ipt->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
		printk(KERN_INFO "isdn, xmit: Packet blocked: %d %d\n", lp->isdn_device, lp->isdn_channel);
		return 1;
	}
	if (isdn_net_send(&buf[bo], lp->isdn_device, lp->isdn_channel, pktlen - bo)) {
		dev->tbusy = 0;
		lp->stats.tx_packets++;
	}
	return 0;
}

void isdn_ppp_free_mpqueue(isdn_net_dev * p)
{
	struct mpqueue *ql, *q = p->mp_last;
	while (q) {
		ql = q->next;
		kfree(q->buf);
		kfree(q);
		q = ql;
	}
}

static int isdn_ppp_bundle(int minor, int unit)
{
	char ifn[IFNAMSIZ + 1];
	long flags;
	isdn_net_dev *p;
	isdn_net_local *lp;
	isdn_net_local *nlp, *olp;

	sprintf(ifn, "ippp%d", unit);
	p = isdn_net_findif(ifn);
	if (!p)
		return -1;

	nlp = kmalloc(sizeof(isdn_net_local), GFP_KERNEL);
	if (!nlp)
		return -EINVAL;

	isdn_timer_ctrl(ISDN_TIMER_IPPP, 1);	/* enable timer for ippp/MP */

	save_flags(flags);
	cli();

/*  check whether interface is up */

	olp = ippp_table[minor].lp;
	*nlp = *olp;		/* copy lp to new (dynamic) struct */
	ippp_table[minor].lp = nlp;

	lp = p->queue;
	nlp->last = lp->last;
	lp->last->next = nlp;
	lp->last = nlp;
	nlp->next = lp;
	p->queue = nlp;

	if (!(nlp->flags & ISDN_NET_TMP))
		printk(KERN_WARNING "isdn_ppp_bundle: bundled non-tmp interface ..\n");

	nlp->flags &= ~ISDN_NET_TMP;
	nlp->flags |= ISDN_NET_DYNAMIC;

	nlp->netdev = lp->netdev;
	nlp->phone[0] = NULL;	/* pointers are invalid after removing the parent-struct */
	nlp->phone[1] = NULL;
	nlp->dial = NULL;
	strcpy(nlp->name, lp->name);
	ippp_table[nlp->ppp_minor].unit = ippp_table[lp->ppp_minor].unit;
/* maybe also SC_CCP stuff */
	ippp_table[nlp->ppp_minor].pppcfg |= ippp_table[lp->ppp_minor].pppcfg &
	    (SC_ENABLE_IP | SC_NO_TCP_CCID | SC_REJ_COMP_TCP);

	ippp_table[nlp->ppp_minor].mpppcfg |= ippp_table[lp->ppp_minor].mpppcfg &
	    (SC_MP_PROT | SC_REJ_MP_PROT | SC_OUT_SHORT_SEQ | SC_IN_SHORT_SEQ);
#if 0
	if (ippp_table[nlp->ppp_minor].mpppcfg != ippp_table[lp->ppp_minor].mpppcfg) {
		printk(KERN_WARNING "isdn_ppp_bundle: different MP options %04x and %04x\n",
		       ippp_table[nlp->ppp_minor].mpppcfg, ippp_table[lp->ppp_minor].mpppcfg);
	}
#endif

	restore_flags(flags);

	/* dieses interface loeschen(tmp) oder
	   zuruecksetzen(!tmp) */

	if (olp->flags & ISDN_NET_TMP)
		isdn_net_rm(olp->name);
#if 0
	else
		isdn_net_hangup(olp);	/* hangup ist schlecht .. man muss nur einige Werte zuruecksetzen */
#endif

	return 0;
}


static void isdn_ppp_mask_queue(isdn_net_dev * dev, long mask)
{
	struct mpqueue *q = dev->mp_last;
	while (q) {
		q->sqno &= mask;
		q = q->next;
	}
}


static int isdn_ppp_fill_mpqueue(isdn_net_dev * dev, u_char ** buf, int *len, int offset, int BEbyte, int *sqnop, int min_sqno)
{
	struct mpqueue *qe, *q1, *q;
	long cnt, flags;
	int pktlen, sqno_end;
	int sqno = *sqnop;

	q1 = (struct mpqueue *) kmalloc(sizeof(struct mpqueue), GFP_KERNEL);
	if (!q1) {
		printk(KERN_WARNING "isdn_ppp_fill_mpqueue: Can't alloc struct memory.\n");
		save_flags(flags);
		cli();
		isdn_ppp_cleanup_queue(dev, min_sqno);
		restore_flags(flags);
		return -1;
	}
	q1->buf = kmalloc(*len - offset, GFP_KERNEL);
	if (!q1->buf) {
		kfree(q1);
		printk(KERN_WARNING "isdn_ppp_fill_mpqueue: Can't alloc buf memory.\n");
		save_flags(flags);
		cli();
		isdn_ppp_cleanup_queue(dev, min_sqno);
		restore_flags(flags);
		return -1;
	}
	q1->sqno = sqno;
	q1->BEbyte = BEbyte;
	q1->pktlen = *len - offset;
	q1->time = jiffies;
	memcpy(q1->buf, *buf + offset, *len - offset);

	save_flags(flags);
	cli();

	if (!(q = dev->mp_last)) {
		dev->mp_last = q1;
		q1->next = NULL;
		q1->last = NULL;
		isdn_ppp_cleanup_queue(dev, min_sqno);	/* not necessary */
		restore_flags(flags);
		return -1;
	}
	for (;;) {		/* the faster way would be to step from the queue-end to the start */
		if (sqno > q->sqno) {
			if (q->next) {
				q = q->next;
				continue;
			}
			q->next = q1;
			q1->next = NULL;
			q1->last = q;
			break;
		}
		if (sqno == q->sqno)
			printk(KERN_WARNING "isdn_fill_mpqueue: illegal sqno received!!\n");
		q1->last = q->last;
		q1->next = q;
		if (q->last) {
			q->last->next = q1;
		} else
			dev->mp_last = q1;
		q->last = q1;
		break;
	}

/* now we check whether we completed a packet with this fragment */
	pktlen = -q1->pktlen;
	q = q1;
	cnt = q1->sqno;
	while (!(q->BEbyte & MP_END_FRAG)) {
		cnt++;
		if (!(q->next) || q->next->sqno != cnt) {
			isdn_ppp_cleanup_queue(dev, min_sqno);
			restore_flags(flags);
			return -1;
		}
		pktlen += q->pktlen;
		q = q->next;
	}
	pktlen += q->pktlen;
	qe = q;

	q = q1;
	cnt = q1->sqno;
	while (!(q->BEbyte & MP_BEGIN_FRAG)) {
		cnt--;
		if (!(q->last) || q->last->sqno != cnt) {
			isdn_ppp_cleanup_queue(dev, min_sqno);
			restore_flags(flags);
			return -1;
		}
		pktlen += q->pktlen;
		q = q->last;
	}
	pktlen += q->pktlen;

	if (q->last)
		q->last->next = qe->next;
	else
		dev->mp_last = qe->next;

	if (qe->next)
		qe->next->last = q->last;
	qe->next = NULL;
	sqno_end = qe->sqno;
	*sqnop = q->sqno;

	isdn_ppp_cleanup_queue(dev, min_sqno);
	restore_flags(flags);

	*buf = kmalloc(pktlen + offset, GFP_KERNEL);
	*len = pktlen + offset;
	if (!(*buf)) {
		while (q) {
			struct mpqueue *ql = q->next;
			kfree(q->buf);
			kfree(q);
			q = ql;
		}
		return -2;
	}
	cnt = 0;
	while (q) {
		struct mpqueue *ql = q->next;
		memcpy(*buf + offset + cnt, q->buf, q->pktlen);
		cnt += q->pktlen;
		kfree(q->buf);
		kfree(q);
		q = ql;
	}

	return sqno_end;
}

/*
 * remove stale packets from list
 */

static void isdn_ppp_cleanup_queue(isdn_net_dev * dev, long min_sqno)
{
/* z.z einfaches aussortieren gammeliger pakete. Fuer die Zukunft:
   eventuell, solange vorne kein B-paket ist und sqno<=min_sqno: auch rauswerfen
   wenn sqno<min_sqno und Luecken vorhanden sind: auch weg (die koennen nicht mehr gefuellt werden)
   bei paketen groesser min_sqno: ueber mp_mrru: wenn summe ueber pktlen der rumhaengenden Pakete 
   groesser als mrru ist: raus damit , Pakete muessen allerdings zusammenhaengen sonst koennte
   ja ein Paket mit B und eins mit E dazwischenpassen */

	struct mpqueue *ql, *q = dev->mp_last;
	while (q) {
		if (q->sqno < min_sqno) {
			if (q->BEbyte & MP_END_FRAG) {
				printk(KERN_DEBUG "ippp: freeing stale packet!\n");
				if ((dev->mp_last = q->next))
					q->next->last = NULL;
				while (q) {
					ql = q->last;
					kfree(q->buf);
					kfree(q);
					q = ql;
				}
				q = dev->mp_last;
			} else
				q = q->next;
		} else
			break;
	}
}

/*
 * a buffered packet timed-out?
 */

void isdn_ppp_timer_timeout(void)
{
	isdn_net_dev *net_dev = dev->netdev;
	struct sqqueue *q, *ql = NULL, *qn;

	while (net_dev) {
		isdn_net_local *lp = &net_dev->local;
		if (net_dev->ib.modify)		/* interface locked? */
			continue;

		q = net_dev->ib.sq;
		while (q) {
			if (q->sqno_start == net_dev->ib.next_num || q->timer < jiffies) {
				ql = net_dev->ib.sq;
				net_dev->ib.sq = q->next;
				net_dev->ib.next_num = q->sqno_end + 1;
				q->next = NULL;
				for (; ql;) {
					isdn_ppp_push_higher(net_dev, lp, ql->buf, ql->b, -1, ql->pkt_len);
					if (ql->freebuf)
						kfree(ql->buf);
					qn = ql->next;
					kfree(ql);
					ql = qn;
				}
				q = net_dev->ib.sq;
			} else
				q = q->next;
		}
		net_dev = net_dev->next;
	}
}

int isdn_ppp_dev_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	int error;
	char *r;
	int len;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;

	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -EINVAL;

	switch (cmd) {
	case SIOCGPPPVER:
		r = (char *) ifr->ifr_ifru.ifru_data;
		len = strlen(PPP_VERSION) + 1;
		error = verify_area(VERIFY_WRITE, r, len);
		if (!error)
			memcpy_tofs(r, PPP_VERSION, len);
		break;
	default:
		error = -EINVAL;
	}
	return error;
}

static int isdn_ppp_if_get_unit(char **namebuf)
{
	char *name = *namebuf;
	int len, i, unit = 0, deci;

	len = strlen(name);
	for (i = 0, deci = 1; i < len; i++, deci *= 10) {
		if (name[len - 1 - i] >= '0' && name[len - 1 - i] <= '9')
			unit += (name[len - 1 - i] - '0') * deci;
		else
			break;
	}
	if (!i)
		unit = -1;

	*namebuf = name + len - 1 - i;
	return unit;

}





