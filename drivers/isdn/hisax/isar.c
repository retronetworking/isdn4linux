/* $Id$

 * isar.c   ISAR (Siemens PSB 7110) specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log$
 * Revision 1.1.2.1  1998/09/27 13:01:43  keil
 * Start support for ISAR based cards
 *
 * Revision 1.1  1998/08/13 23:33:47  keil
 * First version, only init
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isar.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

#define DBG_LOADFIRM	0
#define DUMP_MBOXFRAME	2

#define MIN(a,b) ((a<b)?a:b)

static inline int
waitforHIA(struct IsdnCardState *cs, int timeout)
{

	while ((cs->BC_Read_Reg(cs, 0, ISAR_HIA) & 1) && timeout) {
		udelay(1);
		timeout--;
	}
	if (!timeout)
		printk(KERN_WARNING "HiSax: ISAR waitforHIA timeout\n");
	return(timeout);
}


int
sendmsg(struct IsdnCardState *cs, u_char his, u_char creg, u_char len,
	u_char *msg)
{
	long flags;
	int i;
	
	if (!waitforHIA(cs, 10000))
		return(0);
#if DUMP_MBOXFRAME
	if (cs->debug & L1_DEB_HSCX) {
		char tmp[32];
		
		sprintf(tmp, "sendmsg(%02x,%02x,%d)", his, creg, len);
		debugl1(cs, tmp);
	}
#endif
	save_flags(flags);
	cli();
	cs->BC_Write_Reg(cs, 0, ISAR_CTRL_H, creg);
	cs->BC_Write_Reg(cs, 0, ISAR_CTRL_L, len);
	cs->BC_Write_Reg(cs, 0, ISAR_WADR, 0);
	if (msg && len) {
		cs->BC_Write_Reg(cs, 1, ISAR_MBOX, msg[0]);
		for (i=1; i<len; i++)
			cs->BC_Write_Reg(cs, 2, ISAR_MBOX, msg[i]);
#if DUMP_MBOXFRAME>1
		if (cs->debug & L1_DEB_HSCX_FIFO) {
			char tmp[256], *t;
			
			i = len;
			while (i>0) {
				t = tmp;
				t += sprintf(t, "sendmbox cnt %d", len);
				QuickHex(t, &msg[len-i], (i>64) ? 64:i);
				debugl1(cs, tmp);
				i -= 64;
			}
		}
#endif
	}
	cs->BC_Write_Reg(cs, 1, ISAR_HIS, his);
	restore_flags(flags);
	return(1);
}

/* Call only with IRQ disabled !!! */
inline void
receivemsg(struct IsdnCardState *cs, u_char *msg)
{
	int i;

	cs->hw.sedl.cmsb = cs->BC_Read_Reg(cs, 1, ISAR_CTRL_H);
	cs->hw.sedl.clsb = cs->BC_Read_Reg(cs, 1, ISAR_CTRL_L);
#if DUMP_MBOXFRAME
	if (cs->debug & L1_DEB_HSCX) {
		char tmp[32];
		
		sprintf(tmp, "rcv_mbox(%02x,%02x,%d)", cs->hw.sedl.iis,
			cs->hw.sedl.cmsb, cs->hw.sedl.clsb);
		debugl1(cs, tmp);
	}
#endif
	cs->BC_Write_Reg(cs, 1, ISAR_RADR, 0);
	if (msg && cs->hw.sedl.clsb) {
		msg[0] = cs->BC_Read_Reg(cs, 1, ISAR_MBOX);
		for (i=1; i < cs->hw.sedl.clsb; i++)
			 msg[i] = cs->BC_Read_Reg(cs, 2, ISAR_MBOX);
#if DUMP_MBOXFRAME>1
		if (cs->debug & L1_DEB_HSCX_FIFO) {
			char tmp[256], *t;
			
			i = cs->hw.sedl.clsb;
			while (i>0) {
				t = tmp;
				t += sprintf(t, "rcv_mbox cnt %d", cs->hw.sedl.clsb);
				QuickHex(t, &msg[cs->hw.sedl.clsb-i], (i>64) ? 64:i);
				debugl1(cs, tmp);
				i -= 64;
			}
		}
#endif
	}
	cs->BC_Write_Reg(cs, 1, ISAR_IIA, 0);
}

