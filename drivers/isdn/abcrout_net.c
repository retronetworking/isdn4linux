

/* $Id$

 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
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
 * Fa. abc Agentur fuer
 * Bildschirm-Communication GmbH
 * Mercedesstrasse 14
 * 71063 Sindelfingen
 * Germany
 *
 * $Log$
 * Revision 1.1.2.7  1998/04/27 12:01:06  detabc
 * *** empty log message ***
 *
 * Revision 1.1.2.6  1998/04/26 19:53:24  detabc
 * remove unused code
 *
 * Revision 1.1.2.5  1998/04/26 11:26:47  detabc
 * add abc_tx_queues support.
 * remove some now unused code.
 *
 * Revision 1.1.2.4  1998/04/21 17:56:34  detabc
 * added code to reset secure-counter with a spezial udp-packets
 *
 * Revision 1.1.2.3  1998/03/20 12:25:50  detabc
 * Insert the timru recalc_timeout function in the abc_test_receive() function.
 * The timru function will be called after decrypt and decompress is done.
 * Note! Received pakets will be in same cases queued.
 *
 * Revision 1.1.2.2  1998/03/08 11:35:06  detabc
 * Add cvs header-controls an remove unused funktions
 *
 */

#include <linux/config.h>
#ifdef CONFIG_ISDN_WITH_ABC
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include <linux/udp.h>
#include <linux/icmp.h>

#include "isdn_common.h"
#include "isdn_net.h"

char *abcrout_net_revision = "$Revision$";


static struct INCALL_NUMTEST {

	struct INCALL_NUMTEST *icnt_prev;
	struct INCALL_NUMTEST *icnt_next;
	u_long icnt_ictime;
	u_long icnt_count;
	u_long icnt_disabled;
	u_char icnt_number[ISDN_MSNLEN];

} *icnt_first = NULL,*icnt_last = NULL, *icnt_free = NULL;

static void *icnt_mmm[4];
#define NUM_OF_MMM (sizeof(icnt_mmm) / sizeof(void *))

/*
** hash soll das letzte byte der ip-nummer sein
** daher bei intel + 3
*/

#define MIN_JIFFIES_FOR_KEEP_RESPONDS	(((u_long)50 * HZ))

/*
** 0 == ohne tcp keepalive-responds  != 0 mit tcp-responds
*/

#define MAX_TCP_MERK 128

struct TCP_CONMERK	{

	struct	TCP_CONMERK	*tm_sprev;	/* dliste seriell	previos		*/
	struct	TCP_CONMERK	*tm_snext;	/* dliste seriell	next		*/

	struct	TCP_CONMERK	*tm_hprev;	/* dliste hash	previos			*/
	struct	TCP_CONMERK	*tm_hnext;	/* dliste hash	next			*/

	u_long 	tm_srcadr;			/*	source ipadr						*/
	u_long 	tm_dstadr;			/*	destination ipadr					*/
	u_long 	tm_seqnr;			/*	tcp sequenz-nummer (IN HOSTBYTEORDER)*/
	u_long 	tm_acknr;			/*	tcp acknowled-nummer				*/
	u_long	tm_time;			/*	timerclick							*/
	u_short	tm_srcport;			/*	source portnumemr					*/
	u_short	tm_dstport;			/*	destination portnummer				*/
	u_short	tm_window;			
	u_short tm_hashnr;

};

#define HASH_MAX 256
#define HASH_MASK 0xFF
#define HASH_NR(nr)		(*(((u_char *)(&nr)+3)) & HASH_MASK)

static u_long tcp_merk_disable_to = 0;

static struct TCP_CONMERK *tcp_c_hash[HASH_MAX];
static struct TCP_CONMERK *tcp_c_first = NULL;
static struct TCP_CONMERK *tcp_c_last = NULL;
static struct TCP_CONMERK *tcp_c_free = NULL;
static u_short tcp_anzalloc = 0;
static u_long tcp_next_police = 0;
static u_long tcp_last_police = 0;

static void *tcp_mem_merk[8];

static volatile u_char tcp_inuse = 0;
struct sk_buff_head abc_receive_q;



#define ABC_ROUT_FIRSTMAGIC 27019413L
#define ABC_ROUT_VERSION 47

#define ABCR_NORM 0
#define ABCR_FIRST_REQ	0x1001
#define ABCR_FIRST_REP	0x1002
#define ABCR_KEEPALIVE	0x1004
#define ABCR_ISDATA		0x2000
#define ABCR_BYTECOMP	0x4000
#define ABCR_TCPKEEP	0x8000
#define ABCR_MAXBYTES	0x07FF


#define ABCR_ALLE 	(ABCR_FIRST_REQ|ABCR_ISDATA|ABCR_FIRST_REQ|ABCR_KEEPALIVE)

struct ABCR_FIRST_DATA {

	ulong fd_magic;
	ulong fd_version;
	ulong fd_flags;
	ushort fd_compnr;
	ushort fd_timeout;
	ulong fd_reserv[10];
} ;


static u_short abc_checksum(void *wrd,int anz);
static char *abc_h_ipnr(u_long ipadr);



static struct INCALL_NUMTEST *abc_icnt_find(u_char *number)
/*
** needed for incomig call to tty's
** reason: to many call's in a short time must be a mistake !
** wrong config in one or both sides.
** 
** I dont want pay for this reason !
*/
{
	struct INCALL_NUMTEST *r = icnt_first;
	long cnt = 0;

	for(;r != NULL && cnt < 10000;cnt++,r = r->icnt_next) {

		u_char *s = number;
		u_char *d = r->icnt_number;
		u_char *e = number + ISDN_MSNLEN;

		for(;s < e && *s != 0 && *s == *d;s++,d++);

		if(s >= e || *s == *d)
			break;
	}

	if(cnt >= 10000) {

		r = 0;
		printk(KERN_DEBUG "abc_icnt_find: impossible chain-count\n");
	}

	return(r);
}

static void abc_icnt_putnew(struct INCALL_NUMTEST *nt)
{
	if((nt->icnt_next = icnt_first) != NULL)
		icnt_first->icnt_prev = nt;
	else
		icnt_last = nt;

	icnt_first = nt;
	nt->icnt_prev = NULL;
}

static void abc_icnt_clear(struct INCALL_NUMTEST *nt)
{

	if(nt->icnt_prev != NULL)
		nt->icnt_prev->icnt_next = nt->icnt_next;
	else
		icnt_first = nt->icnt_next;

	if(nt->icnt_next != NULL)
		nt->icnt_next->icnt_prev = nt->icnt_prev;
	else
		icnt_last = nt->icnt_prev;

	memset((void *)nt,0,sizeof(*nt));
	nt->icnt_prev = nt->icnt_next = NULL;
}
		
static void abc_icnt_putfirst(struct INCALL_NUMTEST *nt)
{
	if(nt->icnt_prev != NULL) {

		abc_icnt_clear(nt);
		abc_icnt_putnew(nt);
	}
}

static void abc_icnt_free(struct INCALL_NUMTEST *nt)
{
	abc_icnt_clear(nt);
	nt->icnt_next = icnt_free;
	icnt_free = nt;
	nt->icnt_prev = NULL;
}

static struct INCALL_NUMTEST *abc_icnt_getnew(void)
{
	struct INCALL_NUMTEST *r;
	static long save_counter = 0;

	if((r = icnt_last) != NULL) {

		if(r->icnt_disabled < jiffies && 
			((jiffies - r->icnt_ictime) / HZ) > 120) {
			
			abc_icnt_clear(r);
			return(r);
		} 
	}

	do {

		int shl = 0;
		int cnt = NUM_OF_MMM;

		if((r = icnt_free) != NULL) {

			icnt_free = r->icnt_next;
			save_counter = 0;
			r->icnt_prev = r->icnt_next = NULL;
			memset((void *)r,0,sizeof(*r));
			return(r);
		}

		for(shl = 0; shl < cnt && icnt_mmm[shl] != NULL;shl++);

		if(shl < cnt) {

			r = 
				(struct INCALL_NUMTEST *)icnt_mmm[shl] = 
				kmalloc(4096,GFP_ATOMIC);

			if(r != NULL) {

				struct INCALL_NUMTEST *er = r + (4096 / sizeof(*r));

				for(;r < er;r++) {

					r->icnt_next = icnt_free;
					icnt_free = r;
				}

			} else {

				printk(KERN_DEBUG
					"abc_icnt_getnew: kmalloc failt for 4096 bytes\n");
			}

		} else {

			printk(KERN_DEBUG
				"abc_icnt_getnew: max availible mem in use\n");
		}

	} while(icnt_free != NULL && save_counter++ < 1000);

	printk(KERN_DEBUG "abc_icnt_getnew: cannot get new entry\n");

	return(NULL);
}
	
