
/* $Id$

 * Linux ISDN subsystem, abc-extension releated funktions.
 *
 * Author: abc GmbH written by Detlef Wengorz <detlefw@isdn4linux.de>
 * 
 * Many thanks for testing, debugging and writing Doku to:
 * Mario Schugowski <mario@mediatronix.de>
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
 * Revision 1.9  1999/11/30 11:29:06  detabc
 * add a on the fly frame-counter and limit
 *
 * Revision 1.8  1999/11/28 14:49:07  detabc
 * In case of rawip-compress adjust dev[x]->ibytes/obytes to reflect the
 * uncompressed size.
 *
 * Revision 1.7  1999/11/26 15:54:59  detabc
 * added compression (isdn_bsdcompress) for rawip interfaces with x75i B2-protocol.
 *
 * Revision 1.6  1999/11/20 22:14:13  detabc
 * added channel dial-skip in case of external use
 * (isdn phone or another isdn device) on the same NTBA.
 * usefull with two or more card's connected the different NTBA's.
 * global switchable in kernel-config and also per netinterface.
 *
 * add auto disable of netinterface's in case of:
 * 	to many connection's in short time.
 * 	config mistakes (wrong encapsulation, B2-protokoll or so on) on local
 * 	or remote side.
 * 	wrong password's or something else to a ISP (syncppp).
 *
 * possible encapsulations for this future are:
 * ISDN_NET_ENCAP_SYNCPPP, ISDN_NET_ENCAP_UIHDLC, ISDN_NET_ENCAP_RAWIP,
 * and ISDN_NET_ENCAP_CISCOHDLCK.
 *
 * Revision 1.5  1999/11/07 21:57:04  detabc
 * added dwabc-udpinfo-dial support
 *
 * Revision 1.4  1999/10/27 21:21:17  detabc
 * Added support for building logically-bind-group's per interface.
 * usefull for outgoing call's with more then one isdn-card.
 *
 * Switchable support to dont reset the hangup-timeout for
 * receive frames. Most part's of the timru-rules for receiving frames
 * are now obsolete. If the input- or forwarding-firewall deny
 * the frame, the line will be not hold open.
 *
 * Revision 1.3  1999/09/23 22:22:41  detabc
 * added tcp-keepalive-detect with local response (ipv4 only)
 * added host-only-interface support
 * (source ipaddr == interface ipaddr) (ipv4 only)
 * ok with kernel 2.3.18 and 2.2.12
 *
 * Revision 1.2  1999/09/14 22:53:53  detabc
 *
 * Test LCR ioctl call/ change a wrong pointer++/
 * LCR LL->user_space(isdnlog)->ioctl->LL ok now
 *
 * Revision 1.1  1999/09/12 16:19:39  detabc
 * added abc features
 * least cost routing for net-interfaces (only the HL side).
 * need more implementation in the isdnlog-utility
 * udp info support (first part).
 * different EAZ on outgoing call's.
 * more checks on D-Channel callbacks (double use of channels).
 * tested and running with kernel 2.3.17
 *
 *
 */

#include <linux/config.h>
#define __NO_VERSION__

#ifdef CONFIG_ISDN_WITH_ABC

static char *dwabcrevison = "$Revision$";

#include <asm/semaphore.h>
#include <linux/isdn.h>
#include "isdn_common.h"

#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
#include <linux/skbuff.h>
#include <net/ip.h>
#include <net/tcp.h>
#ifdef CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR
#include <linux/inetdevice.h>
#endif
#endif

#include <net/udp.h>
#include <net/checksum.h>
#include <linux/isdn_dwabc.h>

#if CONFIG_ISDN_WITH_ABC_RAWIPCOMPRESS && CONFIG_ISDN_PPP
#include <linux/isdn_ppp.h>
extern struct isdn_ppp_compressor *isdn_ippp_comp_head;
#define ipc_head isdn_ippp_comp_head
static struct isdn_ppp_comp_data BSD_COMP_INIT_DATA;
#ifndef CI_BSD_COMPRESS
#define CI_BSD_COMPRESS 21
#endif
#endif

#define NBYTEORDER_30BYTES      0x1e00 
#define DWABC_TMRES (HZ)

//#define KEEPALIVE_VERBOSE 1
//#define DYNADDR_VERBOSE	 1

#define VERBLEVEL (dev->net_verbose > 2)

static struct timer_list dw_abc_timer;
static volatile short dw_abc_timer_running = 0;
static volatile short dw_abc_timer_need = 0;


#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
static struct semaphore lcr_sema;
#define LCR_LOCK() down_interruptible(&lcr_sema);
#define LCR_ULOCK() up(&lcr_sema);

typedef struct ISDN_DW_ABC_LCR {

	struct ISDN_DW_ABC_LCR *lcr_prev;
	struct ISDN_DW_ABC_LCR *lcr_next;
	char lcr_printbuf[64 + ISDN_MSNLEN + ISDN_MSNLEN];
	char *lcr_poin;
	char *lcr_epoin;

} ISDN_DW_ABC_LCR;

static ISDN_DW_ABC_LCR *first_lcr = NULL;
static ISDN_DW_ABC_LCR *last_lcr = NULL;

static int lcr_open_count = 0;
static volatile u_long lcr_call_counter = 0;
static long lcr_requests = 0;

#endif

#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE

static short 	deadloop = 0;

static struct semaphore ipv4keep_sema;
#define TKAL_LOCK 	down_interruptible(&ipv4keep_sema)
#define TKAL_ULOCK 	up(&ipv4keep_sema)

struct TCPM {
	u_short         tcpm_srcport;
	u_short         tcpm_dstport;
	u_short         tcpm_window;
	struct TCPM    *tcpm_prev;
	struct TCPM    *tcpm_next;
	u_long          tcpm_srcadr;
	u_long          tcpm_dstadr;
	u_long          tcpm_seqnr;
	u_long          tcpm_acknr;
	u_long          tcpm_time;
};

static u_long   next_police = 0;
static u_long   last_police = 0;

#define MAX_MMA 16
static void    *MMA[MAX_MMA];
static struct TCPM *tcp_first = NULL;
static struct TCPM *tcp_last = NULL;
static struct TCPM *tcp_free = NULL;
#endif


/***********************************
currently unused
static void start_timer(void)
{
	dw_abc_timer_need = 1;

	if(!dw_abc_timer_running) {

		dw_abc_timer_running = 1;
		add_timer(&dw_abc_timer);
	}
}
*************************************/


#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT

static int myjiftime(char *p,u_long nj)
{
	sprintf(p,"%02ld:%02ld.%02ld",
		((nj / 100) / 60) % 100, (nj / 100) % 60,nj % 100);

	return(8);
}


static void dw_lcr_clear_all(void)
{

	ISDN_DW_ABC_LCR *p;

	LCR_LOCK();

	while((p = first_lcr) != NULL) {

		first_lcr = p->lcr_next;
		kfree(p);
	}

	last_lcr = NULL;
	lcr_requests = 0;
	LCR_ULOCK();
}

void isdn_dw_abc_lcr_open(void) { lcr_open_count++; }

void isdn_dw_abc_lcr_close(void) 
{ 
	lcr_open_count--;

	if(lcr_open_count < 1) {

		lcr_open_count = 0;
		dw_lcr_clear_all();
	}
}


