
/*
 *
  Copyright (c) Eicon Networks, 2000.
 *
  This source file is supplied for the exclusive use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    1.9
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/* --------------------------------------------------------------------------
 file:        io.c
 program:     IDI Driver for active cards
 abstract:    interface of the driver
 author:      Christian Koessel/Peter Kietzmann
 Copyright (C) Eicon Networks 1993-2000
  --------------------------------------------------------------------------- */
#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "pr_pc.h"
#include "divasync.h"
#define MIPS_SCOM
#include "pkmaint.h" /* pc_main.h, packed in os-dependent fashion */
#include "di.h"
#include "mi_pc.h"
#include "io.h"
extern ADAPTER * adapter[MAX_ADAPTER];
extern PISDN_ADAPTER IoAdapters[MAX_ADAPTER];
void request (PISDN_ADAPTER, ENTITY *);
void pcm_req (PISDN_ADAPTER, ENTITY *);
/* --------------------------------------------------------------------------
  local functions
  -------------------------------------------------------------------------- */
#define ReqFunc(N) \
static void Request##N(ENTITY *e) \
{ if ( IoAdapters[N] ) (* IoAdapters[N]->DIRequest)(IoAdapters[N], e) ; }
ReqFunc(0)
ReqFunc(1)
ReqFunc(2)
ReqFunc(3)
ReqFunc(4)
ReqFunc(5)
ReqFunc(6)
ReqFunc(7)
ReqFunc(8)
ReqFunc(9)
ReqFunc(10)
ReqFunc(11)
ReqFunc(12)
ReqFunc(13)
ReqFunc(14)
ReqFunc(15)
IDI_CALL Requests[MAX_ADAPTER] =
{ &Request0, &Request1, &Request2, &Request3,
 &Request4, &Request5, &Request6, &Request7,
 &Request8, &Request9, &Request10, &Request11,
 &Request12, &Request13, &Request14, &Request15
};
/*****************************************************************************/
/*
  This array should indicate all new services, that this version of XDI
  is able to provide to his clients
  */
