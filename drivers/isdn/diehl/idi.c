/* $Id$
 *
 * ISDN lowlevel-module for Diehl active cards.
 *        IDI encoder/decoder
 *
 * Copyright 1998 Armin Schindler (mac@gismo.telekom.de)
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
 *
 */

#define __NO_VERSION__
#include "diehl.h"
#include "idi.h"

#undef IDI_DEBUG


int
idi_assign_req(diehl_pci_REQ *reqbuf, int signet, diehl_chan *chan)
{
	int l = 0;
  if (!signet) {
	/* Signal Layer */
	reqbuf->XBuffer.P[l++] = OAD;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = 0xfd; /* all CPN be will accepted */
	reqbuf->XBuffer.P[l++] = KEY;
	reqbuf->XBuffer.P[l++] = 3;
	reqbuf->XBuffer.P[l++] = 'I';
	reqbuf->XBuffer.P[l++] = '4';
	reqbuf->XBuffer.P[l++] = 'L';
	reqbuf->XBuffer.P[l++] = ESC;
	reqbuf->XBuffer.P[l++] = 2;
	reqbuf->XBuffer.P[l++] = CHI;
	reqbuf->XBuffer.P[l++] = chan->No + 1;
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = 0x09; 
	reqbuf->XBuffer.P[l++] = SHIFT|6;
	reqbuf->XBuffer.P[l++] = SIN;
	reqbuf->XBuffer.P[l++] = 2;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */
  }
  else {
	/* Network Layer */
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = chan->e.D3Id;
	reqbuf->XBuffer.P[l++] = LLC;
	reqbuf->XBuffer.P[l++] = 2;
	switch(chan->l2prot) {
		case ISDN_PROTO_L2_HDLC:
			reqbuf->XBuffer.P[l++] = 2;
			break;
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
			reqbuf->XBuffer.P[l++] = 5;
			break;
		case ISDN_PROTO_L2_TRANS:
			reqbuf->XBuffer.P[l++] = 2;
			break;
		default:
			reqbuf->XBuffer.P[l++] = 1;
	}
	switch(chan->l3prot) {
		case ISDN_PROTO_L3_TRANS:
		default:
			reqbuf->XBuffer.P[l++] = 4;
	}
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0x20;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 1; /* Net Entity */
  }
   return(0);
}

int
idi_indicate_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = INDICATE_REQ;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 0; /* Sig Entity */
   return(0);
}

int
idi_remove_req(diehl_pci_REQ *reqbuf, int signet)
{
	reqbuf->Req = REMOVE;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = signet;
   return(0);
}

int
idi_hangup_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = HANGUP;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 0; /* Sig Entity */
   return(0);
}

int
idi_reject_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = REJECT;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 0; /* Sig Entity */
   return(0);
}

int
idi_call_alert_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = CALL_ALERT;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 0; /* Sig Entity */
   return(0);
}

int
idi_call_res_req(diehl_pci_REQ *reqbuf, diehl_chan *chan)
{
	int l = 0;
	reqbuf->Req = CALL_RES;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = 0x09;
	switch(chan->l2prot) {
		case ISDN_PROTO_L2_HDLC:
			reqbuf->XBuffer.P[2] = 0x05;
			break;
		case ISDN_PROTO_L2_V11096:
			reqbuf->XBuffer.P[1] = 3;
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 5;
			reqbuf->XBuffer.P[4] = 0;
			l+=2;
			break;
		case ISDN_PROTO_L2_V11019:
			reqbuf->XBuffer.P[1] = 3;
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 6;
			reqbuf->XBuffer.P[4] = 0;
			l+=2;
			break;
		case ISDN_PROTO_L2_V11038:
			reqbuf->XBuffer.P[1] = 3;
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 7;
			reqbuf->XBuffer.P[4] = 0;
			l+=2;
			break;
		case ISDN_PROTO_L2_MODEM:
			reqbuf->XBuffer.P[2] = 0x10;
			break;
	}
	/* TODO maybe here should follow Q.931 LLC */
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */
#ifdef IDI_DEBUG
	printk(KERN_DEBUG"idi: Call_Res\n");
#endif
   return(0);
}