size_t isdn_dw_abc_lcr_readstat(char *buf,size_t count) 
{
	size_t r = 0;

	if(buf != NULL && count > 0) {

		ISDN_DW_ABC_LCR *p;

		LCR_LOCK();

		while(count > 0 && (p = first_lcr) != NULL) {

			if(p->lcr_poin < p->lcr_epoin) {

				size_t  n = p->lcr_epoin - p->lcr_poin;

				if(n > count)
					n = count;

				copy_to_user(buf,p->lcr_poin,n);
				p->lcr_poin += n;
				count -= n;
				buf += n;
				r += n;
			}

			if(p->lcr_poin < p->lcr_epoin) 
				break;

			if((first_lcr = p->lcr_next) != NULL)
				first_lcr->lcr_prev = NULL;
			else
				last_lcr = NULL;

			kfree(p);
			lcr_requests--;
		}

		LCR_ULOCK();
	}

	return(r);
}

void isdn_dw_abc_lcr_clear(isdn_net_local *lp)
{

	if(lp != NULL) {

		u_long flags;

		save_flags(flags);
		cli();

		if(lp->dw_abc_lcr_cmd != NULL) 
			kfree(lp->dw_abc_lcr_cmd);

		if(lp->dw_abc_lcr_io != NULL) 
			kfree(lp->dw_abc_lcr_io);

		lp->dw_abc_lcr_io = NULL;
		lp->dw_abc_lcr_cmd = NULL;

		lp->dw_abc_lcr_callid = 
		lp->dw_abc_lcr_start_request =
		lp->dw_abc_lcr_end_request = 0;

		restore_flags(flags);
	}
}


u_long isdn_dw_abc_lcr_call_number( isdn_net_local *lp,isdn_ctrl *call_cmd)
{
	u_long mid = 0;

	isdn_dw_abc_lcr_clear(lp);

	if(lcr_requests < 100 && lp != NULL && lcr_open_count > 0 && call_cmd != NULL) {

		u_long flags = 0;
		ISDN_DW_ABC_LCR  *lc = NULL;
		int ab = 0;

		save_flags(flags);
		cli();

		if((lp->dw_abc_lcr_cmd = 
			( isdn_ctrl *)kmalloc(sizeof(isdn_ctrl),GFP_ATOMIC)) == NULL) {

			restore_flags(flags);
			printk(KERN_DEBUG "%s %d : LCR no memory\n",__FILE__,__LINE__);
			return(0);
		}

		memcpy(lp->dw_abc_lcr_cmd,call_cmd,sizeof(*call_cmd));

		if(!(++lcr_call_counter))
			lcr_call_counter++;

		lp->dw_abc_lcr_callid = mid = lcr_call_counter++;
		lp->dw_abc_lcr_end_request = lp->dw_abc_lcr_start_request = jiffies;
		lp->dw_abc_lcr_end_request += HZ * 3;

		restore_flags(flags);

		if((lc = (ISDN_DW_ABC_LCR  *)kmalloc(sizeof(*lc),GFP_KERNEL)) == NULL) {

			printk(KERN_DEBUG "%s %d : LCR no memory\n",__FILE__,__LINE__);
			return(0);
		}

		lc->lcr_poin = lc->lcr_epoin = lc->lcr_printbuf;
		lc->lcr_epoin += myjiftime(lc->lcr_epoin,jiffies);

		sprintf(lc->lcr_epoin," DW_ABC_LCR\t%lu\t%.*s\t%.*s\n",
			mid,
			(int)ISDN_MSNLEN,
			call_cmd->parm.setup.eazmsn,
			(int)ISDN_MSNLEN,
			call_cmd->parm.setup.phone);

		lc->lcr_epoin += strlen(lc->lcr_epoin);
		ab = lc->lcr_epoin - lc->lcr_poin;

		LCR_LOCK();

		if((lc->lcr_prev = last_lcr) != NULL) 
			lc->lcr_prev->lcr_next = lc;
		else
			first_lcr = lc;

		lc->lcr_next = NULL;
		last_lcr = lc;
		lcr_requests++;
		LCR_ULOCK();

		if(ab > 0) {

			save_flags(flags);
			cli();

			if(dev->drv[0] != NULL ) {

				dev->drv[0]->stavail += ab;
				wake_up_interruptible(&dev->drv[0]->st_waitq);
			}

			restore_flags(flags);
		}
	}

	return(mid);
}


void isdn_dw_abc_lcr_ioctl(u_long arg)
{
	struct ISDN_DWABC_LCR_IOCTL	i;
	int need = sizeof(struct ISDN_DWABC_LCR_IOCTL); 
	isdn_net_dev *p; 
	u_long flags;

	memset(&i,0,sizeof(struct ISDN_DWABC_LCR_IOCTL));
	copy_from_user(&i,(char *)arg,sizeof(int));

	if(i.lcr_ioctl_sizeof < need)
		need = i.lcr_ioctl_sizeof;

	if(need > 0) 
		copy_from_user(&i,(char *)arg,need);

	 save_flags(flags);
	 cli();
	 p = dev->netdev; 

	 for(;p ; p = p->next) {

	 	isdn_net_local *lp = p->local;

	 	if(lp->dw_abc_lcr_callid == i.lcr_ioctl_callid &&
			lp->dw_abc_lcr_cmd != NULL) {

			if(lp->dw_abc_lcr_io == NULL) {

				lp->dw_abc_lcr_io = (struct ISDN_DWABC_LCR_IOCTL *)
					kmalloc(sizeof(struct ISDN_DWABC_LCR_IOCTL),GFP_ATOMIC);
			}

			if(lp->dw_abc_lcr_io == NULL) {

				printk(KERN_DEBUG "%s %d : no memory\n",__FILE__,__LINE__);

			} else {

				memcpy(lp->dw_abc_lcr_io,&i,sizeof(struct ISDN_DWABC_LCR_IOCTL));
				if(i.lcr_ioctl_flags & DWABC_LCR_FLG_NEWNUMBER) {

					char *xx = i.lcr_ioctl_nr;
					char *exx = xx + sizeof(i.lcr_ioctl_nr);
					char *d = lp->dw_abc_lcr_cmd->parm.setup.phone;
					char *ed = 
						d + sizeof(lp->dw_abc_lcr_cmd->parm.setup.phone) - 1;

					while(d < ed && xx < exx && *xx) *(d++) = *(xx++);
					while(d < ed) *(d++) = 0;
					*d = 0;
				}
			}
		}
	 }

	 restore_flags(flags);
}

#endif


