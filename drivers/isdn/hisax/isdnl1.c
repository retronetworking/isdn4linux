/* $Id$
 *
 * isdnl1.c     common low level stuff for Siemens Chipsetbased isdn cards
 *              based on the teles driver from Jan den Ouden
 * 
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to	Jan den Ouden
 *	        Fritz Elfert	
 * 	        Beat Doebeli
 *            
 * 
 * $Log$
 * Revision 1.2  1996/10/27 22:16:54  keil
 * ISAC/HSCX version lookup
 *
 * Revision 1.1  1996/10/13 20:04:53  keil
 * Initial revision
 *
 *
 *
*/

const	char	*l1_revision        = "$Revision$";

#define __NO_VERSION__
#include "hisax.h"
#include "isdnl1.h"

#if CARD_TELES0
#include "teles0.h"
#endif

#if CARD_TELES3
#include "teles3.h"
#endif

#if CARD_AVM_A1
#include "avm_a1.h"
#endif

#if CARD_ELSA
#include "elsa.h"
#endif

#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>
#include <linux/interrupt.h>

const   char    *CardType[] =  {"No Card","Teles 16.0","Teles 8.0","Teles 16.3",
				"Creatix PNP","AVM A1","Elsa ML PCC16"};

static	char	*HSCXVer[] = {"A1","?1","A2","?3","A3","V2.1","?6","?7",
			      "?8","?9","?10","?11","?12","?13","?14","???"};

static  char    *ISACVer[] = {"2086/2186 V1.1","2085 B1","2085 B2",
			      "2085 V2.3"};
 
extern void     tei_handler(struct PStack *st, byte pr,
			    struct BufHeader *ibh);
extern struct   IsdnCard cards[];
extern int      nrcards;



void debugl1(struct IsdnCardState *sp, char *msg)
{
	char            tmp[256], tm[32];

	jiftime(tm, jiffies);
	sprintf(tmp, "%s Card %d %s\n", tm, sp->cardnr+1, msg);
	HiSax_putstatus(tmp); 
}

/*
 * HSCX stuff goes here
 */


char *HscxVersion(byte v)
{
	return(HSCXVer[v&0xf]);
}

