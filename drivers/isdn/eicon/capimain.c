/* $Id$
 *
 * ISDN interface module for Eicon active cards DIVA.
 * CAPI Interface
 * 
 * Copyright 2000 by Armin Schindler (mac@melware.de) 
 * Copyright 2000 Cytronics & Melware (info@melware.de)
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
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>

#include <linux/isdn.h>
#include <linux/isdnif.h>

#include <linux/isdn_compat.h>

#include "platform.h"
#include "di_defs.h"
#include "capi20.h"
#include "divacapi.h"
#include "divasync.h"
#include "cp_vers.h"

#include <net/capi/capi.h>
#include <net/capi/driver.h>

EXPORT_NO_SYMBOLS;

#define MAX_DESCRIPTORS  32

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

static char *main_revision = "$Revision$";
static char *DRIVERNAME = "Eicon DIVA - CAPI Interface driver (http://www.melware.net)";
static char *DRIVERLNAME = "divacapi";
#define DRRELMAJOR  1
#define DRRELMINOR  0
#define DRRELEXTRA  "beta8"
static char DRIVERRELEASE[16];

#define M_COMPANY "Eicon Networks"

typedef struct _diva_card {
        int Id;
        struct _diva_card *next;
        struct capi_ctr *capi_ctrl;
        DIVA_CAPI_ADAPTER *adapter;
        DESCRIPTOR d;
        char name[32];
} diva_card;

extern void AutomaticLaw(DIVA_CAPI_ADAPTER *);
extern void callback (ENTITY *);
extern word api_remove_start(void);
extern word CapiRelease(word);
extern word CapiRegister(word);
extern word api_put (APPL *, CAPI_MSG *);

byte UnMapController(byte);

void diva_reset_ctr(struct capi_ctr *);
void diva_remove_ctr(struct capi_ctr *);
void diva_register_appl(struct capi_ctr *, __u16 , capi_register_params *); 
void diva_release_appl(struct capi_ctr *, __u16); 
void diva_send_message(struct capi_ctr *, struct sk_buff *);
static char *diva_procinfo(struct capi_ctr *);
int diva_ctl_read_proc(char *, char **, off_t ,int , int *, struct capi_ctr *);
int diva_driver_read_proc(char *, char **, off_t ,int , int *, struct capi_driver *);

void no_printf (unsigned char *, ...);

DIVA_DI_PRINTF dprintf = no_printf;

DIVA_CAPI_ADAPTER *adapter = (DIVA_CAPI_ADAPTER *) NULL;
APPL *application = (APPL *) NULL;
byte max_appl = MAX_APPL;
byte ControllerMap[MAX_DESCRIPTORS + 1];
CAPI_MSG *mapped_msg = (CAPI_MSG *) NULL;
static byte max_adapter = 0;

static void clean_adapter(int);
static void DIRequest (ENTITY* e);

MODULE_DESCRIPTION(             "CAPI driver for Eicon DIVA cards");
MODULE_AUTHOR(                  "Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE(        "CAPI and DIVA card drivers");


extern void DIVA_DIDD_Read(DESCRIPTOR *, int);

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;

static diva_card *cards;

static struct capi_driver_interface *di;

static struct capi_driver divas_driver = {
    "",
    "",
    diva_reset_ctr,		/* reset_ctr */
    diva_remove_ctr,		/* remove_ctr */
    diva_register_appl,		/* register_appl */
    diva_release_appl,		/* release_appl */
    diva_send_message,		/* send_message */
    diva_procinfo,		/* procinfo */
    diva_ctl_read_proc,		/* read_proc */
    diva_driver_read_proc,	/* driver_read_proc */
    0,				/* conf_driver */
    0,				/* conf_controller */
};

static spinlock_t api_lock;
static spinlock_t ll_lock;

#include "debuglib.c"

void xlog (char * x, ...)
{
  va_list ap;

  if ( myDriverDebugHandle.dbgMask & DL_XLOG )
  {
    va_start (ap, x) ;

    if ( myDriverDebugHandle.dbg_irq )
    {
      myDriverDebugHandle.dbg_irq (myDriverDebugHandle.id, DLI_XLOG, x, ap);
    }
    else if ( myDriverDebugHandle.dbg_old )
    {
      myDriverDebugHandle.dbg_old (myDriverDebugHandle.id, x, ap) ;
    }

    va_end (ap) ;
  }
}

static char *
getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else rev = "1.0";
	return rev;

}

static int
find_free_id(void)
{
  int num = 0;
  diva_card *p;

  spin_lock(&ll_lock);
  while(num < 100) {
    num++;
    p = cards;
    while (p) {
      if (p->Id == num)
        goto next_id;
      p = p->next;
    }
    spin_unlock(&ll_lock);
    return(num);
next_id:
  }
  spin_unlock(&ll_lock);
  return(999);
}

static inline diva_card *
find_card_by_ctrl(word controller)
{
  diva_card *p;

  spin_lock(&ll_lock);
  p = cards;

  while (p) {
    if (p->Id == controller) {
      spin_unlock(&ll_lock);
      return p;
    }
    p = p->next;
  }
  spin_unlock(&ll_lock);
  return (diva_card *) 0;
}



