/* $Id$

 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *              based on the teles driver from Jan den Ouden
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 *
 */

const char *dss1_revision = "$Revision$";

#include "hisax.h"
#include "l4l3if.h"
#include "callc.h" // FIXME
#include "isdnl3.h"
#include "l3dss1.h"
#include <linux/ctype.h>
#include <linux/config.h>

/* The following set of macros allows to compose Q.931 messages rather easily
 * To find out how they're used, just look into the code further down
 */

#define MsgDeclare(len) \
	struct sk_buff *__skb; \
	u_char __tmp[len]; \
	u_char *__p = __tmp; \
	int __l

#define	MsgXHead(cref, mty) \
	*__p++ = 0x8; \
	if (cref == -1) { \
		*__p++ = 0x0; \
	} else { \
		*__p++ = 0x1; \
		*__p++ = (__u8)(cref)^0x80; \
	} \
	*__p++ = (mty)

#define MsgAdd(msg) do { \
        if ((msg)[0]) { \
		if ((msg)[0] & 0x80) { \
			*__p++ = (msg)[0]; \
		} else { \
			memcpy(__p, (msg), (msg)[1] + 2); \
			__p += (msg)[1] + 2; \
		} \
        } \
	} while (0)

#define MsgCause(loc, cause) do { \
        *__p++ = IE_CAUSE; \
        *__p++ = 0x2; \
        *__p++ = (loc) | 0x80; \
        *__p++ = (cause) | 0x80; } while (0)

#define MsgCallState(state) do { \
	*__p++ = IE_CALL_STATE; \
	*__p++ = 0x1; \
	*__p++ = state & 0x3f; } while (0)

#define MsgCallId(id) do { \
	__l = *id++; \
	if (__l && (__l <= 10)) { /* Max length 10 octets */ \
		*__p++ = IE_CALL_ID; \
		*__p++ = __l; \
		for (i = 0; i < __l; i++) \
			*__p++ = *id++; \
	} else if (__l) { \
		l3_debug(pc->st, "wrong CALL_ID len %d", __l); \
	} \
	} while (0)

#define MsgChannelId(ch) do { \
	*__p++ = IE_CHANNEL_ID; \
	*__p++ = 1; \
	*__p++ = ch | 0x80; } while (0)

#define MsgRestartInd(id) do { \
	*__p++ = IE_RESTART_IND; \
	*__p++ = 1; \
	*__p++ = ri; } while (0)

#define MsgUUS(uus) do { \
        *__p++ = IE_USER_USER; /* UUS info element */ \
        *__p++ = strlen(uus) + 1; \
        *__p++ = 0x04; /* IA5 chars */ \
        strcpy(__p, uus); \
        __p += strlen(uus); \
        uus[0] = '\0'; \
        } while (0)

#define MsgSend() do { \
	__l = __p - __tmp; \
	if ((__skb = l3_alloc_skb(__l))) { \
	        memcpy(skb_put(__skb, __l), __tmp, __l); \
	        l3_msg(pc->st, DL_DATA | REQUEST, __skb); \
        } \
        } while (0)
//	        l3pc_l3l2(pc, DL_DATA | REQUEST, skb);

static void
dss1down_proc(struct l3_process *pc, int pr, void *arg);

extern char *HiSax_getrev(const char *revision);

#define EXT_BEARER_CAPS 1

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	if (cref == -1) { \
		*ptr++ = 0x0; \
	} else { \
		*ptr++ = 0x1; \
		*ptr++ = cref^0x80; \
	} \
	*ptr++ = mty


/**********************************************/
/* get a new invoke id for remote operations. */
/* Only a return value != 0 is valid          */
/**********************************************/
static unsigned char new_invoke_id(struct PStack *p)
{
	unsigned char retval;
	int flags,i;
  
	i = 32; /* maximum search depth */

	save_flags(flags);
	cli();

	retval = p->prot.dss1.last_invoke_id + 1; /* try new id */
	while ((i) && (p->prot.dss1.invoke_used[retval >> 3] == 0xFF)) {
		p->prot.dss1.last_invoke_id = (retval & 0xF8) + 8;
		i--;
	}  
	if (i) {
		while (p->prot.dss1.invoke_used[retval >> 3] & (1 << (retval & 7)))
		retval++; 
	} else
		retval = 0;
	p->prot.dss1.last_invoke_id = retval;
	p->prot.dss1.invoke_used[retval >> 3] |= (1 << (retval & 7));
	restore_flags(flags);

	return(retval);  
} /* new_invoke_id */

/*************************/
/* free a used invoke id */
/*************************/
static void free_invoke_id(struct PStack *p, unsigned char id)
{ int flags;

  if (!id) return; /* 0 = invalid value */

  save_flags(flags);
  cli();
  p->prot.dss1.invoke_used[id >> 3] &= ~(1 << (id & 7));
  restore_flags(flags);
} /* free_invoke_id */  


/**********************************************************/
/* create a new l3 process and fill in dss1 specific data */
/**********************************************************/
static struct l3_process
*dss1_new_l3_process(struct PStack *st, int cr)
{  struct l3_process *proc;

   if (!(proc = new_l3_process(st, cr))) 
     return(NULL);

   proc->l4l3 = dss1down_proc;

   proc->prot.dss1.invoke_id = 0;
   proc->prot.dss1.remote_operation = 0;
   proc->prot.dss1.uus1_data[0] = '\0';
   
   return(proc);
} /* dss1_new_l3_process */

/************************************************/
/* free a l3 process and all dss1 specific data */
/************************************************/ 
static void
dss1_release_l3_process(struct l3_process *p)
{
   free_invoke_id(p->st,p->prot.dss1.invoke_id);
   p_L3L4(p, CC_RELEASE_CR | INDICATION, 0);
   release_l3_process(p);
} /* dss1_release_l3_process */
 
/********************************************************/
/* search a process with invoke id id and dummy callref */
/********************************************************/
static struct l3_process *
l3dss1_search_dummy_proc(struct PStack *st, int id)
{ struct l3_process *pc = st->l3.proc; /* start of processes */

  if (!id) return(NULL);

  while (pc)
   { if ((pc->callref == -1) && (pc->prot.dss1.invoke_id == id))
       return(pc);
     pc = pc->next;
   } 
  return(NULL);
} /* l3dss1_search_dummy_proc */

/*******************************************************************/
/* called when a facility message with a dummy callref is received */
/* and a return result is delivered. id specifies the invoke id.   */
/*******************************************************************/ 
static void 
l3dss1_dummy_return_result(struct PStack *st, int id, u_char *p, u_char nlen)
{
#ifdef CONFIG_HISAX_LLI
  isdn_ctrl ic;
  struct IsdnCardState *cs;
  struct l3_process *pc = NULL; 

  if ((pc = l3dss1_search_dummy_proc(st, id)))
   { l3pc_deltimer(pc); /* remove timer */

     cs = pc->st->l1.hardware;
     ic.driver = cs->c_if->myid;
     ic.command = ISDN_STAT_PROT;
     ic.arg = DSS1_STAT_INVOKE_RES;
     ic.parm.dss1_io.hl_id = pc->prot.dss1.invoke_id;
     ic.parm.dss1_io.ll_id = pc->prot.dss1.ll_id;
     ic.parm.dss1_io.proc = pc->prot.dss1.proc;
     ic.parm.dss1_io.timeout= 0;
     ic.parm.dss1_io.datalen = nlen;
     ic.parm.dss1_io.data = p;
     free_invoke_id(pc->st, pc->prot.dss1.invoke_id);
     pc->prot.dss1.invoke_id = 0; /* reset id */

     cs->c_if->iif.statcallb(&ic);
     dss1_release_l3_process(pc); 
   }
  else
#endif
   l3_debug(st, "dummy return result id=0x%x result len=%d",id,nlen);
} /* l3dss1_dummy_return_result */

/*******************************************************************/
/* called when a facility message with a dummy callref is received */
/* and a return error is delivered. id specifies the invoke id.    */
/*******************************************************************/ 
static void 
l3dss1_dummy_error_return(struct PStack *st, int id, ulong error)
{ 
#ifdef CONFIG_HISAX_LLI
  isdn_ctrl ic;
  struct IsdnCardState *cs;
  struct l3_process *pc = NULL; 

  if ((pc = l3dss1_search_dummy_proc(st, id)))
   { l3pc_deltimer(pc); /* remove timer */

     cs = pc->st->l1.hardware;
     ic.driver = cs->c_if->myid;
     ic.command = ISDN_STAT_PROT;
     ic.arg = DSS1_STAT_INVOKE_ERR;
     ic.parm.dss1_io.hl_id = pc->prot.dss1.invoke_id;
     ic.parm.dss1_io.ll_id = pc->prot.dss1.ll_id;
     ic.parm.dss1_io.proc = pc->prot.dss1.proc;
     ic.parm.dss1_io.timeout= error;
     ic.parm.dss1_io.datalen = 0;
     ic.parm.dss1_io.data = NULL;
     free_invoke_id(pc->st, pc->prot.dss1.invoke_id);
     pc->prot.dss1.invoke_id = 0; /* reset id */

     cs->c_if->iif.statcallb(&ic);
     dss1_release_l3_process(pc); 
   }
  else
#endif
   l3_debug(st, "dummy return error id=0x%x error=0x%lx",id,error);
} /* l3dss1_error_return */

/*******************************************************************/
/* called when a facility message with a dummy callref is received */
/* and a invoke is delivered. id specifies the invoke id.          */
/*******************************************************************/ 
static void 
l3dss1_dummy_invoke(struct PStack *st, int cr, int id, 
                    int ident, u_char *p, u_char nlen)
{
#ifdef CONFIG_HISAX_LLI
  isdn_ctrl ic;
  struct IsdnCardState *cs;
  
  l3_debug(st, "dummy invoke %s id=0x%x ident=0x%x datalen=%d",
               (cr == -1) ? "local" : "broadcast",id,ident,nlen);
  if (cr >= -1) return; /* ignore local data */

  cs = st->l1.hardware;
  ic.driver = cs->c_if->myid;
  ic.command = ISDN_STAT_PROT;
  ic.arg = DSS1_STAT_INVOKE_BRD;
  ic.parm.dss1_io.hl_id = id;
  ic.parm.dss1_io.ll_id = 0;
  ic.parm.dss1_io.proc = ident;
  ic.parm.dss1_io.timeout= 0;
  ic.parm.dss1_io.datalen = nlen;
  ic.parm.dss1_io.data = p;

  cs->c_if->iif.statcallb(&ic);
#endif
} /* l3dss1_dummy_invoke */