int abc_test_incall(u_char *number)
{
	u_char *ep = number + ISDN_MSNLEN;
	u_char *p = number;
	struct INCALL_NUMTEST *nt = NULL;
	u_long flags = 0;
	int retw = 0;

	if(number == NULL)
		return(0);

	for(;p < ep && *p != 0 && (*p <= '0' || *p > '9');p++);

	if(p >= ep || *p == 0)
		return(0);

	if(dev->net_verbose > 5) 
			printk(KERN_DEBUG "abc_test_incall: <%s>\n",number);

	save_flags(flags);
	cli();

	if((nt = abc_icnt_find(number)) != NULL) {

		if(dev->net_verbose > 5)  {

			printk(KERN_DEBUG
				"abc_test_incall: <%s> found retrys %lu\n",
				number,nt->icnt_count);
		}

		if(jiffies < nt->icnt_disabled) {

			u_long dsek = (nt->icnt_disabled - jiffies) / HZ;

			retw = -1;

			printk(KERN_DEBUG
				"abc_test_incall: <%s> disabled %lu sek left\n",
				number,dsek);

		} else if((jiffies - nt->icnt_ictime ) / HZ > 120) {

			abc_icnt_free(nt);

		} else {

			nt->icnt_ictime = jiffies;
			abc_icnt_putfirst(nt);
		}

	} else if(dev->net_verbose > 5)  {

			printk(KERN_DEBUG "abc_test_incall: <%s> not found\n",number);
	}

	restore_flags(flags);
	return(retw);
}


void abc_insert_incall(u_char *number)
{

	u_char *ep = number + ISDN_MSNLEN;
	u_char *p = number;
	struct INCALL_NUMTEST *nt = NULL;
	u_long njiff = jiffies;
	u_long l = 0;
	u_long flags = 0;

	if(number == NULL)
		return;

	if(dev->net_verbose > 5) 
		printk(KERN_DEBUG
			"abc_insert_incall: search for <%s>\n",number);

	for(;p < ep && *p != 0 && (*p <= '0' || *p > '9');p++);

	if(p >= ep || *p == 0)
		return;

	save_flags(flags);
	cli();

	if((nt = abc_icnt_find(number)) == NULL) {

		if(dev->net_verbose > 5) 
			printk(KERN_DEBUG
				"abc_insert_incall: want to insert <%s>\n",number);

		if((nt = abc_icnt_getnew()) != NULL) {

			u_char *s = number;
			u_char *d = nt->icnt_number;
			u_char *ed = nt->icnt_number  + ISDN_MSNLEN;

			if(dev->net_verbose > 5) 
				printk(KERN_DEBUG "abc_insert_incall: insert <%s>\n",number);

			while(d < ed && *s != 0)
				*(d++) = *(s++);

			if(d < ed)
				*d = 0;

			nt->icnt_ictime = jiffies;
			nt->icnt_count = 0;
			abc_icnt_putnew(nt);
		}

	} else {

		l = (njiff - nt->icnt_ictime) / HZ;

		if(l < 20) {
		
			nt->icnt_count++;

			if(nt->icnt_count > 2) {

				nt->icnt_disabled = 
					jiffies + HZ * 
					nt->icnt_count * 
					nt->icnt_count * 15;


				if(nt->icnt_count > 30)
					nt->icnt_count = 30;
			}

		} else if(l > 60) {

			nt->icnt_count = 0;
		}

		abc_icnt_putfirst(nt);
	}

	restore_flags(flags);
	return;
}

	


void abc_pack_statistik(isdn_net_local *lp)
{
	if(dev->net_verbose > 1) {

		u_long lw = lp->abc_snd_want_bytes;
		u_long lr = lp->abc_snd_real_bytes;

		if(lw && lr) {

			double d;
			long e = 0;

			if(lr < lw) {

				d = lw - lr;
				e = (d * 100.0 / lw) * 100;
			}

			printk(KERN_DEBUG "abc_snd_statistik %lu->%lu %ld.%ld %c\n",
				lw,lr,e/100,e%100,'%');
		}

		lw = lp->abc_rcv_want_bytes;
		lr = lp->abc_rcv_real_bytes;

		if(lw && lr) {

			double d;
			long e = 0;

			if(lr < lw) {

				d = lw - lr;
				e = (d * 100.0 / lw) * 100;
			}

			printk(KERN_DEBUG "abc_rcv_statistik %lu->%lu %ld.%ld %c\n",
				lw,lr,e/100,e%100,'%');
		}
	}

	lp->abc_snd_want_bytes = 
	lp->abc_snd_real_bytes = 
	lp->abc_rcv_want_bytes = 
	lp->abc_rcv_real_bytes = 0;
}


int abc_first_senden(struct device *ndev,isdn_net_local *lp)
{
	int retw = 0;
	int len = 0;
	struct sk_buff *skb = NULL;
	struct ABCR_FIRST_DATA fd;
	u_char *p;


	len = sizeof(struct ABCR_FIRST_DATA) + 2;
	skb = dev_alloc_skb(len + dev->abc_max_hdrlen);

	if(skb == NULL) {
					
		printk(KERN_DEBUG "abc_send_first no space for new skb\n");
		return(-1);
	}
	skb_reserve(skb,dev->abc_max_hdrlen);
	skb->pkt_type = PACKET_HOST;
	p = skb_put(skb,len);
	SET_SKB_FREE(skb);

	*(p++) = ABCR_FIRST_REQ & 0xFF;
	*(p++) = (ABCR_FIRST_REQ >> 8) & 0xFF;
	
	memset((void *)&fd,0,sizeof(struct ABCR_FIRST_DATA));
	fd.fd_magic = ABC_ROUT_FIRSTMAGIC;
	fd.fd_version =  ABC_ROUT_VERSION ;
	fd.fd_flags =   ABCR_ALLE ;
	memcpy(p,(void *)&fd,sizeof(struct ABCR_FIRST_DATA));
	lp->abc_snd_want_bytes += sizeof(struct ABCR_FIRST_DATA);
	abc_put_tx_que(lp,0,0,skb);

	if(dev->net_verbose > 1)
		printk(KERN_DEBUG " abc_firstdata_send retw %d len %d\n",
			retw,len);
	
	return(retw);
}


void abc_test_phone(isdn_net_local *lp)
{
	if(lp != NULL) {

		isdn_net_phone *ph = lp->phone[0];
		*lp->abc_rx_key = 0;
		*lp->abc_out_msn = 0;

		for(; ph != NULL ; ph = ph->next) {

			char *p = ph->num;
			char *ep = ph->num + ISDN_MSNLEN - 1;
			u_char *dp = lp->abc_rx_key;

			if(p == NULL)
				continue;

			if(*p == '>') {

				/*
				** for second MSN 
				** use isdnctrl addphone isdnx in ">4711"
				** Note: > must be escaped to shell
				** this MSN will be use for outgoing call's only
				*/

				dp = (u_char *)lp->abc_out_msn;
				p++;

			} else {

				/*
				** needed for crypted connection
				** use isdnctrl addphone in isdnx -keyword 
				** for old cryptmode (slowly)
				**
				** or
				**
				** use isdnctrl addphone in isdnx _keyword 
				** new crypt (quick)
				**
				** Note: both ends need the same key
				*/

				if(*p != '-' && *p != '_' )
					continue;
			}

			while(*p && p < ep)
				*(dp++) = *(u_char *)(p++);

			*dp = 0;
		}
	}
}


void abc_simple_crypt(u_char *poin,int len,u_char *key)
/*
** slowly crypt-funktion
** will be not supportet in future
*/
{
	short art = 0;
	int max_bits;
	u_char rt[128];
	
	if( !*key || poin == NULL || len < 1)
		return;

	max_bits = len * 8;
	
	for(; *key ; key++, art = !art) {

		u_char k = *key - 'A';

		k %= sizeof(rt);

		if(!art) {

			int nb = 0; 

			k %= (max_bits >> 4);
			k++;

			for(;nb < max_bits;nb += k) {

				u_char bit = 1 << (nb & 7);
				u_char *np = poin + (nb >> 3);

				if(*np & bit)
					*np &= ~bit;
				else
					*np |= bit;
			}

		} else {

			u_char *p = poin;
			u_char *ep = poin + len;
			int l;


			k %= len >> 2;
			k++;

			l = len - k;

			for(;p < ep ; p += k)
				*p = ~*p;

			{
				p = poin;
				ep = poin + len -1;

				for(;p < ep;p += k,ep -= k) {

					u_char x = *p;
					*p = *ep;
					*ep = x;
				}
			}

			memcpy(rt,poin,k);
			memcpy(poin,poin + k, l);
			memcpy(poin+l,rt,k);
		}
	}
}