static byte extended_xdi_features[DIVA_XDI_EXTENDED_FEATURES_MAX_SZ+1] = {
 (DIVA_XDI_EXTENDED_FEATURES_VALID       |
  DIVA_XDI_EXTENDED_FEATURE_CMA          |
  DIVA_XDI_EXTENDED_FEATURE_SDRAM_BAR    |
  DIVA_XDI_EXTENDED_FEATURE_CAPI_PRMS    |
  DIVA_XDI_EXTENDED_FEATURE_NO_CANCEL_RC),
 0
};
/*****************************************************************************/
void
dump_xlog_buffer (PISDN_ADAPTER IoAdapter, Xdesc *xlogDesc)
{
 dword   logLen ;
 word *Xlog   = xlogDesc->buf ;
 word  logCnt = xlogDesc->cnt ;
 word  logOut = xlogDesc->out / sizeof(*Xlog) ;
 DBG_FTL(("%s: ************* XLOG recovery (%d) *************",
          &IoAdapter->Name[0], (int)logCnt))
 DBG_FTL(("Microcode: %s", &IoAdapter->ProtocolIdString[0]))
 for ( ; logCnt > 0 ; --logCnt )
 {
  if ( !Xlog[logOut] )
  {
   if ( --logCnt == 0 )
    break ;
   logOut = 0 ;
  }
  if ( Xlog[logOut] <= (logOut * sizeof(*Xlog)) )
  {
   if ( logCnt > 2 )
   {
    DBG_FTL(("Possibly corrupted XLOG: %d entries left",
             (int)logCnt))
   }
   break ;
  }
  logLen = (dword)(Xlog[logOut] - (logOut * sizeof(*Xlog))) ;
  DBG_FTL_MXLOG(( (char *)&Xlog[logOut + 1], (dword)(logLen - 2) ))
  logOut = (Xlog[logOut] + 1) / sizeof(*Xlog) ;
 }
 DBG_FTL(("%s: ***************** end of XLOG *****************",
          &IoAdapter->Name[0]))
}
/*****************************************************************************/
char *(ExceptionCauseTable[]) =
{
 "Interrupt",
 "TLB mod /IBOUND",
 "TLB load /DBOUND",
 "TLB store",
 "Address error load",
 "Address error store",
 "Instruction load bus error",
 "Data load/store bus error",
 "Syscall",
 "Breakpoint",
 "Reverd instruction",
 "Coprocessor unusable",
 "Overflow",
 "TRAP",
 "VCEI",
 "Floating Point Exception",
 "CP2",
 "Reserved 17",
 "Reserved 18",
 "Reserved 19",
 "Reserved 20",
 "Reserved 21",
 "Reserved 22",
 "WATCH",
 "Reserved 24",
 "Reserved 25",
 "Reserved 26",
 "Reserved 27",
 "Reserved 28",
 "Reserved 29",
 "Reserved 30",
 "VCED"
} ;
void
dump_trap_frame (PISDN_ADAPTER IoAdapter, byte *exceptionFrame)
{
 MP_XCPTC *xcept = (MP_XCPTC *)exceptionFrame ;
 dword    *regs  = &xcept->regs[0] ;
 DBG_FTL(("%s: ***************** CPU TRAPPED *****************",
          &IoAdapter->Name[0]))
 DBG_FTL(("Microcode: %s", &IoAdapter->ProtocolIdString[0]))
 DBG_FTL(("Cause: %s",
          ExceptionCauseTable[(xcept->cr & 0x0000007c) >> 2]))
 DBG_FTL(("sr    0x%08x cr    0x%08x epc   0x%08x vaddr 0x%08x",
          xcept->sr, xcept->cr, xcept->epc, xcept->vaddr))
 DBG_FTL(("zero  0x%08x at    0x%08x v0    0x%08x v1    0x%08x",
          regs[ 0], regs[ 1], regs[ 2], regs[ 3]))
 DBG_FTL(("a0    0x%08x a1    0x%08x a2    0x%08x a3    0x%08x",
          regs[ 4], regs[ 5], regs[ 6], regs[ 7]))
 DBG_FTL(("t0    0x%08x t1    0x%08x t2    0x%08x t3    0x%08x",
          regs[ 8], regs[ 9], regs[10], regs[11]))
 DBG_FTL(("t4    0x%08x t5    0x%08x t6    0x%08x t7    0x%08x",
          regs[12], regs[13], regs[14], regs[15]))
 DBG_FTL(("s0    0x%08x s1    0x%08x s2    0x%08x s3    0x%08x",
          regs[16], regs[17], regs[18], regs[19]))
 DBG_FTL(("s4    0x%08x s5    0x%08x s6    0x%08x s7    0x%08x",
          regs[20], regs[21], regs[22], regs[23]))
 DBG_FTL(("t8    0x%08x t9    0x%08x k0    0x%08x k1    0x%08x",
          regs[24], regs[25], regs[26], regs[27]))
 DBG_FTL(("gp    0x%08x sp    0x%08x s8    0x%08x ra    0x%08x",
          regs[28], regs[29], regs[30], regs[31]))
 DBG_FTL(("md    0x%08x|%08x         resvd 0x%08x class 0x%08x",
          xcept->mdhi, xcept->mdlo, xcept->reseverd, xcept->xclass))
}
/* --------------------------------------------------------------------------
  Real XDI Request function
  -------------------------------------------------------------------------- */
