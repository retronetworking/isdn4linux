/* $Id$
 *
 * Header for the Linux ISDN abc-extension.
 *
 * Copyright           by abc GmbH
 *                     written by Detlef Wengorz <detlefw@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef ISDN_DWABC_H
#define ISDN_DWABC_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/errno.h>


typedef struct ISDN_DWSPINLOCK {

	spinlock_t 	spin;
	short  		owner;
	short		my_flags;
	ulong 		irq_flags;

} ISDN_DWSPINLOCK;

#define ISDN_DWSPIN_UNLOCKED				\
	(ISDN_DWSPINLOCK) {						\
		spin: 		SPIN_LOCK_UNLOCKED,		\
		owner:		-1,						\
		my_flags:	0,						\
		irq_flags:	0,						\
	}

#define ISDN_DWSPIN_INIT(x)			\
			do { *(x) = ISDN_DWSPIN_UNLOCKED; } while(0);

static __inline__ int isdn_dwspin_trylock(ISDN_DWSPINLOCK *spin)
{
	if(!spin_trylock(&spin->spin)) {

		if(spin->owner == smp_processor_id())
			return(-EAGAIN);

		spin_lock(&spin->spin);
	}

	spin->owner = smp_processor_id();
	return(0);
}

static __inline__ void isdn_dwspin_unlock(ISDN_DWSPINLOCK *spin)
{
	spin->owner = -1;
	spin_unlock(&spin->spin);
}


#else
#include <sys/types.h>
#endif

#define DWABC_LCR_FLG_NEWNUMBER		0x00000001L
#define DWABC_LCR_FLG_DISABLE		0x00000002L
#define DWABC_LCR_FLG_NEWHUPTIME	0x00000004L


struct ISDN_DWABC_LCR_IOCTL {

	int 	lcr_ioctl_sizeof;	/* mustbe sizeof(ISDN_DWABC_LCR_IOCTL)	*/
	u_short lcr_ioctl_onhtime;	/* new hanguptime						*/
	u_long 	lcr_ioctl_callid;	/* callid from lcr-subsystem			*/
	u_long 	lcr_ioctl_flags;	/* see above							*/
	char 	lcr_ioctl_nr[32];	/* new destination phonenumber			*/
};


enum iptdwisdn {
	IPT_DWISDN_NOT		= 0x80,
	IPT_DWISDN_CON		= 1,
	IPT_DWISDN_IDEV		= 2,
	IPT_DWISDN_CHARGE	= 3,
	IPT_DWISDN_OUTGOING	= 4,
	IPT_DWISDN_CBOUT	= 5,
	IPT_DWISDN_DIALMODE	= 6,
	IPT_DWISDN_ADDROK	= 7,
	IPT_DWISDN_FEQIADR	= 8,
};


enum iptdwisdn_constat {

	IPTCS_DWISN_OFFL	= 0x01,
	IPTCS_DWISN_ONL		= 0x02,
	IPTCS_DWISN_DIAL	= 0x04,
};

enum tiptdwisdn {
	TIPT_DWISDN_NOT			= 0x80,
	TIPT_DWISDN_CLEAR		= 1,
	TIPT_DWISDN_SET			= 2,
	TIPT_DWISDN_DIAL		= 3,
	TIPT_DWISDN_HANGUP		= 4,
	TIPT_DWISDN_IDEV		= 5,
	TIPT_DWISDN_DIALMODE	= 6,
	TIPT_DWISDN_UNREACH		= 7,
	TIPT_DWISDN_HUPRESET	= 8,
};


#ifdef IPT_ISDN_DWISDN_H_NEED_OPTS
#include <getopt.h>

static struct option IPT_dwisdn_opts[] = {

	{"con_stat", 	1,0,IPT_DWISDN_CON	},
	{"in_dev", 		0,0,IPT_DWISDN_IDEV	},
	{"charge",		1,0,IPT_DWISDN_CHARGE	},
	{"outgoing",	0,0,IPT_DWISDN_OUTGOING	},
	{"cbout",		0,0,IPT_DWISDN_CBOUT	},
	{"dialmode",	1,0,IPT_DWISDN_DIALMODE	},
	{"addr_ok",		0,0,IPT_DWISDN_ADDROK	},
	{"f_eq_iadr",	0,0,IPT_DWISDN_FEQIADR	},
	{0},
};
#define IPTDWISDN_ANZOPS ((sizeof(IPT_dwisdn_opts)/sizeof(*IPT_dwisdn_opts))-1)
#endif

#ifdef IPT_ISDN_DWISDN_TIPTH_NEED_OPTS
#include <getopt.h>
static struct option TIPT_dwisdn_opts[] = {
	{"clear", 			0,0,TIPT_DWISDN_CLEAR		},
	{"huptimer",	 	0,0,TIPT_DWISDN_SET			},
	{"dial",	 		0,0,TIPT_DWISDN_DIAL		},
	{"hangup",	 		0,0,TIPT_DWISDN_HANGUP		},
	{"in_dev", 			0,0,TIPT_DWISDN_IDEV		},
	{"dialmode", 		1,0,TIPT_DWISDN_DIALMODE	},
	{"unreach", 		0,0,TIPT_DWISDN_UNREACH		},
	{"hupreset", 		0,0,TIPT_DWISDN_HUPRESET	},
	{0},
};
#define IPTDWISDN_ANZOPS ((sizeof(IPT_dwisdn_opts)/sizeof(*IPT_dwisdn_opts))-1)
#endif

#define IPTDWISDN_MAXOPS 	16
#define IPTDWISDN_REVISION	(u_short)3


typedef struct IPTDWISDN_INFO {

	u_short revision;
	u_short parcount;

	u_char	inst[IPTDWISDN_MAXOPS];
	u_long	value[IPTDWISDN_MAXOPS];

} IPTDWISDN_INFO;

#endif