int
waitrecmsg(struct IsdnCardState *cs, u_char *len,
	u_char *msg, int maxdelay)
{
	int timeout = 0;
	long flags;
	
	while((!(cs->BC_Read_Reg(cs, 0, ISAR_IRQBIT) & ISAR_IRQSTA)) &&
		(timeout++ < maxdelay))
		udelay(1);
	if (timeout >= maxdelay) {
		printk(KERN_WARNING"isar recmsg IRQSTA timeout\n");
		return(0);
	}
	save_flags(flags);
	cli();
	cs->hw.sedl.iis = cs->BC_Read_Reg(cs, 1, ISAR_IIS);
	receivemsg(cs, msg);
	*len = cs->hw.sedl.clsb;
	restore_flags(flags);
	return(1);
}

int
ISARVersion(struct IsdnCardState *cs, char *s)
{
	int ver;
	u_char msg[] = ISAR_MSG_HWVER;
	u_char tmp[64];
	u_char len;
	int debug;

	cs->cardmsg(cs, CARD_RESET,  NULL);
	/* disable ISAR IRQ */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	debug = cs->debug;
	cs->debug &= ~(L1_DEB_HSCX | L1_DEB_HSCX_FIFO);
	if (!sendmsg(cs, ISAR_HIS_VNR, 0, 3, msg))
		return(-1);
	if (!waitrecmsg(cs, &len, tmp, 100000))
		 return(-2);
	cs->debug = debug;
	if (cs->hw.sedl.iis == ISAR_IIS_VNR) {
		if (len == 1) {
			ver = tmp[0] & 0xf;
			printk(KERN_INFO "%s ISAR version %d\n", s, ver);
			return(ver);
		}
		return(-3);
	}
	return(-4);
}

