/* $Id$
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * Revision 1.1  1996/01/09 04:12:34  fritz
 * Initial revision
 *
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include <linux/if_arp.h>
#include "isdn_common.h"
#include "isdn_net.h"
#ifdef CONFIG_ISDN_PPP
#include "isdn_ppp.h"
#endif

/* Prototypes */
int isdn_net_force_dial_lp(isdn_net_local *);
static int isdn_net_wildmat(char *s, char *p);
static int isdn_net_start_xmit(struct sk_buff *skb, struct device *ndev);

char *isdn_net_revision = "$Revision$";

 /*
  * Code for raw-networking over ISDN
  */

static void
isdn_net_reset(struct device *dev)
{
	ulong flags;

	save_flags(flags);
	cli();			/* Avoid glitch on writes to CMD regs */
	dev->interrupt = 0;
	dev->tbusy = 0;
	restore_flags(flags);
}

/* Open/initialize the board. */
static int
isdn_net_open(struct device *dev)
{
	int i;
	struct device *p;

	isdn_net_reset(dev);
	dev->start = 1;
	/* Fill in the MAC-level header. */
	for (i = 0; i < ETH_ALEN - sizeof(ulong); i++)
		dev->dev_addr[i] = 0xfc;
	memcpy(&(dev->dev_addr[i]), &dev->pa_addr, sizeof(ulong));
	if ((p = (((isdn_net_local *) dev->priv)->slave))) {
		/* If this interface has slaves, start them also */
		while (p) {
			isdn_net_reset(p);
			p->start = 1;
			p = (((isdn_net_local *) p->priv)->slave);
		}
	}
	isdn_MOD_INC_USE_COUNT();
	return 0;
}

/*
 * Assign an ISDN-channel to a net-interface
 */
static void
isdn_net_bind_channel(isdn_net_local * lp, int idx)
{
	lp->isdn_device = dev->drvmap[idx];
	lp->isdn_channel = dev->chanmap[idx];
}

/*
 * Perform auto-hangup and cps-calculation for net-interfaces.
 *
 * auto-hangup:
 * Increment idle-counter (this counter is reset on any incoming or
 * outgoing packet), if counter exceeds configured limit either do a
 * hangup immediately or - if configured - wait until just before the next
 * charge-info.
 *
 * cps-calculation (needed for dynamic channel-bundling):
 * Since this function is called every second, simply reset the
 * byte-counter of the interface after copying it to the cps-variable.
 */
void
isdn_net_autohup()
{
	isdn_net_dev *p = dev->netdev;
	ulong flags;

	save_flags(flags);
	cli();
	while (p) {
		isdn_net_local *l = (isdn_net_local *) & (p->local);
		l->cps = l->transcount;
		l->transcount = 0;
		if (dev->net_verbose > 3)
			printk(KERN_DEBUG "%s: %d bogocps\n", l->name, l->cps);
		if ((l->flags & ISDN_NET_CONNECTED) && (!l->dialstate)) {
			l->huptimer++;
			if ((l->onhtime) && (l->huptimer > l->onhtime))
				if (l->outgoing) {
					if (l->hupflags & 4) {
						if (l->hupflags & 1)
							isdn_net_hangup(&p->dev);
						else if (jiffies - l->chargetime > l->chargeint)
							isdn_net_hangup(&p->dev);
					} else
						isdn_net_hangup(&p->dev);
				} else if (l->hupflags & 8)
					isdn_net_hangup(&p->dev);
		}
		p = (isdn_net_dev *) p->next;
	}
	restore_flags(flags);
}

/*
 * Handle status-messages from ISDN-interfacecard.
 * This function is called from within the main-status-dispatcher
 * isdn_status_callback, which itself is called from the lowlevel-driver.
 */
void
isdn_net_stat_callback(int di, int ch, int cmd)
{
	isdn_net_dev *p = dev->netdev;
	ulong flags;

	save_flags(flags);
	cli();
	while (p) {
		if (p->local.isdn_device == di && p->local.isdn_channel == ch)
			switch (cmd) {
			case ISDN_STAT_BSENT:
				/* A packet has successfully been sent out */
				if ((p->local.flags & ISDN_NET_CONNECTED) &&
				    (!p->local.dialstate)) {
					p->local.stats.tx_packets++;
                                        p->dev.tbusy = 0;
                                        mark_bh(NET_BH);
				}
				break;
			case ISDN_STAT_DCONN:
				/* D-Channel is up */
				if (p->local.dialstate == 4 || p->local.dialstate == 7)
					p->local.dialstate++;
				break;
			case ISDN_STAT_DHUP:
				/* Either D-Channel-hangup or error during dialout */
				if ((!p->local.dialstate) && (p->local.flags & ISDN_NET_CONNECTED)) {
					p->local.flags &= ~ISDN_NET_CONNECTED;
					isdn_free_channel(p->local.isdn_device, p->local.isdn_channel,
						     ISDN_USAGE_NET);
#ifdef CONFIG_ISDN_PPP
					isdn_ppp_free(&p->local);
#endif
					isdn_all_eaz(p->local.isdn_device, p->local.isdn_channel);
					printk(KERN_INFO "%s: remote hangup\n", p->local.name);
					printk(KERN_INFO "%s: Chargesum is %d\n", p->local.name,
					       p->local.charge);
					p->local.isdn_device = -1;
					p->local.isdn_channel = -1;
				}
				break;
			case ISDN_STAT_BCONN:
				/* B-Channel is up */
				if (p->local.dialstate >= 5 && p->local.dialstate <= 9) {
					if (p->local.dialstate <= 6) {
						int i = isdn_dc2minor(p->local.isdn_device, p->local.isdn_channel);
						if (i >= 0) {
							dev->usage[i] |= ISDN_USAGE_OUTGOING;
							isdn_info_update();
						}
					}
					p->local.dialstate = 0;
					printk(KERN_INFO "isdn_net: %s connected\n", p->local.name);
					/* If first Chargeinfo comes before B-Channel connect,
					 * we correct the timestamp here.
					 */
					p->local.chargetime = jiffies;
				}
				break;
			case ISDN_STAT_NODCH:
				/* No D-Channel avail. */
				if (p->local.dialstate == 4)
					p->local.dialstate--;
				break;
			case ISDN_STAT_CINF:
				/* Charge-info from TelCo. Calculate interval between
				 * charge-infos and set timestamp for last info for
				 * usage by isdn_net_autohup()
				 */
				p->local.charge++;
				if (p->local.hupflags & 2) {
					p->local.hupflags &= ~1;
					p->local.chargeint = jiffies - p->local.chargetime - (2 * HZ);
				}
				if (p->local.hupflags & 1)
					p->local.hupflags |= 2;
				p->local.chargetime = jiffies;
				break;
			}
		p = (isdn_net_dev *) p->next;
	}
	restore_flags(flags);
}

/*
 * Check, if a numer contains wilcard-characters, in which case it
 * is for incoming purposes only.
 */
static int
isdn_net_checkwild(char *num)
{
	return ((strchr(num, '?')) ||
		(strchr(num, '*')) ||
		(strchr(num, '[')) ||
		(strchr(num, ']')) ||
		(strchr(num, '^')));
}

/*
 * Perform dialout for net-interfaces and timeout-handling for
 * D-Channel-up and B-Channel-up Messages.
 * This function is initially called from within isdn_net_start_xmit() or
 * or isdn_net_find_icall() after initializing the dialstate for an
 * interface. If further calls are needed, the function schedules itself
 * for a timer-callback via isdn_timer_function().
 * The dialstate is also affected by incoming status-messages from
 * the ISDN-Channel which are handled in isdn_net_stat_callback() above.
 */