/************* interface functions *************/

void no_printf (unsigned char * x ,...)
{
  /* dummy debug function */
}

/*
** main function called by message.c
*/
void sendf(APPL *appl, word command, dword Id, word Number, byte *format, ...)
{
  word i, j;
  word length = 12, dlength = 0;
  byte *write;
  CAPI_MSG msg;
  byte *string = 0;
  va_list ap;
  struct sk_buff *skb;
  diva_card *card = NULL;
  dword tmp;

  if(!appl) return;

  DBG_PRV1(("sendf(a=%d,cmd=%x,format=%s)",
             appl->Id,command,(byte *)format))

  msg.header.appl_id = appl->Id;
  msg.header.command = command;
  if((byte)(command>>8) == 0x82)
    Number = appl->Number++;
  msg.header.number = Number;

  WRITE_DWORD(((byte *)&msg.header.controller), Id);
  write = (byte *) &msg;
  write += 12;

  va_start(ap,format);
  for(i=0; format[i]; i++) {
    switch(format[i]) {
      case 'b':
        tmp = va_arg(ap,dword);
        *(byte *)write = (byte)(tmp & 0xff);
        write += 1;
        length += 1;
        break;
      case 'w':
        tmp = va_arg(ap,dword);
        WRITE_WORD (write, (tmp & 0xffff));
        write += 2;
        length += 2;
        break;
      case 'd':
        tmp = va_arg(ap,dword);
        WRITE_DWORD(write, tmp);
        write += 4;
        length += 4;
        break;
      case 's':
      case 'S':
        string = va_arg(ap,byte *);
        length += string[0]+1;
        for(j = 0; j <= string[0]; j++)
          *write++ = string[j];
        break;
    }
  }
  va_end(ap);

  msg.header.length = length;
  msg.header.controller = UnMapController(msg.header.controller);

  if (command == _DATA_B3_I)
    dlength = READ_WORD(((byte*)&msg.info.data_b3_ind.Data_Length));

  if (!(skb = alloc_skb(length + dlength, GFP_ATOMIC))) {
    printk(KERN_ERR "%s: alloc_skb failed, incoming msg dropped.\n", DRIVERLNAME);
    return;
  } else {
    write = (byte *) skb_put(skb, length + dlength);

    /* copy msg header to sk_buff */
    memcpy(write, (byte *)&msg, length);

    /* if DATA_B3_IND, copy data too */
    if (command == _DATA_B3_I) {
      dword data = READ_DWORD(((byte*)&msg.info.data_b3_ind.Data));
      memcpy (write + length, (void *)data, dlength);
    }

    if ( myDriverDebugHandle.dbgMask & DL_XLOG )
    {
      switch ( command )
      {
        default:
          xlog ("\x00\x02", msg, 0x81, length) ;
          break ;

        case _DATA_B3_R|CONFIRM:
          if ( myDriverDebugHandle.dbgMask & DL_BLK)
            xlog ("\x00\x02", msg, 0x81, length) ;
          break ;

        case _DATA_B3_I:
          if ( myDriverDebugHandle.dbgMask & DL_BLK)
          {
            xlog ("\x00\x02", msg, 0x81, length) ;
            for (i = 0; i < dlength; i += 256)
            {
              DBG_BLK((((char *)msg.info.data_b3_ind.Data) + i,
                ((dlength - i) < 256) ?
                 (dlength - i) : 256  ))
              if ( !(myDriverDebugHandle.dbgMask & DL_PRV0) )
                break ; // not more if not explicitely requested
            }
          }
          break ;
      }
    }

    /* find the card structure for this controller */
    if(!(card = find_card_by_ctrl(skb->data[8] & 0x7f))) {
      printk(KERN_ERR "%s: controller %d not found, incoming msg dropped.\n",
              DRIVERLNAME, skb->data[8] & 0x7f);
      DBG_ERR(("sendf - controller not found, incoming msg dropped"))
      kfree_skb(skb);
      return;
    }
    
    /* tell capi that we have a new ncci assigned */
    if ((command == (_CONNECT_B3_R | CONFIRM)) ||
        (command == _CONNECT_B3_I))
      card->capi_ctrl->new_ncci(card->capi_ctrl, appl->Id, Id, MAX_DATA_B3);

    /* tell capi that we have released the ncci */
    if (command == _DISCONNECT_B3_I)
      card->capi_ctrl->free_ncci(card->capi_ctrl, appl->Id, Id);

    /* send capi msg to capi */
    card->capi_ctrl->handle_capimsg(card->capi_ctrl, appl->Id, skb);
  }
}

void sync_callback(ENTITY *e)
{
  DBG_TRC(("cb:Id=%x,Rc=%x,Ind=%x", e->Id, e->Rc, e->Ind))

  spin_lock(&api_lock);

  callback(e);

  spin_unlock(&api_lock);
}


/*
** Buffer RX/TX 
*/
void *TransmitBufferSet(APPL *appl, dword ref)
{
  appl->xbuffer_used[ref] = TRUE;
  DBG_PRV1(("%d:xbuf_used(%d)", appl->Id, ref + 1))
  return (void *) ref;
}