static void
l3dss1_parse_facility(struct PStack *st, struct l3_process *pc,
                      int cr, u_char * p)
{
	int qd_len = 0;
	unsigned char nlen = 0, ilen, cp_tag;
	int ident, id;
	ulong err_ret;

	if (pc) 
		st = pc->st; /* valid Stack */
	else
		if ((!st) || (cr >= 0)) return; /* neither pc nor st specified */

	p++;
	qd_len = *p++;
	if (qd_len == 0) {
		l3_debug(st, "qd_len == 0");
		return;
	}
	if ((*p & 0x1F) != 0x11) {	/* Service discriminator, supplementary service */
		l3_debug(st, "supplementary service != 0x11");
		return;
	}
	while (qd_len > 0 && !(*p & 0x80)) {	/* extension ? */
		p++;
		qd_len--;
	}
	if (qd_len < 2) {
		l3_debug(st, "qd_len < 2");
		return;
	}
	p++;
	qd_len--;
	if ((*p & 0xE0) != 0xA0) {	/* class and form */
		l3_debug(st, "class and form != 0xA0");
		return;
	}
       
        cp_tag = *p & 0x1F; /* remember tag value */

        p++;
	qd_len--;
	if (qd_len < 1) 
          { l3_debug(st, "qd_len < 1");
	    return;
	  }
	if (*p & 0x80) 
          { /* length format indefinite or limited */
	    nlen = *p++ & 0x7F; /* number of len bytes or indefinite */
            if ((qd_len-- < ((!nlen) ? 3 : (1 + nlen))) ||
                (nlen > 1))   
	     { l3_debug(st, "length format error or not implemented");
	       return;
             }
            if (nlen == 1)
	     { nlen = *p++; /* complete length */
               qd_len--;
             } 
            else
	     { qd_len -= 2; /* trailing null bytes */
               if ((*(p+qd_len)) || (*(p+qd_len+1)))
		{ l3_debug(st,"length format indefinite error");
                  return;
                }
               nlen = qd_len;
             }
	  }
        else
	  { nlen = *p++;
	    qd_len--;
          } 
	if (qd_len < nlen) 
          { l3_debug(st, "qd_len < nlen");
	    return;
	  }
	qd_len -= nlen;

	if (nlen < 2) 
          { l3_debug(st, "nlen < 2");
	    return;
	  }
        if (*p != 0x02) 
          {  /* invoke identifier tag */
	     l3_debug(st, "invoke identifier tag !=0x02");
	     return;
	  }
	p++;
	nlen--;
	if (*p & 0x80) 
          { /* length format */
	    l3_debug(st, "invoke id length format 2");
	    return;
	  }
	ilen = *p++;
	nlen--;
	if (ilen > nlen || ilen == 0) 
          { l3_debug(st, "ilen > nlen || ilen == 0");
	    return;
	  }
	nlen -= ilen;
	id = 0;
	while (ilen > 0) 
          { id = (id << 8) | (*p++ & 0xFF);	/* invoke identifier */
	    ilen--;
	  }

	switch (cp_tag) {	/* component tag */
		case 1:	/* invoke */
				if (nlen < 2) {
					l3_debug(st, "nlen < 2 22");
					return;
				}
				if (*p != 0x02) {	/* operation value */
					l3_debug(st, "operation value !=0x02");
					return;
				}
				p++;
				nlen--;
				ilen = *p++;
				nlen--;
				if (ilen > nlen || ilen == 0) {
					l3_debug(st, "ilen > nlen || ilen == 0 22");
					return;
				}
				nlen -= ilen;
				ident = 0;
				while (ilen > 0) {
					ident = (ident << 8) | (*p++ & 0xFF);
					ilen--;
				}

                                if (!pc) 
			         { l3dss1_dummy_invoke(st, cr, id, ident, p, nlen);
                                   return;
                                 } 
#if HISAX_DE_AOC
			{

#define FOO1(s,a,b) \
	    while(nlen > 1) {		\
		    int ilen = p[1];	\
		    if(nlen < ilen+2) {	\
			    l3_debug(st, "FOO1  nlen < ilen+2"); \
			    return;		\
		    }			\
		    nlen -= ilen+2;		\
		    if((*p & 0xFF) == (a)) {	\
			    int nlen = ilen;	\
			    p += 2;		\
			    b;		\
		    } else {		\
			    p += ilen+2;	\
		    }			\
	    }

				switch (ident) {
				case 0x22:	/* during */
					FOO1("1A", 0x30, FOO1("1C", 0xA1, FOO1("1D", 0x30, FOO1("1E", 0x02, ( {
						   ident = 0;
						   nlen = (nlen)?nlen:0; /* Make gcc happy */
						   while (ilen > 0) {
							   ident = (ident << 8) | *p++;
							   ilen--;
						   }
						   if (ident > pc->para.chargeinfo) {
							   pc->para.chargeinfo = ident;
							   l3pc_l3l4(pc, CC_CHARGE | INDICATION, 0);
						   }
						   if (st->l3.debug & L3_DEB_CHARGE) {
							   if (*(p + 2) == 0) {
								   l3_debug(st, "charging info during %d", pc->para.chargeinfo);
							   }
							   else {
								   l3_debug(st, "charging info final %d", pc->para.chargeinfo);
							   }
						   }
					}
						)))))
						break;
				case 0x24:	/* final */
					FOO1("2A", 0x30, FOO1("2B", 0x30, FOO1("2C", 0xA1, FOO1("2D", 0x30, FOO1("2E", 0x02, ( {
						   ident = 0;
						   nlen = (nlen)?nlen:0; /* Make gcc happy */
						   while (ilen > 0) {
							   ident = (ident << 8) | *p++;
							   ilen--;
						   }
						   if (ident > pc->para.chargeinfo) {
							   pc->para.chargeinfo = ident;
							   l3l3pc_l3l4(pc, CC_CHARGE | INDICATION, 0);
						   }
						   if (st->l3.debug & L3_DEB_CHARGE) {
							   l3_debug(st, "charging info final %d", pc->para.chargeinfo);
						   }
					}
						))))))
						break;
				default:
					l3_debug(st, "invoke break invalid ident %02x",ident);
					break;
				}
#undef FOO1

			}
#else  not HISAX_DE_AOC
                        l3_debug(st, "invoke break");
#endif not HISAX_DE_AOC 
			break;
		case 2:	/* return result */
			 /* if no process available handle separately */ 
                        if (!pc)
			 { if (cr == -1) 
                             l3dss1_dummy_return_result(st, id, p, nlen);
                           return; 
                         }   
                        if ((pc->prot.dss1.invoke_id) && (pc->prot.dss1.invoke_id == id))
                          { /* Diversion successfull */
                            free_invoke_id(st,pc->prot.dss1.invoke_id);
                            pc->prot.dss1.remote_result = 0; /* success */     
                            pc->prot.dss1.invoke_id = 0;
                            pc->redir_result = pc->prot.dss1.remote_result; 
                            p_L3L4(pc, CC_REDIR | INDICATION, 0);                                  } /* Diversion successfull */
                        else
                          l3_debug(st,"return error unknown identifier");
			break;
		case 3:	/* return error */
                            err_ret = 0;
	                    if (nlen < 2) 
                              { l3_debug(st, "return error nlen < 2");
	                        return;
	                      }
                            if (*p != 0x02) 
                              { /* result tag */
	                        l3_debug(st, "invoke error tag !=0x02");
	                        return;
	                      }
	                    p++;
	                    nlen--;
	                    if (*p > 4) 
                              { /* length format */
	                        l3_debug(st, "invoke return errlen > 4 ");
	                        return;
	                      }
	                    ilen = *p++;
	                    nlen--;
	                    if (ilen > nlen || ilen == 0) 
                              { l3_debug(st, "error return ilen > nlen || ilen == 0");
	                        return;
	                       }
	                    nlen -= ilen;
	                    while (ilen > 0) 
                             { err_ret = (err_ret << 8) | (*p++ & 0xFF);	/* error value */
	                       ilen--;
	                     }
			 /* if no process available handle separately */ 
                        if (!pc)
			 { if (cr == -1)
                             l3dss1_dummy_error_return(st, id, err_ret);
                           return; 
                         }   
                        if ((pc->prot.dss1.invoke_id) && (pc->prot.dss1.invoke_id == id))
                          { /* Deflection error */
                            free_invoke_id(st,pc->prot.dss1.invoke_id);
                            pc->prot.dss1.remote_result = err_ret; /* result */
                            pc->prot.dss1.invoke_id = 0; 
                            pc->redir_result = pc->prot.dss1.remote_result; 
                            p_L3L4(pc, CC_REDIR | INDICATION, 0);  
                          } /* Deflection error */
                        else
                          l3_debug(st,"return result unknown identifier");
			break;
		default:
			l3_debug(st, "facility default break tag=0x%02x",cp_tag);
			break;
	}
}

static void
l3dss1_message(struct l3_process *pc, u_char mt)
{
	MsgDeclare(4);

        MsgXHead(pc->callref, mt);	
	MsgSend();
}

static void
l3dss1_message_cause(struct l3_process *pc, u_char mt, u_char cause)
{
	MsgDeclare(16);

	MsgXHead(pc->callref, mt);
	MsgCause(0, cause);
	MsgSend();
}

static void
l3dss1_status_send(struct l3_process *pc, u_char pr, u_char cause)
{
	MsgDeclare(16);

	pc->para.cause = cause;
	MsgXHead(pc->callref, MT_STATUS);
	MsgCause(0, cause);
	MsgCallState(pc->state);
	MsgSend();
}

static void
l3dss1_msg_without_setup(struct l3_process *pc, u_char pr, void *arg)
{
	/* This routine is called if here was no SETUP made (checks in dss1up and in
	 * l3dss1_setup) and a RELEASE_COMPLETE have to be sent with an error code
	 * MT_STATUS_ENQUIRE in the NULL state is handled too
	 */
	MsgDeclare(16);

	switch (pc->para.cause) {
		case CAU_INVALID_CALL_REFERENCE:
		case CAU_INCOMPATIBLE_DESTINATION:
		case CAU_MANDATORY_IE_MISSING:
		case CAU_INVALID_IE_CONTENTS:
		case CAU_MSG_NOT_COMPATIBLE_WITH_CALL_STATE:
			MsgXHead(pc->callref, MT_RELEASE_COMPLETE);
			MsgCause(0, pc->para.cause);
			break;
		default:
			printk(KERN_ERR "HiSax l3dss1_msg_without_setup wrong cause %d\n",
				pc->para.cause);
			return;
	}
	MsgSend();
	dss1_release_l3_process(pc);
}

static int ie_ALERTING[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
		IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, IE_HLC,
		IE_USER_USER, -1};
static int ie_CALL_PROCEEDING[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
		IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_HLC, -1};
static int ie_CONNECT[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1, 
		IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_DATE, IE_SIGNAL,
		IE_CONNECT_PN, IE_CONNECT_SUB, IE_LLC, IE_HLC, IE_USER_USER, -1};