void
hscx_sched_event(struct HscxState *hsp, int event)
{
	hsp->event |= 1 << event;
	queue_task_irq_off(&hsp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
 * ISAC stuff goes here
 */

char *ISACVersion(byte v)
{
	return(ISACVer[(v>>5)&3]);
}

void
isac_sched_event(struct IsdnCardState *sp, int event)
{
	sp->event |= 1 << event;
	queue_task_irq_off(&sp->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

int
act_wanted(struct IsdnCardState *sp)
{
	struct PStack  *st;

	st = sp->stlist;
	while (st)
		if (st->l1.act_state)
			return (!0);
		else
			st = st->next;
	return (0);
}

void
isac_new_ph(struct IsdnCardState *sp)
{
	int             enq;

	enq = act_wanted(sp);

	switch (sp->ph_state) {
	  case (0):
	  case (6):
		  if (enq)
			  sp->ph_command(sp, 0);
		  else
			  sp->ph_command(sp, 15);
		  break;
	  case (7):
		  if (enq)
			  sp->ph_command(sp, 9);
		  break;
	  case (12):
	          sp->ph_command(sp, 8);
		  sp->ph_active = 5;
		  isac_sched_event(sp, ISAC_PHCHANGE);
		  if (!sp->xmtibh)
			  if (!BufQueueUnlink(&sp->xmtibh, &sp->sq))
				  sp->sendptr = 0;
		  if (sp->xmtibh)
			  sp->isac_fill_fifo(sp);
		  break;
	  case (13):
	          sp->ph_command(sp, 9);
		  sp->ph_active = 5;
		  isac_sched_event(sp, ISAC_PHCHANGE);
		  if (!sp->xmtibh)
			  if (!BufQueueUnlink(&sp->xmtibh, &sp->sq))
				  sp->sendptr = 0;
		  if (sp->xmtibh)
			  sp->isac_fill_fifo(sp);
		  break;
	  case (4):
	  case (8):
		  break;
	  default:
		  sp->ph_active = 0;
		  break;
	}
}

static void
restart_ph(struct IsdnCardState *sp)
{
	switch (sp->ph_active) {
	  case (0):
		  if (sp->ph_state == 6)
			  sp->ph_command(sp, 0);
		  else
			  sp->ph_command(sp, 1);
		  sp->ph_active = 1;
		  break;
	}
}


static void
act_ivated(struct IsdnCardState *sp)
{
	struct PStack  *st;

	st = sp->stlist;
	while (st) {
		if (st->l1.act_state == 1) {
			st->l1.act_state = 2;
			st->l1.l1man(st, PH_ACTIVATE, NULL);
		}
		st = st->next;
	}
}

static void
process_new_ph(struct IsdnCardState *sp)
{
	if (sp->ph_active == 5)
		act_ivated(sp);
}

static void
process_xmt(struct IsdnCardState *sp)
{
	struct PStack  *stptr;

	if (sp->xmtibh)
		return;

	stptr = sp->stlist;
	while (stptr != NULL)
		if (stptr->l1.requestpull) {
			stptr->l1.requestpull = 0;
			stptr->l1.l1l2(stptr, PH_PULL_ACK, NULL);
			break;
		} else
			stptr = stptr->next;
}

static void
process_rcv(struct IsdnCardState *sp)
{
	struct BufHeader *ibh, *cibh;
	struct PStack    *stptr;
	byte             *ptr;
	int              found, broadc;
	char             tmp[64];

	while (!BufQueueUnlink(&ibh, &sp->rq)) {
		stptr = sp->stlist;
		ptr = DATAPTR(ibh);
		broadc = (ptr[1] >> 1) == 127;

		if (broadc) {
			if (sp->dlogflag && (!(ptr[0] >> 2))) {
				LogFrame(sp, ptr, ibh->datasize);
				dlogframe(sp, ptr + 3, ibh->datasize - 3,
					"Q.931 frame network->user broadcast");
			}
			sp->CallFlags = 3;
			while (stptr != NULL) {
				if ((ptr[0] >> 2) == stptr->l2.sap)
					if (!BufPoolGet(&cibh, &sp->rbufpool, GFP_ATOMIC,
							(void *) 1, 5)) {
						memcpy(DATAPTR(cibh), DATAPTR(ibh), ibh->datasize);
						cibh->datasize = ibh->datasize;
						stptr->l1.l1l2(stptr, PH_DATA, cibh);
					} else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
				stptr = stptr->next;
			}
			BufPoolRelease(ibh);
		} else {
			found = 0;
			while (stptr != NULL)
				if (((ptr[0] >> 2) == stptr->l2.sap) &&
				    ((ptr[1] >> 1) == stptr->l2.tei)) {
					stptr->l1.l1l2(stptr, PH_DATA, ibh);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found) {
				/* BD 10.10.95
				 * Print out D-Channel msg not processed
				 * by isdn4linux
                                 */

				if ((!(ptr[0] >> 2)) && (!(ptr[2] & 0x01))) {
					sprintf(tmp,
					"Q.931 frame network->user with tei %d (not for us)",
					ptr[1] >> 1);
					LogFrame(sp, ptr, ibh->datasize);
					dlogframe(sp, ptr + 4, ibh->datasize - 4, tmp);
				}
				BufPoolRelease(ibh);
			}
		}

	}

}

static void
isac_bh(struct IsdnCardState *sp)
{
	if (!sp)
		return;

	if (clear_bit(ISAC_PHCHANGE, &sp->event))
		process_new_ph(sp);
	if (clear_bit(ISAC_RCVBUFREADY, &sp->event))
		process_rcv(sp);
	if (clear_bit(ISAC_XMTBUFREADY, &sp->event))
		process_xmt(sp);
}

static void
l2l1(struct PStack *st, int pr,
	   struct BufHeader *ibh)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;


	switch (pr) {
	  case (PH_DATA):
		  if (sp->xmtibh)
			  BufQueueLink(&sp->sq, ibh);
		  else {
			  sp->xmtibh = ibh;
			  sp->sendptr = 0;
			  sp->releasebuf = !0;
			  sp->isac_fill_fifo(sp);
		  }
		  break;
	  case (PH_DATA_PULLED):
		  if (sp->xmtibh) {
			  if (sp->debug & L1_DEB_WARN) 
        			debugl1(sp, " l2l1 xmtibh exist this shouldn't happen");
			  break;
		  }
		  sp->xmtibh = ibh;
		  sp->sendptr = 0;
		  sp->releasebuf = 0;
		  sp->isac_fill_fifo(sp);
		  break;
	  case (PH_REQUEST_PULL):
		  if (!sp->xmtibh) {
			  st->l1.requestpull = 0;
			  st->l1.l1l2(st, PH_PULL_ACK, NULL);
		  } else
			  st->l1.requestpull = !0;
		  break;
	}
}


static void
hscx_process_xmt(struct HscxState *hsp)
{
	struct PStack  *st = hsp->st;

	if (hsp->xmtibh)
		return;

	if (st->l1.requestpull) {
		st->l1.requestpull = 0;
		st->l1.l1l2(st, PH_PULL_ACK, NULL);
	}
	if (!hsp->active)
		if ((!hsp->xmtibh) && (!hsp->sq.head))
			hsp->sp->modehscx(hsp, 0, 0);
}

static void
hscx_process_rcv(struct HscxState *hsp)
{
	struct BufHeader *ibh;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_process_rcv magic not 301270\n");
		return;
	}
#endif
	while (!BufQueueUnlink(&ibh, &hsp->rq)) {
		hsp->st->l1.l1l2(hsp->st, PH_DATA, ibh);
	}
}

static void
hscx_bh(struct HscxState *hsp)
{

	if (!hsp)
		return;

	if (clear_bit(HSCX_RCVBUFREADY, &hsp->event))
		hscx_process_rcv(hsp);
	if (clear_bit(HSCX_XMTBUFREADY, &hsp->event))
		hscx_process_xmt(hsp);

}

/*
 * interrupt stuff ends here
 */

void
HiSax_addlist(struct IsdnCardState *sp,
	      struct PStack *st)
{
	st->next = sp->stlist;
	sp->stlist = st;
}

void
HiSax_rmlist(struct IsdnCardState *sp,
	     struct PStack *st)
{
	struct PStack  *p;

	if (sp->stlist == st)
		sp->stlist = st->next;
	else {
		p = sp->stlist;
		while (p)
			if (p->next == st) {
				p->next = st->next;
				return;
			} else
				p = p->next;
	}
}

static void
check_ph_act(struct IsdnCardState *sp)
{
	struct PStack  *st = sp->stlist;

	while (st) {
		if (st->l1.act_state)
			return;
		st = st->next;
	}
	sp->ph_active = 0;
}

static void
HiSax_manl1(struct PStack *st, int pr,
	    void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	long            flags;

	switch (pr) {
	  case (PH_ACTIVATE):
		  save_flags(flags);
		  cli();
		  if (sp->ph_active == 5) {
			  st->l1.act_state = 2;
			  restore_flags(flags);
			  st->l1.l1man(st, PH_ACTIVATE, NULL);
		  } else {
			  st->l1.act_state = 1;
			  if (sp->ph_active == 0)
				  restart_ph(sp);
			  restore_flags(flags);
		  }
		  break;
	  case (PH_DEACTIVATE):
		  st->l1.act_state = 0;
		  check_ph_act(sp);
		  break;
	}
}

static void
HiSax_l2l1discardq(struct PStack *st, int pr,
		   void *heldby, int releasetoo)
{
	struct IsdnCardState *sp = (struct IsdnCardState *) st->l1.hardware;

#ifdef DEBUG_MAGIC
	if (sp->magic != 301271) {
		printk(KERN_DEBUG "isac_discardq magic not 301271\n");
		return;
	}
#endif

	BufQueueDiscard(&sp->sq, pr, heldby, releasetoo);
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *sp)
{
	st->l1.hardware = sp;
	st->l1.sbufpool = &(sp->sbufpool);
	st->l1.rbufpool = &(sp->rbufpool);
	st->l1.smallpool = &(sp->smallpool);
	st->protocol = sp->protocol;

	setstack_tei(st);

	st->l1.stlistp = &(sp->stlist);
	st->l1.act_state = 0;
	st->l2.l2l1 = l2l1;
	st->l2.l2l1discardq = HiSax_l2l1discardq;
	st->ma.manl1 = HiSax_manl1;
	st->l1.requestpull = 0;
}

void
init_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	hsp->sp = sp;
	hsp->hscx = hscx;

	hsp->tqueue.next = 0;
	hsp->tqueue.sync = 0;
	hsp->tqueue.routine = (void *) (void *) hscx_bh;
	hsp->tqueue.data = hsp;

	hsp->inuse = 0;
	hsp->init = 0;
	hsp->active = 0;

#ifdef DEBUG_MAGIC
	hsp->magic = 301270;
#endif
}

int
get_irq(int cardnr, void *routine)
{
	struct IsdnCard *card = cards + cardnr;
	long            flags;

	save_flags(flags);
	cli();
	if (request_irq(card->sp->irq, routine,
			SA_INTERRUPT, "HiSax", NULL)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
                       card->sp->irq);
		restore_flags(flags);
		return (0);
	}
	irq2dev_map[card->sp->irq] = (void *) card->sp;
	restore_flags(flags);
	return (1);
}