#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK
int dw_abc_udp_test(struct sk_buff *skb,struct net_device *ndev)
{
	if(ndev != NULL && skb != NULL && skb->protocol == htons(ETH_P_IP)) {

		struct iphdr *iph = (struct iphdr *)skb->data;
		isdn_net_local *lp = (isdn_net_local *) ndev->priv;

		if(skb->len >= 20 && iph->version == 4 && !(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_NO_UDP_CHECK)) {

			if(	iph->tot_len == NBYTEORDER_30BYTES	&& iph->protocol == IPPROTO_UDP) {

				struct udphdr *udp = (struct udphdr *)(skb->data + (iph->ihl << 2));
				ushort usrc = ntohs(udp->source);

				if(udp->dest == htons(25001) && usrc >= 20000 && usrc < 25000) {

					char *p = (char *)(udp + 1);

					if(p[0] == p[1]) {

						char mc = 0;

						switch(*p) {
						case 0x30:

							mc = *p;

							if((lp->flags & ISDN_NET_CONNECTED) && (!lp->dialstate))
								mc++;

							break;

						case 0x32:

							mc = *p;
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK_DIAL
							if((lp->flags & ISDN_NET_CONNECTED) && (!lp->dialstate)) {

								mc++;
								break;
							}

							if(!isdn_net_force_dial_lp(lp)) mc++;
#endif
							break;

						case 0x11:
							mc = *p + 1;
							isdn_dw_abc_reset_interface(lp,1);
							break;

						case 0x28:	mc = *p + 1;	break;
						case 0x2a:
						case 0x2c:

							mc = *p;
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK_HANGUP
							if(!(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_NO_UDP_HANGUP)) {

								if(lp->isdn_device >= 0) {

									isdn_net_hangup(ndev);
									mc = *p + 1;
								}
							}
#endif
							break;
						}

						if(mc) {

							struct sk_buff *nskb;
							int need = 2+sizeof(struct iphdr)+sizeof(struct udphdr);
							int hneed = need + ndev->hard_header_len;

							if((nskb = (struct sk_buff *)dev_alloc_skb(hneed)) != NULL) {

								ushort n = sizeof(struct udphdr) + 2;
								struct iphdr *niph;
								struct udphdr *nup;
								skb_reserve(nskb,ndev->hard_header_len);

								if((niph = (struct iphdr *)skb_put(nskb,need))==NULL){

									printk(KERN_DEBUG "%s: skb_put failt (%d bytes)\n", lp->name,hneed);
									dev_kfree_skb(nskb);
									return(0);
								}

								nup = (struct udphdr *)(niph + 1);
								((char *)(nup + 1))[0] = mc;
								((char *)(nup + 1))[1] = mc;
								nup->source=udp->dest;
								nup->dest=udp->source;
								nup->len=htons(n);
								nup->check=0; /* dont need checksum */
								memset((void *)niph,0,sizeof(*niph));
								niph->version=4;
								niph->ihl=5;
								niph->tot_len=NBYTEORDER_30BYTES;
								niph->ttl = 32;
								niph->protocol = IPPROTO_UDP;
								niph->saddr=iph->daddr;
								niph->daddr=iph->saddr;
								niph->id=iph->id;
								niph->check=ip_fast_csum((unsigned char *)niph,niph->ihl);
								nskb->dev = ndev;
								nskb->pkt_type = PACKET_HOST;
								nskb->protocol = htons(ETH_P_IP);
								nskb->mac.raw = nskb->data;
								netif_rx(nskb);
							}

							return(1);
						}
					}
				}
			}
		}
	}

	return(0);
}
#endif




void isdn_dw_clear_if(ulong pm,isdn_net_local *lp)
{
	if(lp != NULL) {

#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
		isdn_dw_abc_lcr_clear(lp);
#endif

	}
}



static void dw_abc_timer_func(u_long dont_need_yet)
{

	if(dw_abc_timer_need) {

		dw_abc_timer.expires = jiffies + DWABC_TMRES;
		add_timer(&dw_abc_timer);
	
	} else dw_abc_timer_running = 0;
}



#if CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE || CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR

static char    *ipnr2buf(u_long ipadr)
{
	static char     buf[8][16];
	static u_char   bufp = 0;
	char           *p = buf[bufp = (bufp + 1) & 7];
	u_char         *up = (u_char *) & ipadr;

	sprintf(p, "%d.%d.%d.%d", up[0], up[1], up[2], up[3]);
	return (p);
}
#endif

#if CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE

static __inline void free_tcpm(struct TCPM *fb)
{
	if (fb != NULL) {

		if (fb->tcpm_prev != NULL)
			fb->tcpm_prev->tcpm_next = fb->tcpm_next;
		else
			tcp_first = fb->tcpm_next;

		if (fb->tcpm_next != NULL)
			fb->tcpm_next->tcpm_prev = fb->tcpm_prev;
		else
			tcp_last = fb->tcpm_prev;

		fb->tcpm_next = tcp_free;
		tcp_free = fb;
		fb->tcpm_prev = NULL;
	}
}

static void     ip4keepalive_get_memory(void)
{
	struct TCPM    *nb;
	struct TCPM    *enb;
	int             anz = 1024 / sizeof(struct TCPM);
	int             shl;

	for (shl = 0; shl < MAX_MMA && MMA[shl] != NULL; shl++) ;

	if (shl >= MAX_MMA)
		return;

	nb = (struct TCPM *) kmalloc(sizeof(struct TCPM) * anz, GFP_ATOMIC);

	if (nb == NULL) {

		printk(KERN_DEBUG "ip4keepalive no mem\n");
		return;

	}

	memset((void *) nb, 0, sizeof(struct TCPM) * anz);
	enb = nb + anz;
	MMA[shl] = (void *) nb;

	for (; nb < enb; nb++) {

		nb->tcpm_next = tcp_free;
		tcp_free = nb;
	}
}

static __inline struct TCPM *get_free_tm(void)
{
	struct TCPM    *nb;

	if ((nb = tcp_free) == NULL) {

		if(MMA[MAX_MMA - 1] == NULL) 
			ip4keepalive_get_memory();

		if ((nb = tcp_free) == NULL) {

			if ((nb = tcp_last) == NULL) 
				return (NULL);

			free_tcpm(nb);

			if ((nb = tcp_free) == NULL) 
				return (NULL);
		}
	}

	tcp_free = nb->tcpm_next;

	if ((nb->tcpm_next = tcp_first) != NULL)
		tcp_first->tcpm_prev = nb;
	else
		tcp_last = nb;

	nb->tcpm_prev = NULL;
	tcp_first = nb;
	return (nb);
}


static int  isdn_tcpipv4_test(struct net_device *ndev, struct iphdr *ip, struct sk_buff *skb)
/******************************************************************************
	
	return	==	0	==	no keepalive 
			==	1	==	keepalive response transmited

****************************************************************************/

