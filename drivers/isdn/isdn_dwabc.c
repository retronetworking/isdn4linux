
/* $Id$

 * Linux ISDN subsystem, abc-extension releated funktions.
 *
 * Author: abc GmbH written by Detlef Wengorz <detlefw@isdn4linux.de>
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
#include <net/udp.h>
#include <net/checksum.h>
#include <linux/isdn_dwabc.h>

#define NBYTEORDER_30BYTES      0x1e00 
#define DWABC_TMRES (HZ)

static struct timer_list dw_abc_timer;
static volatile short dw_abc_timer_running = 0;
static volatile short dw_abc_timer_need = 0;


static struct semaphore lcr_sema;
#define LCR_LOCK() down(&lcr_sema);
#define LCR_ULOCK() up(&lcr_sema);


#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT

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


/***********************************8
static void start_timer(void)
{
	dw_abc_timer_need = 1;

	if(!dw_abc_timer_running) {

		dw_abc_timer_running = 1;
		add_timer(&dw_abc_timer);
	}
}
*************************************/


static int myjiftime(char *p,u_long nj)
{
	sprintf(p,"%02ld:%02ld.%02ld",
		((nj / 100) / 60) % 100, (nj / 100) % 60,nj % 100);

	return(8);
}



#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT

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
			printk(KERN_INFO "%s %d : LCR no memory\n",__FILE__,__LINE__);
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

			printk(KERN_INFO "%s %d : LCR no memory\n",__FILE__,__LINE__);
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

				printk(KERN_INFO "%s %d : no memory\n",__FILE__,__LINE__);

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
		if(skb->len >= 20 && iph->version == 4) {
			if(	iph->tot_len == NBYTEORDER_30BYTES	&& iph->protocol == IPPROTO_UDP) {
				struct udphdr *udp = (struct udphdr *)(skb->data + (iph->ihl << 2));
				if(udp->dest == htons(25001) && udp->source >= htons(20000) && udp->source < htons(25000)) {
					char *p = (char *)(udp + 1);
					if(p[0] == p[1]) {
						char mc = 0;
						switch(*p) {
						case 0x11:
							mc = *p + 1;
							/**********
							lp->conn_start =
							lp->conn_count =
							lp->anz_conf_err = 0;
							lp->last_pkt_rcv = jiffies;
							*************/
							break;
						case 0x28:	mc = *p + 1;	break;
						case 0x2a:
						case 0x2c:
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK_HANGUP
							{
								ulong flags;
								mc = *p;
								save_flags(flags);
								cli();

								if(lp->dialstate == 20) {

									lp->dialstate = 0;

									if(lp->first_skb) {

										dev_kfree_skb(lp->first_skb);
										lp->first_skb = NULL;
									}

									mc = *p + 1;
								}

								restore_flags(flags);

								if(lp->isdn_device >= 0) {

									isdn_net_hangup(ndev);
									mc = *p + 1;
								}
							}
#else
							printk(KERN_DEBUG "%s: UDP-INFO-HANGUP not supportet\n",
								lp->name);
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


void isdn_dw_abc_init_func(void)
{

#ifdef COMPAT_HAS_NEW_WAITQ
	init_MUTEX(&lcr_sema);
#else
	lcr_sema = MUTEX;
#endif


#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
	lcr_open_count = 0;
#endif

	init_timer(&dw_abc_timer);
	dw_abc_timer.function = dw_abc_timer_func;
	dw_abc_timer_running = 0;

	printk( KERN_INFO
		"abc-extension %s\n"
		"written by\nDetlef Wengorz <detlefw@isdn4linux.de>\n"
		"Installed options:\n"
#ifdef CONFIG_ISDN_WITH_ABC_CALLB
		"CONFIG_ISDN_WITH_ABC_CALLB\n"
#endif
#ifdef  CONFIG_ISDN_WITH_ABC_CALL_CHECK_SYNCRO
		"CONFIG_ISDN_WITH_ABC_CALL_CHECK_SYNCRO\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK
		"CONFIG_ISDN_WITH_ABC_UDP_CHECK\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
		"CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ\n"
#endif
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
		"CONFIG_ISDN_WITH_ABC_LCR_SUPPORT\n"
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

	printk( KERN_INFO
		"abc-extension %s\n"
		"written by\nDetlef Wengorz <detlefw@isdn4linux.de>\n"
		"unloaded\n",
		dwabcrevison);
}

#endif