int
isar_load_firmware(struct IsdnCardState *cs, u_char *buf)
{
	int ret, size, cnt, debug;
	u_char len, nom, noc;
	u_short sadr, left, *sp;
	u_char *p = buf;
	u_char *msg, *tmpmsg, *mp, tmp[64];
	long flags;
	
	
	struct {u_short sadr;
		u_short len;
		u_short d_key;
	} blk_head;
		
#define	BLK_HEAD_SIZE 6
	if (1 != (ret = ISARVersion(cs, "Testing"))) {
		printk(KERN_ERR"isar_load_firmware wrong isar version %d\n", ret);
		return(1);
	}
	debug = cs->debug;
#if DBG_LOADFIRM<2
	cs->debug &= ~(L1_DEB_HSCX | L1_DEB_HSCX_FIFO);
#endif
	printk(KERN_DEBUG"isar_load_firmware buf %#lx\n", (u_long)buf);
	if ((ret = verify_area(VERIFY_READ, (void *) p, sizeof(int)))) {
		printk(KERN_ERR"isar_load_firmware verify_area ret %d\n", ret);
		return ret;
	}
	if ((ret = copy_from_user(&size, p, sizeof(int)))) {
		printk(KERN_ERR"isar_load_firmware copy_from_user ret %d\n", ret);
		return ret;
	}
	p += sizeof(int);
	printk(KERN_DEBUG"isar_load_firmware size: %d\n", size);
	if ((ret = verify_area(VERIFY_READ, (void *) p, size))) {
		printk(KERN_ERR"isar_load_firmware verify_area ret %d\n", ret);
		return ret;
	}
	cnt = 0;
	/* disable ISAR IRQ */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	if (!(msg = kmalloc(256, GFP_KERNEL))) {
		printk(KERN_ERR"isar_load_firmware no buffer\n");
		return (1);
	}
	if (!(tmpmsg = kmalloc(256, GFP_KERNEL))) {
		printk(KERN_ERR"isar_load_firmware no tmp buffer\n");
		kfree(msg);
		return (1);
	}
	while (cnt < size) {
		if ((ret = copy_from_user(&blk_head, p, BLK_HEAD_SIZE))) {
			printk(KERN_ERR"isar_load_firmware copy_from_user ret %d\n", ret);
			goto reterror;
		}
		cnt += BLK_HEAD_SIZE;
		p += BLK_HEAD_SIZE;
		printk(KERN_DEBUG"isar firmware block (%#x,%5d,%#x)\n",
			blk_head.sadr, blk_head.len, blk_head.d_key & 0xff);
		sadr = blk_head.sadr;
		left = blk_head.len;
		if (!sendmsg(cs, ISAR_HIS_DKEY, blk_head.d_key & 0xff, 0, NULL)) {
			printk(KERN_ERR"isar sendmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if (!waitrecmsg(cs, &len, tmp, 100000)) {
			printk(KERN_ERR"isar waitrecmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if ((cs->hw.sedl.iis != ISAR_IIS_DKEY) || cs->hw.sedl.cmsb || len) {
			printk(KERN_ERR"isar wrong dkey response (%x,%x,%x)\n",
				cs->hw.sedl.iis, cs->hw.sedl.cmsb, len);
			ret = 1;goto reterror;
		}
		while (left>0) {
			noc = MIN(126, left);
			nom = 2*noc;
			mp  = msg;
			*mp++ = sadr / 256;
			*mp++ = sadr % 256;
			left -= noc;
			*mp++ = noc;
			if ((ret = copy_from_user(tmpmsg, p, nom))) {
				printk(KERN_ERR"isar_load_firmware copy_from_user ret %d\n", ret);
				goto reterror;
			}
			p += nom;
			cnt += nom;
			nom += 3;
			sp = (u_short *)tmpmsg;
#if DBG_LOADFIRM
			printk(KERN_DEBUG"isar: load %3d words at %04x\n",
				 noc, sadr);
#endif
			sadr += noc;
			while(noc) {
				*mp++ = *sp / 256;
				*mp++ = *sp % 256;
				sp++;
				noc--;
			}
			if (!sendmsg(cs, ISAR_HIS_FIRM, 0, nom, msg)) {
				printk(KERN_ERR"isar sendmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if (!waitrecmsg(cs, &len, tmp, 100000)) {
				printk(KERN_ERR"isar waitrecmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if ((cs->hw.sedl.iis != ISAR_IIS_FIRM) || cs->hw.sedl.cmsb || len) {
				printk(KERN_ERR"isar wrong prog response (%x,%x,%x)\n",
					cs->hw.sedl.iis, cs->hw.sedl.cmsb, len);
				ret = 1;goto reterror;
			}
		}
		printk(KERN_DEBUG"isar firmware block %5d words loaded\n",
			blk_head.len);
	}
	msg[0] = 0xff;
	msg[1] = 0xfe;
	cs->hw.sedl.bstat = 0;
	if (!sendmsg(cs, ISAR_HIS_STDSP, 0, 2, msg)) {
		printk(KERN_ERR"isar sendmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if (!waitrecmsg(cs, &len, tmp, 100000)) {
		printk(KERN_ERR"isar waitrecmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if ((cs->hw.sedl.iis != ISAR_IIS_STDSP) || cs->hw.sedl.cmsb || len) {
		printk(KERN_ERR"isar wrong start dsp response (%x,%x,%x)\n",
			cs->hw.sedl.iis, cs->hw.sedl.cmsb, len);
		ret = 1;goto reterror;
	} else
		printk(KERN_DEBUG"isar start dsp success\n");
	/* NORMAL mode entered */
	/* Enable IRQs of ISAR */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, ISAR_IRQSTA);
	save_flags(flags);
	sti();
	cnt = 1000; /* max 1s */
	while ((!cs->hw.sedl.bstat) && cnt) {
		udelay(1000);
		cnt--;
	}
	if (!cnt) {
		printk(KERN_ERR"isar no general status event received\n");
		ret = 1;goto reterrflg;
	} else {
		printk(KERN_DEBUG"isar general status event %x\n",
			cs->hw.sedl.bstat);
	}
	cs->hw.sedl.iis = 0;
	if (!sendmsg(cs, ISAR_HIS_DIAG, ISAR_CTRL_STST, 0, NULL)) {
		printk(KERN_ERR"isar sendmsg self tst failed\n");
		ret = 1;goto reterrflg;
	}
	cnt = 1000; /* max 10 ms */
	while ((cs->hw.sedl.iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	if (!cnt) {
		printk(KERN_ERR"isar no self tst response\n");
		ret = 1;goto reterrflg;
	} else if ((cs->hw.sedl.cmsb == ISAR_CTRL_STST) && (cs->hw.sedl.clsb == 1)
		&& (cs->hw.sedl.par[0] == 0)) {
		printk(KERN_DEBUG"isar seft test OK\n");
	} else {
		printk(KERN_DEBUG"isar seft test not OK %x/%x/%x\n",
			cs->hw.sedl.cmsb, cs->hw.sedl.clsb, cs->hw.sedl.par[0]);
		ret = 1;goto reterror;
	}
	cs->hw.sedl.iis = 0;
	if (!sendmsg(cs, ISAR_HIS_DIAG, ISAR_CTRL_SWVER, 0, NULL)) {
		printk(KERN_ERR"isar RQST SVN failed\n");
		ret = 1;goto reterror;
	}
	cnt = 10000; /* max 100 ms */
	while ((cs->hw.sedl.iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	if (!cnt) {
		printk(KERN_ERR"isar no SVN response\n");
		ret = 1;goto reterrflg;
	} else {
		if ((cs->hw.sedl.cmsb == ISAR_CTRL_SWVER) && (cs->hw.sedl.clsb == 1))
			printk(KERN_DEBUG"isar software version %#x\n",
				cs->hw.sedl.par[0]);
		else {
			printk(KERN_ERR"isar wrong swver response (%x,%x) cnt(%d)\n",
				cs->hw.sedl.cmsb, cs->hw.sedl.clsb, cnt);
			ret = 1;goto reterrflg;
		}
	}
	ret = 0;
reterrflg:
	restore_flags(flags);
reterror:
	cs->debug = debug;
	if (ret)
		/* disable ISAR IRQ */
		cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	kfree(msg);
	kfree(tmpmsg);
	return(ret);
}

void
isar_int_main(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	cli();
	cs->hw.sedl.iis = cs->BC_Read_Reg(cs, 1, ISAR_IIS);
	switch (cs->hw.sedl.iis) {
		case ISAR_IIS_GSTEV:
			receivemsg(cs, NULL);
			cs->hw.sedl.bstat = cs->hw.sedl.cmsb;
			break;
		case ISAR_IIS_DIAG:
			receivemsg(cs, (u_char *)cs->hw.sedl.par);
			break;
		default:
			receivemsg(cs, NULL);
			
			if (cs->debug & L1_DEB_WARN) {
				char tmp[64];
				sprintf(tmp, "unhandled msg iis(%x) ctrl(%x/%x)",
					cs->hw.sedl.iis, cs->hw.sedl.cmsb,
					cs->hw.sedl.clsb);
				debugl1(cs, tmp);
			}
			break;
	}
	restore_flags(flags);
}


void
modeisar(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int dpath = bcs->hw.isar.dpath;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "isar dp%d mode %d ichan %d",
			dpath, mode, bc);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;
	bcs->channel = bc;
}

void
isar_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
isar_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			save_flags(flags);
			cli();
			if (st->l1.bcs->tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				if (st->l1.bcs->cs->debug & L1_DEB_HSCX)
					debugl1(st->l1.bcs->cs, "DRQ set BC_FLG_BUSY");
				st->l1.bcs->hw.isar.txcnt = 0;
				restore_flags(flags);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			}
			break;
		case (PH_PULL | INDICATION):
			if (st->l1.bcs->tx_skb) {
				printk(KERN_WARNING "isar_l2l1: this shouldn't happen\n");
				break;
			}
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			if (st->l1.bcs->cs->debug & L1_DEB_HSCX)
				debugl1(st->l1.bcs->cs, "PUI set BC_FLG_BUSY");
			st->l1.bcs->tx_skb = skb;
			st->l1.bcs->hw.isar.txcnt = 0;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			break;
		case (PH_PULL | REQUEST):
			if (!st->l1.bcs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			modeisar(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			if (st->l1.bcs->cs->debug & L1_DEB_HSCX)
				debugl1(st->l1.bcs->cs, "PDAC clear BC_FLG_BUSY");
			modeisar(st->l1.bcs, 0, st->l1.bc);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_isarstate(struct BCState *bcs)
{
	modeisar(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.isar.rcvbuf) {
			kfree(bcs->hw.isar.rcvbuf);
			bcs->hw.isar.rcvbuf = NULL;
		}
		discard_queue(&bcs->rqueue);
		discard_queue(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb(bcs->tx_skb, FREE_WRITE);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			if (bcs->cs->debug & L1_DEB_HSCX)
				debugl1(bcs->cs, "closeisar clear BC_FLG_BUSY");
		}
	}
}

int
open_isarstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.isar.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for isar.rcvbuf\n");
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "openisar clear BC_FLG_BUSY");
	bcs->event = 0;
	bcs->hw.isar.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

int
setstack_isar(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_isarstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = isar_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

HISAX_INITFUNC(void 
initisar(struct IsdnCardState *cs))
{
	cs->bcs[0].BC_SetStack = setstack_isar;
	cs->bcs[1].BC_SetStack = setstack_isar;
	cs->bcs[0].BC_Close = close_isarstate;
	cs->bcs[1].BC_Close = close_isarstate;
	cs->bcs[0].hw.isar.dpath = 0;
	cs->bcs[1].hw.isar.dpath = 0;
}