{
	int             ip_hd_len = ip->ihl << 2;
	struct tcphdr  *tcp = (struct tcphdr *) (((u_char *) ip) + ip_hd_len);

	int             tcp_hdr_len = tcp->doff << 2;
	int             tcp_data_len = ntohs(ip->tot_len) - tcp_hdr_len - ip_hd_len;

	u_long          tcp_seqnr;
	struct TCPM    *nb;
	int             retw = 0;
	int				shl = 0;
	int				secure = 10000;

#ifdef KEEPALIVE_VERBOSE
	if (VERBLEVEL)
		printk(KERN_DEBUG "isdn_keepalive %s-called ipver %d ipproto %d %s->%s len %d\n",
				(ndev != NULL) ? "Tx" : "Rx",
			   ip->version,
			   ip->protocol,
			   ipnr2buf(ip->saddr),
			   ipnr2buf(ip->daddr),
			   ntohs(ip->tot_len));
#endif

	if (next_police < jiffies || last_police > jiffies) {

		next_police = last_police = jiffies;
		next_police += HZ * 60;
		nb = tcp_first;

		for (shl=0;nb != NULL && shl < secure;shl++) {

			struct TCPM    *b = nb;

			nb = nb->tcpm_next;

			if (b->tcpm_time > jiffies || ((jiffies - b->tcpm_time) > (HZ * 3600 * 12)))
				free_tcpm(b);
		}

		if(shl >= secure) {

			printk(KERN_WARNING "ip_isdn_tcp_keepalive: police deadloop\n");
			deadloop = 1;
		}
	}

#ifdef KEEPALIVE_VERBOSE
	if (VERBLEVEL) {

		printk(KERN_DEBUG
			   "isdn_keepalive %hd->%hd %d %d %d/%d\n",
			   ntohs(tcp->source),
			   ntohs(tcp->dest),
			   ip_hd_len,
			   tcp->doff,
			   tcp_hdr_len,
			   tcp_data_len);
	}
#endif
	if (tcp_data_len < 0) {

		printk(KERN_DEBUG
			   "isdn_keepalive adaten < 0 %d %d %d/%d\n",
			   ip_hd_len,
			   tcp->doff,
			   tcp_hdr_len,
			   tcp_data_len);

		return (0);
	}

	if(ndev == NULL) {

		for (nb = tcp_first,shl = 0; nb != NULL && shl < secure &&
			 ((nb->tcpm_srcadr ^ ip->daddr) ||
			  (nb->tcpm_dstadr ^ ip->saddr) ||
			  (nb->tcpm_srcport ^ tcp->dest) ||
			  (nb->tcpm_dstport ^ tcp->source)); nb = nb->tcpm_next,shl++) ;

	} else {

		for (nb = tcp_first,shl = 0; nb != NULL && shl < secure &&
			 ((nb->tcpm_srcadr ^ ip->saddr) ||
			  (nb->tcpm_dstadr ^ ip->daddr) ||
			  (nb->tcpm_srcport ^ tcp->source) ||
			  (nb->tcpm_dstport ^ tcp->dest)); nb = nb->tcpm_next,shl++) ;
	}

	if(shl >= secure) {

		printk(KERN_WARNING "ip_isdn_tcp_keepalive: search deadloop\n");
		deadloop = 1;
	}

#ifdef KEEPALIVE_VERBOSE
	if (VERBLEVEL) {

		printk(KERN_DEBUG
			   "isdn_keepalive found tcp_merk 0x%08lX %d %d/%d\n",
			   (long) nb,
			   tcp->doff,
			   tcp_hdr_len,
			   tcp_data_len);

		printk(KERN_DEBUG
			   "isdn_keepalive syn %d ack %d fin %d rst %d psh %d urg %d\n",
			   tcp->syn,
			   tcp->ack,
			   tcp->fin,
			   tcp->rst,
			   tcp->psh,
			   tcp->urg);
	}
#endif
	if (nb == NULL) {

		if (!ndev || !tcp->ack || tcp->fin || tcp->rst || tcp->syn || tcp->urg) {
			

#ifdef KEEPALIVE_VERBOSE
			if (VERBLEVEL)
				printk(KERN_DEBUG "isdn_keepalive flags ausgang\n");
#endif
			return (0);
		}

		if ((nb = get_free_tm()) == NULL) {

#ifdef KEEPALIVE_VERBOSE
			if (VERBLEVEL)
				printk(KERN_DEBUG "isdn_keepalive get_free_tm == NULL\n");
#endif

			return (0);
		}

		nb->tcpm_srcadr = ip->saddr;
		nb->tcpm_dstadr = ip->daddr;
		nb->tcpm_srcport = tcp->source;
		nb->tcpm_dstport = tcp->dest;
		nb->tcpm_acknr = tcp->ack_seq;
		nb->tcpm_window = tcp->window;
		nb->tcpm_time = jiffies;
		nb->tcpm_seqnr = ntohl(tcp->seq) + tcp_data_len;

#ifdef KEEPALIVE_VERBOSE
		if (VERBLEVEL) {

			printk(KERN_DEBUG
				   "isdn_keepalive put new %s:%hu->%s:%hu  ack %lu oseq %lu len %d nseq %lu\n",
				   ipnr2buf(ip->saddr),
				   ntohs(tcp->source),
				   ipnr2buf(ip->daddr),
				   ntohs(tcp->dest),
				   ntohl(tcp->ack_seq),
				   ntohl(tcp->seq),
				   tcp_data_len,
				   nb->tcpm_seqnr);
		}
#endif
		return (0);
	}

	if(ndev == NULL) {

		/*
		** we receive a frame from the peer
		** in most case the local-side will respond with a frame
		** if the respons-frame is a keealive-response
		** then we cannot local answer (peer will drop the connection)
		** so we update only the time-step 
		*/

		nb->tcpm_time = jiffies;
		return(0);
	}

	if (!tcp->ack || tcp->fin || tcp->rst || tcp->urg) {

#ifdef KEEPALIVE_VERBOSE
		if (VERBLEVEL)
			printk(KERN_DEBUG
				   "isdn_keepalive received || flags != 0 || ack == 0\n");
#endif

		free_tcpm(nb);
		return (0);
	}

	tcp_seqnr = ntohl(tcp->seq);

	if (!tcp->psh 						&& 
		tcp_data_len < 2 				&&
		nb->tcpm_acknr == tcp->ack_seq 	&&
		nb->tcpm_window == tcp->window	) {

		if (nb->tcpm_seqnr == tcp_seqnr || (nb->tcpm_seqnr - 1) == tcp_seqnr) {

			/*
			 * so, das ist schon ein fast ein sicherer
			 * keepalive-request.
			 * aber es koennte immer noch ein retransmit sein
			 */

			if (nb->tcpm_time < jiffies &&
				(jiffies - nb->tcpm_time) >= ((u_long) 50 * HZ)) {

				struct sk_buff *nskb;
				int             need;
				int             hlen;

				/*
				 * ok jetzt eine antwort generieren
				 */

				need = sizeof(struct iphdr) + sizeof(struct tcphdr);

				hlen = ndev->hard_header_len;
				hlen = (hlen + 15) & ~15;
				nskb = dev_alloc_skb(need + hlen);

				if (nskb != NULL) {

					struct tcphdr  *ntcp;
					struct iphdr   *iph = (struct iphdr *) ((u_char *) skb->data);

					struct PSH { u_long saddr; u_long daddr; u_char zp[2]; u_short len; } *psh;

					skb_reserve(nskb, hlen);
					iph = (struct iphdr *) skb_put(nskb, sizeof(*iph));
					ntcp = (struct tcphdr *) skb_put(nskb, sizeof(*ntcp));

					if(dev->net_verbose > 0)
						printk(KERN_DEBUG "isdn_keepalive send response %s->%s %d->%d\n",
							   ipnr2buf(ip->daddr),
							   ipnr2buf(ip->saddr),
							   ntohs(tcp->dest),
							   ntohs(tcp->source));

					memset((void *) ntcp, 0, sizeof(*ntcp));
					psh = (struct PSH *) (((u_char *) ntcp) - sizeof(*psh));
					psh->saddr = ip->daddr;
					psh->daddr = ip->saddr;
					psh->zp[0] = 0;
					psh->zp[1] = 6;
					psh->len = htons(sizeof(*tcp));

					ntcp->source = tcp->dest;
					ntcp->dest = tcp->source;
					ntcp->seq = tcp->ack_seq;
					ntcp->ack_seq = htonl(tcp_seqnr + tcp_data_len);
					ntcp->window = tcp->window;
					ntcp->doff = sizeof(*tcp) >> 2;
					ntcp->check = 0;
					ntcp->urg_ptr = 0;
					ntcp->ack = 1;
					ntcp->check = ip_compute_csum((void *) psh, 32);
					memset(iph, 0, sizeof(*iph));
					iph->version = 4;
					iph->ihl = 5;
					iph->tos = ip->tos;
					iph->tot_len = htons(sizeof(*iph) + sizeof(*ntcp));
					iph->id = (u_short) (jiffies & 0xFFFF);
					iph->frag_off = 0;
					iph->ttl = IPDEFTTL;
					iph->protocol = ip->protocol;
					iph->daddr = ip->saddr;
					iph->saddr = ip->daddr;
					iph->check = ip_compute_csum((void *) iph, sizeof(*iph));
					nskb->dev = ndev;
					nskb->mac.raw = nskb->data;
					nskb->protocol = htons(ETH_P_IP);
					nskb->pkt_type = PACKET_HOST;
					netif_rx(nskb);
					retw = 1;

				} else {

					printk(KERN_DEBUG
						   "isdn_keepalive no space for new skb\n");
				}

			} else {

#ifdef KEEPALIVE_VERBOSE
				if (VERBLEVEL) {

					printk(KERN_DEBUG
						   "isdn_keepalive jiffies %lu %lu\n",
						   jiffies, nb->tcpm_time);
				}
#endif
			}

		} else {

#ifdef KEEPALIVE_VERBOSE
			if (VERBLEVEL) {

				printk(KERN_DEBUG
					   "isdn_keepalive no keep  nb->tcpm_seqnr %lu tcp->seqnr %lu dlen %d nb->ack %lu tcp->ack %lu nm->window %d tcp->window %d\n",
					   nb->tcpm_seqnr,
					   tcp_seqnr,
					   tcp_data_len,
					   ntohl(nb->tcpm_acknr),
					   ntohl(tcp->ack_seq),
					   ntohs(nb->tcpm_window),
					   ntohs(tcp->window));
			}
#endif
		}

	} else {

#ifdef KEEPALIVE_VERBOSE
		if (VERBLEVEL) {

			printk(KERN_DEBUG
				   "isdn_keepalive datenlen %d ack==seq %d win==win %d\n",
				   tcp_data_len,
				   nb->tcpm_acknr == tcp->ack_seq,
				   nb->tcpm_window == tcp->window);
		}
#endif
	}

	nb->tcpm_seqnr = tcp_seqnr + tcp_data_len;
	nb->tcpm_acknr = tcp->ack_seq;
	nb->tcpm_window = tcp->window;
	nb->tcpm_time = jiffies;

	if (nb->tcpm_prev != NULL) {

		nb->tcpm_prev->tcpm_next = nb->tcpm_next;

		if (nb->tcpm_next != NULL)
			nb->tcpm_next->tcpm_prev = nb->tcpm_prev;
		else
			tcp_last = nb->tcpm_prev;

		if ((nb->tcpm_next = tcp_first) != NULL)
			nb->tcpm_next->tcpm_prev = nb;
		else
			tcp_last = nb;

		tcp_first = nb;
		nb->tcpm_prev = NULL;
	}

	return (retw);
}