void
isdn_net_dial(void)
{
	isdn_net_dev *p = dev->netdev;
	int anymore = 0;
	int i;
	isdn_ctrl cmd;

	while (p) {
		switch (p->local.dialstate) {
		case 0:
			/* Nothing to do for this interface */
			break;
		case 1:
			/* Initiate dialout. Set phone-number-pointer to first number
			 * of interface.
			 */
			p->local.dial = p->local.phone[1];
			anymore = 1;
			p->local.dialstate++;
			break;
			/* Prepare dialing. Clear EAZ, then set EAZ. */
		case 2:
			cmd.driver = p->local.isdn_device;
			cmd.arg = p->local.isdn_channel;
			cmd.command = ISDN_CMD_CLREAZ;
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			sprintf(cmd.num, "%s", isdn_map_eaz2msn(p->local.msn, cmd.driver));
			cmd.command = ISDN_CMD_SETEAZ;
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			p->local.dialretry = 0;
			anymore = 1;
			p->local.dialstate++;
			break;
		case 3:
			/* Setup interface, dial current phone-number, switch to next number.
			 * If list of phone-numbers is exhausted, increment
			 * retry-counter.
			 */
			cmd.driver = p->local.isdn_device;
			cmd.command = ISDN_CMD_SETL2;
			cmd.arg = p->local.isdn_channel + (p->local.l2_proto << 8);
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			cmd.driver = p->local.isdn_device;
			cmd.command = ISDN_CMD_SETL3;
			cmd.arg = p->local.isdn_channel + (p->local.l3_proto << 8);
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			cmd.driver = p->local.isdn_device;
			cmd.arg = p->local.isdn_channel;
			p->local.huptimer = 0;
			p->local.outgoing = 1;
			p->local.hupflags |= 1;
			if (!strcmp(p->local.dial->num, "LEASED")) {
				p->local.dialstate = 4;
				printk(KERN_INFO "%s: Open leased line ...\n", p->local.name);
			} else {
				cmd.command = ISDN_CMD_DIAL;
				sprintf(cmd.num, "%s,%s,7,0", p->local.dial->num,
				  isdn_map_eaz2msn(p->local.msn, cmd.driver));
				i = isdn_dc2minor(p->local.isdn_device, p->local.isdn_channel);
				if (i >= 0) {
					strcpy(dev->num[i], p->local.dial->num);
					isdn_info_update();
				}
				printk(KERN_INFO "%s: dialing %d %s...\n", p->local.name,
				 p->local.dialretry, p->local.dial->num);
				/*
				 * Switch to next number or back to start if at end of list.
				 */
				if (!(p->local.dial = (isdn_net_phone *) p->local.dial->next)) {
					p->local.dial = p->local.phone[1];
					p->local.dialretry++;
				}
				p->local.dtimer = 0;
#ifdef ISDN_DEBUG_NET_DIAL
				printk(KERN_DEBUG "dial: d=%d c=%d\n", p->local.isdn_device,
				       p->local.isdn_channel);
#endif
				dev->drv[p->local.isdn_device]->interface->command(&cmd);
			}
			anymore = 1;
			p->local.dialstate++;
			break;
		case 4:
			/* Wait for D-Channel-connect or incoming call, if passive
			 * callback configured. If timeout and max retries not
			 * reached, switch back to state 3.
			 */
			if (p->local.dtimer++ > ISDN_TIMER_DTIMEOUT10)
				if (p->local.dialretry < p->local.dialmax) {
					p->local.dialstate = 3;
				} else
					isdn_net_hangup(&p->dev);
			anymore = 1;
			break;
		case 5:
			/* Got D-Channel-Connect, send B-Channel-request */
			cmd.driver = p->local.isdn_device;
			cmd.arg = p->local.isdn_channel;
			cmd.command = ISDN_CMD_ACCEPTB;
			anymore = 1;
			p->local.dtimer = 0;
			p->local.dialstate++;
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			break;
		case 6:
			/* Wait for B-Channel-connect. If timeout, switch back to
			 * state 3.
			 */
#ifdef ISDN_DEBUG_NET_DIAL
			printk(KERN_DEBUG "dialtimer2: %d\n", p->local.dtimer);
#endif
			if (p->local.dtimer++ > ISDN_TIMER_DTIMEOUT10)
				p->local.dialstate = 3;
			anymore = 1;
			break;
		case 7:
			/* Got incoming Call, setup L2 and L3 protocols, send accept,
			   then wait for D-Channel-connect */
#ifdef ISDN_DEBUG_NET_DIAL
			printk(KERN_DEBUG "dialtimer4: %d\n", p->local.dtimer);
#endif
			cmd.driver = p->local.isdn_device;
			cmd.command = ISDN_CMD_SETL2;
			cmd.arg = p->local.isdn_channel + (p->local.l2_proto << 8);
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			cmd.driver = p->local.isdn_device;
			cmd.command = ISDN_CMD_SETL3;
			cmd.arg = p->local.isdn_channel + (p->local.l3_proto << 8);
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			if (p->local.dtimer++ > ISDN_TIMER_DTIMEOUT15)
				isdn_net_hangup(&p->dev);
			else
				anymore = 1;
			break;
		case 8:
			/* Got incoming D-Channel-Connect, send B-Channel-request */
			cmd.driver = p->local.isdn_device;
			cmd.arg = p->local.isdn_channel;
			cmd.command = ISDN_CMD_ACCEPTB;
			dev->drv[p->local.isdn_device]->interface->command(&cmd);
			anymore = 1;
			p->local.dtimer = 0;
			p->local.dialstate++;
			break;
		case 9:
			/*  Wait for B-channel-connect */
#ifdef ISDN_DEBUG_NET_DIAL
			printk(KERN_DEBUG "dialtimer4: %d\n", p->local.dtimer);
#endif
			if (p->local.dtimer++ > ISDN_TIMER_DTIMEOUT10)
				isdn_net_hangup(&p->dev);
			else
				anymore = 1;
			break;
		default:
			printk(KERN_WARNING "isdn_net: Illegal dialstate %d for device %s\n",
			       p->local.dialstate, p->local.name);
		}
		p = (isdn_net_dev *) p->next;
	}
	isdn_timer_ctrl(ISDN_TIMER_NETDIAL, anymore);
}

/*
 * Send-data-helpfunction for net-interfaces
 */
int
isdn_net_send(u_char * buf, int di, int ch, int len)
{
	int l;

	if ((l = dev->drv[di]->interface->writebuf(di, ch, buf, len, 0)) == len)
		return 1;
	/* Device driver queue full (or packet > 4000 bytes, should never
	 * happen)
	 */
	if (l == -EINVAL)
		printk(KERN_ERR "isdn_net: Huh, sending pkt too big!\n");
	return 0;
}

/*
 * Perform hangup for a net-interface.
 */
void
isdn_net_hangup(struct device *d)
{
	isdn_net_local *lp = (isdn_net_local *) d->priv;
	isdn_ctrl cmd;
	ulong flags;

	save_flags(flags);
	cli();
	if (lp->flags & ISDN_NET_CONNECTED) {
		printk(KERN_INFO "isdn_net: local hangup %s\n", lp->name);
		lp->dialstate = 0;
		isdn_free_channel(lp->isdn_device, lp->isdn_channel, ISDN_USAGE_NET);
#ifdef CONFIG_ISDN_PPP
		isdn_ppp_free(lp);
#endif
		lp->flags &= ~ISDN_NET_CONNECTED;
		cmd.driver = lp->isdn_device;
		cmd.command = ISDN_CMD_HANGUP;
		cmd.arg = lp->isdn_channel;
		(void) dev->drv[cmd.driver]->interface->command(&cmd);
		printk(KERN_INFO "%s: Chargesum is %d\n", lp->name, lp->charge);
		isdn_all_eaz(lp->isdn_device, lp->isdn_channel);
		lp->isdn_device = -1;
		lp->isdn_channel = -1;
	}
	restore_flags(flags);
}

typedef struct {
	unsigned short source;
	unsigned short dest;
} ip_ports;

static void
isdn_net_log_packet(u_char * buf, isdn_net_local * lp)
{
	int data_ofs = ((buf[0] & 15) * 4);
	ip_ports *ipp;
	char addinfo[100];
	unsigned short proto;

        /* Open a Connection */
        addinfo[0] = '\0';
        proto = ntohs(*(unsigned short *) buf);
        switch (proto) {
		case ETH_P_IP:
			switch (buf[11]) {
                                case 1:
                                        strcpy(addinfo, " ICMP");
                                        break;
                                case 2:
                                        strcpy(addinfo, " IGMP");
                                        break;
                                case 4:
                                        strcpy(addinfo, " IPIP");
                                        break;
                                case 6:
                                        ipp = (ip_ports *) (&buf[data_ofs]);
                                        sprintf(addinfo, " TCP, port: %d -> %d", ntohs(ipp->source),
                                                ntohs(ipp->dest));
                                        break;
                                case 8:
                                        strcpy(addinfo, " EGP");
                                        break;
                                case 12:
                                        strcpy(addinfo, " PUP");
                                        break;
                                case 17:
                                        ipp = (ip_ports *) (&buf[data_ofs]);
                                        sprintf(addinfo, " UDP, port: %d -> %d", ntohs(ipp->source),
                                                ntohs(ipp->dest));
                                        break;
                                case 22:
                                        strcpy(addinfo, " IDP");
                                        break;
			}
			printk(KERN_INFO "OPEN: %d.%d.%d.%d -> %d.%d.%d.%d%s\n",
			       buf[14], buf[15], buf[16], buf[17],
			       buf[18], buf[19], buf[20], buf[21],
			       addinfo);
			break;
		case ETH_P_ARP:
			printk(KERN_INFO "OPEN: ARP %d.%d.%d.%d -> *.*.*.* ?%d.%d.%d.%d\n",
			       buf[16], buf[17], buf[18], buf[19],
			       buf[26], buf[27], buf[28], buf[29]);
			break;
        }
}