static int ie_CONNECT_ACKNOWLEDGE[] = {IE_CHANNEL_ID, IE_DISPLAY, IE_SIGNAL, -1};
static int ie_DISCONNECT[] = {IE_CAUSE | IE_MANDATORY, IE_FACILITY,
		IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
static int ie_INFORMATION[] = {IE_COMPLETE, IE_DISPLAY, IE_KEYPAD, IE_SIGNAL,
		IE_CALLED_PN, -1};
static int ie_NOTIFY[] = {IE_BEARER, IE_NOTIFY | IE_MANDATORY, IE_DISPLAY, -1};
static int ie_PROGRESS[] = {IE_BEARER, IE_CAUSE, IE_FACILITY, IE_PROGRESS |
		IE_MANDATORY, IE_DISPLAY, IE_HLC, IE_USER_USER, -1};
static int ie_RELEASE[] = {IE_CAUSE | IE_MANDATORY_1, IE_FACILITY, IE_DISPLAY,
		IE_SIGNAL, IE_USER_USER, -1};
/* a RELEASE_COMPLETE with errors don't require special actions 
static int ie_RELEASE_COMPLETE[] = {IE_CAUSE | IE_MANDATORY_1, IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
*/
static int ie_RESUME_ACKNOWLEDGE[] = {IE_CHANNEL_ID| IE_MANDATORY, IE_FACILITY,
		IE_DISPLAY, -1};
static int ie_RESUME_REJECT[] = {IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
static int ie_SETUP[] = {IE_COMPLETE, IE_BEARER  | IE_MANDATORY,
		IE_CHANNEL_ID| IE_MANDATORY, IE_FACILITY, IE_PROGRESS,
		IE_NET_FAC, IE_DISPLAY, IE_KEYPAD, IE_SIGNAL, IE_CALLING_PN,
		IE_CALLING_SUB, IE_CALLED_PN, IE_CALLED_SUB, IE_REDIR_NR,
		IE_LLC, IE_HLC, IE_USER_USER, -1};
static int ie_SETUP_ACKNOWLEDGE[] = {IE_CHANNEL_ID | IE_MANDATORY, IE_FACILITY,
		IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, -1};
static int ie_STATUS[] = {IE_CAUSE | IE_MANDATORY, IE_CALL_STATE |
		IE_MANDATORY, IE_DISPLAY, -1};
static int ie_STATUS_ENQUIRY[] = {IE_DISPLAY, -1};
static int ie_SUSPEND_ACKNOWLEDGE[] = {IE_DISPLAY, IE_FACILITY, -1};
static int ie_SUSPEND_REJECT[] = {IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
/* not used 
 * static int ie_CONGESTION_CONTROL[] = {IE_CONGESTION | IE_MANDATORY,
 *		IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
 * static int ie_USER_INFORMATION[] = {IE_MORE_DATA, IE_USER_USER | IE_MANDATORY, -1};
 * static int ie_RESTART[] = {IE_CHANNEL_ID, IE_DISPLAY, IE_RESTART_IND |
 *		IE_MANDATORY, -1};
 */
static int ie_FACILITY[] = {IE_FACILITY | IE_MANDATORY, IE_DISPLAY, -1};
static int comp_required[] = {1,2,3,5,6,7,9,10,11,14,15,-1};
static int l3_valid_states[] = {0,1,2,3,4,6,7,8,9,10,11,12,15,17,19,25,-1};

struct ie_len {
	int ie;
	int len;
};

static
struct ie_len max_ie_len[] = {
	{IE_SEGMENT, 4},
	{IE_BEARER, 13},
	{IE_CAUSE, 32},
	{IE_CALL_ID, 10},
	{IE_CALL_STATE, 3},
	{IE_CHANNEL_ID,	34},
	{IE_FACILITY, 255},
	{IE_PROGRESS, 4},
	{IE_NET_FAC, 255},
	{IE_NOTIFY, 3},
	{IE_DISPLAY, 82},
	{IE_DATE, 8},
	{IE_KEYPAD, 34},
	{IE_SIGNAL, 3},
	{IE_INFORATE, 6},
	{IE_E2E_TDELAY, 11},
	{IE_TDELAY_SEL, 5},
	{IE_PACK_BINPARA, 3},
	{IE_PACK_WINSIZE, 4},
	{IE_PACK_SIZE, 4},
	{IE_CUG, 7},
	{IE_REV_CHARGE, 3},
	{IE_CALLING_PN, 24},
	{IE_CALLING_SUB, 23},
	{IE_CALLED_PN, 23},
	{IE_CALLED_SUB, 23},
	{IE_REDIR_NR, 255},
	{IE_TRANS_SEL, 255},
	{IE_RESTART_IND, 3},
	{IE_LLC, 16},
	{IE_HLC, 4},
	{IE_USER_USER, 131},
	{-1,0},
};

int
getmax_ie_len(u_char ie) 
{
	int i = 0;
	while (max_ie_len[i].ie != -1) {
		if (max_ie_len[i].ie == ie)
			return(max_ie_len[i].len);
		i++;
	}
	return(255);
}

static int
ie_in_set(struct l3_process *pc, u_char ie, int *checklist) {
	int ret = 1;

	while (*checklist != -1) {
		if ((*checklist & 0xff) == ie) {
			if (ie & 0x80)
				return(-ret);
			else
				return(ret);
		}
		ret++;
		checklist++;
	}
	return(0);
}

static int
check_infoelements(struct l3_process *pc, struct sk_buff *skb, int *checklist)
{
	int *cl = checklist;
	u_char mt;
	u_char *p, ie;
	int l, newpos, oldpos;
	int err_seq = 0, err_len = 0, err_compr = 0, err_ureg = 0;
	
	p = skb->data;
	/* skip cr */
	p++;
	l = (*p++) & 0xf;
	p += l;
	mt = *p++;
	oldpos = 0;
/* shift codeset procedure not implemented in the moment */
	while ((p - skb->data) < skb->len) {
		if ((newpos = ie_in_set(pc, *p, cl))) {
			if (newpos > 0) {
				if (newpos < oldpos)
					err_seq++;
				else
					oldpos = newpos;
			}
		} else {
			if (ie_in_set(pc, *p, comp_required))
				err_compr++;
			else
				err_ureg++;
		}
		ie = *p++;
		if (ie & 0x80) {
			l = 1;
		} else {
			l = *p++;
			p += l;
			l += 2;
		}
		if (l > getmax_ie_len(ie))
			err_len++;
	}
	if (err_compr | err_ureg | err_len | err_seq) {
		if (pc->debug & L3_DEB_CHECK)
			l3_debug(pc->st, "check_infoelements mt %x %d/%d/%d/%d",
				mt, err_compr, err_ureg, err_len, err_seq);
		if (err_compr)
			return(ERR_IE_COMPREHENSION);
		if (err_ureg)
			return(ERR_IE_UNRECOGNIZED);
		if (err_len)
			return(ERR_IE_LENGTH);
		if (err_seq)
			return(ERR_IE_SEQUENCE);
	} 
	return(0);
}

/* verify if a message type exists and contain no IE error */
static int
l3dss1_check_messagetype_validity(struct l3_process *pc, int mt, void *arg)
{
	switch (mt) {
		case MT_ALERTING:
		case MT_CALL_PROCEEDING:
		case MT_CONNECT:
		case MT_CONNECT_ACKNOWLEDGE:
		case MT_DISCONNECT:
		case MT_INFORMATION:
		case MT_FACILITY:
		case MT_NOTIFY:
		case MT_PROGRESS:
		case MT_RELEASE:
		case MT_RELEASE_COMPLETE:
		case MT_SETUP:
		case MT_SETUP_ACKNOWLEDGE:
		case MT_RESUME_ACKNOWLEDGE:
		case MT_RESUME_REJECT:
		case MT_SUSPEND_ACKNOWLEDGE:
		case MT_SUSPEND_REJECT:
		case MT_USER_INFORMATION:
		case MT_RESTART:
		case MT_RESTART_ACKNOWLEDGE:
		case MT_CONGESTION_CONTROL:
		case MT_STATUS:
		case MT_STATUS_ENQUIRY:
			if (pc->debug & L3_DEB_CHECK)
				l3_debug(pc->st, "l3dss1_check_messagetype_validity mt(%x) OK", mt);
			break;
		case MT_RESUME: /* RESUME only in user->net */
		case MT_SUSPEND: /* SUSPEND only in user->net */
		default:
			if (pc->debug & (L3_DEB_CHECK | L3_DEB_WARN))
				l3_debug(pc->st, "l3dss1_check_messagetype_validity mt(%x) fail", mt);
			l3dss1_status_send(pc, 0, CAU_MESSAGE_TYPE_NON_EXISTENT);
			return(1);
	}
	return(0);
}

static void
l3dss1_std_ie_err(struct l3_process *pc, int ret) {

	if (pc->debug & L3_DEB_CHECK)
		l3_debug(pc->st, "check_infoelements ret %d", ret);
	switch(ret) {
		case 0: 
			break;
		case ERR_IE_COMPREHENSION:
			l3dss1_status_send(pc, 0, CAU_MANDATORY_IE_MISSING);
			break;
		case ERR_IE_UNRECOGNIZED:
			l3dss1_status_send(pc, 0, CAU_IE_NON_EXISTENT);
			break;
		case ERR_IE_LENGTH:
			l3dss1_status_send(pc, 0, CAU_INVALID_IE_CONTENTS);
			break;
		case ERR_IE_SEQUENCE:
		default:
			break;
	}
}

static int
l3dss1_get_channel_id(struct l3_process *pc, struct sk_buff *skb) {
	u_char *p;

	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		p++;
		if (*p != 1) { /* len for BRI = 1 */
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "wrong chid len %d", *p);
			return (-2);
		}
		p++;
		if (*p & 0x60) { /* only base rate interface */
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "wrong chid %x", *p);
			return (-3);
		}
		return(*p & 0x3);
	} else
		return(-1);
}

static int
l3dss1_get_cause(struct l3_process *pc, struct sk_buff *skb) {
	u_char l, i=0;
	u_char *p;

	p = skb->data;
	pc->para.cause = 31;
	pc->para.loc = 0;
	if ((p = findie(p, skb->len, IE_CAUSE, 0))) {
		p++;
		l = *p++;
		if (l>30)
			return(1);
		if (l) {
			pc->para.loc = *p++;
			l--;
		} else {
			return(2);
		}
		if (l && !(pc->para.loc & 0x80)) {
			l--;
			p++; /* skip recommendation */
		}
		if (l) {
			pc->para.cause = *p++;
			l--;
			if (!(pc->para.cause & 0x80))
				return(3);
		} else
			return(4);
		while (l && (i<6)) {
			pc->para.diag[i++] = *p++;
			l--;
		}
	} else
		return(-1);
	return(0);
}

static void
l3dss1_msg_with_uus(struct l3_process *pc, u_char cmd)
{
	MsgDeclare(16+40);

	MsgXHead(pc->callref, cmd);
        if (pc->prot.dss1.uus1_data[0]) 
		MsgUUS(pc->prot.dss1.uus1_data);
	MsgSend();
}

static void
l3dss1_release_req(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	l3pc_newstate(pc, 19);
	if (!pc->prot.dss1.uus1_data[0]) 
		l3dss1_message(pc, MT_RELEASE);
	else
		l3dss1_msg_with_uus(pc, MT_RELEASE);
	l3pc_addtimer(pc, T308, CC_T308_1);
}

static void
l3dss1_release_cmpl(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((ret = l3dss1_get_cause(pc, skb))>0) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "RELCMPL get_cause ret(%d)",ret);
	} else if (ret < 0)
		pc->para.cause = NO_CAUSE;
	l3pc_deltimer(pc);
	l3pc_newstate(pc, 0);
	p_L3L4(pc, CC_RELEASE | CONFIRM, skb);
	dss1_release_l3_process(pc);
}

#if EXT_BEARER_CAPS

u_char *
EncodeASyncParams(u_char * p, u_char si2)
{				// 7c 06 88  90 21 42 00 bb

	p[0] = 0;
	p[1] = 0x40;		// Intermediate rate: 16 kbit/s jj 2000.02.19
	p[2] = 0x80;
	if (si2 & 32)		// 7 data bits

		p[2] += 16;
	else			// 8 data bits

		p[2] += 24;

	if (si2 & 16)		// 2 stop bits

		p[2] += 96;
	else			// 1 stop bit

		p[2] += 32;

	if (si2 & 8)		// even parity

		p[2] += 2;
	else			// no parity

		p[2] += 3;

	switch (si2 & 0x07) {
		case 0:
			p[0] = 66;	// 1200 bit/s

			break;
		case 1:
			p[0] = 88;	// 1200/75 bit/s

			break;
		case 2:
			p[0] = 87;	// 75/1200 bit/s

			break;
		case 3:
			p[0] = 67;	// 2400 bit/s

			break;
		case 4:
			p[0] = 69;	// 4800 bit/s

			break;
		case 5:
			p[0] = 72;	// 9600 bit/s

			break;
		case 6:
			p[0] = 73;	// 14400 bit/s

			break;
		case 7:
			p[0] = 75;	// 19200 bit/s

			break;
	}
	return p + 3;
}

u_char
EncodeSyncParams(u_char si2, u_char ai)
{

	switch (si2) {
		case 0:
			return ai + 2;	// 1200 bit/s

		case 1:
			return ai + 24;		// 1200/75 bit/s

		case 2:
			return ai + 23;		// 75/1200 bit/s

		case 3:
			return ai + 3;	// 2400 bit/s

		case 4:
			return ai + 5;	// 4800 bit/s

		case 5:
			return ai + 8;	// 9600 bit/s

		case 6:
			return ai + 9;	// 14400 bit/s

		case 7:
			return ai + 11;		// 19200 bit/s

		case 8:
			return ai + 14;		// 48000 bit/s

		case 9:
			return ai + 15;		// 56000 bit/s

		case 15:
			return ai + 40;		// negotiate bit/s

		default:
			break;
	}
	return ai;
}


static u_char
DecodeASyncParams(u_char si2, u_char * p)
{
	u_char info;

	switch (p[5]) {
		case 66:	// 1200 bit/s

			break;	// si2 don't change

		case 88:	// 1200/75 bit/s

			si2 += 1;
			break;
		case 87:	// 75/1200 bit/s

			si2 += 2;
			break;
		case 67:	// 2400 bit/s

			si2 += 3;
			break;
		case 69:	// 4800 bit/s

			si2 += 4;
			break;
		case 72:	// 9600 bit/s

			si2 += 5;
			break;
		case 73:	// 14400 bit/s

			si2 += 6;
			break;
		case 75:	// 19200 bit/s

			si2 += 7;
			break;
	}

	info = p[7] & 0x7f;
	if ((info & 16) && (!(info & 8)))	// 7 data bits

		si2 += 32;	// else 8 data bits

	if ((info & 96) == 96)	// 2 stop bits

		si2 += 16;	// else 1 stop bit

	if ((info & 2) && (!(info & 1)))	// even parity

		si2 += 8;	// else no parity

	return si2;
}


static u_char
DecodeSyncParams(u_char si2, u_char info)
{
	info &= 0x7f;
	switch (info) {
		case 40:	// bit/s negotiation failed  ai := 165 not 175!

			return si2 + 15;
		case 15:	// 56000 bit/s failed, ai := 0 not 169 !

			return si2 + 9;
		case 14:	// 48000 bit/s

			return si2 + 8;
		case 11:	// 19200 bit/s

			return si2 + 7;
		case 9:	// 14400 bit/s

			return si2 + 6;
		case 8:	// 9600  bit/s

			return si2 + 5;
		case 5:	// 4800  bit/s

			return si2 + 4;
		case 3:	// 2400  bit/s

			return si2 + 3;
		case 23:	// 75/1200 bit/s

			return si2 + 2;
		case 24:	// 1200/75 bit/s

			return si2 + 1;
		default:	// 1200 bit/s

			return si2;
	}
}