static void isdn_tcp_keepalive_init(void)
{
	TKAL_LOCK;
	tcp_first = NULL;
	tcp_last = NULL;
	tcp_free = NULL;
	next_police = 0;
	last_police = 0;
	deadloop = 0;
	memset(MMA,0,sizeof(MMA));
	TKAL_ULOCK;
}

static void isdn_tcp_keepalive_done(void)
{
	int shl;

	TKAL_LOCK;
	tcp_first = NULL;
	tcp_last = NULL;
	tcp_free = NULL;
	next_police = 0;
	last_police = 0;
	for (shl = 0; shl < MAX_MMA && MMA[shl] != NULL; shl++) kfree(MMA[shl]);
	memset(MMA,0,sizeof(MMA));
	TKAL_ULOCK;
}
#endif

#if CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE || CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR
int isdn_dw_abc_ip4_keepalive_test(struct net_device *ndev,struct sk_buff *skb)
{
	int rklen;
	struct iphdr *ip;
	isdn_net_local *lp = NULL;

	if(ndev == NULL) {
#ifndef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
		return(0);
#endif
	} else lp = (isdn_net_local *)ndev->priv;

	if (skb == NULL)
		return (0);

	if(ntohs(skb->protocol) != ETH_P_IP) {

#ifdef KEEPALIVE_VERBOSE
		if(VERBLEVEL)
			printk(KERN_WARNING "ip_isdn_keepalive: protocol != ETH_P_IP\n");
#endif

		return(0);
	}

	rklen = skb->len;
	ip = (struct iphdr *)skb->data;

	if (skb->nh.raw > skb->data && skb->nh.raw < skb->tail) {

		rklen -= (char *)skb->nh.raw - (char *)skb->data;
		ip = (struct iphdr *)skb->nh.raw;
	}

	if (rklen < sizeof(struct iphdr)) {
#ifdef KEEPALIVE_VERBOSE
		if(VERBLEVEL)
			printk(KERN_WARNING "ip_isdn_keepalive: len %d < iphdr\n",
			rklen);
#endif
		return (0);
	}

#ifdef CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR

	if(ndev != NULL && ndev->ip_ptr != NULL && (lp->dw_abc_flags & ISDN_DW_ABC_FLAG_DYNADDR)) {

		struct in_device *indev = (struct in_device *)ndev->ip_ptr;
		struct in_ifaddr *ifaddr = NULL;

		if((ifaddr = indev->ifa_list) != NULL) {

			ulong ipaddr = ifaddr->ifa_local;

#ifdef DYNADDR_VERBOSE
			if(VERBLEVEL)
				printk(KERN_DEBUG 
					"isdn_dynaddr: %s syncpp %d %s->%s %s\n",
					lp->name,
					lp->p_encap == ISDN_NET_ENCAP_SYNCPPP,
					ipnr2buf(ip->saddr),
					ipnr2buf(ip->daddr),
					ipnr2buf(ipaddr));
#endif
			if(ip->saddr ^ ipaddr) {

				isdn_net_log_skb_dwabc(skb,lp,"isdn_dynaddr drop");
				return(1);
			}
		}
	}
#endif

#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE

	if(lp != NULL && (lp->dw_abc_flags & ISDN_DW_ABC_FLAG_NO_TCP_KEEPALIVE))
		return(0);

	if (rklen < (sizeof(struct iphdr) + sizeof(struct tcphdr))) {

#ifdef KEEPALIVE_VERBOSE
			if(VERBLEVEL)
				printk(KERN_WARNING "ip_isdn_keepalive: len %d < \n",skb->len);
#endif
		return (0);
	}

	if(ip->version != 4 ) {

#ifdef KEEPALIVE_VERBOSE
		if(VERBLEVEL)
			printk(KERN_WARNING
				"ip_isdn_keepalive: ipversion %d != 4\n",
				ip->version);
#endif

		return(0);
	}

	if(ip->protocol != IPPROTO_TCP) {

#ifdef KEEPALIVE_VERBOSE
		if(VERBLEVEL)
			printk(KERN_WARNING
				"ip_isdn_keepalive: ip->proto %d != IPPROTO_TCP\n",
				ip->protocol);
#endif

		return(0);
	}

	if(deadloop) {

		if(deadloop < 10) {
			printk(KERN_WARNING "ip_isdn_keepalive: sorry deadloop detected\n");
			deadloop++;
		}

		return(0);
	}

	{
		int retw = 0;
		TKAL_LOCK;
		retw = isdn_tcpipv4_test(ndev,ip,skb);
		TKAL_ULOCK;
		return(retw);
	}
#else
	return(0);
#endif
}
#endif