static void
release_irq(int cardnr)
{
	struct	IsdnCard *card = cards + cardnr;

	irq2dev_map[card->sp->irq] = NULL;
	free_irq(card->sp->irq, NULL);
}

void
close_hscxstate(struct HscxState *hs)
{
	hs->sp->modehscx(hs, 0, 0);
	hs->inuse = 0;

	if (hs->init) {
		BufPoolFree(&hs->smallpool);
		BufPoolFree(&hs->rbufpool);
		BufPoolFree(&hs->sbufpool);
	}
	hs->init = 0;
}

static void
closecard(int cardnr)
{
	struct IsdnCardState *sp = cards[cardnr].sp;

	Sfree(sp->dlogspace);

	BufPoolFree(&sp->smallpool);
	BufPoolFree(&sp->rbufpool);
	BufPoolFree(&sp->sbufpool);

	close_hscxstate(sp->hs + 1);
	close_hscxstate(sp->hs);

        switch (sp->typ) {
#if CARD_TELES0
          case ISDN_CTYPE_16_0:
          case ISDN_CTYPE_8_0:
		release_io_teles0(&cards[cardnr]);
                break;
#endif
#if CARD_TELES3
          case ISDN_CTYPE_PNP:
          case ISDN_CTYPE_16_3:
		release_io_teles3(&cards[cardnr]);
                break;
#endif
#if CARD_AVM_A1
          case ISDN_CTYPE_A1:
		release_io_avm_a1(&cards[cardnr]);
                break;
#endif
#if CARD_ELSA
          case ISDN_CTYPE_ELSA:
		release_io_elsa(&cards[cardnr]);
                break;
#endif
          default:
          	break;
	}
}