void *TransmitBufferGet(APPL *appl, void *p)
{
  if(appl->xbuffer_internal[(dword)p])
    return appl->xbuffer_internal[(dword)p];

  return appl->xbuffer_ptr[(dword)p];
}

void TransmitBufferFree(APPL *appl, void *p)
{
  appl->xbuffer_used[(dword) p] = FALSE;
  DBG_PRV1(("%d:xbuf_free(%d)", appl->Id, ((dword) p) + 1))
}

void *ReceiveBufferGet(APPL *appl, int Num)
{
  return &appl->ReceiveBuffer[Num * appl->MaxDataLength];
}


/*
** api_remove_start/complete for cleanup
*/

void api_remove_complete(void)
{
    DBG_PRV1(("api_remove_complete"))
}


/*
** Controller mapping
*/

byte MapController(byte Controller)
{
  byte i;
  byte MappedController = 0;
  byte ctrl = Controller & 0x7f;  // mask external controller bit off

  for (i = 1; i < max_adapter + 1; i++)
  {
    if (ctrl == ControllerMap[i])
    {
      MappedController = (byte) i;
      break;
    }
  }
  if (i > max_adapter)
  {
    ControllerMap[0] = ctrl;
    MappedController = 0;
  }
  return (MappedController | (Controller & 0x80)); // put back external controller bit
}

byte UnMapController(byte MappedController)
{
  byte Controller;
  byte ctrl = MappedController & 0x7f;  // mask external controller bit off

  if (ctrl <= max_adapter)
  {
    Controller = ControllerMap[ctrl];
  }
  else
  {
    Controller = 0;
  }

  return (Controller | (MappedController & 0x80)); // put back external controller bit
}


/*
** we do not provide date/time here,
** the application should do this. 
*/
int fax_head_line_time (char *buffer)
{
  return (0);
}


/************* proc functions *************/

