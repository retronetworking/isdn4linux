/* $Id$
 *
 * ISDN lowlevel-module for the IBM ISDN-S0 Active 2000.
 *
 * Copyright 1997 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Thanks to Friedemann Baitinger and IBM Germany
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

#include "act2000.h"
#include "act2000_isa.h"
#include "capi.h"

extern void act2000_terminate(int board);
extern int act2000_init_dev(int board, int mem_base, int irq);

static unsigned short isa_ports[] =
{
        0x0200, 0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380,
        0xcfe0, 0xcfa0, 0xcf60, 0xcf20, 0xcee0, 0xcea0, 0xce60,
};
#define ISA_NRPORTS (sizeof(isa_ports)/sizeof(unsigned short))
act2000_card *cards = (act2000_card *) NULL;

/* Parameters to be set by insmod */
int act_bus = 0;
int act_port = -1;
int act_irq = 0;
char *act_id = "\0";

act2000_chan *find_channel(act2000_card *card, int channel)
{
	if ((channel >= 0) && (channel < ACT2000_BCH))
        	return &(card->bch[channel]);
	printk(KERN_WARNING "act2000: Invalid channel %d\n", channel);
	return NULL;
}

/*
 * Free MSN list
 */
static void act2000_clear_msn(struct act2000_card *card)
{
        struct msn_entry *ptr,
        *back;

        for (ptr = card->msn_list; ptr;) {
                back = ptr->next;
                kfree(ptr);
                ptr = back;
        }
        card->msn_list = NULL;
	actcapi_listen_req(card, 0);
}

/*
 * Add one ore more MSNs to the MSN list
 */
static void
act2000_set_msn(struct act2000_card *card, char *list)
{
        struct msn_entry *ptr;
        struct msn_entry *back = NULL;
        char *cp,
		*sp;
        int len;
	
        if (strlen(list) == 0) {
                ptr = kmalloc(sizeof(struct msn_entry), GFP_ATOMIC);
                if (!ptr) {
                        printk(KERN_WARNING "kmalloc failed\n");
                        return;
                }
                ptr->msn = NULL;
		
                ptr->next = card->msn_list;
                card->msn_list = ptr;
		actcapi_listen_req(card, 0x3ff);
                return;
        }
        if (card->msn_list)
                for (back = card->msn_list; back->next; back = back->next);
	
        sp = list;
	
        do {
                cp = strchr(sp, ',');
                if (cp)
                        len = cp - sp;
                else
                        len = strlen(sp);
		
                ptr = kmalloc(sizeof(struct msn_entry), GFP_ATOMIC);
		
                if (!ptr) {
                        printk(KERN_WARNING "kmalloc failed\n");
                        return;
                }
                ptr->next = NULL;
		
                ptr->msn = kmalloc(len, GFP_ATOMIC);
                if (!ptr->msn) {
                        printk(KERN_WARNING "kmalloc failed\n");
                        return;
                }
                memcpy(ptr->msn, sp, len - 1);
                ptr->msn[len] = 0;
		
#ifdef DEBUG
                printk(KERN_DEBUG "msn: %s\n", ptr->msn);
#endif
                if (card->msn_list == NULL)
                        card->msn_list = ptr;
                else
                        back->next = ptr;
                back = ptr;
                sp += len;
        } while (cp);
}

/*
 *  check if we do signal or reject an incoming call
 *
 *        if list is null, reject all calls
 *        if first entry has null MSN accept all calls
 *        else accept call if it matches an MSN in list. 
 */

static int
act2000_check_msn(struct act2000_card *card, char *msn)
{
        struct msn_entry *ptr;
	
        for (ptr = card->msn_list; ptr; ptr = ptr->next) {
		
                if (ptr->msn == NULL)
                        return 1;
		
                if (strcmp(ptr->msn, msn) == 0)
                        return 1;
        }
	
        return 0;
}

/*
 * called by interrupt service routine
 */

void
act2000_transmit(struct act2000_card *card)
{
	switch (card->bus) {
		case ACT2000_BUS_ISA:
			isa_send(card);
			break;
		case ACT2000_BUS_PCMCIA:
		case ACT2000_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "act2000_transmit: Illegal bustype %d\n", card->bus);
	}
}

void
act2000_receive(struct act2000_card *card)
{
	switch (card->bus) {
		case ACT2000_BUS_ISA:
			isa_receive(card);
			break;
		case ACT2000_BUS_PCMCIA:
		case ACT2000_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "act2000_receive: Illegal bustype %d\n", card->bus);
	}
}

