/* $Id$
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log$
 * Revision 1.4  1997/01/21 22:25:27  keil
 * cleanups for 2.0
 *
 * Revision 1.3  1996/11/05 19:35:47  keil
 * using config.h
 *
 * Revision 1.2  1996/10/22 23:14:07  fritz
 * Changes for compatibility to 2.0.X and 2.1.X kernels.
 *
 * Revision 1.1  1996/10/13 20:04:56  keil
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include <linux/config.h>
#include <linux/malloc.h>
#include <linux/timer.h>


extern struct Channel *chanlist;
int     	HiSax_Installed=0;
int             drid;
extern		char            *HiSax_id;

isdn_if         iif;

#define HISAX_STATUS_BUFSIZE 4096
static byte    *HiSax_status_buf = NULL;
static byte    *HiSax_status_read = NULL;
static byte    *HiSax_status_write = NULL;
static byte    *HiSax_status_end = NULL;

int
HiSax_readstatus(byte * buf, int len, int user, int id, int channel)
{
	int             count;
	byte           *p;

	for (p = buf, count = 0; count < len; p++, count++) {
		if (user)
			put_user(*HiSax_status_read++, p);
		else
			*p++ = *HiSax_status_read++;
		if (HiSax_status_read > HiSax_status_end)
			HiSax_status_read = HiSax_status_buf;
	}
	return count;
}

void
HiSax_putstatus(char *buf)
{
	long            flags;
	int             len, count, i;
	byte           *p;
	isdn_ctrl       ic;

	save_flags(flags);
	cli();
	count = 0;
	len = strlen(buf);
	if (!HiSax_Installed) {
		printk(KERN_DEBUG "HiSax: %s", buf);
		restore_flags(flags);
		return;
	}
	for (p = buf, i = len; i > 0; i--, p++) {
		*HiSax_status_write++ = *p;
		if (HiSax_status_write > HiSax_status_end)
			HiSax_status_write = HiSax_status_buf;
		count++;
	}
	restore_flags(flags);
	if (count) {
		ic.command = ISDN_STAT_STAVAIL;
		ic.driver = drid;
		ic.arg = count;
		iif.statcallb(&ic);
	}
}


int
ll_init(void)
{
	long            flags;
	isdn_ctrl       ic;

	save_flags(flags);
	cli();
	HiSax_status_buf = Smalloc(HISAX_STATUS_BUFSIZE,
				   GFP_KERNEL, "HiSax_status_buf");
	if (!HiSax_status_buf) {
		printk(KERN_ERR "HiSax: Could not allocate status-buffer\n");
		restore_flags(flags);
		return (-EIO);
	} else {
		HiSax_status_read = HiSax_status_buf;
		HiSax_status_write = HiSax_status_buf;
		HiSax_status_end = HiSax_status_buf + HISAX_STATUS_BUFSIZE - 1;
	}
	iif.channels = CallcNewChan();
	iif.maxbufsize = BUFFER_SIZE(HSCX_SBUF_ORDER, HSCX_SBUF_BPPS);
	iif.features =
	    ISDN_FEATURE_L2_X75I |
	    ISDN_FEATURE_L2_HDLC |
	    ISDN_FEATURE_L2_TRANS |
	    ISDN_FEATURE_L3_TRANS |
#ifdef	CONFIG_HISAX_1TR6
	    ISDN_FEATURE_P_1TR6 |
#endif
#ifdef	CONFIG_HISAX_EURO
	    ISDN_FEATURE_P_EURO |
#endif
	    0;


	iif.command = HiSax_command;
	iif.writebuf = HiSax_writebuf;
	iif.writecmd = NULL;
	iif.readstat = HiSax_readstatus;
	strncpy(iif.id, HiSax_id, sizeof(iif.id) - 1);
	register_isdn(&iif);


	drid = iif.channels;
	ic.driver = drid;
	ic.command = ISDN_STAT_RUN;
	iif.statcallb(&ic);
	restore_flags(flags);
	return 0;
}

void
ll_stop(void)
{
	isdn_ctrl       ic;

	ic.command = ISDN_STAT_STOP;
	ic.driver = drid;
	iif.statcallb(&ic);

	CallcFreeChan();
}

void
ll_unload(void)
{
	isdn_ctrl       ic;

	ic.command = ISDN_STAT_UNLOAD;
	ic.driver = drid;
	iif.statcallb(&ic);
	if (HiSax_status_buf)
		Sfree(HiSax_status_buf);
	HiSax_status_read  = NULL;
	HiSax_status_write = NULL;
	HiSax_status_end   = NULL;
}