static u_char
DecodeSI2(struct sk_buff *skb)
{
	u_char *p;		//, *pend=skb->data + skb->len;

	if ((p = findie(skb->data, skb->len, 0x7c, 0))) {
		switch (p[4] & 0x0f) {
			case 0x01:
				if (p[1] == 0x04)	// sync. Bitratenadaption

					return DecodeSyncParams(160, p[5]);	// V.110/X.30

				else if (p[1] == 0x06)	// async. Bitratenadaption

					return DecodeASyncParams(192, p);	// V.110/X.30

				break;
			case 0x08:	// if (p[5] == 0x02) // sync. Bitratenadaption
				if (p[1] > 3) 
					return DecodeSyncParams(176, p[5]);	// V.120
				break;
		}
	}
	return 0;
}

#endif


static void l3dss1_gen_setup_req(struct l3_process *pc, u_char pr, void *arg);

static void
l3dss1_setup_req(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	u_char channel = 0;

        u_char send_keypad;
	u_char screen = 0x80;
	u_char *teln;
	u_char *msn;
	u_char *sub;
	u_char *sp;
	setup_parm *setup = (setup_parm *) arg;
	struct setup_req_parm setup_req;

	memset(&setup_req, 0, sizeof(struct setup_req_parm));

	teln = setup->phone;
#ifndef CONFIG_HISAX_NO_KEYPAD
        send_keypad = (strchr(teln,'*') || strchr(teln,'#')) ? 1 : 0; 
#else
	send_keypad = 0;
#endif
#ifndef CONFIG_HISAX_NO_SENDCOMPLETE
	if (!send_keypad)
		setup_req.sending_complete[0] = IE_COMPLETE;
#endif

	/*
	 * Set Bearer Capability, Map info from 1TR6-convention to EDSS1
	 */
	p = setup_req.bearer_capability;
	switch (setup->si1) {
	case 1:	                  /* Telephony                                */
		*p++ = IE_BEARER;
		*p++ = 0x3;	  /* Length                                   */
		*p++ = 0x90;	  /* Coding Std. CCITT, 3.1 kHz audio         */
		*p++ = 0x90;	  /* Circuit-Mode 64kbps                      */
		*p++ = 0xa3;	  /* A-Law Audio                              */
		break;
	case 5:	                  /* Datatransmission 64k, BTX                */
	case 7:	                  /* Datatransmission 64k                     */
	default:
		*p++ = IE_BEARER;
		*p++ = 0x2;	  /* Length                                   */
		*p++ = 0x88;	  /* Coding Std. CCITT, unrestr. dig. Inform. */
		*p++ = 0x90;	  /* Circuit-Mode 64kbps                      */
		break;
	}

	if (send_keypad) {
		p = setup_req.keypad_facility;
		*p++ = IE_KEYPAD;
		*p++ = strlen(teln);
		while (*teln)
			*p++ = (*teln++) & 0x7F;
	}
	  
	/*
	 * What about info2? Mapping to High-Layer-Compatibility?
	 */
	if ((*teln) && (!send_keypad)) {
		/* parse number for special things */
		if (!isdigit(*teln)) {
			switch (0x5f & *teln) {
				case 'C':
					channel = 0x08;
				case 'P':
					channel |= 0x80;
					teln++;
					if (*teln == '1')
						channel |= 0x01;
					else
						channel |= 0x02;
					break;
				case 'R':
					screen = 0xA0;
					break;
				case 'D':
					screen = 0x80;
					break;
				
			        default:
					if (pc->debug & L3_DEB_WARN)
						l3_debug(pc->st, "Wrong MSN Code");
					break;
			}
			teln++;
		}
	}
	if (channel) {
		p = setup_req.channel_identification;
		*p++ = IE_CHANNEL_ID;
		*p++ = 1;
		*p++ = channel;
	}
	msn = setup->eazmsn;
	sub = NULL;
	sp = msn;
	while (*sp) {
		if ('.' == *sp) {
			sub = sp;
			*sp = 0;
		} else
			sp++;
	}
	if (*msn) {
		p = setup_req.calling_party_number;
		*p++ = IE_CALLING_PN;
		*p++ = strlen(msn) + (screen ? 2 : 1);
		/* Classify as AnyPref. */
		if (screen) {
			*p++ = 0x01;	/* Ext = '0'B, Type = '000'B, Plan = '0001'B. */
			*p++ = screen;
		} else
			*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		while (*msn)
			*p++ = *msn++ & 0x7f;
	}
	if (sub) {
		*sub++ = '.';
		p = setup_req.calling_party_subaddress;
		*p++ = IE_CALLING_SUB;
		*p++ = strlen(sub) + 2;
		*p++ = 0x80;	/* NSAP coded */
		*p++ = 0x50;	/* local IDI format */
		while (*sub)
			*p++ = *sub++ & 0x7f;
	}
	sub = NULL;
	sp = teln;
	while (*sp) {
		if ('.' == *sp) {
			sub = sp;
			*sp = 0;
		} else
			sp++;
	}
	
        if (!send_keypad) {      
		p = setup_req.called_party_number;
		*p++ = IE_CALLED_PN;
		*p++ = strlen(teln) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		while (*teln)
			*p++ = *teln++ & 0x7f;
		
		if (sub) {
			*sub++ = '.';
			p = setup_req.called_party_subaddress;
			*p++ = IE_CALLED_SUB;
			*p++ = strlen(sub) + 2;
			*p++ = 0x80;	/* NSAP coded */
			*p++ = 0x50;	/* local IDI format */
			while (*sub)
				*p++ = *sub++ & 0x7f;
		}
        }
	p = setup_req.low_layer_compatibility;
#if EXT_BEARER_CAPS
	if ((setup->si2 >= 160) && (setup->si2 <= 175)) {	// sync. Bitratenadaption, V.110/X.30

		*p++ = IE_LLC;
		*p++ = 0x04;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x21;
		*p++ = EncodeSyncParams(setup->si2 - 160, 0x80);
	} else if ((setup->si2 >= 176) && (setup->si2 <= 191)) {	// sync. Bitratenadaption, V.120

		*p++ = IE_LLC;
		*p++ = 0x05;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x28;
		*p++ = EncodeSyncParams(setup->si2 - 176, 0);
		*p++ = 0x82;
	} else if (setup->si2 >= 192) {		// async. Bitratenadaption, V.110/X.30

		*p++ = IE_LLC;
		*p++ = 0x06;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x21;
		p = EncodeASyncParams(p, setup->si2 - 192);
#ifndef CONFIG_HISAX_NO_LLC
	} else {
	  switch (setup->si1) {
		case 1:	                /* Telephony                                */
			*p++ = IE_LLC;
			*p++ = 0x3;	/* Length                                   */
			*p++ = 0x90;	/* Coding Std. CCITT, 3.1 kHz audio         */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                      */
			*p++ = 0xa3;	/* A-Law Audio                              */
			break;
		case 5:	                /* Datatransmission 64k, BTX                */
		case 7:	                /* Datatransmission 64k                     */
		default:
			*p++ = IE_LLC;
			*p++ = 0x2;	/* Length                                   */
			*p++ = 0x88;	/* Coding Std. CCITT, unrestr. dig. Inform. */
			*p++ = 0x90;	/* Circuit-Mode 64kbps                      */
			break;
	  }
#endif
	}
#endif
	memcpy(&pc->setup_req, &setup_req, sizeof(struct setup_req_parm));
	l3dss1_gen_setup_req(pc, pr, &setup_req);
}

static void
l3dss1_gen_setup_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(128);
	struct setup_req_parm *setup_req = &pc->setup_req;

	MsgXHead(pc->callref, MT_SETUP);

	if (!setup_req->bearer_capability[0]) {
		int_error();
		return;
	}
	MsgAdd(setup_req->sending_complete);
	MsgAdd(setup_req->bearer_capability);
	MsgAdd(setup_req->channel_identification);
	MsgAdd(setup_req->keypad_facility);
	MsgAdd(setup_req->calling_party_number);
	MsgAdd(setup_req->calling_party_subaddress);
	MsgAdd(setup_req->called_party_number);
	MsgAdd(setup_req->called_party_subaddress);
	MsgAdd(setup_req->low_layer_compatibility);

	MsgSend();

	l3pc_deltimer(pc);
	l3pc_addtimer(pc, T303, CC_T303);
	l3pc_newstate(pc, 1);
}

// ==========================================================================
// handle messages from call control
// ==========================================================================

static void
l3dss1_dummy_req(struct PStack *st, u_char pr, void *arg)
{
	MsgDeclare(255);
	struct facility_req_parm *facility_req = arg;
	
	MsgXHead(-1, MT_FACILITY);
	MsgAdd(facility_req->facility);
	__l = __p - __tmp;
	if ((__skb = l3_alloc_skb(__l))) {
	        memcpy(skb_put(__skb, __l), __tmp, __l);
	        l3_msg(st, DL_DATA | REQUEST, __skb);
        }
}

// ==========================================================================
// outgoing

static void
l3dss1_x_setup_req(struct l3_process *pc, u_char pr, void *arg)
{
	if (!arg) {
		int_error();
		return;
	}
	memcpy(&pc->setup_req, arg, sizeof(struct setup_req_parm));
	l3dss1_gen_setup_req(pc, pr, arg);
}

// ==========================================================================
// incoming

static void
l3dss1_alerting_req(struct l3_process *pc, u_char pr, void *arg)
{
        MsgDeclare(256);
        struct alerting_req_parm *alerting_req = arg;

        l3pc_newstate(pc, 7);
        MsgXHead(pc->callref, MT_ALERTING);
        if (alerting_req) {
                MsgAdd(alerting_req->facility);
                MsgAdd(alerting_req->progress_indicator);
                MsgAdd(alerting_req->user_user);
        }
        if (pc->prot.dss1.uus1_data[0])
                MsgUUS(pc->prot.dss1.uus1_data);

        MsgSend();
}

static void
l3dss1_x_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
        MsgDeclare(256);
        struct disconnect_req_parm *disconnect_req = arg;

	l3pc_deltimer(pc);
	l3pc_newstate(pc, 11);

        MsgXHead(pc->callref, MT_DISCONNECT);
        if (disconnect_req) {
                if (!disconnect_req->cause[0]) {
                        int_error();
                        return;
                }
                MsgAdd(disconnect_req->cause);
                MsgAdd(disconnect_req->facility);
                MsgAdd(disconnect_req->progress_indicator);
                MsgAdd(disconnect_req->user_user);
		pc->para.cause = disconnect_req->cause[3] & 0x7f;
        } else {
                MsgCause(0, CAU_NORMAL_CALL_CLEARING);
		pc->para.cause = CAU_NORMAL_CALL_CLEARING;
        }
        if (pc->prot.dss1.uus1_data[0])
                MsgUUS(pc->prot.dss1.uus1_data);

        MsgSend();
        l3pc_addtimer(pc, T305, CC_T305);
}

static void
l3dss1_call_proc(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3dss1_get_channel_id(pc, skb)) >= 0) {
		if ((0 == id) || ((3 == id) && (0x10 == pc->para.moderate))) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup answer with wrong chid %x", id);
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
			return;
		}
		pc->para.bchannel = id;
	} else if (1 == pc->state) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup answer wrong chid (ret %d)", id);
		if (id == -1)
			l3dss1_status_send(pc, pr, CAU_MANDATORY_IE_MISSING);
		else
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_CALL_PROCEEDING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);
	l3pc_newstate(pc, 3);
	l3pc_addtimer(pc, T310, CC_T310);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	p_L3L4(pc, CC_PROCEEDING | INDICATION, skb);
}

static void
l3dss1_setup_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3dss1_get_channel_id(pc, skb)) >= 0) {
		if ((0 == id) || ((3 == id) && (0x10 == pc->para.moderate))) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup answer with wrong chid %x", id);
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
			return;
		}
		pc->para.bchannel = id;
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup answer wrong chid (ret %d)", id);
		if (id == -1)
			l3dss1_status_send(pc, pr, CAU_MANDATORY_IE_MISSING);
		else
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_SETUP_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);
	l3pc_newstate(pc, 2);
	l3pc_addtimer(pc, T304, CC_T304);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	p_L3L4(pc, CC_MORE_INFO | INDICATION, skb);
}