int diva_driver_read_proc(char *page, char **start, off_t off,int count, int *eof, struct capi_driver *driver)
{
  int len = 0;
  char tmprev[32];

  strcpy(tmprev, main_revision);
  len += sprintf(page+len, "%-16s divas\n", "name");
  len += sprintf(page+len, "%-16s %s/%s %s(%s)\n", "release",
                 DRIVERRELEASE, getrev(tmprev), diva_capi_common_code_build, DIVA_BUILD);
  len += sprintf(page+len, "%-16s %s\n", "author", "Cytronics & Melware / Eicon Networks");

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

int diva_ctl_read_proc(char *page, char **start, off_t off,int count, int *eof, struct capi_ctr *ctrl)
{
  diva_card *card = ctrl->driverdata;
  int len = 0;

  len += sprintf(page+len, "%s\n", ctrl->name);
  len += sprintf(page+len, "Serial No. : %s\n", ctrl->serial);
  len += sprintf(page+len, "Id         : %d\n", card->Id);
  len += sprintf(page+len, "Channels   : %d\n", card->d.channels);

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

static char *diva_procinfo(struct capi_ctr *ctrl)
{
  return(ctrl->serial);
}


/************** not needed ***************/

void diva_reset_ctr(struct capi_ctr *ctrl)
{
	/* not needed */
}

void diva_remove_ctr(struct capi_ctr *ctrl)
{
	/* not needed */
}



/************** capi functions ***********/

/*
**  register appl
*/

void diva_register_appl(struct capi_ctr *ctrl, __u16 appl, capi_register_params *rp)
{
  APPL *this;
  word bnum, xnum;
  int i = 0, j = 0;
  void * DataNCCI, * DataFlags, * ReceiveBuffer, * xbuffer_used; 
  void ** xbuffer_ptr, ** xbuffer_internal;

  MOD_INC_USE_COUNT;

  if(in_interrupt()) {
    DBG_ERR(("CAPI_REGISTER - in interrupt context !"))
    printk(KERN_ERR "%s: diva_register_appl: in interrupt context.\n", DRIVERLNAME);
    return;
  }

  DBG_TRC(("application register"))

  if(appl > MAX_APPL) {
    DBG_ERR(("CAPI_REGISTER - appl.Id exceeds MAX_APPL"))
    printk(KERN_WARNING "%s: diva_register_appl: appl.Id exceeds MAX_APPL.\n", DRIVERLNAME);
    return; 
  }

  if (rp->level3cnt < 1 ||
      rp->level3cnt > 255 ||
      rp->datablklen < 80 ||
      rp->datablklen > 2150 ||
      rp->datablkcnt > 255)
  {
    DBG_ERR(("CAPI_REGISTER - invalid parameters"))
    printk(KERN_WARNING "%s: diva_register_appl: invalid parameters.\n", DRIVERLNAME);
    return; 
  }

  if (application[appl - 1].Id == appl) {
    ctrl->appl_registered(ctrl, appl);
    return; /* appl already registered */
  }

  /* alloc memory */

  bnum = rp->level3cnt * rp->datablkcnt;
  xnum = rp->level3cnt * MAX_DATA_B3;

  //     this->queue = ExAllocatePool (NonPagedPool, this->queue_size);

  if(!(DataNCCI = kmalloc(bnum * sizeof(word), GFP_ATOMIC))) {
    printk(KERN_WARNING "%s: diva_register_appl: failed alloc DataNCCI.\n", DRIVERLNAME);
    DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
    return;
  }
  memset(DataNCCI, 0, bnum * sizeof(word));

  if(!(DataFlags = kmalloc(bnum * sizeof(word), GFP_ATOMIC))) {
    printk(KERN_WARNING "%s: diva_register_appl: failed alloc DataFlags.\n", DRIVERLNAME);
    DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
    kfree(DataNCCI);
    return;
  }
  memset(DataFlags, 0, bnum * sizeof(word));

  if(!(ReceiveBuffer = vmalloc(bnum * rp->datablklen))) {
    printk(KERN_WARNING "%s: diva_register_appl: failed alloc ReceiveBuffer.\n", DRIVERLNAME);
    DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
    kfree(DataNCCI);
    kfree(DataFlags);
    return;
  }
  memset(ReceiveBuffer, 0, bnum * rp->datablklen);

  if(!(xbuffer_used = (byte *) kmalloc(xnum, GFP_ATOMIC))) {
    printk(KERN_WARNING "%s: diva_register_appl: failed alloc xbuffer_used.\n", DRIVERLNAME);
    DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
    kfree(DataNCCI);
    kfree(DataFlags);
    vfree(ReceiveBuffer);
    return;
  }
  memset(xbuffer_used, 0, xnum);

  if(!(xbuffer_ptr = (void **) kmalloc(xnum * sizeof(void *), GFP_ATOMIC))) {
    printk(KERN_WARNING "%s: diva_register_appl: failed alloc xbuffer_ptr.\n", DRIVERLNAME);
    DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
    kfree(DataNCCI);
    kfree(DataFlags);
    vfree(ReceiveBuffer);
    kfree(xbuffer_used);
    return;
  }
  memset(xbuffer_ptr, 0, xnum * sizeof(void *));

  if(!(xbuffer_internal = (void **) kmalloc(xnum * sizeof(void *), GFP_ATOMIC))) {
    printk(KERN_WARNING "%s: diva_register_appl: failed alloc xbuffer_internal.\n", DRIVERLNAME);
    DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
    kfree(DataNCCI);
    kfree(DataFlags);
    vfree(ReceiveBuffer);
    kfree(xbuffer_used);
    kfree(xbuffer_ptr);
    return;
  }
  memset(xbuffer_internal, 0, xnum * sizeof(void *));

  for ( i = 0 ; i < xnum ; i++ )
  {
    xbuffer_ptr[i] = kmalloc(rp->datablklen, GFP_ATOMIC) ;
    if ( !xbuffer_ptr[i] )
    {
      printk(KERN_WARNING "%s: diva_register_appl: failed alloc xbuffer_ptr block.\n", DRIVERLNAME);
      DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
      if (i) {
        for(j = 0; j < i; j++)
          if(xbuffer_ptr[j])
            kfree(xbuffer_ptr[j]);
      }
      kfree(DataNCCI);
      kfree(DataFlags);
      vfree(ReceiveBuffer);
      kfree(xbuffer_used);
      kfree(xbuffer_ptr);
      kfree(xbuffer_internal);
      return;  
    }
  }

  DBG_LOG(("CAPI_REGISTER - Id = %d", appl))
  DBG_LOG(("  MaxLogicalConnections = %d", rp->level3cnt))
  DBG_LOG(("  MaxBDataBuffers       = %d", rp->datablkcnt))
  DBG_LOG(("  MaxBDataLength        = %d", rp->datablklen))

  /* initialize application data */

  spin_lock(&api_lock);

  this = &application[appl - 1] ;
  memset(this, 0, sizeof (APPL));

  this->Id = appl;

  /* We do not need a list */
  /* InitializeListHead(&this->s_function); */

  for (i = 0; i < max_adapter; i++)
  {
    adapter[i].CIP_Mask[appl - 1] = 0;
  }

  this->queue_size = 1000;

  this->MaxNCCI = (byte) rp->level3cnt;
  this->MaxNCCIData = (byte) rp->datablkcnt;
  this->MaxBuffer = bnum;
  this->MaxDataLength = rp->datablklen;

  this->DataNCCI         = DataNCCI;
  this->DataFlags        = DataFlags;
  this->ReceiveBuffer    = ReceiveBuffer;
  this->xbuffer_used     = xbuffer_used;
  this->xbuffer_ptr      = xbuffer_ptr;
  this->xbuffer_internal = xbuffer_internal;
  for ( i = 0 ; i < xnum ; i++ ) {
    this->xbuffer_ptr[i] = xbuffer_ptr[i];
  }

  CapiRegister(this->Id);
  spin_unlock(&api_lock);

  /* tell kcapi that we actually registerd the appl */
  ctrl->appl_registered(ctrl, appl);
} 


/*
**  release appl
*/

void diva_release_appl(struct capi_ctr *ctrl, __u16 appl)
{
  APPL *this = &application[appl - 1] ;
  int i = 0;

  if(in_interrupt()) {
    DBG_ERR(("CAPI_RELEASE - in interrupt context !"))
    printk(KERN_ERR "%s: diva_release_appl: in interrupt context.\n", DRIVERLNAME);
    return;
  }
  if (this->Id) {
    DBG_TRC(("application %d cleanup", this->Id))
  }

  spin_lock(&api_lock);
  if (this->Id)
  {
    CapiRelease(this->Id);
    if (this->DataNCCI) kfree(this->DataNCCI);
    if (this->DataFlags) kfree(this->DataFlags);
    if (this->ReceiveBuffer) vfree(this->ReceiveBuffer);
    if (this->xbuffer_ptr)
    {
      for (i = 0; i < (MAX_DATA_B3 * this->MaxNCCI); i++)
      {
        if (this->xbuffer_ptr[i]) kfree(this->xbuffer_ptr[i]);
      }
      kfree(this->xbuffer_ptr);
    }
    if (this->xbuffer_internal) kfree(this->xbuffer_internal);
    if (this->xbuffer_used) kfree(this->xbuffer_used);
    this->Id = 0;
  }
  spin_unlock(&api_lock);

  /* tell kcapi that we actually released the appl */
  ctrl->appl_released(ctrl, appl);
  MOD_DEC_USE_COUNT;
} 


/*
**      send message
*/

void diva_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
  int i = 0, j = 0;
  word ret = 0;
  CAPI_MSG *msg = (CAPI_MSG *)skb->data;
  APPL *this = &application[msg->header.appl_id - 1];
  diva_card *card = ctrl->driverdata;
  __u32 length = skb->len;

  if(in_interrupt()) {
    DBG_ERR(("CAPI_SEND_MSG - in interrupt context !"))
    printk(KERN_ERR "%s: diva_send_message: in interrupt context.\n", DRIVERLNAME);
    return;
  }
  DBG_PRV1(("Write - appl = %d, cmd = 0x%x", this->Id, msg->header.command))

  /* patch controller number */
  msg->header.controller = card->Id;

  spin_lock(&api_lock);

  switch ( msg->header.command )
  {
    default:
      xlog ("\x00\x02", msg, 0x80, msg->header.length) ;
      break;

    case _DATA_B3_I|RESPONSE:
      if ( myDriverDebugHandle.dbgMask & DL_BLK )
        xlog ("\x00\x02", msg, 0x80, msg->header.length) ;
      break;

    case _DATA_B3_R:
      if ( myDriverDebugHandle.dbgMask & DL_BLK )
        xlog ("\x00\x02", msg, 0x80, msg->header.length) ;

      if (msg->header.length == 24) msg->header.length = 22; /* workaround for PPcom bug */
                                                             /* header is always 22      */
      if (msg->info.data_b3_req.Data_Length > this->MaxDataLength ||
          msg->info.data_b3_req.Data_Length > (length - msg->header.length))
      {
        DBG_ERR(("Write - invalid message size"))
        goto write_end;
      }

      for (i = 0; i < (MAX_DATA_B3 * this->MaxNCCI) && this->xbuffer_used[i]; i++);
      if (i == (MAX_DATA_B3 * this->MaxNCCI))
      {
        DBG_ERR(("Write - too many data pending"))
        goto write_end;
      }
      msg->info.data_b3_req.Data = i;

      this->xbuffer_internal[i] = NULL;
      memcpy (this->xbuffer_ptr[i], &((__u8 *)msg)[msg->header.length], msg->info.data_b3_req.Data_Length);

      if ( (myDriverDebugHandle.dbgMask & DL_BLK)
        && (myDriverDebugHandle.dbgMask & DL_XLOG))
      {
        for ( j = 0 ; j < msg->info.data_b3_req.Data_Length ; j += 256 )
        {
          DBG_BLK((((char *)this->xbuffer_ptr[i]) + j,
            ((msg->info.data_b3_req.Data_Length - j) < 256)?
             (msg->info.data_b3_req.Data_Length - j) : 256  ))
          if ( !(myDriverDebugHandle.dbgMask & DL_PRV0) )
            break ; // not more if not explicitely requested
        }
      }
      break ;
  }

  memcpy (mapped_msg, msg, (__u32)msg->header.length);
  mapped_msg->header.controller = MapController (mapped_msg->header.controller);

  ret = api_put (this, mapped_msg);
  switch (ret)
  {
    case 0:
      break;
    case _BAD_MSG:
      DBG_ERR(("Write - bad message"))
      break;
    case _QUEUE_FULL:
      DBG_ERR(("Write - queue full"))
      break;
    default:
      DBG_ERR(("Write - api_put returned unknown error"))
      break;
  }

write_end:
  spin_unlock(&api_lock);
  kfree_skb(skb);
}

static void
DIRequest (ENTITY* e)
{
  DIVA_CAPI_ADAPTER *a = &(adapter[(byte)e->user[0]]);
  diva_card* os_card = (diva_card*)a->os_card;

  if (a->FlowControlIdTable[e->ReqCh] == e->Id) {
    a->FlowControlSkipTable[e->ReqCh] = 1;
  }

  (*(os_card->d.request))(e);
}

/************ module functions ***********/

static void __exit
diva_remove_cards(void)
{
  diva_card *last;
  diva_card *card;

  spin_lock(&ll_lock);
  card = cards;

  while(card) {
    di->detach_ctr(card->capi_ctrl);
    clean_adapter(card->Id - 1);
    DBG_TRC(("adapter remove, max_adapter=%d", max_adapter));
    card = card->next;
  }

  card = cards;
  while (card) {
    last = card;
    card = card->next;
    kfree(last);
  }
  spin_unlock(&ll_lock);
}

static void
diva_remove_card(DESCRIPTOR *d)
{
  diva_card *last;
  diva_card *card;

  spin_lock(&ll_lock);
  last = card = cards;
  while(card) {
    if (card->d.request == d->request)
    {
      di->detach_ctr(card->capi_ctrl);
      clean_adapter(card->Id - 1);
      ControllerMap[card->Id] = 0;
      DBG_TRC(("adapter remove, max_adapter=%d", max_adapter));
      if (card == last)
        cards = card->next;
      else
        last->next = card->next;

      kfree(card);
      break;
    }
    last = card;
    card = card->next;
  }
  spin_unlock(&ll_lock);
}

static int
diva_add_card(DESCRIPTOR *d)
{
  int k = 0, i = 0;
  diva_card *card = NULL;
  struct capi_ctr *ctrl = NULL;
  DIVA_CAPI_ADAPTER *a = NULL;
  IDI_SYNC_REQ sync_req;
  char serial[16];

  if (!(card = (diva_card *) kmalloc(sizeof(diva_card), GFP_KERNEL))) {
    printk(KERN_ERR "%s: failed to allocate card struct.\n", DRIVERLNAME);
    return(0);
  }
  memset((char *)card, 0x00, sizeof(diva_card));
  memcpy(&card->d, d, sizeof(DESCRIPTOR));
  sync_req.GetName.Req = 0;
  sync_req.GetName.Rc = IDI_SYNC_REQ_GET_NAME;
  card->d.request((ENTITY *)&sync_req);
  strncpy(card->name, sync_req.GetName.name, sizeof(card->name));

  if (!(card->capi_ctrl = di->attach_ctr(&divas_driver, card->name, card))) {
    printk(KERN_ERR "%s: failed to attach controller.\n", DRIVERLNAME);
    kfree(card);
    return(0);
  } 
  card->Id = find_free_id();
  ctrl = card->capi_ctrl;
  strncpy(ctrl->manu, M_COMPANY, CAPI_MANUFACTURER_LEN);
  ctrl->version.majorversion = 2;
  ctrl->version.minorversion = 0;
  ctrl->version.majormanuversion = DRRELMAJOR;
  ctrl->version.minormanuversion = DRRELMINOR;
  sync_req.GetSerial.Req = 0;
  sync_req.GetSerial.Rc = IDI_SYNC_REQ_GET_SERIAL;
  sync_req.GetSerial.serial = 0;
  card->d.request((ENTITY *)&sync_req);
  if ((i = ((sync_req.GetSerial.serial & 0xff000000) >> 24))) {
    sprintf(serial, "%ld-%d",
            sync_req.GetSerial.serial & 0x00ffffff, i + 1);
  } else {
    sprintf(serial, "%ld", sync_req.GetSerial.serial);
  }
  serial[CAPI_SERIAL_LEN-1] = 0;
  strncpy(ctrl->serial, serial, CAPI_SERIAL_LEN);

  a = &adapter[card->Id - 1];
  card->adapter = a;
  a->os_card = card;
  ControllerMap[card->Id] = (byte)(card->Id);
  sync_req.xdi_capi_prms.Req = 0;
  sync_req.xdi_capi_prms.Rc = IDI_SYNC_REQ_XDI_GET_CAPI_PARAMS;
  sync_req.xdi_capi_prms.info.structure_length = sizeof(diva_xdi_get_capi_parameters_t);
  card->d.request((ENTITY *)&sync_req);
  a->flag_dynamic_l1_down = sync_req.xdi_capi_prms.info.flag_dynamic_l1_down;
  a->group_optimization_enabled = sync_req.xdi_capi_prms.info.group_optimization_enabled;
  a->request = DIRequest; /* card->d.request; */
  a->max_plci = card->d.channels + 30;
  a->max_listen = 2;
  if (!(a->plci = (PLCI *) vmalloc(sizeof(PLCI) * a->max_plci))) {
    printk(KERN_ERR "%s: failed alloc plci struct.\n", DRIVERLNAME);
    memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
    return(0);
  }
  memset(a->plci, 0, sizeof(PLCI) * a->max_plci);

  for (k = 0; k < a->max_plci; k++)
    {
      a->Id = (byte)card->Id;
      a->plci[k].Sig.callback = sync_callback;
      a->plci[k].Sig.XNum = 1;
      a->plci[k].Sig.X = a->plci[k].XData;
      a->plci[k].Sig.user[0] = (word) (card->Id - 1);
      a->plci[k].Sig.user[1] = (word) k;
      a->plci[k].NL.callback = sync_callback;
      a->plci[k].NL.XNum = 1;
      a->plci[k].NL.X = a->plci[k].XData;
      a->plci[k].NL.user[0] = (word) ((card->Id - 1) | 0x8000);
      a->plci[k].NL.user[1] = (word) k;
      a->plci[k].adapter = a;
    }

  a->profile.Number = card->Id;
  a->profile.Channels = card->d.channels;
  if (card->d.features & DI_FAX3)
    {
      a->profile.Global_Options = 0x71;
      if (card->d.features & DI_CODEC)
        a->profile.Global_Options |= 0x6;
#if IMPLEMENT_DTMF
      a->profile.Global_Options |= 0x8;
#endif /* IMPLEMENT_DTMF */
#if IMPLEMENT_LINE_INTERCONNECT
      a->profile.Global_Options |= 0x80;
#endif /* IMPLEMENT_LINE_INTERCONNECT */
      a->profile.B1_Protocols = 0xdf;
      a->profile.B2_Protocols = 0x1fdb;
      a->profile.B3_Protocols = 0xb7;
      a->manufacturer_features = MANUFACTURER_FEATURE_HARDDTMF;
    }
  else
    {
      a->profile.Global_Options = 0x71;
      if (card->d.features & DI_CODEC)
        a->profile.Global_Options |= 0x2;
      a->profile.B1_Protocols = 0x43;
      a->profile.B2_Protocols = 0x1f0f;
      a->profile.B3_Protocols = 0x07;
      a->manufacturer_features = 0;
    }

#if IMPLEMENT_LINE_INTERCONNECT
  a->li_pri = (a->profile.Channels > 2);
  if (a->li_pri)
    {
      if (!(a->li_config.pri = (LI_CONFIG_PRI *) kmalloc(sizeof(LI_CONFIG_PRI), GFP_KERNEL))) {
        printk(KERN_ERR "%s: failed alloc li_config.pri struct.\n", DRIVERLNAME);
        memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
        return(0);
      }
      memset(a->li_config.pri, 0, sizeof(LI_CONFIG_PRI));
    }
  else
    {
      if (!(a->li_config.bri = (LI_CONFIG_BRI *) kmalloc(sizeof(LI_CONFIG_BRI), GFP_KERNEL))) {
        printk(KERN_ERR "%s: failed alloc li_config.bri struct.\n", DRIVERLNAME);
        memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
        return(0);
      }
      memset(a->li_config.bri, 0, sizeof(LI_CONFIG_BRI));
    }
#endif /* IMPLEMENT_LINE_INTERCONNECT */

  spin_lock(&ll_lock);
  card->next = cards;
  cards = card;
  spin_unlock(&ll_lock);

  AutomaticLaw(a);
  while(i++ < 30) {
    if (a->automatic_law > 3)
      break;
    set_current_state(TASK_UNINTERRUPTIBLE);
    schedule_timeout(HZ / 10 + 1);
  }

  /* profile information */
  ctrl->profile.nbchannel = card->d.channels;
  ctrl->profile.goptions = a->profile.Global_Options;
  ctrl->profile.support1 = a->profile.B1_Protocols;
  ctrl->profile.support2 = a->profile.B2_Protocols;
  ctrl->profile.support3 = a->profile.B3_Protocols;
  /* manufacturer profile information */
  ctrl->profile.manu[0] = a->man_profile.private_options;
  ctrl->profile.manu[1] = a->man_profile.rtp_primary_payloads;
  ctrl->profile.manu[2] = a->man_profile.rtp_additional_payloads;
  ctrl->profile.manu[3] = 0;
  ctrl->profile.manu[4] = 0;

  ctrl->ready(ctrl);

  max_adapter++;
  DBG_TRC(("adapter added, max_adapter=%d", max_adapter));
  return(1);
}

static void
clean_adapter(int id)
{
#if IMPLEMENT_LINE_INTERCONNECT
  if (adapter[id].li_pri)
    {
      if(adapter[id].li_config.pri)
        kfree(adapter[id].li_config.pri);
    }
  else
    {
      if(adapter[id].li_config.bri)
        kfree(adapter[id].li_config.bri);
    }
#endif /* IMPLEMENT_LINE_INTERCONNECT */
  if(adapter[id].plci)
    vfree(adapter[id].plci);

  memset(&adapter[id], 0x00, sizeof(DIVA_CAPI_ADAPTER));
  max_adapter--;
}

static void *
didd_callback(void *context, DESCRIPTOR* adapter, int removal)
{
  if (adapter->type == IDI_DADAPTER)
  {
    printk(KERN_ERR "%s: Change in DAdapter ? Oops ?.\n", DRIVERLNAME);
    DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
    return(NULL);
  }
  else if (adapter->type == IDI_DIMAINT)
  {
    if (removal)
    {
      memset(&MAdapter, 0, sizeof(MAdapter));
      dprintf = no_printf;
    }
    else
    {
      memcpy(&MAdapter, adapter, sizeof(MAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      DbgRegister("CAPI20", DRIVERRELEASE, DBG_DEFAULT);
    }
  }
  else if ((adapter->type > 0) &&
           (adapter->type < 16))
  {  /* IDI Adapter */
    if (removal)
    {
      diva_remove_card(adapter);
    }
    else
    {
      diva_add_card(adapter);
    }
  }
  return(NULL);
}

static int __init
connect_didd(void)
{
  int x = 0;
  int dadapter = 0;
  IDI_SYNC_REQ req;
  DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

  DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

  for (x = 0; x < MAX_DESCRIPTORS; x++)
  {
    if (DIDD_Table[x].type == IDI_DADAPTER)
    {  /* DADAPTER found */
      dadapter = 1;
      memcpy(&DAdapter, &DIDD_Table[x], sizeof(DAdapter));
      req.didd_notify.e.Req = 0;
      req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REGISTER_ADAPTER_NOTIFY;
      req.didd_notify.info.callback = didd_callback;
      req.didd_notify.info.context = 0;
      DAdapter.request((ENTITY *)&req);
      if (req.didd_notify.e.Rc != 0xff)
        return(0);
      notify_handle = req.didd_notify.info.handle;
    }
    else if (DIDD_Table[x].type == IDI_DIMAINT)
    {  /* MAINT found */
      memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
      dprintf = (DIVA_DI_PRINTF)MAdapter.request;
      DbgRegister("CAPI20", DRIVERRELEASE, DBG_DEFAULT);
    }
    else if ((DIDD_Table[x].type > 0) &&
             (DIDD_Table[x].type < 16))
    {  /* IDI Adapter found */
      diva_add_card(&DIDD_Table[x]);
    }
  }
  return(dadapter);
}

static int __init
init_main_structs(void)
{
  if(!(mapped_msg = (CAPI_MSG *) kmalloc(MAX_MSG_SIZE, GFP_KERNEL))) {
    printk(KERN_ERR "%s: failed alloc mapped_msg.\n", DRIVERLNAME);
    return 0;
  }

  memset(ControllerMap, 0, MAX_DESCRIPTORS + 1);

  /* TODO: maybe we can alloc this piece by piece when needed */
  if (!(adapter = vmalloc(sizeof(DIVA_CAPI_ADAPTER) * MAX_DESCRIPTORS))) {
    printk(KERN_ERR "%s: failed alloc adapter struct.\n", DRIVERLNAME);
    kfree(mapped_msg);
    return 0;
  }
  memset(adapter, 0, sizeof(DIVA_CAPI_ADAPTER) * MAX_DESCRIPTORS);

  /* TODO: maybe we can alloc this piece by piece when needed */
  if (!(application = vmalloc(sizeof(APPL) * MAX_APPL))) {
    printk(KERN_ERR "%s: failed alloc application struct.\n", DRIVERLNAME);
    kfree(mapped_msg);
    vfree(adapter);
    return 0;
  }
  memset(application, 0, sizeof(APPL) * MAX_APPL);

  return(1);
}

static void __exit
remove_main_structs(void)
{
  if (application)
    vfree(application);
  if (adapter)
    vfree(adapter);
  if (mapped_msg)
    kfree(mapped_msg);
}

static void __exit
disconnect_didd(void)
{
  IDI_SYNC_REQ req;

  req.didd_notify.e.Req = 0;
  req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
  req.didd_notify.info.handle = notify_handle;
  DAdapter.request((ENTITY *)&req);
}

static int __init
divacapi_init(void)
{
  char tmprev[32];
  int ret = 0;      

  MOD_INC_USE_COUNT;

  api_lock = SPIN_LOCK_UNLOCKED;
  ll_lock = SPIN_LOCK_UNLOCKED;

  sprintf(DRIVERRELEASE, "%d.%d%s", DRRELMAJOR, DRRELMINOR, DRRELEXTRA);

  printk(KERN_INFO "%s\n", DRIVERNAME);
  printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE);
  strcpy(tmprev, main_revision);
  printk("%s  Build: %s(%s)\n", getrev(tmprev),
        diva_capi_common_code_build, DIVA_BUILD);

  strcpy(divas_driver.name, DRIVERLNAME);
  strcpy(tmprev, main_revision);
  sprintf(divas_driver.revision, "%s/%s %s(%s)", DRIVERRELEASE,
          getrev(tmprev), diva_capi_common_code_build, DIVA_BUILD);

  di = attach_capi_driver(&divas_driver);

  if (!di) {
    printk(KERN_ERR "%s: failed to attach capi_driver.\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

  if(!init_main_structs()) {
    printk(KERN_ERR "%s: failed to init main structs.\n", DRIVERLNAME);
    detach_capi_driver(&divas_driver);
    ret = -EIO;
    goto out;
  }

  if(!connect_didd()) {
    printk(KERN_ERR "%s: failed to connect to DIDD.\n", DRIVERLNAME);
    detach_capi_driver(&divas_driver);
    ret = -EIO;
    goto out;
  }

out:
  MOD_DEC_USE_COUNT;
  return ret;
}


static void __exit
divacapi_exit(void)
{
  int count = 100;
  word ret = 1;

  while(ret && count--)
  {
    ret = api_remove_start();
    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(10);
  }

  if (ret)
    printk(KERN_WARNING "%s: could not remove signaling ID's.\n", DRIVERLNAME);

  disconnect_didd();
  diva_remove_cards();
  detach_capi_driver(&divas_driver);
  remove_main_structs();
  DbgDeregister();
  printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divacapi_init);
module_exit(divacapi_exit);