/*
 * Generic routine to send out an skbuf.
 * If lowlevel-device does not support supports skbufs, use
 * standard send-routine, else sind directly.
 *
 * Return: 0 on success, !0 on failure.
 * Side-effects: ndev->tbusy is cleared on success.
 */
int
isdn_net_send_skb(struct device *ndev, isdn_net_local *lp,
                  struct sk_buff *skb)
{
	int ret;
	
	lp->transcount += skb->len;
	if (dev->drv[lp->isdn_device]->interface->writebuf_skb) 
		ret = dev->drv[lp->isdn_device]->interface->
			writebuf_skb(lp->isdn_device, lp->isdn_channel, skb);
	else {	      
		if ((ret = isdn_net_send(skb->data, lp->isdn_device,
                                    lp->isdn_channel, skb->len)))
		        dev_kfree_skb(skb, FREE_WRITE);
	}

	if (ret)
		ndev->tbusy  = 0;
	return (!ret);
}                                      
	

/*
 *  Helper function for isdn_net_start_xmit.
 *  When called, the connection is already established.
 *  Based on cps-calculation, check if device is overloaded.
 *  If so, and if a slave exists, trigger dialing for it.
 *  If any slave is online, deliver packets using a simple round robin
 *  scheme.
 *
 *  Return: 0 on success, !0 on failure.
 */

static int
isdn_net_xmit(struct device *ndev, isdn_net_local *lp, 
                         struct sk_buff *skb) 
{
        int ret;

	/* For the other encaps the header has allready been built */
#ifdef CONFIG_ISDN_PPP
	if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
		return (isdn_ppp_xmit(skb, ndev));
#endif		
	/* Reset hangup-timeout */
	lp->huptimer = 0;

	if (lp->cps > 7000) {
		/* Device overloaded */

		/* 
		 * Packet-delivery via round-robin over master 
		 * and all connected slaves.
		 */
		if (lp->master)
			/* Slaves always deliver themselves */
			ret = isdn_net_send_skb(ndev, lp, skb);
		else {
			isdn_net_local *slp = (isdn_net_local *) (lp->srobin->priv);
			/* Master delivers via srobin and maintains srobin */
			if (lp->srobin == ndev)
				ret = isdn_net_send_skb(ndev, lp, skb);
			else
				ret = ndev->tbusy = isdn_net_start_xmit(skb, lp->srobin);
			lp->srobin = (slp->slave) ? slp->slave : ndev;
			slp = (isdn_net_local *) (lp->srobin->priv);
			if (!((slp->flags & ISDN_NET_CONNECTED) && (slp->dialstate == 0)))
				lp->srobin = ndev;
		}
		/* Slave-startup using delay-variable */
		if (lp->slave) {
			if (!lp->sqfull) {
				/* First time overload: set timestamp only */
				lp->sqfull = 1;
				lp->sqfull_stamp = jiffies;
			} 
			else {
				/* subsequent overload: if slavedelay exceeded, start dialing */
				if ((jiffies - lp->sqfull_stamp) > lp->slavedelay)
					isdn_net_force_dial_lp((isdn_net_local *) lp->slave->priv);
			}
		}
	} 
	else {
		/* Not overloaded, deliver locally */
		ret = isdn_net_send_skb(ndev, lp, skb);
		if (lp->sqfull && ((jiffies - lp->sqfull_stamp) > (lp->slavedelay + (10*HZ) )))
			lp->sqfull = 0;
	}
	return ret;
}

/*
 * Try sending a packet.
 * If this interface isn't connected to a ISDN-Channel, find a free channel,
 * and start dialing.
 */
int
isdn_net_start_xmit(struct sk_buff *skb, struct device *ndev)
{
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;


	if (ndev->tbusy) {
		if (jiffies - ndev->trans_start < 20) {
			return 1;
		}
		lp->stats.tx_errors++;
		ndev->tbusy = 0;
		ndev->trans_start = jiffies;
	}
	if (skb == NULL) {
		dev_tint(ndev);
		return 0;
	}
	/* Avoid timer-based retransmission conflicts. */
	if (set_bit(0, (void *) &ndev->tbusy) != 0)
		printk(KERN_WARNING
                       "%s: Transmitter access conflict.\n",
                       ndev->name);
	else {
		u_char *buf = skb->data;
#ifdef ISDN_DEBUG_NET_DUMP
		isdn_dumppkt("S:", buf, skb->len, 40);
#endif
		if (!(lp->flags & ISDN_NET_CONNECTED)) {
			int chi;
			if (lp->phone[1]) {
				ulong flags;
				save_flags(flags);
				cli();
				/* Grab a free ISDN-Channel */
				if ((chi = 
                                     isdn_get_free_channel(ISDN_USAGE_NET,
                                                           lp->l2_proto,
                                                           lp->l3_proto,
                                                           lp->pre_device,
                                                           lp->pre_channel)) < 0) {
                                        printk(KERN_WARNING
                                               "isdn_net: No channel for %s\n",
                                               ndev->name);
					restore_flags(flags);
					return 1;
				}
                                /* Log packet, which triggered dialing */
				if (dev->net_verbose)
                                        isdn_net_log_packet(buf, lp);
				lp->dialstate = 1;
				lp->flags |= ISDN_NET_CONNECTED;
				/* Connect interface with channel */
				isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
				if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
					if (isdn_ppp_bind(lp) < 0) {
						lp->dialstate = 0;
						isdn_free_channel(lp->isdn_device,
                                                                  lp->isdn_channel,
                                                                  ISDN_USAGE_NET);
						return 1;
					}
#endif
				/* Initiate dialing */
				isdn_net_dial();
                                ndev->tbusy = 0; /* suppress tx-errors while dialing */
				restore_flags(flags);
			} else {
                                /*
                                 * Having no phone-number is a permanent
                                 * failure or misconfiguration.
                                 * Instead of dropping, we should respond
                                 * with an ICMP No route to host in the
                                 * future.
                                 */
                                printk(KERN_WARNING
                                       "isdn_net: No phone number for %s, packet dropped\n",
                                       ndev->name);
				dev_kfree_skb(skb, FREE_WRITE);
				ndev->tbusy = 0;
			}
		} else {
                        /* Connection is established, try sending */
			ndev->trans_start = jiffies;
			if (!lp->dialstate) 
				return(isdn_net_xmit(ndev, lp, skb));
			else
				ndev->tbusy = 0; /* suppress tx-errors while dialing */
		}
	}
	return 1;
}

/*
 * Shutdown a net-interface.
 */
static int
isdn_net_close(struct device *dev)
{
	struct device *p;

	dev->tbusy = 1;
	dev->start = 0;
	isdn_net_hangup(dev);
	if ((p = (((isdn_net_local *) dev->priv)->slave))) {
		/* If this interface has slaves, stop them also */
		while (p) {
			isdn_net_hangup(p);
			p->tbusy = 1;
			p->start = 0;
			p = (((isdn_net_local *) p->priv)->slave);
		}
	}
	isdn_MOD_DEC_USE_COUNT();
	return 0;
}

/*
 * Get statistics
 */
static struct enet_statistics *
 isdn_net_get_stats(struct device *dev)
{
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	return &lp->stats;
}

/* 
 * Got a packet from ISDN-Channel.
 */