static void
l3dss1_disconnect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;
	int ret;
	u_char cause = 0;

	l3pc_deltimer(pc);
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "DISC get_cause ret(%d)", ret);
		if (ret < 0)
			cause = CAU_MANDATORY_IE_MISSING;
		else if (ret > 0)
			cause = CAU_INVALID_IE_CONTENTS;
	} 
	if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
		l3dss1_parse_facility(pc->st, pc, pc->callref, p);
	ret = check_infoelements(pc, skb, ie_DISCONNECT);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAU_MANDATORY_IE_MISSING;
	else if ((!cause) && (ERR_IE_UNRECOGNIZED == ret))
		cause = CAU_IE_NON_EXISTENT;
	ret = pc->state;
	l3pc_newstate(pc, 12);
	if (cause)
		l3pc_newstate(pc, 19);
       	if (11 != ret)
		p_L3L4(pc, CC_DISCONNECT | INDICATION, skb);
       	else if (!cause)
		l3dss1_release_req(pc, pr, NULL);
	if (cause) {
		l3dss1_message_cause(pc, MT_RELEASE, cause);
		l3pc_addtimer(pc, T308, CC_T308_1);
	}
}

static void
l3dss1_connect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_CONNECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);	/* T310 */
	l3pc_newstate(pc, 10);
	pc->para.chargeinfo = 0;
	/* here should inserted COLP handling KKe */
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	p_L3L4(pc, CC_SETUP | CONFIRM, skb);
}

static void
l3dss1_alerting(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_ALERTING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);	/* T304 */
	l3pc_newstate(pc, 4);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	p_L3L4(pc, CC_ALERTING | INDICATION, skb);
}

static void
l3dss1_setup(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;
	int id;
	int err = 0;
	setup_parm *setup = &pc->para.setup;

	/*
	 * Bearer Capabilities
	 */
	p = skb->data;
	/* only the first occurence 'll be detected ! */
	if ((p = findie(p, skb->len, 0x04, 0))) {
		if ((p[1] < 2) || (p[1] > 11))
			err = 1;
		else {
			setup->si2 = 0;
			switch (p[2] & 0x7f) {
				case 0x00: /* Speech */
				case 0x10: /* 3.1 Khz audio */
					setup->si1 = 1;
					break;
				case 0x08: /* Unrestricted digital information */
					setup->si1 = 7;
/* JIM, 05.11.97 I wanna set service indicator 2 */
#if EXT_BEARER_CAPS
					setup->si2 = DecodeSI2(skb);
#endif
					break;
				case 0x09: /* Restricted digital information */
					setup->si1 = 2;
					break;
				case 0x11:
					/* Unrestr. digital information  with 
					 * tones/announcements ( or 7 kHz audio
					 */
					setup->si1 = 3;
					break;
				case 0x18: /* Video */
					setup->si1 = 4;
					break;
				default:
					err = 2;
					break;
			}
			switch (p[3] & 0x7f) {
				case 0x40: /* packed mode */
					setup->si1 = 8;
					break;
				case 0x10: /* 64 kbit */
				case 0x11: /* 2*64 kbit */
				case 0x13: /* 384 kbit */
				case 0x15: /* 1536 kbit */
				case 0x17: /* 1920 kbit */
					pc->para.moderate = p[3] & 0x7f;
					break;
				default:
					err = 3;
					break;
			}
		}
		if (pc->debug & L3_DEB_SI)
			l3_debug(pc->st, "SI=%d, AI=%d",
				setup->si1, setup->si2);
		if (err) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "setup with wrong bearer(l=%d:%x,%x)",
					p[1], p[2], p[3]);
			pc->para.cause = CAU_INVALID_IE_CONTENTS;
			l3dss1_msg_without_setup(pc, pr, NULL);
			return;
		}
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup without bearer capabilities");
		/* ETS 300-104 1.3.3 */
		pc->para.cause = CAU_MANDATORY_IE_MISSING;
		l3dss1_msg_without_setup(pc, pr, NULL);
		return;
	}
	/*
	 * Channel Identification
	 */
	if ((id = l3dss1_get_channel_id(pc, skb)) >= 0) {
		if ((pc->para.bchannel = id)) {
			if ((3 == id) && (0x10 == pc->para.moderate)) {
				if (pc->debug & L3_DEB_WARN)
					l3_debug(pc->st, "setup with wrong chid %x",
						id);
				pc->para.cause = CAU_INVALID_IE_CONTENTS;
				l3dss1_msg_without_setup(pc, pr, NULL);
				return;
			}
			bcfound++;
		} else 
                   { if (pc->debug & L3_DEB_WARN)
			 l3_debug(pc->st, "setup without bchannel, call waiting");
                     bcfound++;
                   } 
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "setup with wrong chid ret %d", id);
		if (id == -1)
			pc->para.cause = CAU_MANDATORY_IE_MISSING;
		else
			pc->para.cause = CAU_INVALID_IE_CONTENTS;
		l3dss1_msg_without_setup(pc, pr, NULL);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_SETUP);
	if (ERR_IE_COMPREHENSION == err) {
		pc->para.cause = CAU_MANDATORY_IE_MISSING;
		l3dss1_msg_without_setup(pc, pr, NULL);
		return;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x70, 0)))
		iecpy(setup->eazmsn, p, 1);
	else
		setup->eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, 0x71, 0))) {
		/* Called party subaddress */
		if ((p[1] >= 2) && (p[2] == 0x80) && (p[3] == 0x50)) {
			tmp[0] = '.';
			iecpy(&tmp[1], p, 2);
			strcat(setup->eazmsn, tmp);
		} else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "wrong called subaddress");
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x6c, 0))) {
		setup->plan = p[2];
		if (p[2] & 0x80) {
			iecpy(setup->phone, p, 1);
			setup->screen = 0;
		} else {
			iecpy(setup->phone, p, 2);
			setup->screen = p[3];
		}
	} else {
		setup->phone[0] = 0;
		setup->plan = 0;
		setup->screen = 0;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, 0x6d, 0))) {
		/* Calling party subaddress */
		if ((p[1] >= 2) && (p[2] == 0x80) && (p[3] == 0x50)) {
			tmp[0] = '.';
			iecpy(&tmp[1], p, 2);
			strcat(setup->phone, tmp);
		} else if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "wrong calling subaddress");
	}
	l3pc_newstate(pc, 6);
	if (err) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, err);
	pc->st->l4->l3l4(pc->st, CC_NEW_CR | INDICATION, pc);
	if (!pc->l4pc) { // no l4 process available
		pc->para.cause = CAU_USER_BUSY;
		pc->l4l3(pc, CC_REJECT | REQUEST, 0);
	} else {
		p_L3L4(pc, CC_SETUP | INDICATION, skb);
	}
}

static void
l3dss1_reset(struct l3_process *pc, u_char pr, void *arg)
{
	dss1_release_l3_process(pc);
}

static void
l3dss1_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(16+40);
	u_char cause = CAU_NORMAL_CALL_CLEARING;

	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;

	l3pc_deltimer(pc);

	MsgXHead(pc->callref, MT_DISCONNECT);
	MsgCause(0, cause);
        if (pc->prot.dss1.uus1_data[0])
		MsgUUS(pc->prot.dss1.uus1_data);
	l3pc_newstate(pc, 11);
	MsgSend();
	l3pc_addtimer(pc, T305, CC_T305);
}

static void
l3dss1_setup_rsp(struct l3_process *pc, u_char pr,
		 void *arg)
{
        if (!pc->para.bchannel) 
	 { if (pc->debug & L3_DEB_WARN)
	       l3_debug(pc->st, "D-chan connect for waiting call");
           l3dss1_disconnect_req(pc, pr, arg);
           return;
         }
	l3pc_newstate(pc, 8);
	l3dss1_message(pc, MT_CONNECT);
	l3pc_deltimer(pc);
	l3pc_addtimer(pc, T313, CC_T313);
}

static void
l3dss1_connect_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_CONNECT_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_newstate(pc, 10);
	l3pc_deltimer(pc);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	p_L3L4(pc, CC_SETUP_COMPL | INDICATION, skb);
}

static void
l3dss1_reject_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(16);
	u_char cause = CAU_CALL_REJECTED;

	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;

	MsgXHead(pc->callref, MT_RELEASE_COMPLETE);
	MsgCause(0, cause);
	MsgSend();
	p_L3L4(pc, CC_RELEASE | INDICATION, 0);
	l3pc_newstate(pc, 0);
	dss1_release_l3_process(pc);
}

static void
l3dss1_release(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;
	int ret, cause=0;

	l3pc_deltimer(pc);
	if ((ret = l3dss1_get_cause(pc, skb))>0) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "REL get_cause ret(%d)", ret);
	} else if (ret<0)
		pc->para.cause = NO_CAUSE;
	if ((p = findie(skb->data, skb->len, IE_FACILITY, 0))) {
		l3dss1_parse_facility(pc->st, pc, pc->callref, p);
	}
	if ((ret<0) && (pc->state != 11))
		cause = CAU_MANDATORY_IE_MISSING;
	else if (ret>0)
		cause = CAU_INVALID_IE_CONTENTS;
	ret = check_infoelements(pc, skb, ie_RELEASE);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAU_MANDATORY_IE_MISSING;
	else if ((ERR_IE_UNRECOGNIZED == ret) && (!cause))
		cause = CAU_IE_NON_EXISTENT;  
	if (cause)
		l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, cause);
	else
		l3dss1_message(pc, MT_RELEASE_COMPLETE);
	p_L3L4(pc, CC_RELEASE | INDICATION, skb);
	l3pc_newstate(pc, 0);
	dss1_release_l3_process(pc);
}

static void
l3dss1_proceed_req(struct l3_process *pc, u_char pr,
		   void *arg)
{
	l3pc_newstate(pc, 9);
	l3dss1_message(pc, MT_CALL_PROCEEDING);
	p_L3L4(pc, CC_PROCEED_SEND | INDICATION, 0); 
}

/********************************************/
/* deliver a incoming display message to HL */
/********************************************/
static void
l3dss1_deliver_display(struct l3_process *pc, int pr, u_char *infp)
{
#ifdef CONFIG_HISAX_LLI
	u_char len;
        isdn_ctrl ic; 
	struct IsdnCardState *cs;
        char *p; 

        if (*infp++ != IE_DISPLAY) return;
        if ((len = *infp++) > 80) return; /* total length <= 82 */
	if (!pc->l4pc) return;

	p = ic.parm.display; 
        while (len--)
	  *p++ = *infp++;
	*p = '\0';
	ic.command = ISDN_STAT_DISPLAY;
	cs = pc->st->l1.hardware;
	ic.driver = cs->c_if->myid;
	ic.arg = ((struct Channel *) pc->l4pc->priv)->chan; // FIXME
	cs->c_if->iif.statcallb(&ic);
#endif
} /* l3dss1_deliver_display */


static void
l3dss1_progress(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int err = 0;
	u_char cause = NO_CAUSE;
	u_char *p;

	if ((p = findie(skb->data, skb->len, IE_PROGRESS, 0))) {
		if (p[1] != 2) {
			err = 1;
			cause = CAU_INVALID_IE_CONTENTS;
		} else if (p[2] & 0x60) {
			switch (p[2]) {
				case 0x80:
				case 0x81:
				case 0x82:
				case 0x84:
				case 0x85:
				case 0x87:
				case 0x8a:
					switch (p[3]) {
						case 0x81:
						case 0x82:
						case 0x83:
						case 0x84:
						case 0x88:
							break;
						default:
							err = 2;
							cause = CAU_INVALID_IE_CONTENTS;
							break;
					}
					break;
				default:
					err = 3;
					cause = CAU_INVALID_IE_CONTENTS;
					break;
			}
		}
	} else {
		cause = CAU_MANDATORY_IE_MISSING;
		err = 4;
	}
	if (err) {	
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "progress error %d", err);
		l3dss1_status_send(pc, pr, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_PROGRESS);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		p_L3L4(pc, CC_PROGRESS | INDICATION, skb);
}