void isdn_dw_abc_init_func(void)
{

#ifdef COMPAT_HAS_NEW_WAITQ
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
	init_MUTEX(&lcr_sema);
#endif
#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
	init_MUTEX(&ipv4keep_sema);
#endif
#else
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
	lcr_sema = MUTEX;
#endif
#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
	ipv4keep_sema = MUTEX;
#endif
#endif

#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
	lcr_open_count = 0;
#endif

#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
	isdn_tcp_keepalive_init();
#endif

	init_timer(&dw_abc_timer);
	dw_abc_timer.function = dw_abc_timer_func;
	dw_abc_timer_running = 0;

	printk( KERN_INFO
		"abc-extension %s\n"
		"written by\nDetlef Wengorz <detlefw@isdn4linux.de>\n"
		"Thanks for test's etc. to:\n"
		"Mario Schugowski <mario@mediatronix.de>\n"
		"Installed options:\n"
#ifdef CONFIG_ISDN_WITH_ABC_CALLB
		"CONFIG_ISDN_WITH_ABC_CALLB\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK
		"CONFIG_ISDN_WITH_ABC_UDP_CHECK\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK_HANGUP
		"CONFIG_ISDN_WITH_ABC_UDP_CHECK_HANGUP\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK_DIAL
		"CONFIG_ISDN_WITH_ABC_UDP_CHECK_DIAL\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
		"CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
		"CONFIG_ISDN_WITH_ABC_LCR_SUPPORT\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
		"CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE\n"
#ifdef CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR
		"CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR\n"
#endif
#endif
#ifdef CONFIG_ISDN_WITH_ABC_RCV_NO_HUPTIMER
		"CONFIG_ISDN_WITH_ABC_RCV_NO_HUPTIMER\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_CH_EXTINUSE
		"CONFIG_ISDN_WITH_ABC_CH_EXTINUSE\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_CONN_ERROR
		"CONFIG_ISDN_WITH_ABC_CONN_ERROR\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_RAWIPCOMPRESS
		"CONFIG_ISDN_WITH_ABC_RAWIPCOMPRESS\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_FRAME_LIMIT
		"CONFIG_ISDN_WITH_ABC_FRAME_LIMIT\n"
#endif
		"loaded\n",
		dwabcrevison);
}

void isdn_dw_abc_release_func(void)
{
	del_timer(&dw_abc_timer);
	dw_abc_timer_running = 1;
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
	dw_lcr_clear_all();
#endif
#ifdef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
	isdn_tcp_keepalive_done();
#endif

	printk( KERN_INFO
		"abc-extension %s\n"
		"Thanks for test's etc. to:\n"
		"Mario Schugowski <mario@mediatronix.de>\n"
		"written by\n"
		"Detlef Wengorz <detlefw@isdn4linux.de>\n"
		"unloaded\n",
		dwabcrevison);
}


void isdn_dwabc_test_phone(isdn_net_local *lp) 
{
	if(lp != NULL) {

		isdn_net_phone *h = lp->phone[0];
		ulong oflags = lp->dw_abc_flags;
		int secure = 0;

		lp->dw_abc_flags = 0;
#ifdef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
		*lp->dw_out_msn = 0;
#endif

		for(;h != NULL && secure < 1000;secure++,h = h->next) {

			char *p 	= 	h->num;
			char *ep 	= 	p + ISDN_MSNLEN;

			for(;p < ep && *p && (*p <= ' ' || *p == '"' || *p == '\'');p++);

			if(p >= ep)
				continue;

#ifdef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
			if(*p == '>') {

				if(++p < ep && *p != '<' && *p != '>') {

					char *d = lp->dw_out_msn;

					for(;*p && (p < ep) && (*p == ' ' || *p == '\t');p++);
					for(ep--;*p && (p < ep);) *(d++) = *(p++);
					*d = 0;
					continue;
				}
			}
#endif

			if(*p == '~') {

				/* abc switch's */

				for(p++;p < ep && *p;p++) switch(*p) {
				case 'k':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_NO_TCP_KEEPALIVE;		break;
				case 'u':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_NO_UDP_CHECK;			break;
				case 'h':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_NO_UDP_HANGUP;			break;
				case 'd':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_NO_UDP_DIAL;			break;
				case 'X':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_RCV_NO_HUPTIMER;		break;
				case 'c':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_NO_CH_EXTINUSE;		break;
				case 'e':   lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_NO_CONN_ERROR;			break;

				case 'D':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_DYNADDR;				break;
				case 'B':	lp->dw_abc_flags |= ISDN_DW_ABC_FLAG_BSD_COMPRESS;			break;

				case '"':
				case ' ':
				case '\'':	break;

				default:	
					printk(KERN_DEBUG"isdn_net: %s abc-switch <~%c> unknown\n",lp->name,*p);
					break;
				}
			}
		}

		if(dev->net_verbose  && (lp->dw_abc_flags != oflags || dev->net_verbose > 4))
			printk(KERN_DEBUG "isdn_net %s abc-flags 0x%lx\n",lp->name,lp->dw_abc_flags);

	}
}


#ifdef CONFIG_ISDN_WITH_ABC_ICALL_BIND 

static int get_driverid(isdn_net_local *lp,char *name,char *ename,ulong *bits)
{
	int retw = -1;

	if(name != NULL && lp != NULL) {

		int i = 0;
		char *p;

		for(;name < ename && *name && *name <= ' ' ; name++);
		for(p = name;p < ename && *p && *p != ',';p++);

		for (i = 0; i < ISDN_MAX_DRIVERS; i++) {

			char *s = name;
			char *d = dev->drvid[i];

			for(;s < p && *s == *d && *s;s++,d++);

			if(!*d && s >= p) 
				break;
		}

		if(i >= ISDN_MAX_DRIVERS) {

			printk(KERN_DEBUG "isdn_dwabc_bind %s interface %s not found\n",
				lp->name,name);

		} else {
		
			retw = i;

			if(bits != NULL) {

				char buf[16];
				char *d = buf;
				char *ed = buf + sizeof(buf) - 1;

				*bits = ~0L;

				for(;p < ename && *p == ',';p++);

				while(p < ename && *p && d < ed)
					*(d++) = *(p++);

				*d = 0;

				if(*buf)
					*bits = (ulong)simple_strtoul(buf,&d,0);
			}
		}
	}

	return(retw);
}
	

int isdn_dwabc_check_icall_bind(isdn_net_local *lp,int di,int ch)
{
	int ret = 0;

	if(lp != NULL && lp->pre_device < 0 && lp->pre_channel < 0 && di >= 0 && di < 30) {

		isdn_net_phone *h = lp->phone[0];
		int secure = 0;

		for(;h != NULL && secure < 1000;secure++,h = h->next) {

			char *p 	= 	h->num;
			char *ep 	= 	p + ISDN_MSNLEN;
			ulong bits	=	0;
			
			for(;p < ep && *p && (*p <= ' ' || *p == '"' || *p == '\'');p++);

			if(p >= (ep-1) || *p != '>')
				continue;

			if(*(++p) != '<')
				continue;

			ret = -1;
			p++;

			if(p < ep && (*p == '<' || *p == '>'))
				p++;

			if(get_driverid(lp,p,ep,&bits) == di) {

				if((bits & (1L << ch))) {

					ret = 0;
					break;
				}
			}
		}

		if(ret) {

			printk(KERN_DEBUG "isdn_dwabc_ibind: %s IN-CALL driver %d ch %d %s\n",
				lp->name,
				di,
				ch,
				"not allowed for this interface and channel");
		}
	}

	return(ret);
}