void request(PISDN_ADAPTER IoAdapter, ENTITY * e)
{
 byte i;
 diva_os_spin_lock_magic_t irql;
/*
 * if the Req field in the entity structure is 0,
 * we treat this request as a special function call
 */
 if ( !e->Req )
 {
  IDI_SYNC_REQ *syncReq = (IDI_SYNC_REQ *)e ;
  switch (e->Rc)
  {
    case IDI_SYNC_REQ_XDI_GET_LOGICAL_ADAPTER_NUMBER: {
      diva_xdi_get_logical_adapter_number_s_t *pI = \
                                     &syncReq->xdi_logical_adapter_number.info;
      pI->logical_adapter_number = IoAdapter->ANum;
      pI->controller = IoAdapter->ControllerNumber;
    } return;
    case IDI_SYNC_REQ_XDI_GET_CAPI_PARAMS: {
       diva_xdi_get_capi_parameters_t prms, *pI = &syncReq->xdi_capi_prms.info;
       memset (&prms, 0x00, sizeof(prms));
       prms.structure_length = MIN(sizeof(prms), pI->structure_length);
       memset (pI, 0x00, pI->structure_length);
       prms.flag_dynamic_l1_down    = (IoAdapter->capi_cfg.cfg_1 & \
         DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON) ? 1 : 0;
       prms.group_optimization_enabled = (IoAdapter->capi_cfg.cfg_1 & \
         DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON) ? 1 : 0;
       memcpy (pI, &prms, prms.structure_length);
      } return;
    case IDI_SYNC_REQ_XDI_GET_ADAPTER_SDRAM_BAR:
      syncReq->xdi_sdram_bar.info.bar = IoAdapter->sdram_bar;
      return;
    case IDI_SYNC_REQ_XDI_GET_EXTENDED_FEATURES: {
      dword i;
      diva_xdi_get_extended_xdi_features_t* pI =\
                                 &syncReq->xdi_extended_features.info;
      pI->buffer_length_in_bytes &= ~0x80000000;
      if (pI->buffer_length_in_bytes && pI->features) {
        memset (pI->features, 0x00, pI->buffer_length_in_bytes);
      }
      for (i = 0; ((pI->features) && (i < pI->buffer_length_in_bytes) &&
                   (i < DIVA_XDI_EXTENDED_FEATURES_MAX_SZ)); i++) {
        pI->features[i] = extended_xdi_features[i];
      }
      if ((pI->buffer_length_in_bytes < DIVA_XDI_EXTENDED_FEATURES_MAX_SZ) ||
          (!pI->features)) {
        pI->buffer_length_in_bytes =\
                           (0x80000000 | DIVA_XDI_EXTENDED_FEATURES_MAX_SZ);
      }
     } return;
    case IDI_SYNC_REQ_XDI_GET_STREAM:
      if (IoAdapter) {
        diva_xdi_provide_istream_info (&IoAdapter->a,
                                       &syncReq->xdi_stream_info.info);
      } else {
        syncReq->xdi_stream_info.info.provided_service = 0;
      }
      return;
  case IDI_SYNC_REQ_GET_NAME:
   if ( IoAdapter )
   {
    strcpy (&syncReq->GetName.name[0], IoAdapter->Name) ;
    DBG_TRC(("xdi: Adapter %d / Name '%s'",
             IoAdapter->ANum, IoAdapter->Name))
    return ;
   }
   syncReq->GetName.name[0] = '\0' ;
   break ;
  case IDI_SYNC_REQ_GET_SERIAL:
   if ( IoAdapter )
   {
    syncReq->GetSerial.serial = IoAdapter->serialNo ;
    DBG_TRC(("xdi: Adapter %d / SerialNo %ld",
             IoAdapter->ANum, IoAdapter->serialNo))
    return ;
   }
   syncReq->GetSerial.serial = 0 ;
   break ;
  case IDI_SYNC_REQ_GET_XLOG:
   if ( IoAdapter )
   {
    pcm_req (IoAdapter, e) ;
    return ;
   }
   e->Ind = 0 ;
   break ;
  case IDI_SYNC_REQ_GET_FEATURES:
   if ( IoAdapter )
   {
    syncReq->GetFeatures.features =
      (unsigned short)IoAdapter->features ;
    return ;
   }
   syncReq->GetFeatures.features = 0 ;
   break ;
        case IDI_SYNC_REQ_PORTDRV_HOOK:
            if ( IoAdapter )
            {
                DBG_TRC(("Xdi:IDI_SYNC_REQ_PORTDRV_HOOK - ignored"))
                return ;
            }
            break;
  }
  if ( IoAdapter )
  {
   DBG_FTL(("xdi: unknown Req 0 / Rc %d !", e->Rc))
   return ;
  }
 }
 DBG_TRC(("xdi: Id 0x%x / Req 0x%x / Rc 0x%x", e->Id, e->Req, e->Rc))
 if ( !IoAdapter )
 {
  DBG_FTL(("xdi: uninitialized Adapter used - ignore request"))
  return ;
 }
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
/*
 * assign an entity
 */
 if ( !(e->Id &0x1f) )
 {
  if ( IoAdapter->e_count >= IoAdapter->e_max )
  {
   DBG_FTL(("xdi: all Ids in use (max=%d) --> Req ignored",
            IoAdapter->e_max))
   diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
   return ;
  }
/*
 * find a new free id
 */
  for ( i = 1 ; IoAdapter->e_tbl[i].e ; ++i ) ;
  IoAdapter->e_tbl[i].e = e ;
  IoAdapter->e_count++ ;
  e->No = (byte)i ;
  e->More = 0 ;
  e->RCurrent = 0xff ;
 }
 else
 {
  i = e->No ;
 }
/*
 * if the entity is still busy, ignore the request call
 */
 if ( e->More & XBUSY )
 {
  DBG_FTL(("xdi: Id 0x%x busy --> Req 0x%x ignored", e->Id, e->Req))
  if ( !IoAdapter->trapped && IoAdapter->trapFnc )
  {
   IoAdapter->trapFnc (IoAdapter) ;
  }
  diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
  return ;
 }
/*
 * initialize transmit status variables
 */
 e->More |= XBUSY ;
 e->More &= ~XMOREF ;
 e->XCurrent = 0 ;
 e->XOffset = 0 ;
/*
 * queue this entity in the adapter request queue
 */
 IoAdapter->e_tbl[i].next = 0 ;
 if ( IoAdapter->head )
 {
  IoAdapter->e_tbl[IoAdapter->tail].next = i ;
  IoAdapter->tail = i ;
 }
 else
 {
  IoAdapter->head = i ;
  IoAdapter->tail = i ;
 }
/*
 * queue the DPC to process the request
 */
 diva_os_schedule_soft_isr (&IoAdapter->req_soft_isr);
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req");
}
/* ---------------------------------------------------------------------
  Main DPC routine
   --------------------------------------------------------------------- */