static void
l3dss1_notify(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char cause = NO_CAUSE;
	int err = 0;
	u_char *p;

	if ((p = findie(skb->data, skb->len, IE_NOTIFY, 0))) {
		if (p[1] != 1) {
			err = 1;
			cause = CAU_INVALID_IE_CONTENTS;
		} else {
			switch (p[2]) {
				case 0x80:
				case 0x81:
				case 0x82:
					break;
				default:
					cause = CAU_INVALID_IE_CONTENTS;
					err = 2;
					break;
			}
		}
	} else {
		cause = CAU_MANDATORY_IE_MISSING;
		err = 3;
	}
	if (err) {	
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "notify error %d", err);
		l3dss1_status_send(pc, pr, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_NOTIFY);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		p_L3L4(pc, CC_NOTIFY | INDICATION, skb);
}

static void
l3dss1_status_enq(struct l3_process *pc, u_char pr, void *arg)
{
	int ret;
	struct sk_buff *skb = arg;

	ret = check_infoelements(pc, skb, ie_STATUS_ENQUIRY);
	l3dss1_std_ie_err(pc, ret);
        l3dss1_status_send(pc, pr, CAU_RESPONSE_TO_STATUS_ENQUIRY);
}

static void
l3dss1_information(struct l3_process *pc, u_char pr, void *arg)
{
	int ret;
	struct sk_buff *skb = arg;

	ret = check_infoelements(pc, skb, ie_INFORMATION);
	l3dss1_std_ie_err(pc, ret);
}

/******************************/
/* handle deflection requests */
/******************************/
static void l3dss1_redir_req(struct l3_process *pc, u_char pr, void *arg)
{
	setup_parm *setup = (setup_parm *) arg;
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
        u_char *subp;
        u_char len_phone = 0;
        u_char len_sub = 0;
	int l; 

	
        strcpy(pc->prot.dss1.uus1_data,setup->eazmsn); /* copy uus element if available */
        if (!setup->phone[0])
          { pc->para.cause = -1;
            l3dss1_disconnect_req(pc,pr,arg); /* disconnect immediately */
            return;
          } /* only uus */
 
        if (pc->prot.dss1.invoke_id) 
          free_invoke_id(pc->st,pc->prot.dss1.invoke_id);
 
        if (!(pc->prot.dss1.invoke_id = new_invoke_id(pc->st))) 
          return;

        MsgHead(p, pc->callref, MT_FACILITY);

        for (subp = setup->phone; (*subp) && (*subp != '.'); subp++) len_phone++; /* len of phone number */
        if (*subp++ == '.') len_sub = strlen(subp) + 2; /* length including info subadress element */ 

	*p++ = 0x1c;   /* Facility info element */
        *p++ = len_phone + len_sub + 2 + 2 + 8 + 3 + 3; /* length of element */
        *p++ = 0x91;  /* remote operations protocol */
        *p++ = 0xa1;  /* invoke component */
	  
        *p++ = len_phone + len_sub + 2 + 2 + 8 + 3; /* length of data */
        *p++ = 0x02;  /* invoke id tag, integer */
	*p++ = 0x01;  /* length */
        *p++ = pc->prot.dss1.invoke_id;  /* invoke id */ 
        *p++ = 0x02;  /* operation value tag, integer */
	*p++ = 0x01;  /* length */
        *p++ = 0x0D;  /* Call Deflect */
	  
        *p++ = 0x30;  /* sequence phone number */
        *p++ = len_phone + 2 + 2 + 3 + len_sub; /* length */
	  
        *p++ = 0x30;  /* Deflected to UserNumber */
        *p++ = len_phone+2+len_sub; /* length */
        *p++ = 0x80; /* NumberDigits */
	*p++ = len_phone; /* length */
        for (l = 0; l < len_phone; l++)
	 *p++ = setup->phone[l];

        if (len_sub)
	  { *p++ = 0x04; /* called party subadress */
            *p++ = len_sub - 2;
            while (*subp) *p++ = *subp++;
          }

        *p++ = 0x01; /* screening identifier */
        *p++ = 0x01;
        *p++ = setup->screen;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l))) return;
	memcpy(skb_put(skb, l), tmp, l);

        l3_msg(pc->st, DL_DATA | REQUEST, skb);
} /* l3dss1_redir_req */

/********************************************/
/* handle deflection request in early state */
/********************************************/
static void l3dss1_redir_req_early(struct l3_process *pc, u_char pr, void *arg)
{
  l3dss1_proceed_req(pc,pr,arg);
  l3dss1_redir_req(pc,pr,arg);
} /* l3dss1_redir_req_early */

/***********************************************/
/* handle special commands for this protocol.  */
/* Examples are call independant services like */
/* remote operations with dummy  callref.      */
/***********************************************/
static int l3dss1_cmd_global(struct PStack *st, isdn_ctrl *ic)
{ u_char id;
  u_char temp[265];
  u_char *p = temp;
  int i, l, proc_len; 
  struct sk_buff *skb;
  struct l3_process *pc = NULL;

  switch (ic->arg)
   { case DSS1_CMD_INVOKE:
       if (ic->parm.dss1_io.datalen < 0) return(-2); /* invalid parameter */ 

       for (proc_len = 1, i = ic->parm.dss1_io.proc >> 8; i; i++) 
         i = i >> 8; /* add one byte */    
       l = ic->parm.dss1_io.datalen + proc_len + 8; /* length excluding ie header */
       if (l > 255) 
         return(-2); /* too long */

       if (!(id = new_invoke_id(st))) 
         return(0); /* first get a invoke id -> return if no available */
       
       i = -1; 
       MsgHead(p, i, MT_FACILITY); /* build message head */
       *p++ = 0x1C; /* Facility IE */
       *p++ = l; /* length of ie */
       *p++ = 0x91; /* remote operations */
       *p++ = 0xA1; /* invoke */
       *p++ = l - 3; /* length of invoke */
       *p++ = 0x02; /* invoke id tag */
       *p++ = 0x01; /* length is 1 */
       *p++ = id; /* invoke id */
       *p++ = 0x02; /* operation */
       *p++ = proc_len; /* length of operation */
       
       for (i = proc_len; i; i--)
         *p++ = (ic->parm.dss1_io.proc >> (i-1)) & 0xFF;
       memcpy(p, ic->parm.dss1_io.data, ic->parm.dss1_io.datalen); /* copy data */
       l = (p - temp) + ic->parm.dss1_io.datalen; /* total length */         

       if (ic->parm.dss1_io.timeout > 0)
        if (!(pc = dss1_new_l3_process(st, -1)))
          { free_invoke_id(st, id);
            return(-2);
          } 
       pc->prot.dss1.ll_id = ic->parm.dss1_io.ll_id; /* remember id */ 
       pc->prot.dss1.proc = ic->parm.dss1_io.proc; /* and procedure */

       if (!(skb = l3_alloc_skb(l))) 
         { free_invoke_id(st, id);
           if (pc) dss1_release_l3_process(pc);
           return(-2);
         }
       memcpy(skb_put(skb, l), temp, l);
       
       if (pc)
        { pc->prot.dss1.invoke_id = id; /* remember id */
          l3pc_addtimer(pc, ic->parm.dss1_io.timeout, CC_TDSS1_IO | REQUEST);
        }
       
       l3_msg(st, DL_DATA | REQUEST, skb);
       ic->parm.dss1_io.hl_id = id; /* return id */
       return(0);

     case DSS1_CMD_INVOKE_ABORT:
       if ((pc = l3dss1_search_dummy_proc(st, ic->parm.dss1_io.hl_id)))
	{ l3pc_deltimer(pc); /* remove timer */
          dss1_release_l3_process(pc);
          return(0); 
        } 
       else
	{ l3_debug(st, "l3dss1_cmd_global abort unknown id");
          return(-2);
        } 
       break;
    
     default: 
       l3_debug(st, "l3dss1_cmd_global unknown cmd 0x%lx", ic->arg);
       return(-1);  
   } /* switch ic-> arg */
  return(-1);
} /* l3dss1_cmd_global */

static void 
l3dss1_io_timer(struct l3_process *pc)
{ 
#ifdef CONFIG_HISAX_LLI
  isdn_ctrl ic;
  struct IsdnCardState *cs = pc->st->l1.hardware;

  l3pc_deltimer(pc); /* remove timer */

  ic.driver = cs->c_if->myid;
  ic.command = ISDN_STAT_PROT;
  ic.arg = DSS1_STAT_INVOKE_ERR;
  ic.parm.dss1_io.hl_id = pc->prot.dss1.invoke_id;
  ic.parm.dss1_io.ll_id = pc->prot.dss1.ll_id;
  ic.parm.dss1_io.proc = pc->prot.dss1.proc;
  ic.parm.dss1_io.timeout= -1;
  ic.parm.dss1_io.datalen = 0;
  ic.parm.dss1_io.data = NULL;
  free_invoke_id(pc->st, pc->prot.dss1.invoke_id);
  pc->prot.dss1.invoke_id = 0; /* reset id */

  cs->c_if->iif.statcallb(&ic);

  dss1_release_l3_process(pc); 
#endif
} /* l3dss1_io_timer */

static void
l3dss1_release_ind(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int callState = 0;
	p = skb->data;

	if ((p = findie(p, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1 == *p++)
			callState = *p;
	}
	if (callState == 0) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... and 16.1
		 * set down layer 3 without sending any message
		 */
		p_L3L4(pc, CC_RELEASE | INDICATION, skb);
		l3pc_newstate(pc, 0);
		dss1_release_l3_process(pc);
	} else {
		p_L3L4(pc, CC_IGNORE | INDICATION, skb);
	}
}

static void
l3dss1_dummy(struct l3_process *pc, u_char pr, void *arg)
{
}

static void
l3dss1_t303(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		l3pc_deltimer(pc);
		l3dss1_gen_setup_req(pc, pr, 0);
	} else {
		l3pc_deltimer(pc);
		l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, CAU_RECOVERY_ON_TIMER_EXPIRY);
		p_L3L4(pc, CC_NOSETUP_RSP, 0);
		dss1_release_l3_process(pc);
	}
}

static void
l3dss1_t304(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	pc->para.cause = CAU_RECOVERY_ON_TIMER_EXPIRY;
	l3dss1_disconnect_req(pc, pr, NULL);
	p_L3L4(pc, CC_SETUP_ERR, 0);

}

static void
l3dss1_t305(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(16);
	u_char cause = CAU_NORMAL_CALL_CLEARING;

	l3pc_deltimer(pc);
	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;

	MsgXHead(pc->callref, MT_RELEASE);
	MsgCause(0, cause);
	l3pc_newstate(pc, 19);
	MsgSend();
	l3pc_addtimer(pc, T308, CC_T308_1);
}

static void
l3dss1_t310(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	pc->para.cause = CAU_RECOVERY_ON_TIMER_EXPIRY;
	l3dss1_disconnect_req(pc, pr, NULL);
	p_L3L4(pc, CC_SETUP_ERR, 0);
}

static void
l3dss1_t313(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	pc->para.cause = CAU_RECOVERY_ON_TIMER_EXPIRY;
	l3dss1_disconnect_req(pc, pr, NULL);
	p_L3L4(pc, CC_CONNECT_ERR, 0);
}

static void
l3dss1_t308_1(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_newstate(pc, 19);
	l3pc_deltimer(pc);
	l3dss1_message(pc, MT_RELEASE);
	l3pc_addtimer(pc, T308, CC_T308_2);
}

static void
l3dss1_t308_2(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	p_L3L4(pc, CC_RELEASE_ERR, 0);
	dss1_release_l3_process(pc);
}

static void
l3dss1_t318(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	pc->para.cause = CAU_RECOVERY_ON_TIMER_EXPIRY;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
	p_L3L4(pc, CC_RESUME_ERR, 0);
	l3pc_newstate(pc, 19);
	l3dss1_message(pc, MT_RELEASE);
	l3pc_addtimer(pc, T308, CC_T308_1);
}

static void
l3dss1_t319(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	pc->para.cause = CAU_RECOVERY_ON_TIMER_EXPIRY;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
	p_L3L4(pc, CC_SUSPEND_ERR, 0);
	l3pc_newstate(pc, 10);
}

static void
l3dss1_restart(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
	p_L3L4(pc, CC_RELEASE | INDICATION, 0);
	dss1_release_l3_process(pc);
}