int dwabc_isdn_get_net_free_channel(isdn_net_local *lp) 
{
	int retw = -1;
	int isconf = 0;

	if(lp != NULL && lp->pre_device < 0 && lp->pre_channel < 0) {

		isdn_net_phone *h = lp->phone[0];
		int secure = 0;

		for(;retw < 0 && h != NULL && secure < 1000;secure++,h = h->next) {

			char *p 	= 	h->num;
			char *ep 	= 	p + ISDN_MSNLEN;
			int di		=	0;
			int shl		=	0;
			ulong bits	=	0;
			short down  = 	0;

			for(;p < ep && *p && (*p <= ' ' || *p == '"' || *p == '\'');p++);

			if(p >= (ep-1) || *p != '>') continue;
			if(*(++p) != '>') continue;

			isconf = 1;
			p++;

			if(p < ep && (*p == '<' || *p == '>')) {

				down = *p == '<';
				p++;
			}

			if((di = get_driverid(lp,p,ep,&bits)) < 0)
				continue;

			if(down) for(shl = 31; shl >= 0  && retw < 0; shl--) {

				if(bits & (1L << shl)) {

					if(isdn_dc2minor(di,shl) < 0)
						continue;

					retw = isdn_get_free_channel(
							ISDN_USAGE_NET,
							lp->l2_proto,
							lp->l3_proto,
							di,
							shl);
				}

			} else for(shl = 0; shl < 32 && retw < 0; shl++) {

				if(bits & (1L << shl)) {

					if(isdn_dc2minor(di,shl) < 0)
						break;

					retw = isdn_get_free_channel(
							ISDN_USAGE_NET,
							lp->l2_proto,
							lp->l3_proto,
							di,
							shl);
				}
			}
		}
	}

	if(!isconf) {

		retw = isdn_get_free_channel(
				ISDN_USAGE_NET,
				lp->l2_proto,
				lp->l3_proto,
				lp->pre_device,
				lp->pre_channel);
	}

	return(retw);
}
#endif

int isdn_dw_abc_reset_interface(isdn_net_local *lp,int with_message)
{
	int r = -EINVAL;

	if(lp != NULL) {

		r = 0;

		lp->dw_abc_bchan_last_connect = 0;
		lp->dw_abc_dialstart = 0;
		lp->dw_abc_inuse_secure = 0;
#ifdef CONFIG_ISDN_WITH_ABC_CONN_ERROR
		lp->dw_abc_bchan_errcnt = 0;
#endif

		if(with_message && dev->net_verbose > 0)
			printk(KERN_INFO
				"%s: NOTE: reset (clear) abc-interface-secure-counter\n",
				lp->name);
	}

	return(r);
}

	
#if CONFIG_ISDN_WITH_ABC_RAWIPCOMPRESS && CONFIG_ISDN_PPP

#define DWBSD_PKT_FIRST_LEN 16
#define DWBSD_PKT_SWITCH	165
#define DWBSD_PKT_BSD		189

#define DWBSD_VERSION 		0x1

void dwabc_bsd_first_gen(isdn_net_local *lp)
{
	if(lp != NULL && lp->p_encap == ISDN_NET_ENCAP_RAWIP && 
		(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_BSD_COMPRESS)) { 
		
		struct sk_buff *skb = NULL;
		char *p = NULL;
		char *ep = NULL;

		if(lp->dw_abc_next_skb != NULL)
			dev_kfree_skb(lp->dw_abc_next_skb);

		lp->dw_abc_next_skb = NULL;

		if((skb =(struct sk_buff *)dev_alloc_skb(128)) == NULL) {

			printk(KERN_INFO "%s: dwabc: alloc-skb failed for 128 bytes\n",lp->name);
			return;
		}

		skb_reserve(skb,64);
		p = skb_put(skb,DWBSD_PKT_FIRST_LEN);
		ep = p + DWBSD_PKT_FIRST_LEN;

		*(p++) = DWBSD_PKT_SWITCH;
		*(p++) = DWBSD_VERSION;
		for(;p < ep;p++)	*(p++) = 0;
		lp->dw_abc_next_skb = skb;

		if(dev->net_verbose > 2)
			printk(KERN_INFO "%s: dwabc: sending comm-header version 0x%x\n",lp->name,DWBSD_VERSION);
	}
}


void dwabc_bsd_free(isdn_net_local *lp)
{
	if(lp != NULL) {

		if(lp->dw_abc_bsd_stat_rx || lp->dw_abc_bsd_stat_tx) {

			struct isdn_ppp_compressor *c = NULL;

			if(!(c = (struct isdn_ppp_compressor *)lp->dw_abc_bsd_compressor)) {

				printk(KERN_WARNING
					"%s: PANIC: freeing bsd compressmemory without compressor\n",
					lp->name);

			} else {

				if(lp->dw_abc_bsd_stat_rx) (*c->free)(lp->dw_abc_bsd_stat_rx);
				if(lp->dw_abc_bsd_stat_tx) (*c->free)(lp->dw_abc_bsd_stat_tx);

				if(dev->net_verbose > 2)
					printk(KERN_INFO "%s: free bsd compress-memory\n",lp->name);
			}
		}

		lp->dw_abc_bsd_compressor = NULL;
		lp->dw_abc_bsd_stat_rx = NULL;
		lp->dw_abc_bsd_stat_tx = NULL;
		lp->dw_abc_if_flags &= ~ISDN_DW_ABC_IFFLAG_BSDAKTIV;

		if(dev->net_verbose > 0) {

			if(lp->dw_abc_bsd_rcv != lp->dw_abc_bsd_bsd_rcv) {

				printk(KERN_INFO "%s: Receive %lu<-%lu kb\n",lp->name,
					lp->dw_abc_bsd_rcv >> 10 , lp->dw_abc_bsd_bsd_rcv >> 10);
			}


			if(lp->dw_abc_bsd_snd != lp->dw_abc_bsd_bsd_snd) {

				printk(KERN_INFO "%s: Send  %lu->%lu kb\n",lp->name,
					lp->dw_abc_bsd_snd >> 10 , lp->dw_abc_bsd_bsd_snd >> 10);
			}
		}

		lp->dw_abc_bsd_rcv 		=
		lp->dw_abc_bsd_bsd_rcv	=
		lp->dw_abc_bsd_snd 		=
		lp->dw_abc_bsd_bsd_snd 	= 0;
		atomic_set(&lp->dw_abc_pkt_onl,0);

		if(lp->dw_abc_next_skb) {

			dev_kfree_skb(lp->dw_abc_next_skb);
			lp->dw_abc_next_skb = NULL;
		}
	}
}


