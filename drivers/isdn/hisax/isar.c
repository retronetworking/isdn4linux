/* $Id$

 * isar.c   ISAR (Siemens PSB 7110) specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log$
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "isar.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

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
	printk(KERN_DEBUG"isar sendmsg (%02x,%02x,%3d)\n", his, creg, len);
	save_flags(flags);
	cli();
	cs->BC_Write_Reg(cs, 0, ISAR_CTRL_H, creg);
	cs->BC_Write_Reg(cs, 0, ISAR_CTRL_L, len);
	cs->BC_Write_Reg(cs, 0, ISAR_WADR, 0);
	if (msg && len) {
		char tmp[1024],*tp = tmp;

		cs->BC_Write_Reg(cs, 1, ISAR_MBOX, msg[0]);
		tp += sprintf(tp," %02x",msg[0]);
		for (i=1; i<len; i++) {
			cs->BC_Write_Reg(cs, 2, ISAR_MBOX, msg[i]);
			tp += sprintf(tp," %02x",msg[i]);
		}
		printk(KERN_DEBUG"isar sendmsg%s\n", tmp);
	}
	cs->BC_Write_Reg(cs, 1, ISAR_HIS, his);
	restore_flags(flags);
	return(1);
}

int
receivemsg(struct IsdnCardState *cs, u_char *iis, u_char *creg, u_char *len,
	u_char *msg)
{
	long flags;
	int i;
	
	if (!(cs->BC_Read_Reg(cs, 0, ISAR_IRQBIT) & ISAR_IRQSTA))
		return(0);
	save_flags(flags);
	cli();
	*iis  = cs->BC_Read_Reg(cs, 1, ISAR_IIS);
	*creg = cs->BC_Read_Reg(cs, 1, ISAR_CTRL_H);
	*len  = cs->BC_Read_Reg(cs, 1, ISAR_CTRL_L);
	cs->BC_Write_Reg(cs, 1, ISAR_RADR, 0);
	if (msg && *len) {
		msg[0] = cs->BC_Read_Reg(cs, 1, ISAR_MBOX);
		for (i=1; i < *len; i++)
			 msg[i] = cs->BC_Read_Reg(cs, 2, ISAR_MBOX);
	}
	cs->BC_Write_Reg(cs, 1, ISAR_IIA, 1);
	restore_flags(flags);
	return(1);
}

int
waitrecmsg(struct IsdnCardState *cs, u_char *iis, u_char *creg, u_char *len,
	u_char *msg, int maxdelay)
{
	int timeout = 0;
	
	while((!(cs->BC_Read_Reg(cs, 0, ISAR_IRQBIT) & ISAR_IRQSTA)) &&
		(timeout++ < maxdelay))
		udelay(1);
	if (timeout >= maxdelay) {
		printk(KERN_WARNING"isar recmsg IRQSTA timeout\n");
		return(0);
	}
	return(receivemsg(cs, iis, creg, len, msg));
}

int
ISARVersion(struct IsdnCardState *cs, char *s)
{
	int ver;
	u_char msg[] = ISAR_MSG_HWVER;
	u_char tmp[64];
	u_char iis, ctrl, len;
	
	/* disable ISAR IRQ */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, 0);
	if (!sendmsg(cs, ISAR_HIS_VNR, 0, 3, msg))
		return(-1);
	if (!waitrecmsg(cs, &iis, &ctrl, &len, tmp, 100000))
		 return(-2);
	if (iis == ISAR_IIS_VNR) {
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
	int ret, size, cnt;
	u_char iis, ctrl, len, nom, noc;
	u_short sadr, left, *sp;
	u_char *p = buf;
	u_char *msg, *tmpmsg, *mp, tmp[64];
	long flags;
	
	
	struct {u_short sadr;
		u_short len;
		u_short d_key;
	} blk_head;
		
#define	BLK_HEAD_SIZE 6
	cs->cardmsg(cs, CARD_RESET,  NULL);
	if (1 != (ret = ISARVersion(cs, "Testing"))) {
		printk(KERN_ERR"isar_load_firmware wrong isar version %d\n", ret);
		return(1);
	}
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
		if (!waitrecmsg(cs, &iis, &ctrl, &len, tmp, 100000)) {
			printk(KERN_ERR"isar waitrecmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if ((iis != ISAR_IIS_DKEY) || ctrl || len) {
			printk(KERN_ERR"isar wrong dkey response (%x,%x,%x)\n",
				iis, ctrl, len);
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
			printk(KERN_DEBUG"isar: load %3d words at %04x\n",
				 noc, sadr);
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
			if (!waitrecmsg(cs, &iis, &ctrl, &len, tmp, 100000)) {
				printk(KERN_ERR"isar waitrecmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if ((iis != ISAR_IIS_FIRM) || ctrl || len) {
				printk(KERN_ERR"isar wrong prog response (%x,%x,%x)\n",
					iis, ctrl, len);
				ret = 1;goto reterror;
			}
		}
		printk(KERN_DEBUG"isar firmware block %5d words loaded\n",
			blk_head.len);
	}
	msg[0] = 0xff;
	msg[1] = 0xfe;
	if (!sendmsg(cs, ISAR_HIS_STDSP, 0, 2, msg)) {
		printk(KERN_ERR"isar sendmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if (!waitrecmsg(cs, &iis, &ctrl, &len, tmp, 100000)) {
		printk(KERN_ERR"isar waitrecmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if ((iis != ISAR_IIS_STDSP) || ctrl || len) {
		printk(KERN_ERR"isar wrong start dsp response (%x,%x,%x)\n",
			iis, ctrl, len);
		ret = 1;goto reterror;
	} else
		printk(KERN_DEBUG"isar start dsp success\n");
	/* NORMAL mode entered */
	cs->BC_Write_Reg(cs, 0, ISAR_IRQBIT, ISAR_IRQSTA);
	noc =5;
	while (noc--) {
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (1000*HZ)/1000;  /* 1 s */
	schedule();
	restore_flags(flags);
	if (!sendmsg(cs, ISAR_HIS_DIAG, ISAR_CTRL_STST, 0, NULL)) {
		printk(KERN_ERR"isar sendmsg self tst failed\n");
	//	ret = 1;goto reterror;
	}
	if (!waitrecmsg(cs, &iis, &ctrl, &len, tmp, 100000)) {
		printk(KERN_ERR"isar waitrecmsg self tst failed\n");
	//	ret = 1;goto reterror;
	}
	if ((iis == ISAR_IIS_DIAG) && (ctrl == ISAR_CTRL_STST) && (len == 1)) {
		printk(KERN_DEBUG"isar seft test result %#x\n", tmp[0]);
	} else {
		printk(KERN_ERR"isar wrong self tst response (%x,%x,%x)\n",
			iis, ctrl, len);
	//	ret = 1;goto reterror;
	}
	}
	if (!sendmsg(cs, ISAR_HIS_DIAG, ISAR_CTRL_SWVER, 0, NULL)) {
		printk(KERN_ERR"isar sendmsg self tst failed\n");
		ret = 1;goto reterror;
	}
	if (!waitrecmsg(cs, &iis, &ctrl, &len, tmp, 100000)) {
		printk(KERN_ERR"isar waitrecmsg self tst failed\n");
		ret = 1;goto reterror;
	}
	if ((iis == ISAR_IIS_DIAG) && (ctrl == ISAR_CTRL_SWVER) && (len == 1)) {
		printk(KERN_DEBUG"isar software version %d\n", tmp[0]);
	} else {
		printk(KERN_ERR"isar wrong swver response (%x,%x,%x)\n",
			iis, ctrl, len);
		ret = 1;goto reterror;
	}
	ret = 0;
reterror:
	kfree(msg);
	kfree(tmpmsg);
	return(ret);
}

void
isar_int_main(struct IsdnCardState *cs)
{
	printk(KERN_DEBUG"isar irq received\n");
}
