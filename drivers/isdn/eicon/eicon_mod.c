/* $Id$
 *
 * ISDN lowlevel-module for Eicon.Diehl active cards.
 * 
 * Copyright 1997    by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1998,99 by Armin Schindler (mac@topmail.de) 
 * Copyright 1999    Cytronics & Melware (cytronics-melware@topmail.de)
 * 
 * Thanks to    Eicon Technology Diehl GmbH & Co. oHG for
 *              documents, informations and hardware.
 *
 *              Deutsche Telekom AG for S2M support.
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
 * Revision 1.1  1999/01/01 18:09:44  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include "eicon.h"

static diehl_card *cards = (diehl_card *) NULL;

static char *diehl_revision = "$Revision$";

extern char *diehl_pci_revision;
extern char *diehl_isa_revision;
extern char *diehl_idi_revision;

#ifdef MODULE
#define MOD_USE_COUNT (GET_USE_COUNT (&__this_module))
#endif

ulong DebugVar;

/* Parameters to be set by insmod */
static int   type         = -1;
static int   membase      = -1;
static int   irq          = -1;
static char *id           = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

MODULE_DESCRIPTION(             "Driver for Eicon.Diehl active ISDN cards");
MODULE_AUTHOR(                  "Armin Schindler");
MODULE_SUPPORTED_DEVICE(        "ISDN subsystem");
MODULE_PARM_DESC(type,		"Type of first card");
MODULE_PARM_DESC(membase,	"Base address, if ISA card");
MODULE_PARM_DESC(irq,    	"IRQ of card");
MODULE_PARM_DESC(id,   		"ID-String of first card");
MODULE_PARM(type,   	      	"i");
MODULE_PARM(membase,    	"i");
MODULE_PARM(irq,          	"i");
MODULE_PARM(id,           	"s");

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


static char *
diehl_getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else rev = "?.??";
	return rev;

}

static diehl_chan *
find_channel(diehl_card *card, int channel)
{
	if ((channel >= 0) && (channel < card->nchannels))
        	return &(card->bch[channel]);
	if (DebugVar & 1)
		printk(KERN_WARNING "eicon: Invalid channel %d\n", channel);
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

#if 0
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
				if (DebugVar & 8)
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
			if (DebugVar & 8)
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
	if (DebugVar & 8)
		printk(KERN_DEBUG
		       "Mapping %c -> %s added\n",
		       eazmsn[0],
		       &eazmsn[1]);
	return 0;
}
#endif

static void
diehl_rcv_dispatch(struct diehl_card *card)
{
	switch (card->bus) {
		case DIEHL_BUS_ISA:
			break;
		case DIEHL_BUS_PCI:
#if CONFIG_PCI
			diehl_pci_rcv_dispatch(&card->hwif.pci);
			break;
#endif
		case DIEHL_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon_ack_dispatch: Illegal bustype %d\n", card->bus);
	}
}

static void
diehl_ack_dispatch(struct diehl_card *card)
{
	switch (card->bus) {
		case DIEHL_BUS_ISA:
			break;
		case DIEHL_BUS_PCI:
#if CONFIG_PCI
			diehl_pci_ack_dispatch(&card->hwif.pci);
			break;
#endif
		case DIEHL_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
			       		"eicon_ack_dispatch: Illegal bustype %d\n", card->bus);
	}
}