static void
isdn_net_receive(struct device *ndev, struct sk_buff *skb)
{
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
#ifdef CONFIG_ISDN_PPP
        isdn_net_local *olp = lp;  /* original 'lp' */
#endif

	lp->transcount += skb->len;
	lp->stats.rx_packets++;
	lp->huptimer = 0;

	if (lp->master) {
		/* Bundling: If device is a slave-device, deliver to master, also
		 * handle master's statistics and hangup-timeout
		 */
		ndev = lp->master;
		lp = (isdn_net_local *) ndev->priv;
		lp->stats.rx_packets++;
		lp->huptimer = 0;
	}

	skb->dev = ndev;
	skb->pkt_type = PACKET_HOST;
	switch (lp->p_encap) {
	case ISDN_NET_ENCAP_ETHER:
		/* Ethernet over ISDN */
	  	skb->mac.raw = skb->data;
		skb->protocol = ntohs(*(unsigned short *)&(skb->data[12]));
                if (memcmp(&skb->data[6],ndev->dev_addr,6))
			skb->pkt_type = PACKET_OTHERHOST;
                if (skb->data[6]&1)
			skb->pkt_type = PACKET_MULTICAST;
		if (!memcmp(&skb->data[6],"\377\377\377\377\377\377",6))
			skb->pkt_type = PACKET_BROADCAST;
		break;
	case ISDN_NET_ENCAP_RAWIP:
		/* RAW-IP without MAC-Header */
		skb->protocol = htons(ETH_P_IP);
		skb->mac.raw = skb->data;
		break;
	case ISDN_NET_ENCAP_IPTYP:
		/* IP with type field */
		skb->protocol = ntohs(*(unsigned short *)&(skb->data[0]));
		skb_pull(skb, 2);
		skb->mac.raw = skb->data;
		break;
	case ISDN_NET_ENCAP_CISCOHDLC:
		/* CISCO-HDLC IP with type field and  fake I-frame-header */
		skb->protocol = ntohs(*(unsigned short *)&(skb->data[2]));
		skb_pull(skb, 4);
		skb->mac.raw = skb->data;
		break;
#ifdef CONFIG_ISDN_PPP
	case ISDN_NET_ENCAP_SYNCPPP:
		isdn_ppp_receive(lp->netdev, olp, skb);
		return;
#endif
	}

       	
#ifdef ISDN_DEBUG_NET_DUMP
	isdn_dumppkt("R:", skb->data, skb->len, 40);
#endif
	netif_rx(skb);
	return;
}

/*
 * A packet arrived via ISDN. Search interface-chain for a corresponding
 * interface. If found, deliver packet to receiver-function and return 1,
 * else return 0.
 */
int
isdn_net_receive_callback(int di, int ch, u_char * buf, int len)
{
	isdn_net_dev *p = dev->netdev;
	struct sk_buff *skb;

	while (p) {
		isdn_net_local *lp = &p->local;
		if ((lp->isdn_device == di) &&
		    (lp->isdn_channel == ch) &&
		    (lp->flags & ISDN_NET_CONNECTED) &&
		    (!lp->dialstate)) {
			skb = dev_alloc_skb(len);
			if (skb == NULL) {
				printk(KERN_WARNING "out of memory\n");
				return 0;
			}
			memcpy(skb_put(skb, len), buf, len);
			isdn_net_receive(&p->dev, skb);
			return 1;
		}
		p = (isdn_net_dev *) p->next;
	}
	return 0;
}

/*
 *  receive callback for lovlevel drivers, which support skb's
 */

int
isdn_net_rcv_skb(int di, int ch, struct sk_buff *skb) 
{
	isdn_net_dev *p = dev->netdev;

	while (p) {
		isdn_net_local *lp = &p->local;
		if ((lp->isdn_device == di) &&
		    (lp->isdn_channel == ch) &&
		    (lp->flags & ISDN_NET_CONNECTED) &&
		    (!lp->dialstate)) {
			isdn_net_receive(&p->dev, skb);
			return 1;
		}
		p = (isdn_net_dev *) p->next;
	}
	return 0;
}

static int
my_eth_header(struct sk_buff *skb, struct device *dev, unsigned short type,
              void *daddr, void *saddr, unsigned len)
{
	struct ethhdr *eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);

	/* 
	 * Set the protocol type. For a packet of type ETH_P_802_3 we
         * put the length here instead. It is up to the 802.2 layer to
         * carry protocol information.
	 */
	
	if(type!=ETH_P_802_3) 
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/*
	 *	Set the source hardware address. 
	 */
	 
	if(saddr)
		memcpy(eth->h_source,saddr,dev->addr_len);
	else
		memcpy(eth->h_source,dev->dev_addr,dev->addr_len);

	/*
	 *	Anyway, the loopback-device should never use this function... 
	 */

	if (dev->flags & IFF_LOOPBACK) {
		memset(eth->h_dest, 0, dev->addr_len);
		return(dev->hard_header_len);
	}
	
	if(daddr) {
		memcpy(eth->h_dest,daddr,dev->addr_len);
		return dev->hard_header_len;
	}
	
	return -dev->hard_header_len;
}

/*
 *  build an header
 *  depends on encaps that is beeing used.
 */
 
static int
isdn_net_header(struct sk_buff *skb, struct device *dev, unsigned short type,
                void *daddr, void *saddr, unsigned plen)
{
	isdn_net_local *lp = dev->priv;
	ushort len = 0;
	
	switch (lp->p_encap) {
                case ISDN_NET_ENCAP_ETHER:
                        my_eth_header(skb, dev, type, daddr, saddr, plen);
                        len = ETH_HLEN; 
                        break;
                case ISDN_NET_ENCAP_RAWIP:
                        printk(KERN_WARNING "isdn_net_header called with RAW_IP!\n");
			len = 0;
                        break;
                case ISDN_NET_ENCAP_IPTYP:
                        /* ethernet type field */
                        *((ushort*) skb_push(skb, 2)) = htons(type);
                        len = 2;
                        break;
                case ISDN_NET_ENCAP_CISCOHDLC:
			skb_push(skb, 4);
                        skb->data[0] = 0x0f;
                        skb->data[1] = 0x00;
                        *((ushort*)&skb->data[2]) = htons(type);
                        len = 4;
                        break;
                case ISDN_NET_ENCAP_SYNCPPP:
                        skb_push(skb, 4);
                        /* reserve space to be filled in isdn_ppp_xmit */
                        len = 4;
                        break;
	}
	return len;
}

/* We don't need to send arp, because we have point-to-point connections. */

static int
isdn_net_rebuild_header(void *buff, struct device *dev, ulong dst,
                        struct sk_buff *skb)
{
	/* 
	 * as we return 0 as len of ENCAP_RAWIP rebuild header will be
	 * called... 
	 * ask Alan if we can put >= 0 in ip_build_xmit
	 * instead of > 0
	 * it would save a function call ... and every cycle counts ;-)
	 */
	return 0;
}

/*
 * Interface-setup. (called just after registering a new interface)
 */
static int
isdn_net_init(struct device *ndev)
{
	ushort max_hlhdr_len = 0;
	int drvidx, i;

	if (ndev == NULL) {
		printk(KERN_WARNING "isdn_net_init: dev = NULL!\n");
		return -ENODEV;
	}
	if (ndev->priv == NULL) {
		printk(KERN_WARNING "isdn_net_init: dev->priv = NULL!\n");
		return -ENODEV;
	}

	/* Setup the generic properties */
	ndev->hard_header = (((isdn_net_local *)(ndev->priv))->p_encap == ISDN_NET_ENCAP_RAWIP)?
		NULL:isdn_net_header;
	ndev->mtu        = 1500;
	ndev->flags      = IFF_NOARP;
        ndev->family     = AF_INET;

	ndev->type       = ARPHRD_ETHER;  

	ndev->addr_len   = ETH_ALEN;
        ndev->pa_addr    = 0;
        ndev->pa_brdaddr = 0;
        ndev->pa_mask    = 0;
        ndev->pa_alen    = 4;
	
        for (i = 0; i < ETH_ALEN; i++)
                ndev->broadcast[i]=0xff;

	for (i = 0; i < DEV_NUMBUFFS; i++)
                skb_queue_head_init(&ndev->buffs[i]);
	
	/* The ISDN-specific entries in the device structure. */
	ndev->open = &isdn_net_open;
	ndev->hard_start_xmit = &isdn_net_start_xmit;

	/* 
	 *  up till binding we ask the protocol layer to reserve as much
	 *  as we migth need for HL layer
         */
	
	for (drvidx = 0; drvidx < ISDN_MAX_DRIVERS; drvidx++)
		if (dev->drv[drvidx])
			if (max_hlhdr_len < dev->drv[drvidx]->interface->hl_hdrlen)
				max_hlhdr_len = dev->drv[drvidx]->interface->hl_hdrlen;

	ndev->hard_header_len = ETH_HLEN + max_hlhdr_len;

	ndev->stop = &isdn_net_close;
	ndev->get_stats = &isdn_net_get_stats;
	ndev->rebuild_header = &isdn_net_rebuild_header;

#ifdef CONFIG_ISDN_PPP
	ndev->do_ioctl = isdn_ppp_dev_ioctl;
#endif
	return 0;
}