int dwabc_bsd_init(isdn_net_local *lp)
{
	int r = 1;

	if(lp != NULL) {

		dwabc_bsd_free(lp);

		if(lp->p_encap == ISDN_NET_ENCAP_RAWIP) {

			if(lp->l2_proto == ISDN_PROTO_L2_X75I) {

				if(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_BSD_COMPRESS) {

					ulong flags = 0;
					struct isdn_ppp_compressor *c = ipc_head;

					save_flags(flags);
					cli();
					for(;c != NULL && c->num != CI_BSD_COMPRESS; c = c->next);

					if(c == NULL) {

						printk(KERN_INFO "%s: Module isdn_bsdcompress not loaded\n",lp->name);
						r = -1;
					
					} else {

						void *rx = NULL;
						void *tx = NULL;
						struct isdn_ppp_comp_data *cp = &BSD_COMP_INIT_DATA;

						memset(cp,0,sizeof(*cp));
						cp->num = CI_BSD_COMPRESS;
						cp->optlen = 1;
						
						/*
						** set BSD_VERSION 1 and 12 bits compressmode
						*/
						*cp->options = (1 << 5) | 12;

						if((rx = (*c->alloc)(cp)) == NULL) {

							printk(KERN_INFO "%s: allocation of bsd rx-memory failed\n",lp->name);
							r = -1;

						} else if(!(*c->init)(rx,cp,0,1)) {

							printk(KERN_INFO "%s: init of bsd rx-stream  failed\n",lp->name);
							(*c->free)(rx);
							rx = NULL;
						}

						if(rx != NULL) {

							cp->flags = IPPP_COMP_FLAG_XMIT;
							
							if((tx = (*c->alloc)(cp)) == NULL) {

								printk(KERN_INFO
									"%s: allocation of bsd tx-memory failed\n",lp->name);
								r = -1;

							} else if(!(*c->init)(tx,cp,0,1)) {

								printk(KERN_INFO "%s: init of bsd tx-stream  failed\n",lp->name);
								(*c->free)(tx);
								tx = NULL;
							}
						}

						if(tx != NULL) {

							lp->dw_abc_bsd_compressor = (void *)c;
							lp->dw_abc_bsd_stat_rx = rx;
							lp->dw_abc_bsd_stat_tx = tx;
							r = 0;

							if(dev->net_verbose > 2)
								printk(KERN_INFO "%s: bsd compress-memory and init ok\n",lp->name);
						}
					}

					restore_flags(flags);
				}

			} else if(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_BSD_COMPRESS) {
			
				printk(KERN_INFO "%s: bsd-compress only with L2-Protocol x75i allowed\n",lp->name);
			}

		} else if(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_BSD_COMPRESS) {
		
			printk(KERN_INFO "%s: bsd-compress only with encapsulation rawip allowed\n",lp->name);
		}
	}

	return(r);
}

struct sk_buff *dwabc_bsd_compress(isdn_net_local *lp,struct sk_buff *skb,struct net_device *ndev)
{
	if(lp != NULL && lp->p_encap == ISDN_NET_ENCAP_RAWIP 	&& 
		(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_BSD_COMPRESS)	&&
		(lp->dw_abc_if_flags & ISDN_DW_ABC_IFFLAG_BSDAKTIV)) {

		if(lp->dw_abc_bsd_stat_tx != NULL && lp->dw_abc_bsd_compressor) {

			struct isdn_ppp_compressor *cp = (struct isdn_ppp_compressor *)lp->dw_abc_bsd_compressor;
			struct sk_buff *nskb = (struct sk_buff *)dev_alloc_skb(skb->len * 2 + ndev->hard_header_len);
			int l = 0;

			if(nskb == NULL) {

				(void)(*cp->reset)(lp->dw_abc_bsd_stat_tx,0,0,NULL,0,NULL);
				printk(KERN_INFO "%s: dwabc-compress no memory\n",lp->name);

			} else {

				skb_reserve(nskb,ndev->hard_header_len);
				*(unsigned char *)skb_put(nskb,1) = DWBSD_PKT_BSD;
				l = (*cp->compress)(lp->dw_abc_bsd_stat_tx,skb,nskb,0x21);

				if(l < 1 || l > skb->len) {

					(void)(*cp->reset)(lp->dw_abc_bsd_stat_tx,0,0,NULL,0,NULL);
					dev_kfree_skb(nskb);

				} else {

					dev_kfree_skb(skb);
					skb = nskb;
				}
			}
		}
	}
	return(skb);
}

struct sk_buff *dwabc_bsd_rx_pkt(isdn_net_local *lp,struct sk_buff *skb,struct net_device *ndev)
{
	struct sk_buff *r = skb;

	if(lp != NULL && lp->p_encap == ISDN_NET_ENCAP_RAWIP && 
		(lp->dw_abc_flags & ISDN_DW_ABC_FLAG_BSD_COMPRESS)) { 

		unsigned char *p = (unsigned char *)skb->data;
		struct isdn_ppp_compressor *cp = (struct isdn_ppp_compressor *)lp->dw_abc_bsd_compressor;

		if(*p == DWBSD_PKT_SWITCH) {

			if(skb->len == DWBSD_PKT_FIRST_LEN) {

				lp->dw_abc_remote_version = p[1];
				lp->dw_abc_if_flags |= ISDN_DW_ABC_IFFLAG_BSDAKTIV;
				kfree_skb(skb);

				if(dev->net_verbose > 2)
					printk(KERN_INFO "%s: receive comm-header rem-version 0x%02x\n",
						lp->name,lp->dw_abc_remote_version);

				return(NULL);
			}

		} else if(*p != DWBSD_PKT_BSD) {

			if(lp->dw_abc_bsd_stat_rx != NULL && cp && lp->dw_abc_bsd_is_rx) {
				
				(void)(*cp->reset)(lp->dw_abc_bsd_stat_rx,0,0,NULL,0,NULL);
				lp->dw_abc_bsd_is_rx = 0;
			}

		} else if(lp->dw_abc_bsd_stat_rx != NULL && cp) {

			struct sk_buff *nskb = NULL;

			if(test_and_set_bit(ISDN_DW_ABC_BITLOCK_RECEIVE,&lp->dw_abc_bitlocks)) {

				printk(KERN_INFO "%s: bsd-decomp called recursivly\n",lp->name);
				kfree_skb(skb);
				return(NULL);
			} 
			
			nskb = (struct sk_buff *)dev_alloc_skb(2048 + ndev->hard_header_len);

			if(nskb != NULL) {

				int l = 0;

				skb_reserve(nskb,ndev->hard_header_len);
				skb_pull(skb, 1);

				if((l = (*cp->decompress)(lp->dw_abc_bsd_stat_rx,skb,nskb,NULL)) < 1 || l>8000) {

					printk(KERN_INFO "%s: abc-decomp failed\n",lp->name);
					dev_kfree_skb(nskb);
					dev_kfree_skb(skb);
					nskb = NULL;

				} else {

					if (nskb->data[0] & 0x1)
						skb_pull(nskb, 1);   /* protocol ID is only 8 bit */
					else
						skb_pull(nskb, 2);

					nskb->dev = skb->dev;
					nskb->pkt_type = skb->pkt_type;
					nskb->mac.raw = nskb->data;
					dev_kfree_skb(skb);
					lp->dw_abc_bsd_is_rx = 1;
				}

			} else {

				printk(KERN_INFO "%s: PANIC abc-decomp no memory\n",lp->name);
				dev_kfree_skb(skb);
			}

			clear_bit(ISDN_DW_ABC_BITLOCK_RECEIVE,&lp->dw_abc_bitlocks);
			r = nskb;
		}
	}

	return(r);
}

#else
int dwabc_bsd_init(isdn_net_local *lp) { return(1); }
void dwabc_bsd_free(isdn_net_local *lp) { return; }
void dwabc_bsd_first_gen(isdn_net_local *lp) { return ; }

struct sk_buff *dwabc_bsd_compress(isdn_net_local *lp,struct sk_buff *skb,struct net_device *ndev)
{ return(skb); }

struct sk_buff *dwabc_bsd_rx_pkt(isdn_net_local *lp,struct sk_buff *skb,struct net_device *ndev)
{ return(skb); }
#endif
#endif