void abc_simple_decrypt(u_char *poin,int len,u_char *key)
/*
** slowly de-crypt-funktion
** will be not supportet in future
*/
{
	short art = 0;
	u_char *start_key = key;
	int max_bits;
	u_char rt[128];
	
	if( !*key || poin == NULL || len < 1)
		return;

	max_bits = len * 8;

	for(; *key; key++);
	key--;
	art = (key - start_key) & 1;
	
	for(; key >= start_key ; key--, art = !art) {

		u_char k = *key - 'A';

		k %= sizeof(rt);

		if(!art) {

			int nb = 0; 

			k %= (max_bits >> 4);
			k++;

			for(;nb < max_bits;nb += k) {

				u_char bit = 1 << (nb & 7);
				u_char *np = poin + (nb >> 3);

				if(*np & bit)
					*np &= ~bit;
				else
					*np |= bit;
			}

		} else {

			u_char *dp,*sp;
			int l;

			k %= len >> 2;
			k++;
			l = len - k;
			memcpy(rt,poin + l,k);

			dp = poin + len;
			sp = poin + l;
			dp--;
			sp--; 

			while(sp >= poin)
				*(dp--) = *(sp--);

			memcpy(poin,rt,k);
			
			{
				u_char *p = poin;
				u_char *ep = poin + len -1;

				for(;p < ep;p += k,ep -= k) {

					u_char x = *p;
					*p = *ep;
					*ep = x;
				}
			}

			dp = poin;
			sp = poin + len;

			for(; dp < sp; dp += k)
				*dp = ~*dp;
		}
	}
}


static void  dwtblcrypt(int decrypt,u_char *buf,int bytes,u_char *key)
/******************************************************************
new quick crypt- decrypt funktion. I hope that will be OK
*******************************************************************/
{

	u_char ta[64];
	int shl = 0;
	ulong nr = 0;
	u_char *de = buf + bytes;
	u_char *da = NULL;
	u_char *dp = NULL;
	u_char *kp = key;
	u_char count = 0;

	if(buf == NULL || bytes < 1 || key == NULL || !*key)
		return;

	for(shl = 0; shl < sizeof(ta); shl++,kp++) {

		if(!*kp)
			kp = key;

		ta[shl] = *kp;
		nr += *kp;
	}

	dp = da = buf + (nr % bytes);
	count = (nr & 0xF) + 1;

	for(nr = 0; nr < count;nr++) {

		u_char *d = ta;
		u_char *s = ta + 1;

		for(shl = 1; shl < sizeof(ta);shl++) 
			*(d++) ^= *s++;

		*d ^= *ta;
	}


	for(shl = count = 0 ; shl < 2; shl++) {

		if(shl) {

			dp = buf;
			de = da;
		}

		if(decrypt) {

			for(;dp < de;dp++,kp++) {
				
				u_char z = *dp;

				if(!*kp)
					kp = key;

				*dp ^= ta[count];
				ta[count++] = z ^ *kp;
				count %= sizeof(ta);
			}

		} else {

			for(;dp < de;dp++,kp++) {

				if(!*kp)
					kp = key;

				*dp ^= ta[count];
				ta[count++] = *dp ^ *kp;
				count %= sizeof(ta);
			}
		}
	}
}


struct sk_buff *abc_get_keep_skb()
{

	struct sk_buff *skb;
	u_char *buf;

	skb = dev_alloc_skb(2 + dev->abc_max_hdrlen);

	if(skb == NULL) {

		printk(KERN_DEBUG
			"abc_keep_senden no space for new skb\n");

		return NULL;
	}
	
	skb_reserve(skb,dev->abc_max_hdrlen);
	buf = skb_put(skb,2);
	skb->pkt_type = PACKET_HOST;
	SET_SKB_FREE(skb);
	buf[0] =  ABCR_KEEPALIVE & 0xFF;
	buf[1] =  (ABCR_KEEPALIVE >> 8) & 0xFF;

	return(skb);
}
 


int abc_keep_senden(struct device *ndev,isdn_net_local *lp)
{
	int retw = 0;
	int len = 2;

	struct sk_buff *skb;

	if( (skb = abc_get_keep_skb()) == NULL) {

		printk(KERN_DEBUG
			"abc_keep_senden no space for new skb\n");

		return -1;
	}
	abc_put_tx_que(lp,0,0,skb);

	if(dev->net_verbose > 5)
		printk(KERN_DEBUG " %s abc_keepal_send retw %d len %d\n",
			lp->name,retw,len);
	
	return(retw);
}


struct sk_buff *abc_snd_data(struct device *ndev,struct sk_buff *skb)
{
	u_short k = ABCR_ISDATA | (skb->len & ABCR_MAXBYTES);
	u_char *p = NULL;
	int nlen;
	struct sk_buff *ns = NULL;
	isdn_net_local *lp = NULL;

	if(ndev != NULL)
		lp = (isdn_net_local *) ndev->priv;

	if((ns = dev_alloc_skb(skb->len + dev->abc_max_hdrlen)) == NULL) {

		if(dev->net_verbose > 1)
			printk(KERN_DEBUG
				"abc_snd: packbytes no space for pack \n");

		return(NULL);

	} else {

		skb_reserve(ns,dev->abc_max_hdrlen);
		SET_SKB_FREE(ns);
	}

	if(	skb->len > 10 &&
		(nlen = abcgmbh_pack((u_char *)skb->data,ns->data,skb->len)) > 0 &&
		nlen < skb->len) {

		if(dev->net_verbose > 9)
			printk(KERN_DEBUG
			"abc_snd: packbytes %ld -> %d\n",skb->len,nlen);

		p = skb_put(ns,nlen);
		k |= ABCR_BYTECOMP;
		
	} else {

		p = skb_put(ns,skb->len);
		memcpy((void *)p,(void *)skb->data,skb->len);

		if(dev->net_verbose > 9)
			printk(KERN_DEBUG
				"abc_snd: no pack %ld \n",skb->len);
	}

	p = skb_push(ns,2);
	*(p++) = k & 0xFF;
	*(p++) = (k >> 8) & 0xFF;

	if(lp != NULL && *lp->abc_rx_key != 0 && ns->len > 2) {

		if(*lp->abc_rx_key == '-')
			abc_simple_crypt(ns->data + 2, ns->len - 2,lp->abc_rx_key+1);
		else
			dwtblcrypt(0,(u_char *)(ns->data + 2),ns->len - 2,lp->abc_rx_key+1);
	}

	return(ns);
}



int abc_test_rcvq(struct device *ndev)
{

	struct sk_buff *skb;
	int a;

	if((a = skb_queue_len(&abc_receive_q)) < 1)
		return(0);

	for(a++;a > 0 && (skb = skb_dequeue(&abc_receive_q)) != NULL;a--) {

		if(ndev == skb->dev) {

			SET_SKB_FREE(skb);
			skb->protocol = 0;
			skb->dev = NULL;
			kfree_skb(skb,FREE_READ);

		} else abc_test_receive(NULL,skb);
	}

	return(1);
}
		


void abc_free_receive(void)
{

	struct sk_buff *skb;
	u_long a;

	if((a = skb_queue_len(&abc_receive_q)) < 1)
		return;

	a += 10000;

	for(a++;a > 0 && (skb = skb_dequeue(&abc_receive_q)) != NULL;a--) {

		SET_SKB_FREE(skb);
		skb->protocol = 0;
		skb->dev = NULL;
		kfree_skb(skb,FREE_READ);
	}
}


struct sk_buff *abc_test_receive(struct device *ndev, struct sk_buff *skb)
{
	isdn_net_local *lp = NULL;

	u_short k = 0;
	int nlen;
	u_char *p;
	int len;
	struct sk_buff *nskb = NULL;
	int is_tcp_keep = 0;
	u_long orig_len = 0;

	if(skb == NULL)
		return(skb);

	if(ndev != NULL) 
		lp = (isdn_net_local *) ndev->priv;

	p = skb->data;

	if((len = skb->len) < 2) {

		/*
		** endof transmission 
		*/

		if(dev->net_verbose > 1) {

			printk(KERN_DEBUG 
				"%s: received eot\n", lp->name);
		}

		SET_SKB_FREE(skb);
		skb->protocol = 0;
		kfree_skb(skb,FREE_READ);
		return(NULL);
	}


	k = *(p++) & 0xFF;
	k |= (*(p++) & 0xFF) << 8;
	len -= 2;
	orig_len = (u_long)len;

	if(k == ABCR_KEEPALIVE) {

		if(dev->net_verbose > 5)
			printk(KERN_DEBUG " abc_keepal_received len %d\n",len);

		if(lp != NULL)
			lp->abc_life_to = ABC_DST_LIFETIME;

		SET_SKB_FREE(skb);
		skb->protocol = 0;
		kfree_skb(skb,FREE_READ);
		return(NULL);
	}

