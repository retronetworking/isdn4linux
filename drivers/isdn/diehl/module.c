/* $Id$
 *
 * ISDN lowlevel-module for Diehl active cards.
 *
 * Copyright 1997 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1998 by Armin Schindler (mac@gismo.telekom.de) 
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
 * Revision 1.1  1998/06/04 10:23:37  fritz
 * First check in. YET UNUSABLE!
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include "diehl.h"
/* #include "capi.h" */

static diehl_card *cards = (diehl_card *) NULL;

/* Parameters to be set by insmod */
static int   diehl_type         = -1;
static int   diehl_membase      = -1;
static int   diehl_irq          = -1;
static char *diehl_id           = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

MODULE_DESCRIPTION(             "Driver for Diehl active ISDN cards");
MODULE_AUTHOR(                  "Fritz Elfert");
MODULE_SUPPORTED_DEVICE(        "ISDN subsystem");
MODULE_PARM_DESC(diehl_type,    "Type of first card");
MODULE_PARM_DESC(diehl_membase, "Base address, if ISA card");
MODULE_PARM_DESC(diehl_irq,     "IRQ of card");
MODULE_PARM_DESC(diehl_id,      "ID-String of first card");
MODULE_PARM(diehl_type,         "i");
MODULE_PARM(diehl_membase,      "i");
MODULE_PARM(diehl_irq,          "i");
MODULE_PARM(diehl_id,           "s");

char *diehl_ctype_name[] = {
	"ISDN-S",
	"ISDN-SX",
	"ISDN-SCOM",
	"ISDN-QUADRO",
	"ISDN-PRI",
	"DIVA Server BRI/PCI",
	"DIVA Server 4BRI/PCI",
	"DIVA Server 4BRI/PCI",
	"DIVA Server PRI/PCI"
};

static diehl_chan *
find_channel(diehl_card *card, int channel)
{
	if ((channel >= 0) && (channel < card->nchannels))
        	return &(card->bch[channel]);
	printk(KERN_WARNING "diehl: Invalid channel %d\n", channel);
	return NULL;
}

/*
 * Free MSN list
 */
static void
diehl_clear_msn(diehl_card *card)
{
        struct msn_entry *p = card->msn_list;
        struct msn_entry *q;
	unsigned long flags;

	save_flags(flags);
	cli();
        card->msn_list = NULL;
	restore_flags(flags);
        while (p) {
                q  = p->next;
                kfree(p);
                p = q;
        }
}

/*
 * Find an MSN entry in the list.
 * If ia5 != 0, return IA5-encoded EAZ, else
 * return a bitmask with corresponding bit set.
 */
static __u16
diehl_find_msn(diehl_card *card, char *msn, int ia5)
{
        struct msn_entry *p = card->msn_list;
	__u8 eaz = '0';

	while (p) {
		if (!strcmp(p->msn, msn)) {
			eaz = p->eaz;
			break;
		}
		p = p->next;
	}
	if (!ia5)
		return (1 << (eaz - '0'));
	else
		return eaz;
}

/*
 * Find an EAZ entry in the list.
 * return a string with corresponding msn.
 */
char *
diehl_find_eaz(diehl_card *card, char eaz)
{
        struct msn_entry *p = card->msn_list;

	while (p) {
		if (p->eaz == eaz)
			return(p->msn);
		p = p->next;
	}
	return("\0");
}

/*
 * Add or delete an MSN to the MSN list
 *
 * First character of msneaz is EAZ, rest is MSN.
 * If length of eazmsn is 1, delete that entry.
 */