static void
act2000_poll(unsigned long data)
{
	act2000_card * card = (act2000_card *)data;
	unsigned long flags;

	act2000_transmit(card);
	act2000_receive(card);
        save_flags(flags);
        cli();
        del_timer(&card->ptimer);
        card->ptimer.expires = jiffies + (HZ/10);
        add_timer(&card->ptimer);
        restore_flags(flags);
}

static int
act2000_command(act2000_card * card, isdn_ctrl * c)
{
        ulong a;
        struct act2000_chan *chan;
	isdn_ctrl cmd;
	int ret;
	unsigned long flags;
        char *cp;
 
        switch (c->command) {
		case ISDN_CMD_IOCTL:
			memcpy(&a, c->parm.num, sizeof(ulong));
			switch (c->arg) {
				case ACT2000_IOCTL_LOADBOOT:
					switch (card->bus) {
						case ACT2000_BUS_ISA:
							ret = isa_download(card,
									   (act2000_ddef *)a);
							if (!ret) {
								card->flags |= ACT2000_FLAGS_LOADED;
#if 0
								if (!(card->flags & ACT2000_FLAGS_IVALID)) {
#endif
									card->ptimer.expires = jiffies + 3;
									card->ptimer.function = act2000_poll;
									card->ptimer.data = (unsigned long)card;
									add_timer(&card->ptimer);
#if 0
								}
#endif
								actcapi_manufacturer_req_errh(card);
							}
							break;
						default:
							printk(KERN_WARNING
							       "act2000: Illegal BUS type %d\n",
							       card->bus);
							ret = -EIO;
					}
					return ret;
				case ACT2000_IOCTL_SETPROTO:
					card->ptype = a?ISDN_PTYPE_EURO:ISDN_PTYPE_1TR6;
					if (!(card->flags & ACT2000_FLAGS_RUNNING))
						return 0;
					if (a) {
						ret = copy_from_user(card->msn, (char *)a,
								  sizeof(card->msn));
						if (ret)
							return ret;
					}
					actcapi_manufacturer_req_net(card);
					return 0;
				case ACT2000_IOCTL_TEST:
					if (!(card->flags & ACT2000_FLAGS_RUNNING))
						return -ENODEV;
					actcapi_listen_req(card, (a & 0xffff));
					return 0;
				default:
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			save_flags(flags);
			cli();
			if (chan->fsm_state != ACT2000_STATE_NULL) {
				restore_flags(flags);
				printk(KERN_WARNING "Dial on Non-NULL channel\n");
				return -EBUSY;
			}
			chan->fsm_state = ACT2000_STATE_OCALL;
			chan->callref = 0xffff;
			restore_flags(flags);
			ret = actcapi_connect_req(card, chan, c->parm.setup.phone,
						  c->parm.setup.eazmsn, c->parm.setup.si1,
						  c->parm.setup.si2);
			if (ret) {
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg &= 0x0f;
				card->interface.statcallb(&cmd);
			}
			return ret;
		case ISDN_CMD_ACCEPTD:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			if (chan->fsm_state == ACT2000_STATE_ICALL)
				actcapi_select_b2_protocol_req(card, chan);
			return 0;
		case ISDN_CMD_ACCEPTB:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			return 0;
		case ISDN_CMD_HANGUP:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			switch (chan->fsm_state) {
				case ACT2000_STATE_ICALL:
				case ACT2000_STATE_BSETUP:
					actcapi_connect_resp(card, chan, 0x15);
					break;
				case ACT2000_STATE_ACTIVE:
					actcapi_disconnect_b3_req(card, chan);
					break;
			}
			return 0;
		case ISDN_CMD_SETEAZ:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			act2000_set_msn(card, c->parm.num);
			return 0;
		case ISDN_CMD_CLREAZ:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			act2000_clear_msn(card);
			return 0;
		case ISDN_CMD_SETL2:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->l2prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL2:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			return chan->l2prot;
		case ISDN_CMD_SETL3:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if ((c->arg >> 8) != ISDN_PROTO_L3_TRANS) {
				printk(KERN_WARNING "L3 protocol unknown\n");
				return -1;
			}
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->l3prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL3:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			return chan->l3prot;
		case ISDN_CMD_GETEAZ:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			printk(KERN_DEBUG "act2000 CMD_GETEAZ not implemented\n");
			return 0;
		case ISDN_CMD_SETSIL:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			printk(KERN_DEBUG "act2000 CMD_SETSIL not implemented\n");
			return 0;
		case ISDN_CMD_GETSIL:
			if (!card->flags & ACT2000_FLAGS_RUNNING)
				return -ENODEV;
			printk(KERN_DEBUG "act2000 CMD_GETSIL not implemented\n");
			return 0;
		case ISDN_CMD_LOCK:
			MOD_INC_USE_COUNT;
			return 0;
		case ISDN_CMD_UNLOCK:
			MOD_DEC_USE_COUNT;
			return 0;
        }
	
        return -EINVAL;
}

int
act2000_sendbuf(act2000_card *card, int channel, struct sk_buff *skb)
{
        struct sk_buff *xmit_skb;
        int len;
        act2000_chan *chan;
	actcapi_msg *msg;

        if (!(chan = find_channel(card, channel)))
		return -1;
        if (chan->fsm_state != ACT2000_STATE_ACTIVE)
                return -1;
        len = skb->len;
        if ((chan->queued + len) >= ACT2000_MAX_QUEUED)
                return 0;
	if (!len)
		return 0;
	if (skb_headroom(skb) < 19) {
		printk(KERN_WARNING "act2000_sendbuf: Headroom only %d\n",
		       skb_headroom(skb));
		xmit_skb = alloc_skb(len + 19, GFP_ATOMIC);
		if (!xmit_skb) {
			printk(KERN_WARNING "act2000_sendbuf: Out of memory\n");
			return 0;
		}
		skb_reserve(xmit_skb, 19);
		memcpy(skb_put(xmit_skb, len), skb->data, len);
		dev_kfree_skb(skb, FREE_WRITE);
	} else
		xmit_skb = skb;
	msg = (actcapi_msg *)skb_push(xmit_skb, 19);
	msg->hdr.len = 19 + len;
	msg->hdr.applicationID = 1;
	msg->hdr.cmd.cmd = 0x86;
	msg->hdr.cmd.subcmd = 0x00;
	msg->hdr.msgnum = actcapi_nextsmsg(card);
	msg->msg.data_b3_req.datalen = len;
	msg->msg.data_b3_req.blocknr = (msg->hdr.msgnum & 0xff);
	msg->msg.data_b3_req.fakencci = MAKE_NCCI(chan->plci, 0, chan->ncci);
	msg->msg.data_b3_req.flags = 0;
	actcapi_debug_msg(xmit_skb, 1);
        chan->queued += len;
	skb_queue_tail(&card->sndq, xmit_skb);
	act2000_schedule_tx(card);
        return len;
}


/* Read the Status-replies from the Interface */
static int
act2000_readstatus(u_char * buf, int len, int user, act2000_card * card)
{
        int count;
        u_char *p;

        for (p = buf, count = 0; count < len; p++, count++) {
                if (card->status_buf_read == card->status_buf_write)
                        return count;
                if (user)
                        put_user(*card->status_buf_read++, p);
                else
                        *p = *card->status_buf_read++;
                if (card->status_buf_read > card->status_buf_end)
                        card->status_buf_read = card->status_buf;
        }
        return count;
}

static void
act2000_putmsg(act2000_card *card, char c)
{
        ulong flags;

        save_flags(flags);
        cli();
        *card->status_buf_write++ = c;
        if (card->status_buf_write == card->status_buf_read) {
                if (++card->status_buf_read > card->status_buf_end)
                card->status_buf_read = card->status_buf;
        }
        if (card->status_buf_write > card->status_buf_end)
                card->status_buf_write = card->status_buf;
        restore_flags(flags);
}

static void
act2000_logstat(struct act2000_card *card, char *str)
{
        char *p = str;
        isdn_ctrl c;

	while (*p)
		act2000_putmsg(card, *p++);
        c.command = ISDN_STAT_STAVAIL;
        c.driver = card->myid;
        c.arg = strlen(str);
        card->interface.statcallb(&c);
}

/*
 * Find card with given driverId
 */
static inline act2000_card *
act2000_findcard(int driverid)
{
        act2000_card *p = cards;

        while (p) {
                if (p->myid == driverid)
                        return p;
                p = p->next;
        }
        return (act2000_card *) 0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int
if_command(isdn_ctrl * c)
{
        act2000_card *card = act2000_findcard(c->driver);

        if (card)
                return (act2000_command(card, c));
        printk(KERN_ERR
             "act2000: if_command %d called with invalid driverId %d!\n",
               c->command, c->driver);
        return -ENODEV;
}

static int
if_writecmd(const u_char * buf, int len, int user, int id, int channel)
{
        act2000_card *card = act2000_findcard(id);

        if (card) {
                if (!card->flags & ACT2000_FLAGS_RUNNING)
                        return -ENODEV;
                return (len);
        }
        printk(KERN_ERR
               "act2000: if_writecmd called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_readstatus(u_char * buf, int len, int user, int id, int channel)
{
        act2000_card *card = act2000_findcard(id);
	
        if (card) {
                if (!card->flags & ACT2000_FLAGS_RUNNING)
                        return -ENODEV;
                return (act2000_readstatus(buf, len, user, card));
        }
        printk(KERN_ERR
               "act2000: if_readstatus called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_sendbuf(int id, int channel, struct sk_buff *skb)
{
        act2000_card *card = act2000_findcard(id);
	
        if (card) {
                if (!card->flags & ACT2000_FLAGS_RUNNING)
                        return -ENODEV;
		return (act2000_sendbuf(card, channel, skb));
        }
        printk(KERN_ERR
               "act2000: if_sendbuf called with invalid driverId!\n");
        return -ENODEV;
}


/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list.
 */
static void
act2000_alloccard(int bus, int port, int irq, char *id)
{
	int i;
        act2000_card *card;
        if (!(card = (act2000_card *) kmalloc(sizeof(act2000_card), GFP_KERNEL))) {
                printk(KERN_WARNING
		       "act2000: (%s) Could not allocate card-struct.\n", id);
                return;
        }
        memset((char *) card, 0, sizeof(act2000_card));
	skb_queue_head_init(&card->sndq);
	skb_queue_head_init(&card->rcvq);
	skb_queue_head_init(&card->ackq);
	card->snd_tq.routine = (void *) (void *) act2000_transmit;
	card->snd_tq.data = card;
	card->rcv_tq.routine = (void *) (void *) actcapi_dispatch;
	card->rcv_tq.data = card;
	init_timer(&card->ptimer);
        card->interface.channels = ACT2000_BCH;
        card->interface.maxbufsize = 4000;
        card->interface.command = if_command;
        card->interface.writebuf_skb = if_sendbuf;
        card->interface.writecmd = if_writecmd;
        card->interface.readstat = if_readstatus;
        card->interface.features =
		ISDN_FEATURE_L2_X75I |
		ISDN_FEATURE_L2_HDLC |
		ISDN_FEATURE_L2_TRANS |
		ISDN_FEATURE_L3_TRANS |
		ISDN_FEATURE_P_UNKNOWN;
        card->interface.hl_hdrlen = 20;
        card->ptype = ISDN_PTYPE_EURO;
        strncpy(card->interface.id, id, sizeof(card->interface.id) - 1);
        for (i=0; i<ACT2000_BCH; i++) {
                card->bch[i].plci = 0x8000;
                card->bch[i].ncci = 0x8000;
                card->bch[i].l2prot = ISDN_PROTO_L2_X75I;
                card->bch[i].l3prot = ISDN_PROTO_L3_TRANS;
        }
        card->myid = -1;
        card->bus = bus;
        card->port = port;
        card->irq = irq;
        card->next = cards;
        cards = card;
}

/*
 * register card at linklevel
 */
static int
act2000_registercard(act2000_card * card)
{
        switch (card->bus) {
		case ACT2000_BUS_ISA:
			break;
		case ACT2000_BUS_MCA:
		case ACT2000_BUS_PCMCIA:
		default:
			printk(KERN_WARNING
			       "act2000: Illegal BUS type %d\n",
			       card->bus);
			return -1;
        }
        if (!register_isdn(&card->interface)) {
                printk(KERN_WARNING
                       "act2000: Unable to register %s\n",
                       card->interface.id);
                return -1;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "act2000-isdn (%s)", card->interface.id);
        return 0;
}

static
void unregister_card(act2000_card * card)
{
        isdn_ctrl cmd;

        cmd.command = ISDN_STAT_UNLOAD;
        cmd.driver = card->myid;
        card->interface.statcallb(&cmd);
        switch (card->bus) {
        case ACT2000_BUS_ISA:
                isa_release(card);
                break;
        case ACT2000_BUS_MCA:
        case ACT2000_BUS_PCMCIA:
        default:
                printk(KERN_WARNING
                       "act2000: Invalid BUS type %d\n",
                       card->bus);
                break;
        }
}

#if 0
static int
act2000_addcard(int bus, int port, int irq, int proto, char *id)
{
        ulong flags;
        act2000_card *card;

        save_flags(flags);
        cli();
        /*
           if (!(card = act2000_initcard(port, id))) {
           restore_flags(flags);
           return -EIO;
           }
           restore_flags(flags);
         */
        printk(KERN_INFO
               "act2000: %s added\n",
               card->interface.id);
        return 0;
}
#endif

#ifdef MODULE
#define act2000_init init_module
#endif

int
act2000_init(void)
{
        act2000_card *p = cards;
        act2000_card *q = NULL;
        int anycard = 0;
        int found;

        printk(KERN_INFO "IBM Active 2000 ISDN driver\n");
        if (!cards) {
                /* No cards defined via kernel-commandline */
                if (act_bus) {
                        /* card defined via insmod parameters */
                        act2000_alloccard(act_bus, act_port, act_irq, act_id);
                } else {
                        int i;
                        /* no cards defined at all, perform autoprobing */
                        for (i = 0; i < ISA_NRPORTS; i++)
                                if (isa_detect(isa_ports[i])) {
                                        act2000_alloccard(ACT2000_BUS_ISA,
                                                  isa_ports[i], -1, "\0");
                                        printk(KERN_INFO
                                               "act2000: Detected ISA card at port 0x%x\n",
                                               isa_ports[i]);
                                }
                        /* Todo: Scanning for PCMCIA and MCA cards */
                }
        }
        p = cards;
        while (p) {
                found = 0;
                switch (p->bus) {
			case ACT2000_BUS_ISA:
				if (isa_detect(p->port)) {
					if (act2000_registercard(p))
						break;
					if (isa_config_port(p, p->port)) {
						printk(KERN_WARNING
						       "act2000: Could not request port\n");
						unregister_card(p);
						break;
					}
					if (isa_config_irq(p, p->irq)) {
						printk(KERN_INFO
						       "act2000: Could not request irq\n");
#if 0
						unregister_card(p);
						break;
#else
						p->irq = 0;
#endif
					}
					printk(KERN_INFO
					       "act2000: ISA"
					       "-type card at port "
					       "0x%04x ",
					       p->port);
					if (p->irq)
						printk("irq %d\n", p->irq);
					else
						printk("polled\n");
					found = 1;
				}
				break;
			case ACT2000_BUS_MCA:
			case ACT2000_BUS_PCMCIA:
			default:
				printk(KERN_WARNING
				       "act2000: init: Invalid BUS type %d\n",
				       p->bus);
                }
                if (found) {
                        anycard++;
                        q = p;
                        p = p->next;
                } else {
                        /* Init failed, remove card from list, free memory */
                        printk(KERN_WARNING
                               "act2000: Initialization of %s failed\n",
                               p->interface.id);
                        if (q) {
                                q->next = p->next;
                                kfree(p);
                                p = q->next;
                        } else {
                                cards = p->next;
                                kfree(p);
                                p = cards;
                        }
                }
        }
        if (!anycard)
                printk(KERN_INFO "act2000: No cards defined yet\n");
        /* No symbols to export, hide all symbols */
        EXPORT_NO_SYMBOLS;
	
        return 0;
}

#ifdef MODULE
void
cleanup_module(void)
{
        act2000_card *card = cards;
        act2000_card *last;
        /*                                                                                                                                                                                                      act2000_stopallcards(); */
        while (card) {
                unregister_card(card);
		del_timer(&card->ptimer);
                card = card->next;
        }
        card = cards;
        while (card) {
                last = card;
                card = card->next;
                kfree(last);
        }
        printk(KERN_INFO "IBM act2000 driver unloaded\n");
}

#else
void
act2000_setup(char *str, int *ints)
{
        int i,
         j,
         argc;

        argc = ints[0];
        i = 1;

        if (argc)
                while (argc) {
                        port = irq = proto = bus = -1;
                        if (argc) {
                                bus = ints[i];
                                i++;
                                argc--;
                        }
                        if (argc) {
                                port = ints[i];
                                i++;
                                argc--;
                        }
                        if (argc) {
                                irq = ints[i];
                                i++;
                                argc--;
                        }
                        if (argc) {
                                proto = ints[i];
                                i++;
                                argc--;
                        }
                        if (proto == -1)
                                continue;
                        switch (bus) {
                        case ACT2000_BUS_ISA:
                                act2000_alloccard(bus, port, irq, proto, idstr);
                                break;
                        case ACT2000_BUS_MCA:
                        case ACT2000_BUS_PCMCIA:
                        default:
                                printk(KERN_WARNING
                                       "act2000: Unknown BUS type %d\n",
                                       bus);
                                break;
                        }
        } else {
        }
}
#endif