	nlen = k & ABCR_MAXBYTES;

	if(k == ABCR_FIRST_REQ || k == ABCR_FIRST_REP) {

		struct ABCR_FIRST_DATA *fd = (struct ABCR_FIRST_DATA *) p; 

		if(len != sizeof(struct ABCR_FIRST_DATA)) {

			printk(KERN_DEBUG 
				"%s: abc_first_data len %d wrong want %d\n",
				(lp != NULL) ? lp->name : "unknown",
				len,
				sizeof(struct ABCR_FIRST_DATA));

			SET_SKB_FREE(skb);
			skb->protocol = 0;
			kfree_skb(skb,FREE_READ);
			return(NULL);
		}

		if(lp != NULL) {

			lp->abc_life_to = ABC_DST_LIFETIME;
			lp->abc_anz_wrong_data_prot = 0;
			lp->abc_call_disabled = 0;

			if(dev->net_verbose > 1) {

				printk(KERN_DEBUG 
"%s: abc_first_data received router version %ld timeout %hu/%lu\n",
						lp->name,
						fd->fd_version,
						fd->fd_timeout,
						fd->fd_reserv[0]);
			}
		}

		SET_SKB_FREE(skb);
		skb->protocol = 0;
		kfree_skb(skb,FREE_READ);
		return(NULL);
	}

	if(lp != NULL) {

		if(lp->abc_anz_wrong_data_prot ) {

			if(!(lp->abc_flags & ABC_WRONG_DSP)) {

				printk(KERN_DEBUG 
"%s: reading datablock before dataprot-first-block or firstreply k 0x%x l %d\n",
					lp->name,
					k,len);

				lp->abc_flags |= ABC_WRONG_DSP;
			}

/******************************
			SET_SKB_FREE(skb);
			skb->protocol = 0;
			kfree_skb(skb,FREE_READ);
			return(NULL);

			
********************************/
		}
	}

	if(!(k & ABCR_ISDATA) || len < 1 || nlen < 1) {

		if(!k && len == -2) {

			/*
			**  end-of-connection
			*/

			if(lp != NULL)
				lp->abc_rem_disconnect = jiffies + HZ * 5;

		} else {

			if(dev->net_verbose > 1)
				printk(KERN_DEBUG
			" abc__received destroy packet len %d nlen %d isdata %d k=0x%8x\n",
					len,nlen,!!(k & ABCR_ISDATA),k);
		}

		SET_SKB_FREE(skb);
		skb->protocol = 0;
		skb->dev = NULL;
		kfree_skb(skb,FREE_READ);
		return(NULL);
	}

	is_tcp_keep = !!(k & ABCR_TCPKEEP);

	if(!is_tcp_keep && lp != NULL)
		lp->abc_last_traffic = jiffies;

	if(lp != NULL && *lp->abc_rx_key != 0 && len > 0) {

		if(*lp->abc_rx_key == '-')
			abc_simple_decrypt(p,len,lp->abc_rx_key+1);
		else
			dwtblcrypt(1,(u_char *)p,len,lp->abc_rx_key+1);
	}

	if(k & ABCR_BYTECOMP) {

		if(dev->net_verbose > 9)
			printk(KERN_DEBUG " abc__received call depack len %d nlen %d\n",
				len,nlen);

		if(len <= 1550) {

			if((nskb = dev_alloc_skb(nlen + dev->abc_max_hdrlen)) != NULL) {

				int r;

				skb_reserve(nskb,dev->abc_max_hdrlen);
				r = abcgmbh_depack(p,len,skb_put(nskb,nlen),nlen);

				if(r < 0) {

					if(dev->net_verbose > 1) {

						printk(KERN_DEBUG
							"abc_test_receivce  depack-mem in use\n");
					}

					skb_queue_tail(&abc_receive_q,skb);
					SET_SKB_FREE(nskb);
					kfree_skb(nskb,FREE_READ);

					if(is_tcp_keep)
						return(NULL);

					return(skb);

				} else if(!r) {

					if(dev->net_verbose > 1) {

						printk(KERN_DEBUG
							"abc_test_receivce  depack-error\n");
					}

					SET_SKB_FREE(nskb);
					kfree_skb(nskb,FREE_READ);
					SET_SKB_FREE(skb);
					kfree_skb(skb,FREE_READ);
					return(NULL);
				}

				nskb->dev = skb->dev;
				nskb->protocol = skb->protocol;
				nskb->pkt_type = skb->pkt_type;
				nskb->mac.raw = nskb->data;

			} else {

				printk(KERN_DEBUG
					"abc_test_receivce no space for new skb\n");
			}
		}

		SET_SKB_FREE(skb);
		skb->dev = NULL;
		skb->protocol = 0;
		skb->dev = NULL;
		kfree_skb(skb,FREE_READ);

		if((skb = nskb) != NULL) {

			if(dev->net_verbose > 0 && lp != NULL && !lp->abc_first_disp) {

				isdn_net_log_packet(skb->data,lp);
				lp->abc_first_disp = 1;
			}
#ifdef CONFIG_ISDN_TIMEOUT_RULES
	isdn_net_recalc_timeout(ISDN_TIMRU_KEEPUP_IN,
		ISDN_TIMRU_PACKET_SKB, ndev, skb, 0);
#endif
			netif_rx(skb);
		}

	} else {

		skb_pull(skb,2);
		skb->mac.raw = skb->data;

		if(dev->net_verbose > 9)
			printk(KERN_DEBUG " abc__received not call depack len %d nlen %d\n",
				len,nlen);

		if(dev->net_verbose > 0 && lp != NULL && !lp->abc_first_disp) {

			isdn_net_log_packet(skb->data,lp);
			lp->abc_first_disp = 1;
		}

#ifdef CONFIG_ISDN_TIMEOUT_RULES
	isdn_net_recalc_timeout(ISDN_TIMRU_KEEPUP_IN,
		ISDN_TIMRU_PACKET_SKB, ndev, skb, 0);
#endif
		netif_rx(skb);
	}
		
	if(is_tcp_keep)
		return(NULL);

	if(lp != NULL) {

		int i = isdn_dc2minor( lp->isdn_device,lp->isdn_channel);

		if(i > -1)
			dev->ibytes[i] += skb->len - orig_len;

		lp->abc_rcv_want_bytes += (u_long)skb->len;
		lp->abc_rcv_real_bytes += orig_len;
	}

	return(skb);
}


static char *abc_h_ipnr(u_long ipadr)
{
	static char buf[8][16];
	static u_char bufp = 0;
	char *p = buf[bufp = (bufp + 1) & 7];

	u_char *up = (u_char *)&ipadr;

	sprintf(p,"%d.%d.%d.%d",up[0],up[1],up[2],up[3]);
	return(p);
}



static u_short abc_checksum(void *wrd,int anz)

/************************************************************************

	pruefen der ip_header-checksumme

	return == 0	== checksumme ok
	return != 0 == checksumme falsch

*************************************************************************/

{

	u_long cksum = 0;
	u_short n_cksum;
	register u_short *cword = (ushort *)wrd;
	register u_short *ecword;

	ecword = ((ushort *)cword) + anz;

	while(cword < ecword) 
		cksum += *(cword++);

	cksum = (cksum >> 16) + (cksum & 0xFFFF);
	cksum += (cksum >> 16);
	n_cksum = ~cksum & 0xFFFF;

	return(n_cksum);
}



static __inline void free_used_tm(struct TCP_CONMERK *fb)
/*****************************************************************************
	gibt einen block wieder frei
******************************************************************************/

{
	if(fb != NULL) {

		if(fb->tm_sprev != NULL)
			fb->tm_sprev->tm_snext = fb->tm_snext;
		else
			tcp_c_first = fb->tm_snext;
		
		if(fb->tm_snext != NULL)
			fb->tm_snext->tm_sprev = fb->tm_sprev;
		else
			tcp_c_last = fb->tm_sprev;

		if(fb->tm_hprev != NULL)
			fb->tm_hprev->tm_hnext = fb->tm_hnext;
		else
			tcp_c_hash[fb->tm_hashnr] = fb->tm_hnext;

		if(fb->tm_hnext != NULL)
			fb->tm_hnext->tm_hprev = fb->tm_hprev;

		if((fb->tm_snext = tcp_c_free) != NULL)
			fb->tm_snext->tm_sprev = fb;
		
		tcp_c_free = fb;
		fb->tm_sprev = NULL;
	}
}
			