/*
 * I picked the pattern-matching-functions from an old GNU-tar version (1.10)
 * It was originaly written and put to PD by rs@mirror.TMC.COM (Rich Salz)
 */

static int
isdn_net_Star(char *s, char *p)
{
	while (isdn_net_wildmat(s, p) == 0)
		if (*++s == '\0')
			return (0);
	return (1);
}

/*
 * Shell-type Pattern-matching for incoming caller-Ids
 * This function gets a string in s and checks, if it matches the pattern
 * given in p. It returns 1 on success, 0 otherwise.
 *
 * Posible Patterns:
 *
 * '?'     matches one character
 * '*'     matches zero or more characters
 * [xyz]   matches the set of charcters in brackets.
 * [^xyz]  matches any single character not in the set of characters
 */

static int
isdn_net_wildmat(char *s, char *p)
{
	register int last;
	register int matched;
	register int reverse;

	for (; *p; s++, p++)
		switch (*p) {
                        case '\\':
                                /*
                                 * Literal match with following character,
                                 * fall through.
                                 */
                                p++;
                        default:
                                if (*s != *p)
                                        return (0);
                                continue;
                        case '?':
                                /* Match anything. */
                                if (*s == '\0')
                                        return (0);
                                continue;
                        case '*':
                                /* Trailing star matches everything. */
                                return (*++p ? isdn_net_Star(s, p) : 1);
                        case '[':
                                /* [^....] means inverse character class. */
                                if ((reverse = (p[1] == '^')))
                                        p++;
                                for (last = 0, matched = 0; *++p && (*p != ']'); last = *p)
                                        /* This next line requires a good C compiler. */
                                        if (*p == '-' ? *s <= *++p && *s >= last : *s == *p)
                                                matched = 1;
                                if (matched == reverse)
                                        return (0);
                                continue;
		}
	return (*s == '\0');
}

static void
isdn_net_swapbind(int drvidx)
{
	isdn_net_dev *p;

#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: swapping ch of %d\n", drvidx);
#endif
	p = dev->netdev;
	while (p) {
		if (p->local.pre_device == drvidx)
			switch (p->local.pre_channel) {
			case 0:
				p->local.pre_channel = 1;
				break;
			case 1:
				p->local.pre_channel = 0;
				break;
			}
		p = (isdn_net_dev *) p->next;
	}
}

static void
isdn_net_swap_usage(int i1, int i2)
{
	int u1 = dev->usage[i1] & ISDN_USAGE_EXCLUSIVE;
	int u2 = dev->usage[i2] & ISDN_USAGE_EXCLUSIVE;

#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: usage of %d and %d\n", i1, i2);
#endif
	dev->usage[i1] &= ~ISDN_USAGE_EXCLUSIVE;
	dev->usage[i1] |= u2;
	dev->usage[i2] &= ~ISDN_USAGE_EXCLUSIVE;
	dev->usage[i2] |= u1;
	isdn_info_update();
}

/*
 * An incoming call-request has arrived.
 * Search the interface-chain for an aproppriate interface.
 * If found, connect the interface to the ISDN-channel and initiate
 * D- and B-Channel-setup. If secure-flag is set, accept only
 * configured phone-numbers. If callback-flag is set, initiate
 * callback-dialing.
 *
 * Return-Value: 0 = No appropriate interface for this call.
 *               1 = Call accepted
 *               2 = Do callback
 */