void DIDpcRoutine (struct _diva_os_soft_isr* psoft_isr, void* Context) {
 PISDN_ADAPTER IoAdapter  = (PISDN_ADAPTER)Context ;
 ADAPTER* a        = &IoAdapter->a ;
 diva_os_atomic_t* pin_dpc = &IoAdapter->in_dpc;
 if (diva_os_atomic_increment (pin_dpc) == 1) {
  do {
   if ( IoAdapter->tst_irq (a) )
   {
    IoAdapter->dpc (a) ;
    IoAdapter->clr_irq (a) ;
   }
   IoAdapter->out (a) ;
     if (a->istream_wakeup) {
      (*(a->istream_wakeup))(a);
     }
  } while (diva_os_atomic_decrement (pin_dpc) > 0);
  /* ----------------------------------------------------------------
    Look for XLOG request (cards with indirect addressing)
    ---------------------------------------------------------------- */
  if (IoAdapter->pcm_pending) {
   struct pc_maint *pcm;
   diva_os_spin_lock_magic_t OldIrql ;
   diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_dpc");
   pcm = (struct pc_maint *)IoAdapter->pcm_data;
   switch (IoAdapter->pcm_pending) {
    case 1: /* ask card for XLOG */
     a->ram_out (a, &IoAdapter->pcm->rc, 0) ;
     a->ram_out (a, &IoAdapter->pcm->req, pcm->req) ;
     IoAdapter->pcm_pending = 2;
     break;
    case 2: /* Try to get XLOG from the card */
     if ((int)(a->ram_in (a, &IoAdapter->pcm->rc))) {
      a->ram_in_buffer (a, IoAdapter->pcm, pcm, sizeof(*pcm)) ;
      IoAdapter->pcm_pending = 3;
     }
     break;
    case 3: /* let XDI recovery XLOG */
     break;
   }
   diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_dpc");
  }
  /* ---------------------------------------------------------------- */
 }
}
/* --------------------------------------------------------------------------
  XLOG interface
  -------------------------------------------------------------------------- */
void
pcm_req (PISDN_ADAPTER IoAdapter, ENTITY *e)
{
 diva_os_spin_lock_magic_t OldIrql ;
 int              i, rc ;
 ADAPTER         *a = &IoAdapter->a ;
 struct pc_maint *pcm = (struct pc_maint *)&e->Ind ;
/*
 * special handling of I/O based card interface
 * the memory access isn't an atomic operation !
 */
 if ( IoAdapter->Properties.Card == CARD_MAE )
 {
  diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_1");
  IoAdapter->pcm_data = (dword)pcm;
  IoAdapter->pcm_pending = 1;
  diva_os_schedule_soft_isr (&IoAdapter->req_soft_isr);
  diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_1");
  for ( rc = 0, i = (IoAdapter->trapped ? 3000 : 250) ; !rc && (i > 0) ; --i )
  {
   diva_os_sleep (1) ;
   if (IoAdapter->pcm_pending == 3) {
    diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
                 &OldIrql,
                 "data_pcm_3");
    IoAdapter->pcm_pending = 0;
    IoAdapter->pcm_data   = 0;
    diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
                 &OldIrql,
                 "data_pcm_3");
    return ;
   }
   diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_pcm_2");
   diva_os_schedule_soft_isr (&IoAdapter->req_soft_isr);
   diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
                &OldIrql,
                "data_pcm_2");
  }
  diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_4");
  IoAdapter->pcm_pending = 0;
  IoAdapter->pcm_data   = 0;
  diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
               &OldIrql,
               "data_pcm_4");
  goto Trapped ;
 }