static void hole_new_bloecke(void )
{
	
	struct TCP_CONMERK *nb ;
	struct TCP_CONMERK *enb ;
	int gute_anz = 4096 / sizeof(struct TCP_CONMERK);
	int shl;
	int max_shl = sizeof(tcp_mem_merk) / sizeof(void *);

	if(tcp_anzalloc >= MAX_TCP_MERK) 
		return;

	for(shl = 0; shl < max_shl && tcp_mem_merk[shl] != NULL;shl++);

	if(shl >= max_shl)
		return;
	
	nb = (struct TCP_CONMERK *)kmalloc(4096,GFP_ATOMIC);

	if(nb == NULL) {

		printk(KERN_DEBUG "abc_hole_new_bloecke no mem\n");
		return;

	} else printk(KERN_DEBUG "abc_hole_new_bloecke get mem for 4096 bytes\n");

	memset((void *)nb , 0, 4096);
	enb = nb + gute_anz;

	tcp_mem_merk[shl] = (void *)nb;

	for(;nb < enb;nb++) {

		nb->tm_snext = tcp_c_free;
		tcp_c_free = nb;
	}
}
		
int abc_clean_up_memory(void )
{
	int shl;
	int max_shl;
	ulong flags;

	if(set_bit(0,(void *)&tcp_inuse) != 0) {

		/*
		** listen werden bereits verwendet
		*/

		printk(KERN_DEBUG "abc-tcp-test dbl's in use\n");
		return(0);
	}

	save_flags(flags);
	cli();

	for(shl = 0; shl < HASH_MAX;shl++)
		tcp_c_hash[shl] = NULL;

	tcp_c_first = NULL;
	tcp_c_last = NULL;
	tcp_c_free = NULL;

	tcp_anzalloc = 0;
	tcp_next_police = 0;
	tcp_last_police = 0;

	max_shl = sizeof(tcp_mem_merk) / sizeof(void *);

	for(shl = 0; shl < max_shl; shl++) {

		if(tcp_mem_merk[shl] != NULL)
			kfree(tcp_mem_merk[shl]);

		tcp_mem_merk[shl] = NULL;
	}

	tcp_inuse = 0;

	for(shl = 0; shl < NUM_OF_MMM; shl++) {

		if(icnt_mmm[shl] != NULL)
			kfree(icnt_mmm[shl]);

		icnt_mmm[shl] = NULL;
	}

	icnt_first = icnt_last = icnt_free = NULL;

	restore_flags(flags);

	return(0);
}



static __inline struct TCP_CONMERK *get_free_tm(u_long ipnr)
/*********************************************************

	liefert einen freien tcp_merk_block
	alle verknuepfungen sind bereits erledigt

	gegebnenfall die routine mit cli() und sti() dichtmachen

	return pointer 
	rerturn == NULL == kein memory 

**********************************************************/

{
	struct TCP_CONMERK *nb;

	if( (nb = tcp_c_free ) == NULL) {

		if(tcp_anzalloc < MAX_TCP_MERK) 
			hole_new_bloecke();

		if((nb = tcp_c_free) == NULL) {

			if(( nb = tcp_c_last) == NULL)  {

				printk(KERN_DEBUG
					"abc-get_free_tm nop tcp_c_last %d/%d\n",
						tcp_anzalloc,
						MAX_TCP_MERK);

				return(NULL);
			}

			free_used_tm(nb);

			if(( nb = tcp_c_last) == NULL) {

				printk(KERN_DEBUG
					"abc-get_free_tm no tcp_c_last %d/%d\n",
						tcp_anzalloc,
						MAX_TCP_MERK);

				return(NULL);
			}
		}
	}

	tcp_c_free = nb->tm_snext;		/* freeliste updaten */

	/*
	** in die serielle kette einfuegen
	** der alte unix bufferpool laesst gruessen
	*/

	if((nb->tm_snext = tcp_c_first) != NULL)
		tcp_c_first->tm_sprev = nb;
	else
		tcp_c_last = nb;

	nb->tm_sprev = NULL;
	tcp_c_first = nb;

	/*
	** in die hashkette einfuegen
	*/

	nb->tm_hashnr =  HASH_NR(ipnr);

	if((nb->tm_hnext = tcp_c_hash[nb->tm_hashnr]) != NULL)
		nb->tm_hnext->tm_hprev = nb;

	tcp_c_hash[nb->tm_hashnr] = nb;
	nb->tm_hprev = NULL;

	return(nb);
}



static __inline void mark_used_tm(struct TCP_CONMERK *nb)
/**************************************************************

	verschiebt diesen block an den anfang der seriellen kette
***************************************************************/
{
	if(nb != NULL) {

		if(nb->tm_sprev != NULL) {

			nb->tm_sprev->tm_snext = nb->tm_snext;

			if(nb->tm_snext != NULL)
				nb->tm_snext->tm_sprev = nb->tm_sprev;
			else
				tcp_c_last = nb->tm_sprev;

			if((nb->tm_snext = tcp_c_first) != NULL)
				nb->tm_snext->tm_sprev = nb;
			else
				tcp_c_last = nb;

			tcp_c_first = nb;
			nb->tm_sprev = NULL;
		}
	}
}



static int  free_all_tm(void)
{
	struct TCP_CONMERK *nb;

	if(set_bit(0,(void *)&tcp_inuse) != 0) {

		/*
		** listen werden bereits verwendet
		*/

		printk(KERN_DEBUG "abc-tcp-test dbl's in use\n");
		return(1);
	}

	while((nb = tcp_c_first) != NULL)
		free_used_tm(nb);

	tcp_inuse = 0;
	return(0);
}



static void tcp_merk_police(void)
{
	struct TCP_CONMERK *nb = tcp_c_first;

	while(nb != NULL) {

		struct TCP_CONMERK *b = nb;

		nb = nb->tm_snext;

		if(b->tm_time > jiffies || 
			((jiffies - b->tm_time) > (HZ * 3600 * 12))) {

			free_used_tm(b);
		}
	}

	tcp_next_police = tcp_last_police = jiffies;
	tcp_next_police += HZ * 60;
}


static  int sende_ip(	struct device *ndev,
						struct sk_buff *skb,
						u_long srcadr,			/* network byteorder */
						u_long dstadr,			/* network byteorder */
						u_short bytes,
						u_short proto			/* protokoll feld	*/
						)
/*****************************************************************************


	vervollstaendigen auf eine ip-paket
	es wird davon ausgegangen  das keine optionen notwendig sind.

	(man soll sich das leben so einfach wie moeglich machen)


	return == 0	== ok paket gsendet
	rerurn != 0 == fehler  skb verworfen

****************************************************************************/
{

	struct iphdr *iph = (struct iphdr *)((u_char *)skb->data);

	if(dev->net_verbose > 1)
		printk(KERN_DEBUG "abc_ipsend %s->%s proto %d\n",
			abc_h_ipnr(srcadr),
			abc_h_ipnr(dstadr),
			proto);

	iph->version 	= 4;
	iph->ihl      	= 5;
	iph->tos      	= 0;
	iph->tot_len	 = htons(20 + bytes);
	iph->id			= (u_short)(jiffies & 0xFFFF);
	iph->frag_off 	= 0;
	iph->ttl      	= 64;
	iph->protocol 	= proto;
	iph->check 		= 0;
	iph->daddr    	= dstadr;
	iph->saddr    	= srcadr;

	iph->check = abc_checksum((void *)iph, 10); /* constant 10 words */
	skb->pkt_type = PACKET_HOST;
	skb->protocol = htons(ETH_P_IP);
	skb->dev = ndev;
	skb->mac.raw = skb->data;
	netif_rx(skb);

	return(0);
}




static int sende_udp(	struct device *ndev,
						u_long srcadr,			/* network byteorder */
						u_long dstadr,			/* network byteorder */
						u_short srcport,		/* network byteorder */
						u_short dstport,		/* network byteorder */
						u_char *dbuf,
						u_short bytes)