static int
checkcard(int cardnr)
{
	int			ret=0;
	struct IsdnCard		*card = cards + cardnr;
	struct IsdnCardState	*sp;


	sp = (struct IsdnCardState *)
	    	    Smalloc(sizeof(struct IsdnCardState), GFP_KERNEL,
		    "struct IsdnCardState");

	card->sp   = sp;
	sp->cardnr = cardnr;
	sp->cfg_reg  = 0;
	sp->protocol = card->protocol;
	
	if ((card->typ>0) && (card->typ<31)) {
		if (!((1<<card->typ) & SUPORTED_CARDS)) {
                        printk(KERN_WARNING
                               "HiSax: Support for %s Card not selected\n",
                               CardType[card->typ]);
                        return(0);
                }
        } else {
        	printk(KERN_WARNING
                        "HiSax: Card Type %d out of range\n",
                        card->typ);
		return (0);
	}
	sp->dlogspace = Smalloc(4096, GFP_KERNEL, "dlogspace");
	sp->typ      = card->typ;
	sp->CallFlags = 0;
	
	printk(KERN_NOTICE
		"HiSax: Card %d Protocol %s\n", cardnr+1,
                (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" : "EDSS1");

        switch (card->typ) {
#if CARD_TELES0
          case ISDN_CTYPE_16_0:
          case ISDN_CTYPE_8_0:
                ret=setup_teles0(card);
                break;
#endif
#if CARD_TELES3
          case ISDN_CTYPE_PNP:
          case ISDN_CTYPE_16_3:
                ret=setup_teles3(card);
                break;
#endif
#if CARD_AVM_A1
          case ISDN_CTYPE_A1:
                ret=setup_avm_a1(card);
                break;
#endif
#if CARD_ELSA
          case ISDN_CTYPE_ELSA:
                ret=setup_elsa(card);
                break;
#endif
          default: 
                printk(KERN_WARNING  "HiSax: Unknown Card Typ %d\n",
                  			card->typ);
				Sfree(sp->dlogspace);
                        	return(0);
        }
	if (!ret) {
		Sfree(sp->dlogspace);
		return (0);
	}
	BufPoolInit(&sp->sbufpool, ISAC_SBUF_ORDER, ISAC_SBUF_BPPS,
		    ISAC_SBUF_MAXPAGES);
	BufPoolInit(&sp->rbufpool, ISAC_RBUF_ORDER, ISAC_RBUF_BPPS,
		    ISAC_RBUF_MAXPAGES);
	BufPoolInit(&sp->smallpool, ISAC_SMALLBUF_ORDER, ISAC_SMALLBUF_BPPS,
		    ISAC_SMALLBUF_MAXPAGES);

	
	sp->rcvibh = NULL;
	sp->rcvptr = 0;
	sp->xmtibh = NULL;
	sp->sendptr = 0;
	sp->event = 0;
	sp->tqueue.next = 0;
	sp->tqueue.sync = 0;
	sp->tqueue.routine = (void *) (void *) isac_bh;
	sp->tqueue.data = sp;

	BufQueueInit(&sp->rq);
	BufQueueInit(&sp->sq);

	sp->stlist = NULL;

	sp->ph_active = 0;

	sp->dlogflag = 0;
	sp->debug = 0xff;

	sp->releasebuf = 0;
#ifdef DEBUG_MAGIC
	sp->magic = 301271;
#endif

	init_hscxstate(sp, 0);
	init_hscxstate(sp, 1);

        switch (card->typ) {
#if CARD_TELES0
          case ISDN_CTYPE_16_0:
          case ISDN_CTYPE_8_0:
                ret=initteles0(sp);
                break;
#endif
#if CARD_TELES3
          case ISDN_CTYPE_PNP:
          case ISDN_CTYPE_16_3:
                ret=initteles3(sp);
                break;
#endif
#if CARD_AVM_A1
          case ISDN_CTYPE_A1:
                ret=initavm_a1(sp);
                break;
#endif
#if CARD_ELSA
          case ISDN_CTYPE_ELSA:
                ret=initelsa(sp);
                break;
#endif
          default:
          	ret=0;
          	break;
	}
	if (!ret) {
                closecard(cardnr);
                return(0);
        }
	return(1);
}

void
HiSax_shiftcards(int idx)
{
        int i;

        for (i = idx; i < 15; i++)
                memcpy(&cards[i],&cards[i+1],sizeof(cards[i]));
}

int
HiSax_inithardware(void)
{
        int             foundcards = 0;
	int             i = 0;

	while (i < nrcards) {
                if (cards[i].typ<1)
                        break;
		if (checkcard(i)) {
                        foundcards++;
                        i++;
                } else {
			printk(KERN_WARNING "HiSax: Card %s not installed !\n",
			  	CardType[cards[i].typ]);
			Sfree((void *) cards[i].sp);
			cards[i].sp = NULL;
                        HiSax_shiftcards(i);
		}
        }
        return foundcards;
}

void
HiSax_closehardware(void)
{
	int             i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].sp) {
			release_irq(i);
			closecard(i);
			Sfree((void *) cards[i].sp);
			cards[i].sp = NULL;
		}
}