static void
diehl_transmit(struct diehl_card *card)
{
	switch (card->bus) {
		case DIEHL_BUS_ISA:
			diehl_isa_transmit(&card->hwif.isa);
			break;
		case DIEHL_BUS_PCI:
#if CONFIG_PCI
			diehl_pci_transmit(&card->hwif.pci);
			break;
#endif
		case DIEHL_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon_transmit: Illegal bustype %d\n", card->bus);
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
							if (DebugVar & 1)
								printk(KERN_WARNING
								       "eicon: Illegal BUS type %d\n",
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
							if (DebugVar & 1)
								printk(KERN_WARNING
							      		"eicon: Illegal BUS type %d\n",
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
							if (DebugVar & 1)
								printk(KERN_WARNING
								       "eicon: Illegal BUS type %d\n",
							       card->bus);
							ret = -ENODEV;
					}
					return ret;

				case DIEHL_IOCTL_MANIF:
					if (!card->flags & DIEHL_FLAGS_RUNNING)
						return -ENODEV;
					ret = eicon_idi_manage(
						card, 
						(eicon_manifbuf *)a);
					return ret;
#if CONFIG_PCI 
				case DIEHL_IOCTL_LOADPCI:
                                                if (card->bus == DIEHL_BUS_PCI) {  
							switch(card->type) {
								case DIEHL_CTYPE_MAESTRA:
                                                		        ret = diehl_pci_load_bri(
		                                                                &(card->hwif.pci),
                		                                                &(((diehl_codebuf *)a)->pci)); 
									break;

								case DIEHL_CTYPE_MAESTRAP:
		                                                        ret = diehl_pci_load_pri(
                		                                                &(card->hwif.pci),
                                		                                &(((diehl_codebuf *)a)->pci)); 
									break;
							}
                                                        if (!ret) {
                                                                card->flags |= DIEHL_FLAGS_LOADED;
                                                                card->flags |= DIEHL_FLAGS_RUNNING;
								cmd.command = ISDN_STAT_RUN;    
								cmd.driver = card->myid;        
								cmd.arg = 0;                    
								card->interface.statcallb(&cmd);
							} 
                                                        return ret;
						} else return -ENODEV;
#endif
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
				case DIEHL_IOCTL_DEBUGVAR:
					DebugVar = a;
					printk(KERN_DEBUG"eicon: Debug Value set to %ld\n", DebugVar);
					return 0;
#ifdef MODULE
				case DIEHL_IOCTL_FREEIT:
					while (MOD_USE_COUNT > 0) MOD_DEC_USE_COUNT;
					MOD_INC_USE_COUNT;
					return 0;
#endif
				default:
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			save_flags(flags);
			cli();
			if ((chan->fsm_state != DIEHL_STATE_NULL) && (chan->fsm_state != DIEHL_STATE_LISTEN)) {
				restore_flags(flags);
				if (DebugVar & 1)
					printk(KERN_WARNING "Dial on channel %d with state %d\n",
					chan->No, chan->fsm_state);
				return -EBUSY;
			}
			if (card->ptype == ISDN_PTYPE_EURO)
				tmp[0] = diehl_find_msn(card, c->parm.setup.eazmsn, 1);
			else
				tmp[0] = c->parm.setup.eazmsn[0];
			chan->fsm_state = DIEHL_STATE_OCALL;
			chan->callref = 0xffff;
			restore_flags(flags);
			
			ret = idi_connect_req(card, chan, c->parm.setup.phone,
						     c->parm.setup.eazmsn,
						     c->parm.setup.si1,
						     c->parm.setup.si2);
			if (ret) {
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg &= 0x1f;
				card->interface.statcallb(&cmd);
			}
			return ret;
		case ISDN_CMD_ACCEPTD:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			if (chan->fsm_state == DIEHL_STATE_ICALL) { 
				idi_connect_res(card, chan);
			}
			return 0;
		case ISDN_CMD_ACCEPTB:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			return 0;
		case ISDN_CMD_HANGUP:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			idi_hangup(card, chan);
			return 0;
		case ISDN_CMD_SETEAZ:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
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
			diehl_idi_listen_req(card, chan);
			return 0;
		case ISDN_CMD_CLREAZ:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->eazmask = 0;
			diehl_idi_listen_req(card, chan);
			return 0;
		case ISDN_CMD_SETL2:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->l2prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL2:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			return chan->l2prot;
		case ISDN_CMD_SETL3:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if ((c->arg >> 8) != ISDN_PROTO_L3_TRANS) {
				if (DebugVar & 1)
					printk(KERN_WARNING "L3 protocol unknown\n");
				return -1;
			}
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			chan->l3prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_GETL3:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x1f)))
				break;
			return chan->l3prot;
		case ISDN_CMD_GETEAZ:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (DebugVar & 1)
				printk(KERN_DEBUG "eicon CMD_GETEAZ not implemented\n");
			return 0;
		case ISDN_CMD_SETSIL:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (DebugVar & 1)
				printk(KERN_DEBUG "eicon CMD_SETSIL not implemented\n");
			return 0;
		case ISDN_CMD_GETSIL:
			if (!card->flags & DIEHL_FLAGS_RUNNING)
				return -ENODEV;
			if (DebugVar & 1)
				printk(KERN_DEBUG "eicon CMD_GETSIL not implemented\n");
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
             "eicon: if_command %d called with invalid driverId %d!\n",
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
               "eicon: if_writecmd called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_readstatus(u_char * buf, int len, int user, int id, int channel)
{
#if 0
	/* Not yet used */
        diehl_card *card = diehl_findcard(id);
	
        if (card) {
                if (!card->flags & DIEHL_FLAGS_RUNNING)
                        return -ENODEV;
                return (diehl_readstatus(buf, len, user, card));
        }
        printk(KERN_ERR
               "eicon: if_readstatus called with invalid driverId!\n");
#endif
        return -ENODEV;
}