/******************************************************************

	senden eines udp paketes 

	return == 0 == alles ok
	return != 0 == nicht gsendet

********************************************************************/
{
	int need;
	struct sk_buff *skb;
	u_char *dp;
	u_short extra_bytes = 0;
	struct udphdr *u;
	int len;

	struct P_UDPHEAD {

		u_long  p_srcip;
		u_long  p_dstip;
		u_char  p_zero;
		u_char  p_proto;
		u_short p_len;

	} *pudp;

	/*
	** anzahl bytes berechnen
	** IPHDR + UDPHDR + databytes + (databytes & 1)
	**
	** ich hoffe das der ip-receiver ein eventuelles extrabyte
	** nicht weiterverarbeitet und sich nach den ip-header-angaben richtet
	** 
	** so brauche ich nicht grossartig swappen und bin immer 
	** auf einer (adr % 2) == 0 adresse 
	*/

	if(dev->net_verbose > 1)
		printk(KERN_DEBUG "abc_udpsend port %d->%d len %d\n",
			ntohs(srcport),
			ntohs(dstport),
			bytes);

	extra_bytes = bytes & 1;

	len =  sizeof(struct iphdr) + sizeof(struct udphdr) + bytes;
	need = len + extra_bytes;
	skb = dev_alloc_skb(need+ dev->abc_max_hdrlen);

	if(skb == NULL) {

		printk(KERN_DEBUG "abc_sende_udp no space for new skb\n");
		return(-1);
	}

	skb_reserve(skb,dev->abc_max_hdrlen);
	dp = (u_char *)skb_put(skb,len);

	/*
	** pseudo header positionieren
	*/

	dp +=  sizeof(struct iphdr);

	pudp = (struct P_UDPHEAD *)(dp - sizeof(struct P_UDPHEAD));
	u = (struct udphdr *)dp;
	dp += sizeof(struct udphdr);

	pudp->p_srcip = srcadr;		 /* erwarte sie in networkbyeorder */
	pudp->p_dstip = dstadr; 	/* erwarte sie in networkbyeorder */

	pudp->p_zero = 0;
	pudp->p_proto = 17;
	pudp->p_len = bytes + sizeof(struct udphdr);

	pudp->p_len = htons(pudp->p_len);

	u->source = srcport; 	
	u->dest = 	dstport; 	
	u->len = 	pudp->p_len;

	u->check = 0;
	memcpy(dp,dbuf,bytes);

	if(extra_bytes)
		dp[bytes] = 0;

	u->check = abc_checksum((void *)pudp,
		(sizeof(*pudp) + sizeof(struct udphdr) + bytes + extra_bytes) >> 1);

	return(sende_ip(ndev,skb,srcadr,dstadr,
		bytes + sizeof(struct udphdr),(u_short)17));
}


int abcgmbh_tcp_test(struct device *ndev,struct sk_buff *sp)

/******************************************************************************
	
	das paket auf keepalive-requests untersucht und
	gegebenefalls automatisch beantwortet

	return 	==	0		==	kein sonderpaket, dies paket muss weiter 
							gesendet werden

	return	!= 0		==	es ist ein sonderpaket
							es wurde eine antwort abgesand und
							dieser skb wurde gefreet

****************************************************************************/
							

{
	int retw = 0;

	/*
	** mich interessieren nur die pakete die mindestens
	** einen udp->header mit 2 bytes daten haben
	** dies sind natuerlich auch alle tcp-pakete
	**
	**iphdr == 20 + tcpheader == 20 bytes == 40;
	**
	*/

	if(sp == NULL)
		return(0);

	if(ntohs(sp->protocol) != ETH_P_IP) {

		if(dev->net_verbose > 1) {

			printk(KERN_DEBUG 
				"abc_tcp called with protocol != ETH_P_IP\n");
		}

		return(0);
	}

	if(sp->len < 40) {

		goto udp_ausgang;

	} else {

		u_char *dpoin = ((u_char *)sp->data);
		struct iphdr *ip = (struct iphdr *)dpoin;

		if(ip->version != 4) {

			printk(KERN_DEBUG	
				"abc_tcp-test version != 4 %d ipproto %d %s->%s len %d\n",
				ip->version,
				ip->protocol,
				abc_h_ipnr(ip->saddr),
				abc_h_ipnr(ip->daddr),
				ntohs(ip->tot_len));

			goto udp_ausgang;
		}

		if(dev->net_verbose > 7)
	printk(KERN_DEBUG "abc_tcp called ipver %d ipproto %d %s->%s len %d\n",
			ip->version,
			ip->protocol,
			abc_h_ipnr(ip->saddr),
			abc_h_ipnr(ip->daddr),
			ntohs(ip->tot_len));

		if(ip->protocol == 6 && jiffies >= tcp_merk_disable_to ) {

			struct 		TCP_CONMERK *nb;
			struct 		tcphdr *tcp;
			u_short 	hashnr;
			int		 	tcp_hdr_data_len;
			int		 	tcp_data_len;
			int  		ip_hd_len;
			int 		tcp_hdr_len;
			u_long 		tcp_seqnr;		/* seqnummer in host-byte-order */

			if(set_bit(0,(void *)&tcp_inuse) != 0) {

				/*
				** listen werden bereits verwendet
				*/

				printk(KERN_DEBUG "abc-tcp-test dbl's in use\n");
				return(0);
			}

			tcp_merk_disable_to = 0;

			if(tcp_next_police < jiffies || tcp_last_police > jiffies)
				tcp_merk_police();

			/*
			** tcp protkoll
			*/

			ip_hd_len = ip->ihl << 2;
			tcp = (struct tcphdr *)(((u_char *)ip) + ip_hd_len);
			tcp_hdr_data_len = tcp_data_len = ntohs(ip->tot_len) - ip_hd_len;
			tcp_hdr_len	= tcp->doff << 2;
			tcp_data_len -= tcp_hdr_len;

			if(dev->net_verbose > 7) {

				printk(KERN_DEBUG
					"abc_tcp %hd->%hd %d %d %d/%d/%d\n",
					ntohs(tcp->source),
					ntohs(tcp->dest),
					ip_hd_len,
					tcp->doff,
					tcp_hdr_data_len,
					tcp_hdr_len,
					tcp_data_len);
			}

			if(tcp_data_len < 0) {

				/*
				** kann sowieso nicht vorkommen
				** ausser dieses paket ist fehlerhaft
				*/

				printk(KERN_DEBUG
					"abc_tcp adaten < 0 %d %d %d/%d/%d\n",
					ip_hd_len,
					tcp->doff,
					tcp_hdr_data_len,
					tcp_hdr_len,
					tcp_data_len);

				goto ausgang;
			}


			hashnr = HASH_NR(ip->saddr);

			/*
			** logische verbindung suchen
			*/


			for(nb = tcp_c_hash[hashnr];	 nb != NULL && (
							(nb->tm_srcadr ^ ip->saddr) ||
							(nb->tm_dstadr ^ ip->daddr)	||
							(nb->tm_srcport ^ tcp->source) ||
							(nb->tm_dstport ^ tcp->dest));
							nb= nb->tm_hnext);


			if(dev->net_verbose > 7) {

				printk(KERN_DEBUG
					"abc_tcp found tcp_merk 0x%08lX %d %d/%d/%d\n",
					(long) nb,
					tcp->doff,
					tcp_hdr_data_len,
					tcp_hdr_len,
					tcp_data_len);

				printk(KERN_DEBUG 
			"abc_tcp syn %d ack %d fin %d rst %d psh %d urg %d\n",
					tcp->syn,
					tcp->ack,
					tcp->fin,
					tcp->rst,
					tcp->psh,
					tcp->urg);
			}


			if(nb == NULL) {


				/*
				** verbindung wurde nicht gefunden
				** feststellen ob es sinnvoll ist diese 
				** in den merkvektor aufzunehmen
				**
				**
				** jetzt sollten wir uns die flags
				** anschauen
				** sollte irgendein flag ausser dem ack-flag 
				** gesetzt sein oder das ack-flag nicht,
				** kann es auf jedenfall kein keepalive sein
				** und aus sicherheitsgruenden wird der gemerkte
				** eintrag verworfen oder gar nicht erst eingetragen.
				**
				** wir warten also auf das naechste gueltige
				** tcp-paket, welches nur das ack-flag gesetzt hat
				*/

				if(!tcp->ack || 
					tcp->fin ||
					tcp->rst ||
					tcp->psh ||
					tcp->syn ||
					tcp->urg) {

					if(dev->net_verbose > 7)
						printk(KERN_DEBUG "abc_tcp flags ausgang\n");

					goto ausgang;
				}


				/*
				** ok
				** paket ersteinmal merken
				*/


				if((nb = get_free_tm(ip->saddr)) != NULL) {
					
					nb->tm_srcadr 		= ip->saddr;
					nb->tm_dstadr 		= ip->daddr;
					nb->tm_srcport 		= tcp->source;
					nb->tm_dstport		= tcp->dest;
					nb->tm_acknr		= tcp->ack_seq;
					nb->tm_window		= tcp->window;
					nb->tm_time 		= jiffies;
						
					/*
					** seqnummer mit laufen lassen
					*/
					nb->tm_seqnr		= ntohl(tcp->seq) + tcp_data_len;

					if(dev->net_verbose > 7) {

						printk(KERN_DEBUG 
		"abc_tcp put new %s:%hu->%s:%hu  ack %lu oseq %lu len %d nseq %lu\n",
							abc_h_ipnr(ip->saddr),
							ntohs(tcp->source),
							abc_h_ipnr(ip->daddr),
							ntohs(tcp->dest),
							ntohl(tcp->ack_seq),
							ntohl(tcp->seq),
							tcp_data_len,
							nb->tm_seqnr);
					}

				} else {

					if(dev->net_verbose > 1)
						printk(KERN_DEBUG "abc_tcp get_free_tm == NULL\n");
				}

				goto ausgang;
			}

 			if(!tcp->ack || tcp->fin || tcp->rst || tcp->psh || tcp->urg) {

				if(dev->net_verbose > 7)
					printk(KERN_DEBUG 
					"abc_tcp flags != 0 || ack == 0 goto ausgang\n");

				free_used_tm(nb);
				goto ausgang;
			}

			tcp_seqnr = ntohl(tcp->seq);

			/*
			** test auf keepalive
			*/

			if(tcp_data_len < 2 && nb->tm_acknr == tcp->ack_seq  &&
				nb->tm_window == tcp->window ) {

				/*
				** keepalive kann nur sein wenn
				** die window-size sich nicht geaendert hat
				** und die datenlaenge 0 || 1 byte betraegt
				** ausserdem muss die letzte ack_nummer mit der
				** jetzigen ack_nummer uebereinstimmen
				**
				**
				** bei einer bestehenden verbindung 
				** koennte ich eigentlich das paket
				** durchlassen. aber dann darf der 
				** timeout-timer unter dem interface
				** nicht resetet werden. dieses ist aber
				** noch nicht implementiert.
				**
				**
				*/


				if((tcp_data_len == 0 || tcp_data_len == 1) &&
					(nb->tm_seqnr == tcp_seqnr || 
					(nb->tm_seqnr - 1)==tcp_seqnr)) {
				
					/* 
					** so, das ist schon ein fast ein sicherer
					** keepalive-request.
					** aber es koennte immer noch ein retransmit sein
					**
					*/

					tcp_data_len = nb->tm_seqnr != tcp_seqnr;

					if(nb->tm_time < jiffies) {

						u_long a = (jiffies - nb->tm_time);

						if(a >= MIN_JIFFIES_FOR_KEEP_RESPONDS) {

							struct sk_buff *nskb;
							int need;

							/*
							** ok jetzt eine antwort generieren
							*/

							need =
								sizeof(struct iphdr) +
								sizeof(struct tcphdr);

							nskb = dev_alloc_skb(need + dev->abc_max_hdrlen);

							if(nskb != NULL) {

								struct tcphdr *ntcp;
								u_char *dp;

								struct PSH {

									u_long saddr;
									u_long daddr;
									u_char zp[2];
									u_short len;

								} *psh;

								skb_reserve(nskb,dev->abc_max_hdrlen);
								dp = skb_put(nskb,need);
									

								if(dev->net_verbose > 7)
		printk(KERN_DEBUG "abc-tcp_keep_merk sende response %s->%s %d->%d\n",
									abc_h_ipnr(ip->daddr),
									abc_h_ipnr(ip->saddr),
									ntohs(tcp->dest),
									ntohs(tcp->source));
							
								ntcp = (struct tcphdr *)
										(dp +sizeof(struct iphdr));


								memset((void *)ntcp,0,sizeof(*ntcp));

								psh = (struct PSH *)
									(((u_char *)ntcp) - sizeof(*psh));

								psh->saddr = ip->daddr;
								psh->daddr = ip->saddr;
								psh->zp[0] = 0;
								psh->zp[1] = 6;
								psh->len = htons(20);

								ntcp->source 	= tcp->dest;
								ntcp->dest 		= tcp->source;
								ntcp->seq 		= tcp->ack_seq;

								ntcp->ack_seq	= 
									htonl(tcp_seqnr + tcp_data_len);


								ntcp->window	= htons(4096);
								ntcp->doff		= sizeof(*tcp) >> 2;
								ntcp->check		= 0;
								ntcp->urg_ptr	= 0;
								ntcp->ack		= 1;

								ntcp->check = 
									 abc_checksum((void *)psh,16);


								retw = !sende_ip(ndev, nskb, ip->daddr,
											ip->saddr, sizeof(*ntcp), 6);

							} else {

								printk(KERN_DEBUG
									"abc_tcp_test no space for new skb\n");
							}
						}
					} 

				} else if(dev->net_verbose > 7) {

				printk(KERN_DEBUG
	"abc_tcp_test no keep  nb->tm_seqnr %lu tcp->seqnr %lu dlen %d nb->ack %lu tcp->ack %lu nm->window %d tcp->window %d\n",
					nb->tm_seqnr,
					tcp_seqnr,
					tcp_data_len,
					ntohl(nb->tm_acknr),
					ntohl(tcp->ack_seq),
					ntohs(nb->tm_window),
					ntohs(tcp->window));
				}

			} else if(dev->net_verbose > 7) {

				printk(KERN_DEBUG
	"abc_tcp_test  nb->tm_seqnr %lu tcp->seqnr %lu dlen %d nb->ack %lu tcp->ack %lu nm->window %d tcp->window %d\n",
					nb->tm_seqnr,
					tcp_seqnr,
					tcp_data_len,
					ntohl(nb->tm_acknr),
					ntohl(tcp->ack_seq),
					ntohs(nb->tm_window),
					ntohs(tcp->window));
			}

			/*
			** die laufenden daten updaten
			*/

			nb->tm_seqnr = tcp_seqnr + tcp_data_len;
			nb->tm_acknr = tcp->ack_seq;
			nb->tm_window = tcp->window;
			nb->tm_time = jiffies;
			mark_used_tm(nb);

ausgang:;
			tcp_inuse = 0;

		} else {

			if(dev->net_verbose > 7)
		printk(KERN_DEBUG "abc_tcp proto != 6 || %ld disabled \n",
				(tcp_merk_disable_to > jiffies) ?
					(tcp_merk_disable_to - jiffies) / HZ : 0);
					
		}
	}

udp_ausgang:;


	/*
	** es wurde ein antwortpaket abgesendet
	** (hoffentlich)
	*/

	if(retw > 0) 
		dev_kfree_skb(sp,FREE_WRITE);

	if(dev->net_verbose > 7)
		printk(KERN_DEBUG "abc_tcp return(%d) \n",retw);

	return(retw);
}