int
isdn_net_find_icall(int di, int ch, int idx, char *num)
{
	char *eaz;
	int si1;
	int si2;
	int ematch;
	int swapped;
	int sidx = 0;
	isdn_net_dev *p;
	isdn_net_phone *n;
	ulong flags;
	char nr[31];
	char *s;

	/* Search name in netdev-chain */
	save_flags(flags);
	cli();
	if (num[0] == ',') {
		nr[0] = '0';
		strncpy(&nr[1], num, 30);
		printk(KERN_WARNING "isdn_net: Incoming call without OAD, assuming '0'\n");
	} else
		strncpy(nr, num, 30);
	s = strtok(nr, ",");
	s = strtok(NULL, ",");
	if (!s) {
		printk(KERN_WARNING "isdn_net: Incoming callinfo garbled, ignored: %s\n",
		       num);
		restore_flags(flags);
		return 0;
	}
	si1 = (int)simple_strtoul(s,NULL,10);
	s = strtok(NULL, ",");
	if (!s) {
		printk(KERN_WARNING "isdn_net: Incoming callinfo garbled, ignored: %s\n",
		       num);
		restore_flags(flags);
		return 0;
	}
	si2 = (int)simple_strtoul(s,NULL,10);
	eaz = strtok(NULL, ",");
	if (!eaz) {
		printk(KERN_WARNING "isdn_net: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	}
	if (dev->net_verbose > 1)
		printk(KERN_INFO "isdn_net: call from %s,%d,%d -> %s\n", nr, si1, si2, eaz);
	/* Accept only calls with Si1 = 7 (Data-Transmission) */
	if (si1 != 7) {
		if (dev->net_verbose > 1)
			printk(KERN_INFO "isdn_net: Service-Indicator not 7, ignored\n");
		return 0;
	}
	n = (isdn_net_phone *) 0;
	p = dev->netdev;
	ematch = 0;
#ifdef ISDN_DEBUG_NET_ICALL
	printk(KERN_DEBUG "n_fi: di=%d ch=%d idx=%d usg=%d\n", di, ch, idx,
	       dev->usage[idx]);
#endif
	swapped = 0;
	while (p) {
		/* If last check has trigered as binding-swap, revert it */
		switch (swapped) {
		case 2:
			isdn_net_swap_usage(idx, sidx);
			/* fall through */
		case 1:
			isdn_net_swapbind(di);
			break;
		}
		swapped = 0;
		if (!strcmp(isdn_map_eaz2msn(p->local.msn, di), eaz))
			ematch = 1;
#ifdef ISDN_DEBUG_NET_ICALL
		printk(KERN_DEBUG "n_fi: if='%s', l.msn=%s, l.flags=%d, l.dstate=%d\n",
		       p->local.name, p->local.msn, p->local.flags, p->local.dialstate);
#endif
		if ((!strcmp(isdn_map_eaz2msn(p->local.msn, di), eaz)) &&	/* EAZ is matching   */
		    (((!(p->local.flags & ISDN_NET_CONNECTED)) &&	/* but not connected */
		      (USG_NONE(dev->usage[idx]))) ||	/* and ch. unused or */
		     (((p->local.dialstate == 4) &&	/* if dialing        */
		       (!(p->local.flags & ISDN_NET_CALLBACK)))		/* but no callback   */
		     ))) {
#ifdef ISDN_DEBUG_NET_ICALL
			printk(KERN_DEBUG "n_fi: match1, pdev=%d pch=%d\n",
			       p->local.pre_device, p->local.pre_channel);
#endif
			if (dev->usage[idx] & ISDN_USAGE_EXCLUSIVE) {
				if ((p->local.pre_channel != ch) ||
				    (p->local.pre_device != di)) {
					/* Here we got a problem:
					   If using an ICN-Card, an incoming call is always signaled on
					   on the first channel of the card, if both channels are
					   down. However this channel may be bound exclusive. If the
					   second channel is free, this call should be accepted.
					   The solution is horribly but it runs, so what:
					   We exchange the exclusive bindings of the two channels, the
					   corresponding variables in the interface-structs.
					 */
					if (ch == 0) {
						sidx = isdn_dc2minor(di, 1);
#ifdef ISDN_DEBUG_NET_ICALL
						printk(KERN_DEBUG "n_fi: ch is 0\n");
#endif
						if (USG_NONE(dev->usage[sidx])) {
							/* Second Channel is free, now see if it is bound
							   exclusive too. */
							if (dev->usage[sidx] & ISDN_USAGE_EXCLUSIVE) {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: 2nd channel is down and bound\n");
#endif
								/* Yes, swap bindings only, if the original
								   binding is bound to channel 1 of this driver */
								if ((p->local.pre_device == di) &&
								    (p->local.pre_channel == 1)) {
									isdn_net_swapbind(di);
									swapped = 1;
								} else {
									/* ... else iterate next device */
									p = (isdn_net_dev *) p->next;
									continue;
								}
							} else {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: 2nd channel is down and unbound\n");
#endif
								/* No, swap always and swap excl-usage also */
								isdn_net_swap_usage(idx, sidx);
								isdn_net_swapbind(di);
								swapped = 2;
							}
							/* Now check for exclusive binding again */
#ifdef ISDN_DEBUG_NET_ICALL
							printk(KERN_DEBUG "n_fi: final check\n");
#endif
							if ((dev->usage[idx] & ISDN_USAGE_EXCLUSIVE) &&
							    ((p->local.pre_channel != ch) ||
							     (p->local.pre_device != di))) {
#ifdef ISDN_DEBUG_NET_ICALL
								printk(KERN_DEBUG "n_fi: final check failed\n");
#endif
								p = (isdn_net_dev *) p->next;
								continue;
							}
						}
					} else {
						/* We are already on the second channel, so nothing to do */
#ifdef ISDN_DEBUG_NET_ICALL
						printk(KERN_DEBUG "n_fi: already on 2nd channel\n");
#endif
						p = (isdn_net_dev *) p->next;
						continue;
					}
				}
			}
#ifdef ISDN_DEBUG_NET_ICALL
			printk(KERN_DEBUG "n_fi: match2\n");
#endif
			n = p->local.phone[0];
			if (p->local.flags & ISDN_NET_SECURE) {
				while (n) {
					if (isdn_net_wildmat(nr, n->num))
						break;
					n = (isdn_net_phone *) n->next;
				}
			}
			if (n || (!(p->local.flags & ISDN_NET_SECURE))) {
				isdn_net_local *lp = &(p->local);
#ifdef ISDN_DEBUG_NET_ICALL
				printk(KERN_DEBUG "n_fi: match3\n");
#endif
				/* Here we got an interface matched, now see if it is up.
				 * If not, reject the call actively.
				 */
				if (!p->dev.start) {
					restore_flags(flags);
					printk(KERN_INFO "%s: incoming call, if down -> rejected\n",
					       lp->name);
					return 3;
				}
				/* Interface is up, now see if it's a slave. If so, see if
				 * it's master and parent slave is online. If not, reject the call.
				 */
				if (lp->master) {
					isdn_net_local *mlp = (isdn_net_local *) lp->master->priv;
					printk(KERN_DEBUG "ICALLslv: %s\n", lp->name);
					printk(KERN_DEBUG "master=%s\n", mlp->name);
					if (mlp->flags & ISDN_NET_CONNECTED) {
						printk(KERN_DEBUG "master online\n");
						/* Master is online, find parent-slave (master if first slave) */
						while (mlp->slave) {
							if ((isdn_net_local *) mlp->slave->priv == lp)
								break;
							mlp = (isdn_net_local *) mlp->slave->priv;
						}
					} else
						printk(KERN_DEBUG "master offline\n");
					/* Found parent, if it's offline iterate next device */
					printk(KERN_DEBUG "mlpf: %d\n", mlp->flags & ISDN_NET_CONNECTED);
					if (!(mlp->flags & ISDN_NET_CONNECTED)) {
						p = (isdn_net_dev *) p->next;
						continue;
					}
				}
				if (lp->flags & ISDN_NET_CALLBACK) {
					int chi;
					printk(KERN_DEBUG "%s: call from %s -> %s, start callback\n",
					       lp->name, nr, eaz);
					if (lp->phone[1]) {
						/* Grab a free ISDN-Channel */
						if ((chi = isdn_get_free_channel(ISDN_USAGE_NET, lp->l2_proto,
							    lp->l3_proto,
							  lp->pre_device,
						 lp->pre_channel)) < 0) {
							printk(KERN_WARNING "isdn_net: No channel for %s\n", lp->name);
							restore_flags(flags);
							return 0;
						}
						/* Setup dialstate. */
						lp->dialstate = 1;
						lp->flags |= ISDN_NET_CONNECTED;
						/* Connect interface with channel */
						isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
						if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
							if (isdn_ppp_bind(lp) < 0) {
								isdn_free_channel(p->local.isdn_device, p->local.isdn_channel,
									     ISDN_USAGE_NET);
								lp->dialstate = 0;
								restore_flags(flags);
								return 0;
							}
#endif
						/* Initiate dialing by returning 2 */
						restore_flags(flags);
						return 2;
					} else
						printk(KERN_WARNING "isdn_net: %s: No phone number\n", lp->name);
					restore_flags(flags);
					return 0;
				} else {
					printk(KERN_DEBUG "%s: call from %s -> %s accepted\n", lp->name, nr,
					       eaz);
#if 0
/* why is this a CONFIG_ISDN_PPP feature ??? */
#ifdef CONFIG_ISDN_PPP
					if (p->local.isdn_device != -1) {
						isdn_free_channel(p->local.isdn_device, p->local.isdn_channel,
							 ISDN_USAGE_NET);
					}
#endif
#endif
					/* if this interface is dialing, it does it probably on a different
					   device, so free this device */
					if (p->local.dialstate == 4)
						isdn_free_channel(p->local.isdn_device, p->local.isdn_channel,
							 ISDN_USAGE_NET);
					dev->usage[idx] &= ISDN_USAGE_EXCLUSIVE;
					dev->usage[idx] |= ISDN_USAGE_NET;
					strcpy(dev->num[idx], nr);
					isdn_info_update();
					p->local.isdn_device = di;
					p->local.isdn_channel = ch;
					p->local.ppp_minor = -1;
					p->local.flags |= ISDN_NET_CONNECTED;
					p->local.dialstate = 7;
					p->local.dtimer = 0;
					p->local.outgoing = 0;
					p->local.huptimer = 0;
					p->local.hupflags |= 1;
#ifdef CONFIG_ISDN_PPP
					if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
						if (isdn_ppp_bind(lp) < 0) {
							isdn_free_channel(p->local.isdn_device, p->local.isdn_channel,
							 ISDN_USAGE_NET);
							lp->dialstate = 0;
							restore_flags(flags);
							return 0;
						}
#endif
					restore_flags(flags);
					return 1;
				}
			}
		}
		p = (isdn_net_dev *) p->next;
	}
	/* If none of configured EAZ/MSN matched and not verbose, be silent */
	if (ematch || dev->net_verbose)
		printk(KERN_INFO "isdn_net: call from %s -> %d %s ignored\n", nr, di, eaz);
	restore_flags(flags);
	return 0;
}

/*
 * Search list of net-interfaces for an interface with given name.
 */
isdn_net_dev *
 isdn_net_findif(char *name)
{
	isdn_net_dev *p = dev->netdev;

	while (p) {
		if (!strcmp(p->local.name, name))
			return p;
		p = (isdn_net_dev *) p->next;
	}
	return (isdn_net_dev *) NULL;
}

/*
 * Force a net-interface to dial out.
 * This is called from the userlevel-routine below or
 * from isdn_net_start_xmit().
 */
int isdn_net_force_dial_lp(isdn_net_local * lp)
{
	if ((!(lp->flags & ISDN_NET_CONNECTED)) && !lp->dialstate) {
		int chi;
		if (lp->phone[1]) {
			ulong flags;
			save_flags(flags);
			cli();
			/* Grab a free ISDN-Channel */
			if ((chi = isdn_get_free_channel(ISDN_USAGE_NET, lp->l2_proto,
						    lp->l3_proto,
						    lp->pre_device,
						 lp->pre_channel)) < 0) {
				printk(KERN_WARNING "isdn_net: No channel for %s\n", lp->name);
				restore_flags(flags);
				return -EAGAIN;
			}
			lp->dialstate = 1;
			lp->flags |= ISDN_NET_CONNECTED;
			/* Connect interface with channel */
			isdn_net_bind_channel(lp, chi);
#ifdef CONFIG_ISDN_PPP
			if (lp->p_encap == ISDN_NET_ENCAP_SYNCPPP)
				if (isdn_ppp_bind(lp) < 0) {
					lp->dialstate = 0;
					isdn_free_channel(lp->isdn_device, lp->isdn_channel, ISDN_USAGE_NET);
					return 1;
				}
#endif
			/* Initiate dialing */
			isdn_net_dial();
			restore_flags(flags);
			return 0;
		} else
			return -EINVAL;
	} else
		return -EBUSY;
}

/*
 * Force a net-interface to dial out.
 * This is always called from within userspace (ISDN_IOCTL_NET_DIAL).
 */
int 
isdn_net_force_dial(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);

	if (!p)
		return -ENODEV;
	return (isdn_net_force_dial_lp(&p->local));
}

/*
 * Allocate a new network-interface and initialize it's data structures.
 */
char *
 isdn_net_new(char *name, struct device *master)
{
	isdn_net_dev *netdev;

	/* Avoid creating an existing interface */
	if (isdn_net_findif(name)) {
		printk(KERN_WARNING "isdn_net: interface %s already exists\n", name);
		return NULL;
	}
	if (!(netdev = (isdn_net_dev *) kmalloc(sizeof(isdn_net_dev), GFP_KERNEL))) {
		printk(KERN_WARNING "isdn_net: Could not allocate net-device\n");
		return NULL;
	}
	memset(netdev, 0, sizeof(isdn_net_dev));
	if (name == NULL)
		strcpy(netdev->local.name, "         ");
	else
		strcpy(netdev->local.name, name);
	netdev->dev.name = netdev->local.name;
	netdev->dev.priv = &netdev->local;
	netdev->dev.init = isdn_net_init;
	if (master) {
		/* Device shall be a slave */
		struct device *p = (((isdn_net_local *) master->priv)->slave);
		struct device *q = master;

		netdev->local.master = master;
		/* Put device at end of slave-chain */
		while (p) {
			q = p;
			p = (((isdn_net_local *) p->priv)->slave);
		}
		((isdn_net_local *) q->priv)->slave = &(netdev->dev);
		q->interrupt = 0;
		q->tbusy = 0;
		q->start = master->start;
	} else {
		/* Device shall be a master */
		if (register_netdev(&netdev->dev) != 0) {
			printk(KERN_WARNING "isdn_net: Could not register net-device\n");
			kfree(netdev);
			return NULL;
		}
	}
	netdev->local.magic = ISDN_NET_MAGIC;

#ifdef CONFIG_ISDN_PPP
	netdev->mp_last = NULL;	/* mpqueue is empty */
	netdev->ib.next_num = 0;
	netdev->ib.last = NULL;
#endif
	netdev->queue = &netdev->local;
	netdev->local.last = &netdev->local;
	netdev->local.netdev = netdev;
	netdev->local.next = &netdev->local;

	netdev->local.isdn_device = -1;
	netdev->local.isdn_channel = -1;
	netdev->local.pre_device = -1;
	netdev->local.pre_channel = -1;
	netdev->local.exclusive = -1;
	netdev->local.ppp_minor = -1;
	netdev->local.p_encap = ISDN_NET_ENCAP_RAWIP;
	netdev->local.l2_proto = ISDN_PROTO_L2_X75I;
	netdev->local.l3_proto = ISDN_PROTO_L3_TRANS;
	netdev->local.slavedelay = 10 * HZ;
	netdev->local.srobin = &netdev->dev;
	netdev->local.hupflags = 8;	/* Do hangup even on incoming calls */
	netdev->local.onhtime = 10;	/* Default hangup-time for saving costs
					   of those who forget configuring this */
	/* The following should be configurable via ioctl */
	netdev->local.dialmax = 1;
	/* Put into to netdev-chain */
	netdev->next = (void *) dev->netdev;
	dev->netdev = netdev;
	/* Enable auto-hangup timer */
	isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 1);
	return netdev->dev.name;
}