int
idi_n_connect_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = IDI_N_CONNECT;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 1; /* Net Entity */
#ifdef IDI_DEBUG
	printk(KERN_DEBUG"idi: N_Connect_Req\n");
#endif
   return(0);
}

int
idi_n_connect_ack_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = IDI_N_CONNECT_ACK;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 1; /* Net Entity */
   return(0);
}

int
idi_n_disc_req(diehl_pci_REQ *reqbuf)
{
	reqbuf->Req = IDI_N_DISC;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = 1; /* Net Entity */
   return(0);
}

int
idi_do_req(diehl_card *card, diehl_chan *chan, int cmd, int layer)
{
        struct sk_buff *skb;
        struct sk_buff *skb2;
	diehl_pci_REQ *reqbuf;
	diehl_chan_ptr *chan2;

        skb = alloc_skb(sizeof(diehl_pci_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(diehl_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
                printk(KERN_WARNING "idi: alloc_skb failed\n");
                return -ENOMEM; 
	}

	chan2 = (diehl_chan_ptr *)skb_put(skb2, sizeof(diehl_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (diehl_pci_REQ *)skb_put(skb, sizeof(diehl_pci_REQ));
#ifdef IDI_DEBUG
	printk(KERN_DEBUG "diehl_idi: Request %d for Ch %d\n", cmd, chan->No);
#endif
	if (layer) cmd |= 0x700;
	switch(cmd) {
		case ASSIGN:
		case ASSIGN|0x700:
			idi_assign_req(reqbuf, layer, chan);
			break;
		case REMOVE:
		case REMOVE|0x700:
			idi_remove_req(reqbuf, layer);
			break;
		case INDICATE_REQ:
			idi_indicate_req(reqbuf);
			break;
		case HANGUP:
			idi_hangup_req(reqbuf);
			break;
		case REJECT:
			idi_reject_req(reqbuf);
			break;
		case CALL_ALERT:
			idi_call_alert_req(reqbuf);
			break;
		case CALL_RES:
			idi_call_res_req(reqbuf, chan);
			break;
		case IDI_N_CONNECT|0x700:
			idi_n_connect_req(reqbuf);
			break;
		case IDI_N_CONNECT_ACK|0x700:
			idi_n_connect_ack_req(reqbuf);
			break;
		case IDI_N_DISC|0x700:
			idi_n_disc_req(reqbuf);
			break;
		default:
			printk(KERN_ERR "idi: Unknown request\n");
			return(-1);
	}

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	diehl_schedule_tx(card);
	return(0);
}


int
diehl_idi_listen_req(diehl_card *card, diehl_chan *chan)
{
  if (chan->eazmask) {
	if (!chan->e.D3Id) {
		idi_do_req(card, chan, ASSIGN, 0); 
	}
	idi_do_req(card, chan, INDICATE_REQ, 0);
  }
  else {
	if (chan->e.B2Id) idi_do_req(card, chan, REMOVE, 1);
	if (chan->e.D3Id) idi_do_req(card, chan, REMOVE, 0);
  }
  return(0);
}

unsigned char
idi_si2bc(int si1, int si2, char *bc)
{
  switch(si1) {
	case 1:
		bc[0] = 0x90;
		bc[1] = 0x90;
		bc[2] = 0xa3;
		return(3);
	case 5:
	case 7:
	default:
		bc[0] = 0x88;
		bc[1] = 0x90;
		return(2);
  }
 return (0);
}

int
idi_hangup(diehl_card *card, diehl_chan *chan)
{
  idi_do_req(card, chan, HANGUP, 0);
  if (chan->fsm_state == DIEHL_STATE_ACTIVE) {
#if 0
  	if (chan->e.B2Id) idi_do_req(card, chan, IDI_N_DISC, 1);
#endif
	if (chan->e.B2Id) idi_do_req(card, chan, REMOVE, 1);
  }
#ifdef IDI_DEBUG
  printk(KERN_DEBUG"idi: Hangup\n");
#endif
  return(0);
}

int
idi_connect_res(diehl_card *card, diehl_chan *chan)
{
  idi_do_req(card, chan, ASSIGN, 1);
  idi_do_req(card, chan, CALL_RES, 0);
  return(0);
}

int
diehl_idi_call_req(diehl_card *card, diehl_chan *chan, char *phone)
{
	int l = 0;
	int i;
        struct sk_buff *skb;
        struct sk_buff *skb2;
	diehl_pci_REQ *reqbuf;
	diehl_chan_ptr *chan2;

        skb = alloc_skb(sizeof(diehl_pci_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(diehl_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
                printk(KERN_WARNING "idi: alloc_skb failed\n");
                return -ENOMEM; 
	}

	chan2 = (diehl_chan_ptr *)skb_put(skb2, sizeof(diehl_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (diehl_pci_REQ *)skb_put(skb, sizeof(diehl_pci_REQ));
	reqbuf->Req = CALL_REQ;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;

	reqbuf->XBuffer.P[l++] = CPN;
	reqbuf->XBuffer.P[l++] = strlen(phone) + 1;
	reqbuf->XBuffer.P[l++] = 0xc1;
	for(i=0; i<strlen(phone);i++) 
		reqbuf->XBuffer.P[l++] = phone[i];

	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	diehl_schedule_tx(card);

   return(0);
}

int
idi_connect_req(diehl_card *card, diehl_chan *chan, char *phone,
                    char *eazmsn, int si1, int si2)
{
	int l = 0;
	int i;
	unsigned char tmp;
	unsigned char bc[5];
        struct sk_buff *skb;
        struct sk_buff *skb2;
	diehl_pci_REQ *reqbuf;
	diehl_chan_ptr *chan2;

        skb = alloc_skb(sizeof(diehl_pci_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(diehl_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
                printk(KERN_WARNING "idi: alloc_skb failed\n");
                return -ENOMEM; 
	}

	chan2 = (diehl_chan_ptr *)skb_put(skb2, sizeof(diehl_chan_ptr));
	chan2->ptr = chan;

	if (chan->e.D3Id) idi_do_req(card, chan, REMOVE, 0);

	reqbuf = (diehl_pci_REQ *)skb_put(skb, sizeof(diehl_pci_REQ));
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;

	reqbuf->XBuffer.P[l++] = OAD;
	reqbuf->XBuffer.P[l++] = strlen(eazmsn) + 2;
	reqbuf->XBuffer.P[l++] = 0x21;
	reqbuf->XBuffer.P[l++] = 0x81;
	for(i=0; i<strlen(eazmsn);i++) 
		reqbuf->XBuffer.P[l++] = eazmsn[i];
	if ((tmp = idi_si2bc(si1, si2, bc)) > 0) {
		reqbuf->XBuffer.P[l++] = BC;
		reqbuf->XBuffer.P[l++] = tmp;
		for(i=0; i<tmp;i++) 
			reqbuf->XBuffer.P[l++] = bc[i];
	}
	reqbuf->XBuffer.P[l++] = ESC;
	reqbuf->XBuffer.P[l++] = 2;
	reqbuf->XBuffer.P[l++] = CHI;
	reqbuf->XBuffer.P[l++] = chan->No + 1;
#if 0
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = 0x09;
#endif	
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	diehl_schedule_tx(card);

	idi_do_req(card, chan, ASSIGN, 1);
	diehl_idi_call_req(card, chan, phone);

#ifdef IDI_DEBUG
  	printk(KERN_DEBUG"idi: Conn_Req %s -> %s\n",eazmsn,phone);
#endif

   return(0);
}

void
idi_IndParse(idi_ind_message *message, unsigned char *buffer, int len)
{
  int i;
  int pos = 0;
  int codeset = 0;
  int wlen = 0;
  int lock = 0;
  __u8 w;
  __u16 code;

  memset(message, 0, sizeof(idi_ind_message));

#if 0
 for(i=0;i<len;i++) printk(KERN_DEBUG"idi: %d   0x%x\n",buffer[i],buffer[i]);
#endif

  if ((!len) || (!buffer[pos])) return;
  while(pos <= len) {
	w = buffer[pos++];
	if (!w) return;
	if (w & 0x80) {
		wlen = 0;
	}
	else {
		wlen = buffer[pos++];
	}
	if ((pos + wlen) >= 270) return;

	if (lock & 0x80) lock &= 0x7f;
	else codeset = lock;

	if((w&0xf0) == SHIFT) {
		codeset = w;
		if(!(codeset & 0x08)) lock = codeset & 7;
		codeset &= 7;
		lock |= 0x80;
	}
	else {
		if (w==ESC && wlen >=2) {
			code = buffer[pos++]|0x800;
			wlen--;
		}
		else code = w;
		code |= (codeset<<8);

		switch(code) {
			case CPN:
				pos++;
				for(i=0; i < wlen-1; i++) 
					message->cpn[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: CPN=%s\n", message->cpn);
#endif
				break;
			case OAD:
				pos++;
				for(i=0; i < wlen-1; i++) 
					message->oad[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: OAD=%s\n", message->oad);
#endif
				break;
			case DSA:
				pos++;
				for(i=0; i < wlen-1; i++) 
					message->dsa[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: DSA=%s\n", message->dsa);
#endif
				break;
			case OSA:
				pos++;
				for(i=0; i < wlen-1; i++) 
					message->osa[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: OSA=%s\n", message->osa);
#endif
				break;
			case BC:
				for(i=0; i < wlen; i++) 
					message->bc[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: BC =%d %d %d %d %d\n", message->bc[0],
					message->bc[1],message->bc[2],message->bc[3],message->bc[4]);
#endif
				break;
			case LLC:
				for(i=0; i < wlen; i++) 
					message->llc[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: LLC=%d %d %d %d\n", message->llc[0],
					message->llc[1],message->llc[2],message->llc[3]);
#endif
				break;
			case HLC:
				for(i=0; i < wlen; i++) 
					message->hlc[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: HLC=%d %d %d %d\n", message->hlc[0],
					message->hlc[1],message->hlc[2],message->hlc[3]);
#endif
				break;
			case CAU:
				for(i=0; i < wlen; i++) 
					message->cau[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: CAU=%d %d %d\n", 
					message->cau[0],message->cau[1],message->cau[2]);
#endif
				break;
			case 0x800|CAU:
				for(i=0; i < wlen; i++) 
					message->e_cau[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: ECAU=%d %d\n", 
					message->e_cau[0],message->e_cau[1]);
#endif
				break;
			case 0x800|0x7a:
				pos ++;
				message->e_mt=buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: EMT=0x%x\n", message->e_mt);
#endif
				break;
			case DT:
				for(i=0; i < wlen; i++) 
					message->dt[i] = buffer[pos++];
				break;
			case 0x600|SIN:
				for(i=0; i < wlen; i++) 
					message->sin[i] = buffer[pos++];
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: SIN=%d %d\n", 
					message->sin[0],message->sin[1]);
#endif
				break;
			case 0x600|CPS:
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: Called Party Status in ind\n");
#endif
				pos += wlen;
				break;
			case 0x600|CIF:
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: Charging Information in ind\n");
#endif
				pos += wlen;
				break;
			case 0x600|DATE:
#ifdef IDI_DEBUG
				printk(KERN_DEBUG"idi: Date in ind\n");
#endif
				pos += wlen;
				break;
			case CHA:
			case CHI:
			case FTY:
			case PI:
			case NI:
			case 0x800:
				/* Not yet interested in this */
				pos += wlen;
				break;
			default:
				pos += wlen;
				printk(KERN_WARNING"idi: unknown information element 0x%x in ind\n", code);
		}
	}
  }
}

void
idi_bc2si(unsigned char *bc, unsigned char *si1, unsigned char *si2)
{
  si1[0] = 0;
  si2[0] = 0;
  if (memcmp(bc, "\0x80\0x90\0xa3", 3) == 0) {	/* Speech */
	si1[0] = 1;
  }
  if (memcmp(bc, "\0x90\0x90\0xa3", 3) == 0) {	/* 3.1kHz audio */
	si1[0] = 1;
  }
  if (memcmp(bc, "\0x88\0x90", 2) == 0) {	/* unrestricted 64 kbits */
	si1[0] = 7;
  }
  if (memcmp(bc, "\0x91\0x90\0xa5", 3) == 0) {	/* video */
	si1[0] = 4;
  }
}

void
idi_handle_ind(diehl_pci_card *card, struct sk_buff *skb)
{
	int tmp;
        diehl_pci_IND *ind = (diehl_pci_IND *)skb->data;
	diehl_chan *chan;
	diehl_card *ccard = (diehl_card *) card->card;
	idi_ind_message message;
	isdn_ctrl cmd;

	if ((chan = card->IdTable[ind->IndId]) == NULL) {
		printk(KERN_ERR "idi: Indication for unknown channel\n");
  		dev_kfree_skb(skb);
		return;
	}

	idi_IndParse(&message, ind->RBuffer.P, ind->RBuffer.length);

	/* Signal Layer */
	if (chan->e.D3Id == ind->IndId) {
		switch(ind->Ind) {
			case HANGUP:
#ifdef IDI_DEBUG
  				printk(KERN_DEBUG"idi_ind: Hangup\n");
#endif
				chan->fsm_state = DIEHL_STATE_NULL;
				cmd.driver = ccard->myid;
				cmd.arg = chan->No;
				sprintf(cmd.parm.num,"(%d) 0x%02x", 
					(message.e_cau[0]&0x80)?1:0, message.e_cau[0]&0x7f);
				cmd.command = ISDN_STAT_CAUSE;
				ccard->interface.statcallb(&cmd);
				cmd.command = ISDN_STAT_DHUP;
				ccard->interface.statcallb(&cmd);
				if (chan->e.B2Id) idi_do_req(ccard, chan, REMOVE, 1);
				if (chan->e.D3Id) idi_do_req(ccard, chan, REMOVE, 0);
				diehl_idi_listen_req(ccard, chan);
				break;
			case INDICATE_IND:
#ifdef IDI_DEBUG
  				printk(KERN_DEBUG"idi_ind: Indicate_Ind\n");
#endif
				chan->fsm_state = DIEHL_STATE_ICALL;
				idi_bc2si(message.bc, &chan->si1, &chan->si2);
				strcpy(chan->cpn, message.cpn);
				strcpy(chan->oad, message.oad + 1);
				try_stat_icall_again: /* I don't like this */
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_ICALL;
				cmd.arg = chan->No;
				cmd.parm.setup.si1 = chan->si1;
				cmd.parm.setup.si2 = chan->si2;
				strcpy(cmd.parm.setup.eazmsn, chan->cpn);
				strcpy(cmd.parm.setup.phone, chan->oad);
				cmd.parm.setup.plan = 0; /* TODO */
				cmd.parm.setup.screen = 0; /* TODO */
				tmp = ccard->interface.statcallb(&cmd);
				switch(tmp) {
					case 0: /* no user responding */
						idi_do_req(ccard, chan, HANGUP, 0);
						break;
					case 1: /* alert */
#ifdef IDI_DEBUG
  						printk(KERN_DEBUG"idi: Call Alert\n");
#endif
						chan->fsm_state = DIEHL_STATE_ICALL;
						idi_do_req(ccard, chan, CALL_ALERT, 0);
						break;
					case 2: /* reject */
#ifdef IDI_DEBUG
  						printk(KERN_DEBUG"idi: Call Reject\n");
#endif
						idi_do_req(ccard, chan, REJECT, 0);
						break;
					case 3: /* incomplete number */
#ifdef IDI_DEBUG
  						printk(KERN_DEBUG"idi: Incomplete Number\n");
#endif
						chan->fsm_state = DIEHL_STATE_ICALLW;
						break;
				}
				break;
			case INFO_IND:
#ifdef IDI_DEBUG
  				printk(KERN_DEBUG"idi_ind: Info_Ind\n");
#endif
				if ((chan->fsm_state == DIEHL_STATE_ICALLW) &&
				    (message.cpn[0])) {
					strcat(chan->cpn, message.cpn);
					goto try_stat_icall_again;
				}
				break;
			case CALL_IND:
#ifdef IDI_DEBUG
  				printk(KERN_DEBUG"idi_ind: Call_Ind\n");
#endif
				if (chan->fsm_state == DIEHL_STATE_ICALL) {
					chan->fsm_state = DIEHL_STATE_IBWAIT;
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_DCONN;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
					idi_do_req(ccard, chan, IDI_N_CONNECT, 1);
					idi_do_req(ccard, chan, IDI_N_CONNECT_ACK, 1);
				} else
				idi_hangup(ccard, chan);
				break;
			case CALL_CON:
#ifdef IDI_DEBUG
  				printk(KERN_DEBUG"idi_ind: Call_Con\n");
#endif
				if (chan->fsm_state == DIEHL_STATE_OCALL) {
					chan->fsm_state = DIEHL_STATE_OBWAIT;
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_DCONN;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
					idi_do_req(ccard, chan, IDI_N_CONNECT, 1);
					idi_do_req(ccard, chan, IDI_N_CONNECT_ACK, 1);
				} else
				idi_hangup(ccard, chan);
				break;
			default:
#ifdef IDI_DEBUG
				printk(KERN_WARNING "idi: UNHANDLED SigIndication 0x%02x\n", ind->Ind);
#endif
		}
	}
	/* Network Layer */
	else if (chan->e.B2Id == ind->IndId) {
		switch(ind->Ind) {
			case IDI_N_CONNECT_ACK:
#ifdef IDI_DEBUG
  				printk(KERN_DEBUG"idi_ind: N_Connect_Ack\n");
#endif
				chan->fsm_state = DIEHL_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break; 
			default:
#ifdef IDI_DEBUG
				printk(KERN_WARNING "idi: UNHANDLED NetIndication 0x%02x\n", ind->Ind);
#endif
		}
	}
	else {
		printk(KERN_ERR "idi: Ind is neither SIG nor NET !\n");
	}
   dev_kfree_skb(skb);
}

void
idi_handle_ack(diehl_pci_card *card, struct sk_buff *skb)
{
	int j;
        diehl_pci_RC *ack = (diehl_pci_RC *)skb->data;
	diehl_chan *chan;
	diehl_card *ccard = (diehl_card *) card->card;

	if ((ack->Rc != ASSIGN_OK) && (ack->Rc != OK)) {
#ifdef IDI_DEBUG
		printk(KERN_ERR "diehl_handle_ack: Not OK: Rc=%d Id=%d Ch=%d\n",
			ack->Rc, ack->RcId, ack->RcCh);
#endif
			if ((chan = card->IdTable[ack->RcId]) != NULL) {
				chan->e.busy = 0;
			}
	} 
	else {
		if ((chan = card->IdTable[ack->RcId]) != NULL) {
#if 0
			printk(KERN_DEBUG "idi_ack: ASSIGN_OK Id=%d Ch=%d Chan %d\n",
			ack->RcId, ack->RcCh, chan->No);
#endif
			chan->e.busy = 0;

			if (chan->e.Req == REMOVE) {
				card->IdTable[ack->RcId] = NULL;
				if (!chan->e.ReqCh) 
					chan->e.D3Id = 0;
				else
					chan->e.B2Id = 0;
			}
		}
		else {
			for(j = 0; j < ccard->nchannels; j++) {
				if (ccard->bch[j].e.ref == ack->Reference) {
					if (!ccard->bch[j].e.ReqCh) 
						ccard->bch[j].e.D3Id  = ack->RcId;
					else
						ccard->bch[j].e.B2Id  = ack->RcId;
					card->IdTable[ack->RcId] = &ccard->bch[j];
					ccard->bch[j].e.busy = 0;
#if 0
					printk(KERN_DEBUG"idi_ack: Id %d assigned to Ch %d\n",
							ack->RcId, j);
#endif
					break;
				}
			}		
			if (j == ccard->nchannels) {
				printk(KERN_ERR"idi: ref %d not found\n", 
						ack->Reference);
			}
		}

	}
  dev_kfree_skb(skb);
  diehl_schedule_tx((diehl_card *)card->card);
}