int abcgmbh_udp_test(struct device *ndev,struct sk_buff *sp)

/******************************************************************************
	
	testen ob ein besonderes udp-paket gesendet werden soll
	bei sonderpaketen werden diese beantwortet un eine entsprechende
	aktion ergriffen

	ausserdem wird das paket auf keepalive-requests untersucht und
	gegebenefalls automatisch beantwortet

	return 	==	0		==	kein sonderpaket, dies paket muss weiter 
							gesendet werden

	return	!= 0		==	es ist ein sonderpaket
							es wurde eine antwort abgesand und
							dieser skb wurde gefreet

****************************************************************************/
							

{
	int retw = 0;


	/*
	** mich interessieren nur die pakete die mindestens
	** einen udp->header mit 2 bytes daten haben
	** dies sind natuerlich auch alle tcp-pakete
	**
	** iphdr == 20 + udphdr == 8 + 2 bytes  ==  bytes
	**
	*/

	if(sp == NULL)
		return(0);

	if(ntohs(sp->protocol) != ETH_P_IP) {

		if(dev->net_verbose > 1) {

			printk(KERN_DEBUG 
				"abc_udp called with protocol != ETH_P_IP\n");
		}

		return(0);
	}

	if(sp->len < 30) {

		if(dev->net_verbose > 7) {

			printk(KERN_DEBUG 
				"abc_udp called with datenlen < 30 (%d)\n",(int)sp->len);
		}

	} else {

		isdn_net_local *lp = (isdn_net_local *) ndev->priv;

		/*
		** ich weiss, der test sp != NULL ist zu 99.99999 %
		** ueberfluessig.
		** ABER, wer weiss was so alles passiert. und ich
		** will ja nicht unbedingt einen kernel-mode-trap 
		** am laufenden band prodozieren.
		**
		** wer ganz optimistisch ist, kann den test ja entfernen !!!
		*/
		
		u_char *dpoin = ((u_char *)sp->data);
		struct iphdr *ip = (struct iphdr *)dpoin;

		if(dev->net_verbose > 7)
	printk(KERN_DEBUG "abc_udp called ipver %d ipproto %d %s->%s len %d\n",
			ip->version,
			ip->protocol,
			abc_h_ipnr(ip->saddr),
			abc_h_ipnr(ip->daddr),
			ntohs(ip->tot_len));


		if(ip->version != 4)
			goto udp_ausgang;

		if(ip->protocol == 17 ) {

			/*
			** udp control pakete fuer sonderfunktion
			** 0x1E00 == laenge in netbyteorder 
			** pakete mit anderen laengen koennen keine sonderpakete sein
			** also nicht beachten
			** 
			** ich gehe auch davon aus, dass die checksumme stimmt.
			** deswegen wird sie hier auch nicht mehr ueberprueft.
			** ich hoffe jedenfalls, dass der kernel sie mindestens
			** einmal ueberprueft hat. wahrscheinlich hat er sie
			** aber bereits 42x ueberprueft.
			** 
			** weil ja jeder weiss, das 42 alles beantwortet !!!.
			**
			** da die laenge gegeben ist, wird auch davon ausgegangen,
			** dass keine optionen im ip-header vorhanden sind.
			** daher muessen die source- und destination- ip-nummern
			** direkt ueber dem udp-header liegen
			*/

			if(dev->net_verbose > 7)
		printk(KERN_DEBUG "%s: abc_udp  %s->%s len netwb 0x%04X host %hd\n",
					lp->name,
					abc_h_ipnr(ip->saddr),
					abc_h_ipnr(ip->daddr),
					ip->tot_len,
					ntohs(ip->tot_len));

			if(ip->tot_len == 0x1E00)  {

				struct udphdr *u;
				u_short  sport ;
				u_short  dport ;
				u_char  *udata;
				u_char transbuf[2];

				u = (struct udphdr *)(((u_char *)ip) + sizeof(*ip));
				udata = ((u_char *)u) + sizeof(*u);

				/*
				** eine portnummer soll auf jedenfall 25001 sein.
				** dies ist eine willkuerlich gewaehlte nummer.
				** und wird von den utility-programmen als dest-port 
				** verwendet.
				*/

				if((sport = ntohs(u->source)) == 25001)
					sport = 7;

				if((dport = ntohs(u->dest)) == 25001)
					dport = 7;

				/*
				** 0x0A00  ist die udp-laenge in networkbyteorder
				** bei 2 byte udp-datenbereich
				*/

				if(dev->net_verbose > 7)
					printk(KERN_DEBUG "%s: abc_udp sport %hd dport %hd\n",
					lp->name,
					ntohs(u->source),
					ntohs(u->dest));


				if(	(u->len == 0x0A00) 										&&
					((sport >= 20000	&& sport <  20200 && dport == 7) 	||
					(dport >= 20000 && dport <  20200 && sport == 7)) 		) {

					if(dev->net_verbose > 7)
	printk(KERN_DEBUG "%s: abc_udp udata[0]=0x%02X udata[1]=0x%02X\n",
						lp->name,
						udata[0],
						udata[1]);

					if(udata[0] != udata[1]) {

						/*
						** die sonderpakete haben auf jeden fall
						** die beiden einzigen datenbytes identisch
						**
						** ja,ja ich weiss.
						** aber dies ist wirklich nur zur sicherheit.
						** denn ich will wirklich kein ungewolltes
						** paket abfangen.
						**
						** na egal. wenn die beiden bytes jedenfalls nicht
						** identisch sind, wird das paket einfach
						** weitergereicht.
						*/

						goto udp_ausgang;
					}

					if(udata[0] == 0x2d ) {

						/*
						** hier das automatische beantworten
						** der keepalive messages fuer
						** ca 1 stunde abschalten
						** dient fuer den notfall
						** falls alles schiefgeht
						**
						** ausserdem den 
						** keepalive merkvektor gesamt loeschen
						**
						**
						**
						**	dieses paket sollte
						** 	dann weitergeroutet werden,
						**	damit alle nachfolgenden router
						** 	auch diese aktion durchfuehren.
						**
						** dieses paket wird dann am zielrechner
						** einfach verworfen
						*/

						tcp_merk_disable_to = jiffies + (HZ * 3600);

						if( free_all_tm())
							return(1);

					} else if( udata[0] == 0x28) {

						transbuf[0] = transbuf[1] = 0x29;

						/*
						** testet ob diese logische verbindung
						** ueber einen isdn-router laeuft.
						** der erste router beantwortet dieses 
						** paket. und somit weis der absender bescheid
						*/

						retw = !sende_udp(ndev,
							ip->daddr,
							ip->saddr,
							u->dest,
							u->source,
							transbuf,
							2);

					} else if( *udata == 0x2c || 
							*udata == 0x2e || *udata == 0x2a ) {

						int isfull = 0;

						if(*udata == 0x2e) {

							isfull = 1;
							lp->abc_call_disabled = 0;
							lp->abc_icall_disabled = 0;
							lp->abc_cbout_secure = 0;
							lp->abc_last_disp_disabled = 0;
							lp->abc_dlcon_cnt = 0;
							transbuf[0] = transbuf[1] = 0x2f;

							if(dev->net_verbose > 1) {

								printk(KERN_DEBUG
									"%s: resetting abc_disable counters\n",
									lp->name);
							}

						} else if(*udata == 0x2a) {
						
							transbuf[0] = transbuf[1] = 0x2b;

						} else transbuf[0] = transbuf[1] = 0x2d;

						/*
						** verbindung soll wenn diese seite der caller ist
						** ansonsten passiert garnichts
						*/

						if(!(lp->flags & ISDN_NET_CONNECTED))
							transbuf[0] = transbuf[1] = 0x0;

						retw = !sende_udp(ndev,
							ip->daddr,
							ip->saddr,
							u->dest,
							u->source,
							transbuf,
							2);

						lp->abc_delayed_hangup = 
							jiffies + ABC_DELAYED_MAXHANGUP_WAIT;

					} 
					/* else und was in zukunft noch 
					** alles notwendig sein wird 
					*/
				}
			}
		}
	}

udp_ausgang:;

	/*
	*/

	if(retw)  {

		dev_kfree_skb(sp,FREE_WRITE);

		if(dev->net_verbose > 7)
			printk(KERN_DEBUG "abc_udp return %d free skb\n",retw);

		return(retw);

	} else {

		u_char *dpoin = ((u_char *)sp->data);
		struct iphdr *ip = (struct iphdr *)dpoin;
		u_long mynet;
		u_long maske;
		u_long dnet;

		if(ip->version != 4) {

			if(dev->net_verbose > 5) {

				printk(KERN_DEBUG 
			"abc_is_broadcast called ipver %d  != 4 ipproto %d %s->%s len %d\n",
				ip->version,
				ip->protocol,
				abc_h_ipnr(ip->saddr),
				abc_h_ipnr(ip->daddr),
				ntohs(ip->tot_len));
			}
			
			return(0);
		}

		mynet = ndev->pa_addr & (maske = ndev->pa_mask);
		dnet = ip->daddr & maske;

		if(dnet != mynet) 
			return(0);

		maske = ~maske;
		mynet = ip->daddr & maske;

		if(!mynet || mynet == maske) {

			if(dev->net_verbose > 5) {

				printk(KERN_DEBUG 
	"abc_is_broadcast called ipver %d ipproto %d %s->%s len %d dropped\n",
				ip->version,
				ip->protocol,
				abc_h_ipnr(ip->saddr),
				abc_h_ipnr(ip->daddr),
				ntohs(ip->tot_len));
			}

			dev_kfree_skb(sp,FREE_WRITE);
			return(1);
		}
	}

	return(0);
}