static void
l3dss1_status(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int ret; 
	u_char cause = 0, callState = 0;
	
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "STATUS get_cause ret(%d)",ret);
		if (ret < 0)
			cause = CAU_MANDATORY_IE_MISSING;
		else if (ret > 0)
			cause = CAU_INVALID_IE_CONTENTS;
	}
	if ((p = findie(skb->data, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1 == *p++) {
			callState = *p;
			if (!ie_in_set(pc, *p, l3_valid_states))
				cause = CAU_INVALID_IE_CONTENTS;
		} else
			cause = CAU_INVALID_IE_CONTENTS;
	} else
		cause = CAU_MANDATORY_IE_MISSING;
	if (!cause) { /*  no error before */
		ret = check_infoelements(pc, skb, ie_STATUS);
		if (ERR_IE_COMPREHENSION == ret)
			cause = CAU_MANDATORY_IE_MISSING;
		else if (ERR_IE_UNRECOGNIZED == ret)
			cause = CAU_IE_NON_EXISTENT;
	}
	if (cause) {
		u_char tmp;
		
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "STATUS error(%d/%d)",ret,cause);
		tmp = pc->para.cause;
		l3dss1_status_send(pc, 0, cause);
		if (cause == CAU_IE_NON_EXISTENT)
			pc->para.cause = tmp;
		else
			return;
	}
	cause = pc->para.cause;
	if (((cause & 0x7f) == CAU_PROTOCOL_ERROR) && (callState == 0)) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1...
		 * if received MT_STATUS with cause == 111 and call
		 * state == 0, then we must set down layer 3
		 */
		p_L3L4(pc, CC_RELEASE | INDICATION, 0);
		l3pc_newstate(pc, 0);
		dss1_release_l3_process(pc);
	}
}

static void
l3dss1_facility(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	
	ret = check_infoelements(pc, skb, ie_FACILITY);
	l3dss1_std_ie_err(pc, ret);
 	  {
		u_char *p;
		if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
			l3dss1_parse_facility(pc->st, pc, pc->callref, p);
	}
}

static void
l3dss1_suspend_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(32);
	int i;
	setup_parm *setup = (setup_parm *) arg;
	u_char *call_id = setup->phone;

	MsgXHead(pc->callref, MT_SUSPEND);
	MsgCallId(call_id);
	MsgSend();
	l3pc_newstate(pc, 15);
	l3pc_addtimer(pc, T319, CC_T319);
}

static void
l3dss1_x_suspend_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(32);
	struct suspend_req_parm *suspend_req = arg;

	MsgXHead(pc->callref, MT_SUSPEND);
	MsgAdd(suspend_req->call_identity);
	MsgSend();
	l3pc_newstate(pc, 15);
	l3pc_addtimer(pc, T319, CC_T319);
}

static void
l3dss1_suspend_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	l3pc_deltimer(pc);
	l3pc_newstate(pc, 0);
	pc->para.cause = NO_CAUSE;
	p_L3L4(pc, CC_SUSPEND | CONFIRM, skb);
	/* We don't handle suspend_ack for IE errors now */
	if ((ret = check_infoelements(pc, skb, ie_SUSPEND_ACKNOWLEDGE)))
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "SUSPACK check ie(%d)",ret);
	dss1_release_l3_process(pc);
}

static void
l3dss1_suspend_rej(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "SUSP_REJ get_cause ret(%d)",ret);
		if (ret < 0) 
			l3dss1_status_send(pc, pr, CAU_MANDATORY_IE_MISSING);
		else
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
		return;
	}
	ret = check_infoelements(pc, skb, ie_SUSPEND_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);
	p_L3L4(pc, CC_SUSPEND_ERR, skb);
	l3pc_newstate(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_info_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(64);
	struct info_req_parm *info_req = arg;

	l3pc_deltimer(pc); // T304
	MsgXHead(pc->callref, MT_INFORMATION);
	MsgAdd(info_req->sending_complete);
	MsgAdd(info_req->keypad_facility);
	MsgAdd(info_req->called_party_number);
	MsgSend();
	l3pc_addtimer(pc, T304, CC_T304);
}

static void
l3dss1_x_resume_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(32);
	struct resume_req_parm *resume_req = arg;

	MsgXHead(pc->callref, MT_RESUME);
	MsgAdd(resume_req->call_identity);
	MsgSend();
	l3pc_newstate(pc, 17);
	l3pc_addtimer(pc, T318, CC_T318);
}

static void
l3dss1_resume_req(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(32);
	int i;
	setup_parm *setup = (setup_parm *) arg;
	u_char *call_id = setup->phone;

	MsgXHead(pc->callref, MT_RESUME);
	MsgCallId(call_id);
	MsgSend();
	l3pc_newstate(pc, 17);
	l3pc_addtimer(pc, T318, CC_T318);
}

static void
l3dss1_resume_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3dss1_get_channel_id(pc, skb)) > 0) {
		if ((0 == id) || ((3 == id) && (0x10 == pc->para.moderate))) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->st, "resume ack with wrong chid %x", id);
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
			return;
		}
		pc->para.bchannel = id;
	} else if (1 == pc->state) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "resume ack without chid (ret %d)", id);
		l3dss1_status_send(pc, pr, CAU_MANDATORY_IE_MISSING);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);
	p_L3L4(pc, CC_RESUME | CONFIRM, skb);
	l3pc_newstate(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_resume_rej(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->st, "RES_REJ get_cause ret(%d)",ret);
		if (ret < 0) 
			l3dss1_status_send(pc, pr, CAU_MANDATORY_IE_MISSING);
		else
			l3dss1_status_send(pc, pr, CAU_INVALID_IE_CONTENTS);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	l3pc_deltimer(pc);
	p_L3L4(pc, CC_RESUME_ERR, skb);
	l3pc_newstate(pc, 0);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	dss1_release_l3_process(pc);
}

static void
l3dss1_global_restart(struct l3_process *pc, u_char pr, void *arg)
{
	MsgDeclare(32);
	u_char *p;
	u_char ri, ch = 0, chan = 0;
	struct sk_buff *skb = arg;
	struct l3_process *up;

	l3pc_newstate(pc, 2);
	l3pc_deltimer(pc);
	p = skb->data;
	if ((p = findie(p, skb->len, IE_RESTART_IND, 0))) {
		ri = p[2];
		l3_debug(pc->st, "Restart %x", ri);
	} else {
		l3_debug(pc->st, "Restart without restart IE");
		ri = 0x86;
	}
	p = skb->data;
	if ((p = findie(p, skb->len, IE_CHANNEL_ID, 0))) {
		chan = p[2] & 3;
		ch = p[2];
		if (pc->st->l3.debug)
			l3_debug(pc->st, "Restart for channel %d", chan);
	}
	l3pc_newstate(pc, 2);
	up = pc->st->l3.proc;
	while (up) {
		if ((ri & 7) == 7)
			up->l4l3(up, CC_RESTART | REQUEST, 0);
		else if (up->para.bchannel == chan)
			up->l4l3(up, CC_RESTART | REQUEST, 0);
		up = up->next;
	}
	MsgXHead(pc->callref, MT_RESTART_ACKNOWLEDGE);
	if (chan) {
		MsgChannelId(ch);
	}
	MsgRestartInd(ri);
	l3pc_newstate(pc, 0);
	MsgSend();
}

static void
l3dss1_dl_reset(struct l3_process *pc, u_char pr, void *arg)
{
        pc->para.cause = CAU_TEMPORARY_FAILURE;
        pc->para.loc = 0;
        l3dss1_disconnect_req(pc, pr, NULL);
        p_L3L4(pc, CC_SETUP_ERR, 0);
}

static void
l3dss1_dl_release(struct l3_process *pc, u_char pr, void *arg)
{
        l3pc_newstate(pc, 0);
        pc->para.cause = CAU_DESTINATION_OUT_OF_ORDER;
        pc->para.loc = 0;
        p_L3L4(pc, CC_RELEASE | INDICATION, 0);
        dss1_release_l3_process(pc);
}

static void
l3dss1_dl_reestablish(struct l3_process *pc, u_char pr, void *arg)
{
        l3pc_deltimer(pc);
        l3pc_addtimer(pc, T309, CC_T309);
        l3_msg(pc->st, DL_ESTABLISH | REQUEST, NULL);
}
 
static void
l3dss1_dl_reest_status(struct l3_process *pc, u_char pr, void *arg)
{
	l3pc_deltimer(pc);
 	l3dss1_status_send(pc, 0, CAU_NORMAL_UNSPECIFIED);
}

/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_SETUP | REQUEST, l3dss1_setup_req},
	{SBIT(0),
	 CC_X_SETUP | REQUEST, l3dss1_x_setup_req},
	{SBIT(0),
	 CC_RESUME | REQUEST, l3dss1_resume_req},
	{SBIT(0),
	 CC_X_RESUME | REQUEST, l3dss1_x_resume_req},
	{SBIT(2),
	 CC_INFO | REQUEST, l3dss1_info_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10),
	 CC_DISCONNECT | REQUEST, l3dss1_disconnect_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10),
	 CC_X_DISCONNECT | REQUEST, l3dss1_x_disconnect_req},
	{SBIT(12),
	 CC_RELEASE | REQUEST, l3dss1_release_req},
	{ALL_STATES,
	 CC_RESTART | REQUEST, l3dss1_restart},
	{SBIT(6),
	 CC_IGNORE | REQUEST, l3dss1_reset},
	{SBIT(6),
	 CC_REJECT | REQUEST, l3dss1_reject_req},
	{SBIT(6),
	 CC_PROCEED_SEND | REQUEST, l3dss1_proceed_req},
	{SBIT(6) | SBIT(9),
	 CC_ALERTING | REQUEST, l3dss1_alerting_req},
	{SBIT(6) | SBIT(7) | SBIT(9),
	 CC_SETUP | RESPONSE, l3dss1_setup_rsp},
	{SBIT(10),
	 CC_SUSPEND | REQUEST, l3dss1_suspend_req},
	{SBIT(10),
	 CC_X_SUSPEND | REQUEST, l3dss1_x_suspend_req},
        {SBIT(6),
         CC_PROCEED_SEND | REQUEST, l3dss1_proceed_req},
        {SBIT(7) | SBIT(9),
         CC_REDIR | REQUEST, l3dss1_redir_req},
        {SBIT(6),
         CC_REDIR | REQUEST, l3dss1_redir_req_early},
        {SBIT(9) | SBIT(25),
         CC_DISCONNECT | REQUEST, l3dss1_disconnect_req},
	{SBIT(1),
	 CC_T303, l3dss1_t303},
	{SBIT(2),
	 CC_T304, l3dss1_t304},
	{SBIT(3),
	 CC_T310, l3dss1_t310},
	{SBIT(8),
	 CC_T313, l3dss1_t313},
	{SBIT(11),
	 CC_T305, l3dss1_t305},
	{SBIT(15),
	 CC_T319, l3dss1_t319},
	{SBIT(17),
	 CC_T318, l3dss1_t318},
	{SBIT(19),
	 CC_T308_1, l3dss1_t308_1},
	{SBIT(19),
	 CC_T308_2, l3dss1_t308_2},
	{SBIT(10),
	 CC_T309, l3dss1_dl_release},
};

#define DOWNSLLEN \
	(sizeof(downstatelist) / sizeof(struct stateentry))

static struct stateentry datastatelist[] =
{
	{ALL_STATES,
	 MT_STATUS_ENQUIRY, l3dss1_status_enq},
	{ALL_STATES,
	 MT_FACILITY, l3dss1_facility},
	{SBIT(19),
	 MT_STATUS, l3dss1_release_ind},
	{ALL_STATES,
	 MT_STATUS, l3dss1_status},
	{SBIT(0),
	 MT_SETUP, l3dss1_setup},
	{SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(12) |
	 SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_SETUP, l3dss1_dummy},
	{SBIT(1) | SBIT(2),
	 MT_CALL_PROCEEDING, l3dss1_call_proc},
	{SBIT(1),
	 MT_SETUP_ACKNOWLEDGE, l3dss1_setup_ack},
	{SBIT(2) | SBIT(3),
	 MT_ALERTING, l3dss1_alerting},
	{SBIT(2) | SBIT(3),
	 MT_PROGRESS, l3dss1_progress},
	{SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_INFORMATION, l3dss1_information},
	{SBIT(10) | SBIT(11) | SBIT(15),
	 MT_NOTIFY, l3dss1_notify},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_RELEASE_COMPLETE, l3dss1_release_cmpl},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(25),
	 MT_RELEASE, l3dss1_release},
	{SBIT(19),  MT_RELEASE, l3dss1_release_ind},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(15) | SBIT(17) | SBIT(25),
	 MT_DISCONNECT, l3dss1_disconnect},
	{SBIT(19),
	 MT_DISCONNECT, l3dss1_dummy},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4),
	 MT_CONNECT, l3dss1_connect},
	{SBIT(8),
	 MT_CONNECT_ACKNOWLEDGE, l3dss1_connect_ack},
	{SBIT(15),
	 MT_SUSPEND_ACKNOWLEDGE, l3dss1_suspend_ack},
	{SBIT(15),
	 MT_SUSPEND_REJECT, l3dss1_suspend_rej},
	{SBIT(17),
	 MT_RESUME_ACKNOWLEDGE, l3dss1_resume_ack},
	{SBIT(17),
	 MT_RESUME_REJECT, l3dss1_resume_rej},
};