char *
 isdn_net_newslave(char *parm)
{
	char *p = strchr(parm, ',');
	isdn_net_dev *n;
	char newname[10];

	if (p) {
		/* Slave-Name MUST not be empty */
		if (!strlen(p + 1))
			return NULL;
		strcpy(newname, p + 1);
		*p = 0;
		/* Master must already exist */
		if (!(n = isdn_net_findif(parm)))
			return NULL;
		/* Master must be a real interface, not a slave */
		if (n->local.master)
			return NULL;
		return (isdn_net_new(newname, &(n->dev)));
	}
	return NULL;
}

/*
 * Set interface-parameters.
 * Allways set all parameters, so the user-level application is responsible
 * for not overwriting existing setups. It has to get the current
 * setup first, if only selected parameters are to be changed.
 */
int isdn_net_setcfg(isdn_net_ioctl_cfg * cfg)
{
	isdn_net_dev *p = isdn_net_findif(cfg->name);
	ulong features;
	int i;
	int drvidx;
	int chidx;
	char drvid[25];

	if (p) {
		/* See if any registered driver supports the features we want */
		features = (1 << cfg->l2_proto) | (256 << cfg->l3_proto);
		for (i = 0; i < ISDN_MAX_DRIVERS; i++)
			if (dev->drv[i])
				if ((dev->drv[i]->interface->features & features) == features)
					break;
		if (i == ISDN_MAX_DRIVERS) {
			printk(KERN_WARNING "isdn_net: No driver with selected features\n");
			return -ENODEV;
		}
		if (strlen(cfg->drvid)) {
			/* A bind has been requested ... */
			char *c,*e;

			drvidx = -1;
			chidx = -1;
			strcpy(drvid, cfg->drvid);
			if ((c = strchr(drvid, ','))) {
				/* The channel-number is appended to the driver-Id with a comma */
				chidx = (int)simple_strtoul(c + 1,&e,10);
				if (e == c)
					chidx = -1;
				*c = '\0';
			}
			for (i = 0; i < ISDN_MAX_DRIVERS; i++)
				/* Lookup driver-Id in array */
				if (!(strcmp(dev->drvid[i], drvid))) {
					drvidx = i;
					break;
				}
			if ((drvidx == -1) || (chidx == -1))
				/* Either driver-Id or channel-number invalid */
				return -ENODEV;
		} else {
			/* Parameters are valid, so get them */
			drvidx = p->local.pre_device;
			chidx = p->local.pre_channel;
		}
		if (cfg->exclusive > 0) {
			int flags;

			/* If binding is exclusive, try to grab the channel */
			save_flags(flags);
			if ((i = isdn_get_free_channel(ISDN_USAGE_NET, p->local.l2_proto,
						  p->local.l3_proto,
						  drvidx,
						  chidx)) < 0) {
				/* Grab failed, because desired channel is in use */
				p->local.exclusive = -1;
				restore_flags(flags);
				return -EBUSY;
			}
			/* All went ok, so update isdninfo */
			dev->usage[i] = ISDN_USAGE_EXCLUSIVE;
			isdn_info_update();
			restore_flags(flags);
			p->local.exclusive = i;
		} else {
			/* Non-exclusive binding or unbind. */
			p->local.exclusive = -1;
			if ((p->local.pre_device != -1) && (cfg->exclusive == -1)) {
				isdn_unexclusive_channel(p->local.pre_device, p->local.pre_channel);
				drvidx = -1;
				chidx = -1;
			}
		}
		strcpy(p->local.msn, cfg->eaz);
		p->local.pre_device = drvidx;
		p->local.pre_channel = chidx;
		p->local.onhtime = cfg->onhtime;
		p->local.charge = cfg->charge;
		p->local.l2_proto = cfg->l2_proto;
		p->local.l3_proto = cfg->l3_proto;
		p->local.slavedelay = cfg->slavedelay * HZ;
		if (cfg->secure)
			p->local.flags |= ISDN_NET_SECURE;
		else
			p->local.flags &= ~ISDN_NET_SECURE;
		if (cfg->callback)
			p->local.flags |= ISDN_NET_CALLBACK;
		else
			p->local.flags &= ~ISDN_NET_CALLBACK;
		if (cfg->chargehup)
			p->local.hupflags |= 4;
		else
			p->local.hupflags &= ~4;
		if (cfg->ihup)
			p->local.hupflags |= 8;
		else
			p->local.hupflags &= ~8;
                if ((p->local.p_encap != cfg->p_encap) &&
		    ((p->local.p_encap == ISDN_NET_ENCAP_RAWIP) ||
		    (cfg->p_encap == ISDN_NET_ENCAP_RAWIP)        ))
			if (p->dev.start) {
				printk(KERN_WARNING
				       "%s: cannot change encap when running\n",
                                       p->local.name);
				return -EBUSY;
			}
		p->local.p_encap = cfg->p_encap;
		p->dev.hard_header = (cfg->p_encap == ISDN_NET_ENCAP_RAWIP)?
			NULL:isdn_net_header;
		return 0;
	}
	return -ENODEV;
}