static void
hscx_l2l1(struct PStack *st, int pr,
	  struct BufHeader *ibh)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;
	long flags;

	switch (pr) {
                case (PH_DATA):
			save_flags(flags);
			cli();
                        if (hsp->xmtibh) {
                                BufQueueLink(&hsp->sq, ibh);
				restore_flags(flags);
			}
                        else {
				restore_flags(flags);
                                hsp->xmtibh = ibh;
                                hsp->sendptr = 0;
                                hsp->releasebuf = !0;
                                sp->hscx_fill_fifo(hsp);
                        }
                        break;
                case (PH_DATA_PULLED):
                        if (hsp->xmtibh) {
                                printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
                                break;
                        }
                        hsp->xmtibh = ibh;
                        hsp->sendptr = 0;
                        hsp->releasebuf = 0;
                        sp->hscx_fill_fifo(hsp);
                        break;
                case (PH_REQUEST_PULL):
                        if (!hsp->xmtibh) {
                                st->l1.requestpull = 0;
                                st->l1.l1l2(st, PH_PULL_ACK, NULL);
                        } else
                                st->l1.requestpull = !0;
                        break;
	}
        
}
extern struct IsdnBuffers *tracebuf;

static void
hscx_l2l1discardq(struct PStack *st, int pr, void *heldby,
		  int releasetoo)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)
	st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;