static int
if_sendbuf(int id, int channel, int ack, struct sk_buff *skb)
{
        diehl_card *card = diehl_findcard(id);
	diehl_chan *chan;
	
        if (card) {
                if (!card->flags & DIEHL_FLAGS_RUNNING) {
			dev_kfree_skb(skb);
                        return -ENODEV;
		}
        	if (!(chan = find_channel(card, channel))) {
			dev_kfree_skb(skb);
			return -ENODEV;
		}
		if (chan->fsm_state == DIEHL_STATE_ACTIVE)
			return (idi_send_data(card, chan, ack, skb));
		else {
			dev_kfree_skb(skb);
			return -ENODEV;
		}
        }
        printk(KERN_ERR
               "eicon: if_sendbuf called with invalid driverId!\n");
	dev_kfree_skb(skb);
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
			       "eicon: (%s) Could not allocate card-struct.\n", id);
			return;
		}
		memset((char *) card, 0, sizeof(diehl_card));
		skb_queue_head_init(&card->sndq);
		skb_queue_head_init(&card->rcvq);
		skb_queue_head_init(&card->rackq);
		skb_queue_head_init(&card->sackq);
		card->snd_tq.routine = (void *) (void *) diehl_transmit;
		card->snd_tq.data = card;
		card->rcv_tq.routine = (void *) (void *) diehl_rcv_dispatch;
		card->rcv_tq.data = card;
		card->ack_tq.routine = (void *) (void *) diehl_ack_dispatch;
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
			case DIEHL_CTYPE_MAESTRA:
				(diehl_pci_card *)pcic = (diehl_pci_card *)membase;
                                card->bus = DIEHL_BUS_PCI;
				card->interface.features |=
					ISDN_FEATURE_L2_V11096 |
					ISDN_FEATURE_L2_V11019 |
					ISDN_FEATURE_L2_V11038 |
					ISDN_FEATURE_L2_MODEM;
                                card->hwif.pci.card = (void *)card;
				card->hwif.pci.PCIreg = pcic->PCIreg;
				card->hwif.pci.PCIcfg = pcic->PCIcfg;
                                card->hwif.pci.master = 1;
                                card->hwif.pci.mvalid = pcic->mvalid;
                                card->hwif.pci.ivalid = 0;
                                card->hwif.pci.irq = irq;
                                card->hwif.pci.type = type;
				card->flags = 0;
                                card->nchannels = 2;
				break;

			case DIEHL_CTYPE_MAESTRAP:
				(diehl_pci_card *)pcic = (diehl_pci_card *)membase;
                                card->bus = DIEHL_BUS_PCI;
				card->interface.features |=
					ISDN_FEATURE_L2_V11096 |
					ISDN_FEATURE_L2_V11019 |
					ISDN_FEATURE_L2_V11038 |
					ISDN_FEATURE_L2_MODEM;
                                card->hwif.pci.card = (void *)card;
                                card->hwif.pci.shmem = (diehl_pci_shmem *)pcic->shmem;
				card->hwif.pci.PCIreg = pcic->PCIreg;
				card->hwif.pci.PCIram = pcic->PCIram;
				card->hwif.pci.PCIcfg = pcic->PCIcfg;
                                card->hwif.pci.master = 1;
                                card->hwif.pci.mvalid = pcic->mvalid;
                                card->hwif.pci.ivalid = 0;
                                card->hwif.pci.irq = irq;
                                card->hwif.pci.type = type;
				card->flags = 0;
                                card->nchannels = 30;
				break;