/*
 * memory based shared ram is accessable from different
 * processors without disturbing concurrent processes.
 */
 a->ram_out (a, &IoAdapter->pcm->rc, 0) ;
 a->ram_out (a, &IoAdapter->pcm->req, pcm->req) ;
 for ( i = (IoAdapter->trapped ? 3000 : 250) ; --i > 0 ; )
 {
  diva_os_sleep (1) ;
  rc = (int)(a->ram_in (a, &IoAdapter->pcm->rc)) ;
  if ( rc )
  {
   a->ram_in_buffer (a, IoAdapter->pcm, pcm, sizeof(*pcm)) ;
   return ;
  }
 }
Trapped:
 if ( IoAdapter->trapFnc )
 {
  IoAdapter->trapFnc (IoAdapter) ;
 }
}
/*------------------------------------------------------------------*/
/* ram access functions for memory mapped cards                     */
/*------------------------------------------------------------------*/
byte mem_in (ADAPTER *a, void *addr)
{
 byte* Base = (byte*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 return (*Base) ;
}
word mem_inw (ADAPTER *a, void *addr)
{
 word* Base = (word*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 return (*Base) ;
}
void mem_in_dw (ADAPTER *a, void *addr, dword* data, int dwords)
{
 volatile dword* Base = (dword*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 while (dwords--) {
  *data++ = *Base++;
 }
}
void mem_in_buffer (ADAPTER *a, void *addr, void *buffer, word length)
{
 byte* Base = (byte*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 memcpy (buffer, Base, length) ;
}
void mem_look_ahead (ADAPTER *a, PBUFFER *RBuffer, ENTITY *e)
{
 PISDN_ADAPTER IoAdapter = (PISDN_ADAPTER)a->io ;
 IoAdapter->RBuffer.length = mem_inw (a, &RBuffer->length) ;
 mem_in_buffer (a, RBuffer->P, IoAdapter->RBuffer.P,
                IoAdapter->RBuffer.length) ;
 e->RBuffer = (DBUFFER *)&IoAdapter->RBuffer ;
}
void mem_out (ADAPTER *a, void *addr, byte data)
{
 byte* Base = (byte*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 *Base = data ;
}
void mem_outw (ADAPTER *a, void *addr, word data)
{
 word* Base = (word*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 *Base = data ;
}
void mem_out_dw (ADAPTER *a, void *addr, const dword* data, int dwords)
{
 volatile dword* Base = (dword*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 while (dwords--) {
  *Base++ = *data++;
 }
}
void mem_out_buffer (ADAPTER *a, void *addr, void *buffer, word length)
{
 byte* Base = (byte*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 memcpy (Base, buffer, length) ;
}
void mem_inc (ADAPTER *a, void *addr)
{
 byte* Base = (byte*)&((PISDN_ADAPTER)a->io)->ram[(dword)addr] ;
 byte  x = *Base ;
 *Base = x + 1 ;
}
/*------------------------------------------------------------------*/
/* ram access functions for io-mapped cards                         */
/*------------------------------------------------------------------*/
byte io_in(ADAPTER * a, void * adr)
{
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  return inpp(((PISDN_ADAPTER)a->io)->port);
}
word io_inw(ADAPTER * a, void * adr)
{
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  return inppw(((PISDN_ADAPTER)a->io)->port);
}
void io_in_buffer(ADAPTER * a, void * adr, void * buffer, word len)
{
 byte* P = (byte*)buffer;
  if ((long)adr & 1) {
    outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
    *P = inpp(((PISDN_ADAPTER)a->io)->port);
    P++;
    adr = ((byte *) adr) + 1;
    len--;
    if (!len) return;
  }
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  inppw_buffer (((PISDN_ADAPTER)a->io)->port, P, len+1);
}
void io_look_ahead(ADAPTER * a, PBUFFER * RBuffer, ENTITY * e)
{
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)RBuffer);
  ((PISDN_ADAPTER)a->io)->RBuffer.length = inppw(((PISDN_ADAPTER)a->io)->port);
  inppw_buffer (((PISDN_ADAPTER)a->io)->port, ((PISDN_ADAPTER)a->io)->RBuffer.P, ((PISDN_ADAPTER)a->io)->RBuffer.length + 1);
  e->RBuffer = (DBUFFER *) &(((PISDN_ADAPTER)a->io)->RBuffer);
}
void io_out(ADAPTER * a, void * adr, byte data)
{
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  outpp(((PISDN_ADAPTER)a->io)->port, data);
}
void io_outw(ADAPTER * a, void * adr, word data)
{
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  outppw(((PISDN_ADAPTER)a->io)->port, data);
}
void io_out_buffer(ADAPTER * a, void * adr, void * buffer, word len)
{
 byte* P = (byte*)buffer;
  if ((long)adr & 1) {
    outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
    outpp(((PISDN_ADAPTER)a->io)->port, *P);
    P++;
    adr = ((byte *) adr) + 1;
    len--;
    if (!len) return;
  }
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  outppw_buffer (((PISDN_ADAPTER)a->io)->port, P, len+1);
}
void io_inc(ADAPTER * a, void * adr)
{
  byte x;
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  x = inpp(((PISDN_ADAPTER)a->io)->port);
  outppw(((PISDN_ADAPTER)a->io)->port+4, (word)(dword)adr);
  outpp(((PISDN_ADAPTER)a->io)->port, x+1);
}
/*------------------------------------------------------------------*/
/* OS specific functions related to queuing of entities             */
/*------------------------------------------------------------------*/
void free_entity(ADAPTER * a, byte e_no)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_free");
  IoAdapter->e_tbl[e_no].e = NULL;
  IoAdapter->e_count--;
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_free");
}
void assign_queue(ADAPTER * a, byte e_no, word ref)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_assign");
  IoAdapter->e_tbl[e_no].assign_ref = ref;
  IoAdapter->e_tbl[e_no].next = (byte)IoAdapter->assign;
  IoAdapter->assign = e_no;
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_assign");
}
byte get_assign(ADAPTER * a, word ref)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  byte e_no;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock,
              &irql,
              "data_assign_get");
  for(e_no = (byte)IoAdapter->assign;
      e_no && IoAdapter->e_tbl[e_no].assign_ref!=ref;
      e_no = IoAdapter->e_tbl[e_no].next);
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock,
              &irql,
              "data_assign_get");
  return e_no;
}
void req_queue(ADAPTER * a, byte e_no)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_q");
  IoAdapter->e_tbl[e_no].next = 0;
  if(IoAdapter->head) {
    IoAdapter->e_tbl[IoAdapter->tail].next = e_no;
    IoAdapter->tail = e_no;
  }
  else {
    IoAdapter->head = e_no;
    IoAdapter->tail = e_no;
  }
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_q");
}
byte look_req(ADAPTER * a)
{
  PISDN_ADAPTER IoAdapter;
  IoAdapter = (PISDN_ADAPTER) a->io;
  return ((byte)IoAdapter->head) ;
}
void next_req(ADAPTER * a)
{
  PISDN_ADAPTER IoAdapter;
 diva_os_spin_lock_magic_t irql;
  IoAdapter = (PISDN_ADAPTER) a->io;
 diva_os_enter_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_next");
  IoAdapter->head = IoAdapter->e_tbl[IoAdapter->head].next;
  if(!IoAdapter->head) IoAdapter->tail = 0;
 diva_os_leave_spin_lock (&IoAdapter->data_spin_lock, &irql, "data_req_next");
}
/*------------------------------------------------------------------*/
/* memory map functions                                             */
/*------------------------------------------------------------------*/
ENTITY * entity_ptr(ADAPTER * a, byte e_no)
{
  PISDN_ADAPTER IoAdapter;
  IoAdapter = (PISDN_ADAPTER) a->io;
  return (IoAdapter->e_tbl[e_no].e);
}
void * PTR_X(ADAPTER * a, ENTITY * e)
{
  return ((void *) e->X);
}
void * PTR_R(ADAPTER * a, ENTITY * e)
{
  return ((void *) e->R);
}
void * PTR_P(ADAPTER * a, ENTITY * e, void * P)
{
  return P;
}
void CALLBACK(ADAPTER * a, ENTITY * e)
{
 if ( e && e->callback )
  e->callback (e) ;
}
/* --------------------------------------------------------------------------
  routines for aligned reading and writing on RISC
  -------------------------------------------------------------------------- */
void outp_words_from_buffer (word* adr, byte* P, word len)
{
  word i = 0;
  word w;
  while (i < (len & 0xfffe)) {
    w = P[i++];
    w += (P[i++])<<8;
    outppw (adr, w);
  }
}
void inp_words_to_buffer (word* adr, byte* P, word len)
{
  word i = 0;
  word w;
  while (i < (len & 0xfffe)) {
    w = inppw (adr);
    P[i++] = (byte)(w);
    P[i++] = (byte)(w>>8);
  }
}