#ifdef DEBUG_MAGIC
	if (hsp->magic != 301270) {
		printk(KERN_DEBUG "hscx_discardq magic not 301270\n");
		return;
	}
#endif

	BufQueueDiscard(&hsp->sq, pr, heldby, releasetoo);
}

static int
open_hscxstate(struct IsdnCardState *sp,
	       int hscx)
{
	struct HscxState *hsp = sp->hs + hscx;

	if (!hsp->init) {
		BufPoolInit(&hsp->sbufpool, HSCX_SBUF_ORDER, HSCX_SBUF_BPPS,
			    HSCX_SBUF_MAXPAGES);
		BufPoolInit(&hsp->rbufpool, HSCX_RBUF_ORDER, HSCX_RBUF_BPPS,
			    HSCX_RBUF_MAXPAGES);
		BufPoolInit(&hsp->smallpool, HSCX_SMALLBUF_ORDER, HSCX_SMALLBUF_BPPS,
			    HSCX_SMALLBUF_MAXPAGES);
	}
	hsp->init = !0;

	BufQueueInit(&hsp->rq);
	BufQueueInit(&hsp->sq);

	hsp->releasebuf = 0;
	hsp->rcvibh = NULL;
	hsp->xmtibh = NULL;
	hsp->rcvptr = 0;
	hsp->sendptr = 0;
	hsp->event = 0;
	return (0);
}

static void
hscx_manl1(struct PStack *st, int pr,
	   void *arg)
{
	struct IsdnCardState *sp = (struct IsdnCardState *)st->l1.hardware;
	struct HscxState *hsp = sp->hs + st->l1.hscx;

	switch (pr) {
	  case (PH_ACTIVATE):
		  hsp->active = !0;
		  sp->modehscx(hsp, st->l1.hscxmode, st->l1.hscxchannel);
		  st->l1.l1man(st, PH_ACTIVATE, NULL);
		  break;
	  case (PH_DEACTIVATE):
		  if (!hsp->xmtibh)
			  sp->modehscx(hsp, 0, 0);

		  hsp->active = 0;
		  break;
	}
}

int
setstack_hscx(struct PStack *st, struct HscxState *hs)
{
	if (open_hscxstate(st->l1.hardware, hs->hscx))
		return (-1);

	st->l1.hscx = hs->hscx;
	st->l2.l2l1 = hscx_l2l1;
	st->ma.manl1 = hscx_manl1;
	st->l2.l2l1discardq = hscx_l2l1discardq;

	st->l1.sbufpool = &hs->sbufpool;
	st->l1.rbufpool = &hs->rbufpool;
	st->l1.smallpool = &hs->smallpool;
	st->l1.act_state = 0;
	st->l1.requestpull = 0;

	hs->st = st;
	return (0);
}

void
HiSax_reportcard(int cardnr)
{
	struct IsdnCardState *sp = cards[cardnr].sp;
	
	printk(KERN_DEBUG "HiSax: reportcard No %d\n",cardnr+1);
	printk(KERN_DEBUG "HiSax: Type %s\n", CardType[sp->typ]);
	printk(KERN_DEBUG "HiSax: debuglevel %x\n", sp->debug);
	if (sp->debug) sp->debug =0;
	else sp->debug =0xff;
}