/*
 * Perform get-interface-parameters.ioctl
 */
int isdn_net_getcfg(isdn_net_ioctl_cfg * cfg)
{
	isdn_net_dev *p = isdn_net_findif(cfg->name);

	if (p) {
		strcpy(cfg->eaz, p->local.msn);
		cfg->exclusive = p->local.exclusive;
		if (p->local.pre_device >= 0) {
			sprintf(cfg->drvid, "%s,%d", dev->drvid[p->local.pre_device],
				p->local.pre_channel);
		} else
			cfg->drvid[0] = '\0';
		cfg->onhtime = p->local.onhtime;
		cfg->charge = p->local.charge;
		cfg->l2_proto = p->local.l2_proto;
		cfg->l3_proto = p->local.l3_proto;
		cfg->p_encap = p->local.p_encap;
		cfg->secure = (p->local.flags & ISDN_NET_SECURE) ? 1 : 0;
		cfg->callback = (p->local.flags & ISDN_NET_CALLBACK) ? 1 : 0;
		cfg->chargehup = (p->local.hupflags & 4) ? 1 : 0;
		cfg->ihup = (p->local.hupflags & 8) ? 1 : 0;
		cfg->slavedelay = p->local.slavedelay / HZ;
		if (p->local.slave)
			strcpy(cfg->slave, ((isdn_net_local *) p->local.slave->priv)->name);
		else
			cfg->slave[0] = '\0';
		if (p->local.master)
			strcpy(cfg->master, ((isdn_net_local *) p->local.master->priv)->name);
		else
			cfg->master[0] = '\0';
		return 0;
	}
	return -ENODEV;
}

/*
 * Add a phone-number to an interface.
 */
int isdn_net_addphone(isdn_net_ioctl_phone * phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	isdn_net_phone *n;

	if (isdn_net_checkwild(phone->phone) && (phone->outgoing & 1))
		return -EINVAL;
	if (p) {
		if (!(n = (isdn_net_phone *) kmalloc(sizeof(isdn_net_phone), GFP_KERNEL)))
			return -ENOMEM;
		strcpy(n->num, phone->phone);
		n->next = p->local.phone[phone->outgoing & 1];
		p->local.phone[phone->outgoing & 1] = n;
		return 0;
	}
	return -ENODEV;
}

/*
 * Return a string of all phone-numbers of an interface.
 */
int isdn_net_getphones(isdn_net_ioctl_phone * phone, char *phones)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	int more = 0;
	int count = 0;
	isdn_net_phone *n;
	int flags;
	int ret;

	if (!p)
		return -ENODEV;
	save_flags(flags);
	cli();
	inout &= 1;
	n = p->local.phone[inout];
	while (n) {
		if (more) {
			put_fs_byte(' ', phones++);
			count++;
		}
		if ((ret = verify_area(VERIFY_WRITE, (void *) phones, strlen(n->num) + 1))) {
			restore_flags(flags);
			return ret;
		}
		memcpy_tofs(phones, n->num, strlen(n->num) + 1);
		phones += strlen(n->num);
		count += strlen(n->num);
		n = n->next;
		more = 1;
	}
	restore_flags(flags);
	count++;
	return count;
}

/*
 * Delete a phone-number from an interface.
 */

int isdn_net_delphone(isdn_net_ioctl_phone * phone)
{
	isdn_net_dev *p = isdn_net_findif(phone->name);
	int inout = phone->outgoing & 1;
	isdn_net_phone *n;
	isdn_net_phone *m;

	if (p) {
		n = p->local.phone[inout];
		m = NULL;
		while (n) {
			if (!strcmp(n->num, phone->phone)) {
				if (m)
					m->next = n->next;
				else
					p->local.phone[inout] = n->next;
				kfree(n);
				return 0;
			}
			m = n;
			n = (isdn_net_phone *) n->next;
		}
		return -EINVAL;
	}
	return -ENODEV;
}

/*
 * Delete all phone-numbers of an interface.
 */
static int isdn_net_rmallphone(isdn_net_dev * p)
{
	isdn_net_phone *n;
	isdn_net_phone *m;
	int flags;
	int i;

	save_flags(flags);
	cli();
	for (i = 0; i < 2; i++) {
		n = p->local.phone[i];
		while (n) {
			m = n->next;
			kfree(n);
			n = m;
		}
		p->local.phone[i] = NULL;
	}
	restore_flags(flags);
	return 0;
}

/*
 * Force a hangup of a network-interface.
 */
int isdn_net_force_hangup(char *name)
{
	isdn_net_dev *p = isdn_net_findif(name);
	int flags;
	struct device *q;

	if (p) {
		save_flags(flags);
		cli();
		if (p->local.isdn_device < 0) {
			restore_flags(flags);
			return 1;
		}
		isdn_net_hangup(&p->dev);
		q = p->local.slave;
		/* If this interface has slaves, do a hangup for them also. */
		while (q) {
			isdn_net_hangup(q);
			q = (((isdn_net_local *) q->priv)->slave);
		}
		restore_flags(flags);
		return 0;
	}
	return -ENODEV;
}

/*
 * Helper-function for isdn_net_rm: Do the real work.
 */
static int isdn_net_realrm(isdn_net_dev * p, isdn_net_dev * q)
{
	int flags;

	save_flags(flags);
	cli();
	if (p->local.master) {
		/* If it's a slave, it may be removed even if it is busy. However
		 * it has to be hung up first.
		 */
		isdn_net_hangup(&p->dev);
		p->dev.start = 0;
	}
	if (p->dev.start) {
		restore_flags(flags);
		return -EBUSY;
	}
	/* Free all phone-entries */
	isdn_net_rmallphone(p);
	/* If interface is bound exclusive, free channel-usage */
	if (p->local.exclusive != -1)
		isdn_unexclusive_channel(p->local.pre_device, p->local.pre_channel);
	if (p->local.master) {
		/* It's a slave-device, so update master's slave-pointer if necessary */
		if (((isdn_net_local *) (p->local.master->priv))->slave == &p->dev)
			((isdn_net_local *) (p->local.master->priv))->slave = p->local.slave;
	} else
		/* Unregister only if it's a master-device */
		unregister_netdev(&p->dev);
	/* Unlink device from chain */
	if (q)
		q->next = p->next;
	else
		dev->netdev = p->next;
	if (p->local.slave) {
		/* If this interface has a slave, remove it also */
		char *slavename = ((isdn_net_local *) (p->local.slave->priv))->name;
		isdn_net_dev *n = dev->netdev;
		q = NULL;
		while (n) {
			if (!strcmp(n->local.name, slavename)) {
				isdn_net_realrm(n, q);
				break;
			}
			q = n;
			n = (isdn_net_dev *) n->next;
		}
	}
	/* If no more net-devices remain, disable auto-hangup timer */
	if (dev->netdev == NULL)
		isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 0);
	restore_flags(flags);

#ifdef CONFIG_ISDN_PPP
	isdn_ppp_free_mpqueue(p);
#endif
	kfree(p);

	return 0;
}

/*
 * Remove a single network-interface.
 */
int isdn_net_rm(char *name)
{
	isdn_net_dev *p;
	isdn_net_dev *q;

	/* Search name in netdev-chain */
	p = dev->netdev;
	q = NULL;
	while (p) {
		if (!strcmp(p->local.name, name))
			return (isdn_net_realrm(p, q));
		q = p;
		p = (isdn_net_dev *) p->next;
	}
	/* If no more net-devices remain, disable auto-hangup timer */
	if (dev->netdev == NULL)
		isdn_timer_ctrl(ISDN_TIMER_NETHANGUP, 0);
	return -ENODEV;
}

/*
 * Remove all network-interfaces
 */
int isdn_net_rmall(void)
{
	int flags;
	int ret;

	/* Walk through netdev-chain */
	save_flags(flags);
	cli();
	while (dev->netdev) {
		if (!dev->netdev->local.master) {
			/* Remove master-devices only, slaves get removed with their master */
			if ((ret = isdn_net_realrm(dev->netdev, NULL))) {
				restore_flags(flags);
			        return ret;
			}
		}
	}
	dev->netdev = NULL;
	restore_flags(flags);
	return 0;
}