#define DATASLLEN \
	(sizeof(datastatelist) / sizeof(struct stateentry))

static struct stateentry globalmes_list[] =
{
	{ALL_STATES,
	 MT_STATUS, l3dss1_status},
	{SBIT(0),
	 MT_RESTART, l3dss1_global_restart},
/*	{SBIT(1),
	 MT_RESTART_ACKNOWLEDGE, l3dss1_restart_ack},
*/
};
#define GLOBALM_LEN \
	(sizeof(globalmes_list) / sizeof(struct stateentry))

static struct stateentry manstatelist[] =
{
        {SBIT(2),
         DL_ESTABLISH | INDICATION, l3dss1_dl_reset},
        {SBIT(10),
         DL_ESTABLISH | CONFIRM, l3dss1_dl_reest_status},
        {SBIT(10),
         DL_RELEASE | INDICATION, l3dss1_dl_reestablish},
        {ALL_STATES,
         DL_RELEASE | INDICATION, l3dss1_dl_release},
};

#define MANSLLEN \
        (sizeof(manstatelist) / sizeof(struct stateentry))
/* *INDENT-ON* */


static void
global_handler(struct PStack *st, int mt, struct sk_buff *skb)
{
	int i;
	struct l3_process *pc = st->l3.global;

	pc->callref = skb->data[2]; /* cr flag */
	for (i = 0; i < GLOBALM_LEN; i++)
		if ((mt == globalmes_list[i].primitive) &&
		    ((1 << pc->state) & globalmes_list[i].state))
			break;
	if (i == GLOBALM_LEN) {
		MsgDeclare(16);

		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "dss1 global state %d mt %x unhandled",
				pc->state, mt);
		}
		MsgXHead(pc->callref, MT_STATUS);
		MsgCause(0, CAU_INVALID_CALL_REFERENCE);
		MsgCallState(pc->state);
		MsgSend();
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "dss1 global %d mt %x",
				 pc->state, mt);
		}
		globalmes_list[i].rout(pc, mt, skb);
	}
}

static void
dss1up(struct PStack *st, int pr, void *arg)
{
	int i, mt, cr, cause, callState;
	char *ptr;
	u_char *p;
	struct sk_buff *skb = arg;
	struct l3_process *proc;

	switch (pr) {
		case (DL_DATA | INDICATION):
		case (DL_UNIT_DATA | INDICATION):
			break;
		case (DL_ESTABLISH | CONFIRM):
		case (DL_ESTABLISH | INDICATION):
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			l3_msg(st, pr, arg);
			return;
			break;
		case (DL_DATA | CONFIRM):
			return; // ignore
                default:
                        printk(KERN_ERR "HiSax dss1up unknown pr=%04x\n", pr);
                        return;
	}
	if (skb->len < 3) {
		l3_debug(st, "dss1up frame too short(%d)", skb->len);
		idev_kfree_skb(skb, FREE_READ);
		return;
	}

	if (skb->data[0] != PROTO_DIS_EURO) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			l3_debug(st, "dss1up%sunexpected discriminator %x message len %d",
				 (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 skb->data[0], skb->len);
		}
		idev_kfree_skb(skb, FREE_READ);
		return;
	}
	cr = getcallref(skb->data);
	if (skb->len < ((skb->data[1] & 0x0f) + 3)) {
		l3_debug(st, "dss1up frame too short(%d)", skb->len);
		idev_kfree_skb(skb, FREE_READ);
		return;
	}
	mt = skb->data[skb->data[1] + 2];
	if (st->l3.debug & L3_DEB_STATE)
		l3_debug(st, "dss1up cr %d", cr);
	if (cr == -2) {  /* wrong Callref */
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "dss1up wrong Callref");
		idev_kfree_skb(skb, FREE_READ);
		return;
	} else if (cr == -1) {	/* Dummy Callref */
		st->l4->l3l4(st, CC_DUMMY | INDICATION, skb);
		if (mt == MT_FACILITY)
			if ((p = findie(skb->data, skb->len, IE_FACILITY, 0))) {
				l3dss1_parse_facility(st, NULL, 
					(pr == (DL_DATA | INDICATION)) ? -1 : -2, p); 
				idev_kfree_skb(skb, FREE_READ);
				return;  
			}
		if (st->l3.debug & L3_DEB_WARN)
			l3_debug(st, "dss1up dummy Callref (no facility msg or ie)");
		idev_kfree_skb(skb, FREE_READ);
		return;
	} else if ((((skb->data[1] & 0x0f) == 1) && (0==(cr & 0x7f))) ||
		(((skb->data[1] & 0x0f) == 2) && (0==(cr & 0x7fff)))) {	/* Global CallRef */
		if (st->l3.debug & L3_DEB_STATE)
			l3_debug(st, "dss1up Global CallRef");
		global_handler(st, mt, skb);
		idev_kfree_skb(skb, FREE_READ);
		return;
	} else if (!(proc = getl3proc(st, cr))) {
		/* No transaction process exist, that means no call with
		 * this callreference is active
		 */
		if (mt == MT_SETUP) {
			/* Setup creates a new transaction process */
			if (skb->data[2] & 0x80) {
				/* Setup with wrong CREF flag */
				if (st->l3.debug & L3_DEB_STATE)
					l3_debug(st, "dss1up wrong CRef flag");
				idev_kfree_skb(skb, FREE_READ);
				return;
			}
			if (!(proc = dss1_new_l3_process(st, cr))) {
				/* May be to answer with RELEASE_COMPLETE and
				 * CAUSE 0x2f "Resource unavailable", but this
				 * need a new_l3_process too ... arghh
				 */
				idev_kfree_skb(skb, FREE_READ);
				return;
			}
		} else if (mt == MT_STATUS) {
			cause = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CAUSE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
				cause = *ptr & 0x7f;
			}
			callState = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CALL_STATE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
				callState = *ptr;
			}
			/* ETS 300-104 part 2.4.1
			 * if setup has not been made and a message type
			 * MT_STATUS is received with call state == 0,
			 * we must send nothing
			 */
			if (callState != 0) {
				/* ETS 300-104 part 2.4.2
				 * if setup has not been made and a message type
				 * MT_STATUS is received with call state != 0,
				 * we must send MT_RELEASE_COMPLETE cause 101
				 */
				if ((proc = dss1_new_l3_process(st, cr))) {
					proc->para.cause = CAU_MSG_NOT_COMPATIBLE_WITH_CALL_STATE;
					l3dss1_msg_without_setup(proc, 0, NULL);
				}
			}
			idev_kfree_skb(skb, FREE_READ);
			return;
		} else if (mt == MT_RELEASE_COMPLETE) {
			idev_kfree_skb(skb, FREE_READ);
			return;
		} else {
			/* ETS 300-104 part 2
			 * if setup has not been made and a message type
			 * (except MT_SETUP and RELEASE_COMPLETE) is received,
			 * we must send MT_RELEASE_COMPLETE cause 81 */
			idev_kfree_skb(skb, FREE_READ);
			if ((proc = dss1_new_l3_process(st, cr))) {
				proc->para.cause = CAU_INVALID_CALL_REFERENCE;
				l3dss1_msg_without_setup(proc, 0, NULL);
			}
			return;
		}
	}
	if (l3dss1_check_messagetype_validity(proc, mt, skb)) {
		idev_kfree_skb(skb, FREE_READ);
		return;
	}
	if ((p = findie(skb->data, skb->len, IE_DISPLAY, 0)) != NULL) 
	  l3dss1_deliver_display(proc, pr, p); /* Display IE included */
	for (i = 0; i < DATASLLEN; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == DATASLLEN) {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "dss1up%sstate %d mt %#x unhandled",
				(pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, mt);
		}
		if ((MT_RELEASE_COMPLETE != mt) && (MT_RELEASE != mt)) {
			l3dss1_status_send(proc, pr, CAU_MSG_NOT_COMPATIBLE_WITH_CALL_STATE);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			l3_debug(st, "dss1up%sstate %d mt %x",
				(pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, mt);
		}
		datastatelist[i].rout(proc, pr, skb);
	}
	idev_kfree_skb(skb, FREE_READ);
	return;
}

static void
dss1down_proc(struct l3_process *pc, int pr, void *arg)
{
	int i;

	if (pr == (CC_TDSS1_IO | REQUEST)) {
		l3dss1_io_timer(pc);
		return;
	}  
	for (i = 0; i < DOWNSLLEN; i++)
		if ((pr == downstatelist[i].primitive) &&
		    ((1 << pc->state) & downstatelist[i].state))
			break;
	if (i == DOWNSLLEN) {
		if (pc->debug & L3_DEB_STATE) {
			l3_debug(pc->st, "dss1down state %d prim %#x unhandled",
				 pc->state, pr);
		}
	} else {
		if (pc->debug & L3_DEB_STATE) {
			l3_debug(pc->st, "dss1down state %d prim %#x",
				 pc->state, pr);
		}
		downstatelist[i].rout(pc, pr, arg);
	}
}

static void
dss1down(struct PStack *st, int pr, void *arg)
{
	int cr;
	struct l3_process *proc;
	struct l4_process *l4pc;

	switch (pr) {
	case DL_ESTABLISH | REQUEST:
		l3_msg(st, pr, NULL);
		break;
	case CC_NEW_CR | REQUEST:
		l4pc = arg;
		cr = newcallref();
		cr |= 0x80;
		if ((proc = dss1_new_l3_process(st, cr))) {
			proc->l4pc = l4pc;
			l4pc->l3pc = proc;
		}
		break;
	case CC_DUMMY | REQUEST:
		l3dss1_dummy_req(st, pr, arg);
		break;
	default:
		int_error();
	}
}

static void
dss1man(struct PStack *st, int pr, void *arg)
{
        int i;
        struct l3_process *proc = arg;
 
        if (!proc) {
                printk(KERN_ERR "HiSax dss1man without proc pr=%04x\n", pr);
                return;
        }
        for (i = 0; i < MANSLLEN; i++)
                if ((pr == manstatelist[i].primitive) &&
                    ((1 << proc->state) & manstatelist[i].state))
                        break;
        if (i == MANSLLEN) {
                if (st->l3.debug & L3_DEB_STATE) {
                        l3_debug(st, "cr %d dss1man state %d prim %#x unhandled",
                                proc->callref & 0x7f, proc->state, pr);
                }
        } else {
                if (st->l3.debug & L3_DEB_STATE) {
                        l3_debug(st, "cr %d dss1man state %d prim %#x",
                                proc->callref & 0x7f, proc->state, pr);
                }
                manstatelist[i].rout(proc, pr, arg);
        }
}
 
void
setstack_dss1(struct PStack *st)
{
	char tmp[64];
	int i;

	st->l3.debug = L3_DEB_WARN;
	st->l3.l4l3 = dss1down;
	st->l3.l4l3_proto = l3dss1_cmd_global;
	st->l3.l2l3 = dss1up;
	st->l3.l3ml3 = dss1man;
	st->l3.N303 = 1;
	st->prot.dss1.last_invoke_id = 0;
	st->prot.dss1.invoke_used[0] = 1; /* Bit 0 must always be set to 1 */
	i = 1;
	while (i < 32) 
		st->prot.dss1.invoke_used[i++] = 0;   

	if (!(st->l3.global = kmalloc(sizeof(struct l3_process), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for dss1 global CR\n");
	} else {
		st->l3.global->state = 0;
		st->l3.global->callref = 0;
		st->l3.global->next = NULL;
		st->l3.global->debug = L3_DEB_WARN;
		st->l3.global->st = st;
		st->l3.global->N303 = 1;
		st->l3.global->prot.dss1.invoke_id = 0; 

		l3pc_inittimer(st->l3.global);
	}
	strcpy(tmp, dss1_revision);
	printk(KERN_INFO "HiSax: DSS1 Rev. %s\n", HiSax_getrev(tmp));
}