#endif
			default:
				printk(KERN_WARNING "eicon_alloccard: Invalid type %d\n", type);
				kfree(card);
				return;
		}
		if (!(card->bch = (diehl_chan *) kmalloc(sizeof(diehl_chan) * (card->nchannels + 1)
							 , GFP_KERNEL))) {
			printk(KERN_WARNING
			       "eicon: (%s) Could not allocate bch-struct.\n", id);
			kfree(card);
			return;
		}
		card->interface.channels = card->nchannels;
		for (j=0; j< (card->nchannels + 1); j++) {
			memset((char *)&card->bch[j], 0, sizeof(diehl_chan));
			card->bch[j].plci = 0x8000;
			card->bch[j].ncci = 0x8000;
			card->bch[j].l2prot = ISDN_PROTO_L2_X75I;
			card->bch[j].l3prot = ISDN_PROTO_L3_TRANS;
			card->bch[j].e.D3Id = 0;
			card->bch[j].e.B2Id = 0;
			card->bch[j].e.Req = 0;
			card->bch[j].No = j;
			skb_queue_head_init(&card->bch[j].e.X);
			skb_queue_head_init(&card->bch[j].e.R);
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
#if CONFIG_PCI
			diehl_pci_printpar(&card->hwif.pci); 
			break;
#endif
		case DIEHL_BUS_MCA:
		default:
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon_registercard: Illegal BUS type %d\n",
			       card->bus);
			return -1;
        }
        if (!register_isdn(&card->interface)) {
                printk(KERN_WARNING
                       "eicon_registercard: Unable to register %s\n",
                       card->interface.id);
                return -1;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "eicon-isdn (%s)", card->interface.id);
        return 0;
}

#ifdef MODULE
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
			if (DebugVar & 1)
				printk(KERN_WARNING
				       "eicon: Invalid BUS type %d\n",
			       card->bus);
			break;
        }
}
#endif /* MODULE */

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
					if (DebugVar & 1)
						printk(KERN_WARNING
						       "eicon: addcard: Invalid BUS type %d\n",
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
                               "eicon: Initialization of %s failed\n",
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

#define DRIVERNAME "Eicon active ISDN driver"

#ifdef MODULE
#define eicon_init init_module
#endif

int
eicon_init(void)
{
	int tmp = 0;
	char tmprev[50];

	DebugVar = 1;

        printk(KERN_INFO "%s Rev: ", DRIVERNAME);
	strcpy(tmprev, diehl_revision);
	printk("%s/", diehl_getrev(tmprev));
	strcpy(tmprev, diehl_pci_revision);
	printk("%s/", diehl_getrev(tmprev));
	strcpy(tmprev, diehl_isa_revision);
	printk("%s/", diehl_getrev(tmprev));
	strcpy(tmprev, diehl_idi_revision);
	printk("%s\n", diehl_getrev(tmprev));

        if ((!cards) && (type != -1))
		tmp = diehl_addcard(type, membase, irq, id);
#if CONFIG_PCI
	tmp += diehl_pci_find_card(id);
#endif
        if (!cards)
                printk(KERN_INFO "eicon: No cards defined yet\n");
	else
		printk(KERN_INFO "eicon: %d card%s added\n", tmp, (tmp>1)?"s":"");
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
eicon_setup(char *str, int *ints)
{
        int i, argc, membase, irq, type;
	
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
			diehl_addcard(type, membase, irq, id);
		}
}
#endif