static int
diehl_set_msn(diehl_card *card, char *eazmsn)
{
        struct msn_entry *p = card->msn_list;
        struct msn_entry *q = NULL;
	unsigned long flags;
	int i;
	
	if (!strlen(eazmsn))
		return 0;
	if (strlen(eazmsn) > 16)
		return -EINVAL;
	for (i = 0; i < strlen(eazmsn); i++)
		if (!isdigit(eazmsn[i]))
			return -EINVAL;
        if (strlen(eazmsn) == 1) {
		/* Delete a single MSN */
		while (p) {
			if (p->eaz == eazmsn[0]) {
				save_flags(flags);
				cli();
				if (q)
					q->next = p->next;
				else
					card->msn_list = p->next;
				restore_flags(flags);
				kfree(p);
				printk(KERN_DEBUG
				       "Mapping for EAZ %c deleted\n",
				       eazmsn[0]);
				return 0;
			}
			q = p;
			p = p->next;
		}
		return 0;
        }
	/* Add a single MSN */
	while (p) {
		/* Found in list, replace MSN */
		if (p->eaz == eazmsn[0]) {
			save_flags(flags);
			cli();
			strcpy(p->msn, &eazmsn[1]);
			restore_flags(flags);
			printk(KERN_DEBUG
			       "Mapping for EAZ %c changed to %s\n",
			       eazmsn[0],
			       &eazmsn[1]);
			return 0;
		}
		p = p->next;
	}
	/* Not found in list, add new entry */
	p = kmalloc(sizeof(msn_entry), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->eaz = eazmsn[0];
	strcpy(p->msn, &eazmsn[1]);
	p->next = card->msn_list;
	save_flags(flags);
	cli();
	card->msn_list = p;
	restore_flags(flags);
	printk(KERN_DEBUG
	       "Mapping %c -> %s added\n",
	       eazmsn[0],
	       &eazmsn[1]);
	return 0;
}

static void
diehl_transmit(struct diehl_card *card)
{
	switch (card->bus) {
		case DIEHL_BUS_ISA:
			diehl_isa_transmit(&card->hwif.isa);
			break;
		case DIEHL_BUS_PCI:
		case DIEHL_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "diehl_transmit: Illegal bustype %d\n", card->bus);
	}
}

static void
diehl_receive(struct diehl_card *card)
{
	switch (card->bus) {
		case DIEHL_BUS_ISA:
			break;
		case DIEHL_BUS_PCI:
		case DIEHL_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "diehl_receive: Illegal bustype %d\n", card->bus);
	}
}