void abc_clear_tx_que(isdn_net_local *lp) 
{
	if(lp != NULL) {

		struct sk_buff_head *tq = lp->abc_tx_que;
		struct sk_buff_head *etq = lp->abc_tx_que + ABC_ANZ_TX_QUE;

		for(;tq < etq;tq++) {

			struct sk_buff *skb = NULL;
			
			while((skb = skb_dequeue(tq)) != NULL)
				dev_kfree_skb(skb,FREE_WRITE);
		}
	}
}

void abc_put_tx_que(isdn_net_local *lp,int qnr,int top,struct sk_buff *skb)
{
	if(lp != NULL && skb != NULL) {

		struct sk_buff_head *tq;

		if(qnr >= ABC_ANZ_TX_QUE || qnr < 0) {

			printk(KERN_DEBUG
				"abc_put_tx_que: qnr %d out of range 0-%d\n",
				qnr,
				ABC_ANZ_TX_QUE);

			return;
		}

		tq = lp->abc_tx_que + qnr;

		if(top)
			skb_queue_head(tq,skb);
		else
			skb_queue_tail(tq,skb);
	}
}


#ifdef PACKEN_SCHON_OK

int abc_comp_check(u_char **s_bufadr,u_char *pbuf, int len)
{

	u_short k;
	int nlen;
	int dlen;
	u_char *p = *s_bufadr;

	if(len < 80 || len > 1520)
		return(len);

	k = *(p++) & 0xFF;
	k |= (*(p++) & 0xFF) << 8;
	dlen = len -2;

	if((k & ~ABCR_MAXBYTES) != ABCR_ISDATA)
		return(len);

	if((nlen = abcgmbh_pack(p,pbuf + 2,dlen)) < 1 || nlen >= dlen)
		return(len);

	p = pbuf;
	k |= ABCR_BYTECOMP;

	*(p++) = k & 0xFF;
	*(p++) = (k >> 8) & 0xFF;
	nlen += 2;

	*s_bufadr = pbuf;
	return(nlen);
}

#endif
	
#endif