static int
diehl_command(diehl_card * card, isdn_ctrl * c)
{
        ulong a;
        diehl_chan *chan;
	diehl_cdef cdef;
	isdn_ctrl cmd;
	char tmp[17];
	int ret = 0;
	unsigned long flags;
 
        switch (c->command) {
		case ISDN_CMD_IOCTL:
			memcpy(&a, c->parm.num, sizeof(ulong));
			switch (c->arg) {
				case DIEHL_IOCTL_GETTYPE:
					return(card->type);
				case DIEHL_IOCTL_GETIRQ:
					switch (card->bus) {
						case DIEHL_BUS_ISA:
							return card->hwif.isa.irq;
#if CONFIG_PCI
						case DIEHL_BUS_PCI:
							return card->hwif.pci.irq;
#endif
						default:
							printk(KERN_WARNING
							       "diehl: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
				case DIEHL_IOCTL_SETIRQ:
					if (card->flags & DIEHL_FLAGS_LOADED)
						return -EBUSY;
					switch (card->bus) {
						case DIEHL_BUS_ISA:
							card->hwif.isa.irq = a;
							return 0;
						default:
							printk(KERN_WARNING
							       "diehl: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}					
				case DIEHL_IOCTL_LOADBOOT:
					switch (card->bus) {
						case DIEHL_BUS_ISA:
							ret = diehl_isa_load(
								&(card->hwif.isa),
								&(((diehl_codebuf *)a)->isa));
							if (!ret)
								card->flags |= DIEHL_FLAGS_LOADED;
							break;
						default:
							printk(KERN_WARNING
							       "diehl: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
					return ret;
#if 0
				case DIEHL_IOCTL_SETMSN:
					if ((ret = copy_from_user(tmp, (char *)a, sizeof(tmp))))
						return -EFAULT;
					if ((ret = diehl_set_msn(card, tmp)))
						return ret;
#if 0
					if (card->flags & DIEHL_FLAGS_RUNNING)
						return(diehl_capi_manufacturer_req_msn(card));
#endif
					return 0;
#endif
				case DIEHL_IOCTL_ADDCARD:
					if ((ret = copy_from_user(&cdef, (char *)a, sizeof(cdef))))
						return -EFAULT;
					if (diehl_addcard(cdef.type, cdef.membase, cdef.irq, cdef.id))
						return -EIO;
					return 0;
				default:
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			save_flags(flags);
			cli();
			if (chan->fsm_state != DIEHL_STATE_NULL) {
				restore_flags(flags);
				printk(KERN_WARNING "Dial on channel with state %d\n",
					chan->fsm_state);
				return -EBUSY;
			}
			if (card->ptype == ISDN_PTYPE_EURO)
				tmp[0] = diehl_find_msn(card, c->parm.setup.eazmsn, 1);
			else
				tmp[0] = c->parm.setup.eazmsn[0];
			chan->fsm_state = DIEHL_STATE_OCALL;
			chan->callref = 0xffff;
			restore_flags(flags);
#if 0
			ret = diehl_capi_connect_req(card, chan, c->parm.setup.phone,
						     tmp[0], c->parm.setup.si1,
						     c->parm.setup.si2);
#endif
			if (ret) {
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg &= 0x0f;
				card->interface.statcallb(&cmd);
			}
			return ret;
		case ISDN_CMD_ACCEPTD:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			if (chan->fsm_state == DIEHL_STATE_ICALL)
#if 0
				diehl_capi_select_b2_protocol_req(card, chan);
#endif
			return 0;
		case ISDN_CMD_ACCEPTB:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			return 0;
		case ISDN_CMD_HANGUP:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			switch (chan->fsm_state) {
				case DIEHL_STATE_ICALL:
				case DIEHL_STATE_BSETUP:
#if 0
					diehl_capi_connect_resp(card, chan, 0x15);
#endif
					break;
				case DIEHL_STATE_ACTIVE:
#if 0
					diehl_capi_disconnect_b3_req(card, chan);
#endif
					break;
			}
			return 0;
		case ISDN_CMD_SETEAZ:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			if (strlen(c->parm.num)) {
				if (card->ptype == ISDN_PTYPE_EURO) {
					chan->eazmask = diehl_find_msn(card, c->parm.num, 0);
				}
				if (card->ptype == ISDN_PTYPE_1TR6) {
					int i;
					chan->eazmask = 0;
					for (i = 0; i < strlen(c->parm.num); i++)
						if (isdigit(c->parm.num[i]))
							chan->eazmask |= (1 << (c->parm.num[i] - '0'));
				}
			} else
				chan->eazmask = 0x3ff;
#if 0
			diehl_capi_listen_req(card);
#endif
			return 0;
		case ISDN_CMD_CLREAZ:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->eazmask = 0;
#if 0
			diehl_capi_listen_req(card);
#endif
			return 0;
		case ISDN_CMD_SETL2:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->l2prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL2:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			return chan->l2prot;
		case ISDN_CMD_SETL3:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
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
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			return chan->l3prot;
		case ISDN_CMD_GETEAZ:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			printk(KERN_DEBUG "diehl CMD_GETEAZ not implemented\n");
			return 0;
		case ISDN_CMD_SETSIL:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			printk(KERN_DEBUG "diehl CMD_SETSIL not implemented\n");
			return 0;
		case ISDN_CMD_GETSIL:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			printk(KERN_DEBUG "diehl CMD_GETSIL not implemented\n");
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

static int
diehl_sendbuf(diehl_card *card, int channel, int ack, struct sk_buff *skb)
{
#if 0
        struct sk_buff *xmit_skb;
        int len;
        diehl_chan *chan;

        if (!(chan = find_channel(card, channel)))
		return -1;
        if (chan->fsm_state != DIEHL_STATE_ACTIVE)
                return -1;
        len = skb->len;
        if ((chan->queued + len) >= DIEHL_MAX_QUEUED)
                return 0;
	if (!len)
		return 0;
	if (skb_headroom(skb) < 19) {
		printk(KERN_WARNING "diehl_sendbuf: Headroom only %d\n",
		       skb_headroom(skb));
		xmit_skb = alloc_skb(len + 19, GFP_ATOMIC);
		if (!xmit_skb) {
			printk(KERN_WARNING "diehl_sendbuf: Out of memory\n");
			return 0;
		}
		skb_reserve(xmit_skb, 19);
		memcpy(skb_put(xmit_skb, len), skb->data, len);
	} else {
		xmit_skb = skb_clone(skb, GFP_ATOMIC);
		if (!xmit_skb) {
			printk(KERN_WARNING "diehl_sendbuf: Out of memory\n");
			return 0;
		}
	}
	dev_kfree_skb(skb);
	msg = (actcapi_msg *)skb_push(xmit_skb, 19);
	msg->hdr.len = 19 + len;
	msg->hdr.applicationID = 1;
	msg->hdr.cmd.cmd = 0x86;
	msg->hdr.cmd.subcmd = 0x00;
	msg->hdr.msgnum = actcapi_nextsmsg(card);
	msg->msg.data_b3_req.datalen = len;
	msg->msg.data_b3_req.blocknr = (msg->hdr.msgnum & 0xff);
	msg->msg.data_b3_req.fakencci = MAKE_NCCI(chan->plci, 0, chan->ncci);
	msg->msg.data_b3_req.flags = ack; /* Will be set to 0 on actual sending */
	actcapi_debug_msg(xmit_skb, 1);
        chan->queued += len;
	skb_queue_tail(&card->sndq, xmit_skb);
	diehl_schedule_tx(card);
        return len;
#endif
	return skb->len;
}


/* Read the Status-replies from the Interface */
static int
diehl_readstatus(u_char * buf, int len, int user, diehl_card * card)
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
diehl_putmsg(diehl_card *card, char c)
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
diehl_logstat(struct diehl_card *card, char *str)
{
        char *p = str;
        isdn_ctrl c;

	while (*p)
		diehl_putmsg(card, *p++);
        c.command = ISDN_STAT_STAVAIL;
        c.driver = card->myid;
        c.arg = strlen(str);
        card->interface.statcallb(&c);
}

/*
 * Find card with given driverId
 */
static inline diehl_card *
diehl_findcard(int driverid)
{
        diehl_card *p = cards;

        while (p) {
                if (p->myid == driverid)
                        return p;
                p = p->next;
        }
        return (diehl_card *) 0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int
if_command(isdn_ctrl * c)
{
        diehl_card *card = diehl_findcard(c->driver);

        if (card)
                return (diehl_command(card, c));
        printk(KERN_ERR
             "diehl: if_command %d called with invalid driverId %d!\n",
               c->command, c->driver);
        return -ENODEV;
}

static int
if_writecmd(const u_char * buf, int len, int user, int id, int channel)
{
        diehl_card *card = diehl_findcard(id);

        if (card) {
                if (!card->flags & DIEHL_FLAGS_RUNNING)
                        return -ENODEV;
                return (len);
        }
        printk(KERN_ERR
               "diehl: if_writecmd called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_readstatus(u_char * buf, int len, int user, int id, int channel)
{
        diehl_card *card = diehl_findcard(id);
	
        if (card) {
                if (!card->flags & DIEHL_FLAGS_RUNNING)
                        return -ENODEV;
                return (diehl_readstatus(buf, len, user, card));
        }
        printk(KERN_ERR
               "diehl: if_readstatus called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_sendbuf(int id, int channel, int ack, struct sk_buff *skb)
{
        diehl_card *card = diehl_findcard(id);
	
        if (card) {
                if (!card->flags & DIEHL_FLAGS_RUNNING)
                        return -ENODEV;
		return (diehl_sendbuf(card, channel, ack, skb));
        }
        printk(KERN_ERR
               "diehl: if_sendbuf called with invalid driverId!\n");
        return -ENODEV;
}


/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list.
 */
static void
diehl_alloccard(int type, int membase, int irq, char *id)
{
	int i;
	int j;
	int qloop;
        diehl_card *card;
#if CONFIG_PCI
	diehl_pci_card *pcic;
#endif

	qloop = (type == DIEHL_CTYPE_QUADRO)?3:0;
	type &= 0x0f;
	for (i = qloop; i >= 0; i--) {
		if (!(card = (diehl_card *) kmalloc(sizeof(diehl_card), GFP_KERNEL))) {
			printk(KERN_WARNING
			       "diehl: (%s) Could not allocate card-struct.\n", id);
			return;
		}
		memset((char *) card, 0, sizeof(diehl_card));
		skb_queue_head_init(&card->sndq);
		skb_queue_head_init(&card->rcvq);
		skb_queue_head_init(&card->rackq);
		skb_queue_head_init(&card->sackq);
		card->snd_tq.routine = (void *) (void *) diehl_transmit;
		card->snd_tq.data = card;
#if 0
		card->rcv_tq.routine = (void *) (void *) diehl_rcv_dispatch;
		card->rcv_tq.data = card;
		card->ack_tq.routine = (void *) (void *) diehl_ack_dispatch;
#endif
		card->ack_tq.data = card;
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
		card->ptype = ISDN_PTYPE_UNKNOWN;
		strncpy(card->interface.id, id, sizeof(card->interface.id) - 1);
		card->myid = -1;
		card->type = type;
		switch (type) {
			case DIEHL_CTYPE_S:
			case DIEHL_CTYPE_SX:
			case DIEHL_CTYPE_SCOM:
			case DIEHL_CTYPE_QUADRO:
				if (membase == -1)
					membase = DIEHL_ISA_MEMBASE;
				if (irq == -1)
					irq = DIEHL_ISA_IRQ;
				card->bus = DIEHL_BUS_ISA;
				card->hwif.isa.card = (void *)card;
				card->hwif.isa.shmem = (diehl_isa_shmem *)membase;
				card->hwif.isa.master = 1;
				if (type == DIEHL_CTYPE_QUADRO) {
					int l;

					card->hwif.isa.shmem = (diehl_isa_shmem *)(membase + i * DIEHL_ISA_QOFFSET);
					card->hwif.isa.master = (i == 0);
					if ((l = strlen(id))) {
						if (l+3 >= sizeof(card->interface.id)) {
							printk(KERN_WARNING
							       "Id-String for Quadro must not exceed %d characters",
							       sizeof(card->interface.id)-4);
							kfree(card);
							return;
						}
						sprintf(card->interface.id, "%s%d/4", id, i+1);
					}
				}
				card->hwif.isa.irq = irq;
				card->hwif.isa.type = type;
				card->nchannels = 2;
				break;
			case DIEHL_CTYPE_PRI:
				if (membase == -1)
					membase = DIEHL_ISA_MEMBASE;
				if (irq == -1)
					irq = DIEHL_ISA_IRQ;
				card->bus = DIEHL_BUS_ISA;
				card->hwif.isa.card = (void *)card;
				card->hwif.isa.shmem = (diehl_isa_shmem *)membase;
				card->hwif.isa.master = 1;
				card->hwif.isa.irq = irq;
				card->hwif.isa.type = type;
				card->nchannels = 30;
				break;
#if CONFIG_PCI
			case DIEHL_CTYPE_MAESTRAP:
				(diehl_pci_card *)pcic = (diehl_pci_card *)membase;
                                card->bus = DIEHL_BUS_PCI;
                                card->hwif.pci.card = (void *)card;
                                card->hwif.pci.shmem = (diehl_pci_shmem *)pcic->shmem;
				card->hwif.pci.PCIreg = (void *)pcic->PCIreg;
				card->hwif.pci.PCIram = (void *)pcic->PCIram;
				card->hwif.pci.PCIcfg = (void *)pcic->PCIcfg;
                                card->hwif.pci.master = 1;
                                card->hwif.pci.mvalid = pcic->mvalid;
                                card->hwif.pci.ivalid = 0;
                                card->hwif.pci.irq = irq;
                                card->hwif.pci.type = type;
                                card->nchannels = 30;
				break;
#endif
			default:
				printk(KERN_WARNING "diehl_alloccard: Invalid type %d\n", type);
				kfree(card);
				return;
		}
		if (!(card->bch = (diehl_chan *) kmalloc(sizeof(diehl_chan) * card->nchannels
							 , GFP_KERNEL))) {
			printk(KERN_WARNING
			       "diehl: (%s) Could not allocate bch-struct.\n", id);
			kfree(card);
			return;
		}
		card->interface.channels = card->nchannels;
		for (j=0; j<card->nchannels; j++) {
			memset((char *)&card->bch[j], 0, sizeof(diehl_chan));
			card->bch[j].plci = 0x8000;
			card->bch[j].ncci = 0x8000;
			card->bch[j].l2prot = ISDN_PROTO_L2_X75I;
			card->bch[j].l3prot = ISDN_PROTO_L3_TRANS;
		}
		card->next = cards;
		cards = card;
	}
}

/*
 * register card at linklevel
 */
static int
diehl_registercard(diehl_card * card)
{
        switch (card->bus) {
		case DIEHL_BUS_ISA:
			diehl_isa_printpar(&card->hwif.isa);
			break;
		case DIEHL_BUS_PCI:
			diehl_pci_printpar(&card->hwif.pci); 
			break;
		case DIEHL_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "diehl_registercard: Illegal BUS type %d\n",
			       card->bus);
			return -1;
        }
        if (!register_isdn(&card->interface)) {
                printk(KERN_WARNING
                       "diehl_registercard: Unable to register %s\n",
                       card->interface.id);
                return -1;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "diehl-isdn (%s)", card->interface.id);
        return 0;
}

static void
unregister_card(diehl_card * card)
{
        isdn_ctrl cmd;

        cmd.command = ISDN_STAT_UNLOAD;
        cmd.driver = card->myid;
        card->interface.statcallb(&cmd);
        switch (card->bus) {
		case DIEHL_BUS_ISA:
			diehl_isa_release(&card->hwif.isa);
			break;
		case DIEHL_BUS_PCI:
#if CONFIG_PCI
			diehl_pci_release(&card->hwif.pci);
			break;
#endif
		case DIEHL_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "diehl: Invalid BUS type %d\n",
			       card->bus);
			break;
        }
}

static void
diehl_freecard(diehl_card *card) {
	diehl_clear_msn(card);
	kfree(card->bch);
	kfree(card);
}

int
diehl_addcard(int type, int membase, int irq, char *id)
{
	diehl_card *p;
	diehl_card *q = NULL;
	int registered;
	int added = 0;
	int failed = 0;

	diehl_alloccard(type, membase, irq, id);
        p = cards;
        while (p) {
		registered = 0;
		if (!p->interface.statcallb) {
			/* Not yet registered.
			 * Try to register and activate it.
			 */
			added++;
			switch (p->bus) {
				case DIEHL_BUS_ISA:
					if (diehl_registercard(p))
						break;
					registered = 1;
					break;
				case DIEHL_BUS_PCI:
#if CONFIG_PCI
					if (diehl_registercard(p))
						break;
					registered = 1;
					break;
#endif
				case DIEHL_BUS_MCA:
				default:
					printk(KERN_WARNING
					       "diehl: addcard: Invalid BUS type %d\n",
					       p->bus);
			}
		} else
			/* Card already registered */
			registered = 1;
                if (registered) {
			/* Init OK, next card ... */
                        q = p;
                        p = p->next;
                } else {
                        /* registering failed, remove card from list, free memory */
                        printk(KERN_WARNING
                               "diehl: Initialization of %s failed\n",
                               p->interface.id);
                        if (q) {
                                q->next = p->next;
                                diehl_freecard(p);
                                p = q->next;
                        } else {
                                cards = p->next;
                                diehl_freecard(p);
                                p = cards;
                        }
			failed++;
                }
	}
        return (added - failed);
}

#define DRIVERNAME "Diehl active ISDN driver"

#ifdef MODULE
#define diehl_init init_module
#endif

int
diehl_init(void)
{
	int tmp = 0;

        printk(KERN_INFO "%s\n", DRIVERNAME);
        if ((!cards) && (diehl_type != -1))
		tmp = diehl_addcard(diehl_type, diehl_membase, diehl_irq, diehl_id);
#if CONFIG_PCI
	tmp += diehl_pci_find_card(diehl_id);
#endif
        if (!cards)
                printk(KERN_INFO "diehl: No cards defined yet\n");
	else
		printk(KERN_INFO "diehl: %d card%s added\n", tmp, (tmp>1)?"s":"");
        /* No symbols to export, hide all symbols */
        EXPORT_NO_SYMBOLS;
        return 0;
}

#ifdef MODULE
void
cleanup_module(void)
{
        diehl_card *card = cards;
        diehl_card *last;
        while (card) {
                unregister_card(card); 
                card = card->next;
        }
        card = cards;
        while (card) {
                last = card;
                card = card->next;
		diehl_freecard(last);
        }
        printk(KERN_INFO "%s unloaded\n", DRIVERNAME);
}

#else
void
diehl_setup(char *str, int *ints)
{
        int i, j, argc, membase, irq, type;
	
        argc = ints[0];
        i = 1;
        if (argc)
                while (argc) {
                        membase = irq = -1;
			type = 0;
                        if (argc) {
                                type = ints[i];
                                i++;
                                argc--;
                        }
                        if (argc) {
                                membase = ints[i];
                                i++;
                                argc--;
                        }
                        if (argc) {
                                irq = ints[i];
                                i++;
                                argc--;
                        }
			diehl_addcard(type, membase, irq, diehl_id);
		}
}
#endif
