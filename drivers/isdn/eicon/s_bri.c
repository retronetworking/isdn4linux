
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
  program:   IDI Driver for DIVA cards
  abstract:   specific code for the DIVA Server BRI series, OS independent
  Copyright (C) Eicon Networks 1993-2000
  -------------------------------------------------------------------------- */
#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "pr_pc.h"
#include "di.h"
#include "mi_pc.h"
#include "pc_maint.h"
#include "divasync.h"
#include "io.h"
#include "helpers.h"
#include "dsrv_bri.h"
#include "dsp_defs.h"
#define MAX_XLOG_SIZE (64 * 1024)
/* --------------------------------------------------------------------------
  Investigate card state, recovery trace buffer
  -------------------------------------------------------------------------- */
static void bri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
 byte  *addrHi, *addrLo, *ioaddr ;
 word *Xlog ;
 dword   regs[4], i ;
 Xdesc   xlogDesc ;
/*
 * first read pointers and trap frame
 */
 if ( !(Xlog = (word *)diva_os_malloc (0, MAX_XLOG_SIZE)) )
  return ;
 addrHi =   IoAdapter->port
   + ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH) ;
 addrLo = IoAdapter->port + ADDR ;
 ioaddr = IoAdapter->port + DATA ;
 outpp (addrHi,  0) ;
 outppw (addrLo, 0) ;
 for ( i = 0 ; i < 0x100 ; Xlog[i++] = inppw(ioaddr) ) ;
/*
 * check for trapped MIPS 3xxx CPU, dump only exception frame
 */
 if ( *(dword *)(&Xlog[0x80 / sizeof(Xlog[0])]) == 0x99999999 )
 {
  dump_trap_frame (IoAdapter, &((byte *)Xlog)[0x90]) ;
  IoAdapter->trapped = 1 ;
 }
 memcpy (&regs[0], &((byte *)Xlog)[0x70], sizeof(regs)) ;
 outpp (addrHi, (regs[1] >> 16) & 0x7F) ;
 outppw (addrLo, regs[1] & 0xFFFF) ;
 xlogDesc.cnt = inppw(ioaddr) ;
 outpp (addrHi, (regs[2] >> 16) & 0x7F) ;
 outppw (addrLo, regs[2] & 0xFFFF) ;
 xlogDesc.out = inppw(ioaddr) ;
 xlogDesc.buf = Xlog ;
 regs[0] &= IoAdapter->MemorySize - 1 ;
 if ( (regs[0] >= 0)
   && (regs[0] < IoAdapter->MemorySize - BRI_SHARED_RAM_SIZE) )
 {
  for ( i = 0 ; i < (MAX_XLOG_SIZE / sizeof(*Xlog)) ; regs[0] += 2 )
  {
   outpp (addrHi, (regs[0] >> 16) & 0x7F) ;
   outppw (addrLo, regs[0] & 0xFFFF) ;
   Xlog[i++] = inppw(ioaddr) ;
  }
  dump_xlog_buffer (IoAdapter, &xlogDesc) ;
  diva_os_free (0, Xlog) ;
  IoAdapter->trapped = 2 ;
 }
 outpp  (addrHi, (byte)((BRI_UNCACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                            BRI_SHARED_RAM_SIZE)) >> 16)) ;
 outppw (addrLo, 0x00) ;
}
/* ---------------------------------------------------------------------
   Reset hardware
  --------------------------------------------------------------------- */
static void reset_bri_hardware (PISDN_ADAPTER IoAdapter) {
 outpp (IoAdapter->ctlReg, 0x00) ;
}
/* ---------------------------------------------------------------------
   Halt system
  --------------------------------------------------------------------- */
static void stop_bri_hardware (PISDN_ADAPTER IoAdapter) {
 if (IoAdapter->reset) {
  outpp (IoAdapter->reset, 0x00) ; /* disable interrupts ! */
 }
 outpp (IoAdapter->ctlReg, 0x00) ;    /* clear int, halt cpu */
}
#if !defined(DIVA_USER_MODE_CARD_CONFIG) /* { */
/* ---------------------------------------------------------------------
  Load protocol on the card
  --------------------------------------------------------------------- */
static dword bri_protocol_load (PISDN_ADAPTER IoAdapter) {
 dword   FileLength ;
 word    test, *File = NULL ;
 byte*  addrHi, *addrLo, *ioaddr ;
 char   *FileName = &IoAdapter->Protocol[0] ;
 dword   Addr, i ;
 /* -------------------------------------------------------------------
   Try to load protocol code. 'File' points to memory location
   that does contain entire protocol code
   ------------------------------------------------------------------- */
 if ( !(File = (word *)xdiLoadArchive (IoAdapter, &FileLength)) )
  return (0) ;
 /* -------------------------------------------------------------------
   Get protocol features and calculate load addresses
   ------------------------------------------------------------------- */
 IoAdapter->features = diva_get_protocol_file_features ((byte*)File,
                     OFFS_PROTOCOL_ID_STRING,
                     IoAdapter->ProtocolIdString,
                     sizeof(IoAdapter->ProtocolIdString));
 DBG_LOG(("Loading %s", IoAdapter->ProtocolIdString))
 Addr = ((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR]))
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 1])) << 8)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 2])) << 16)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 3])) << 24) ;
        if ( Addr != 0 )
 {
  IoAdapter->DspCodeBaseAddr = (Addr + 3) & (~3) ;
  IoAdapter->MaxDspCodeSize = (BRI_UNCACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                                  BRI_SHARED_RAM_SIZE)
                            - IoAdapter->DspCodeBaseAddr) & (IoAdapter->MemorySize - 1) ;
  Addr = IoAdapter->DspCodeBaseAddr ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR] = (byte) Addr ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 1] = (byte)(Addr >> 8) ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 2] = (byte)(Addr >> 16) ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 3] = (byte)(Addr >> 24) ;
  IoAdapter->InitialDspInfo = 0x80 ;
 }
 else
 {
  if ( IoAdapter->features & PROTCAP_V90D )
   IoAdapter->MaxDspCodeSize = BRI_V90D_MAX_DSP_CODE_SIZE ;
  else
   IoAdapter->MaxDspCodeSize = BRI_ORG_MAX_DSP_CODE_SIZE ;
  IoAdapter->DspCodeBaseAddr = BRI_CACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                                BRI_SHARED_RAM_SIZE - IoAdapter->MaxDspCodeSize);
  IoAdapter->InitialDspInfo = (IoAdapter->MaxDspCodeSize - BRI_ORG_MAX_DSP_CODE_SIZE) >> 14 ;
 }
 DBG_LOG(("DSP code base 0x%08lx, max size 0x%08lx (%08lx,%02x)",
      IoAdapter->DspCodeBaseAddr, IoAdapter->MaxDspCodeSize,
      Addr, IoAdapter->InitialDspInfo))
 if ( FileLength > ((IoAdapter->DspCodeBaseAddr -
                     BRI_CACHED_ADDR (IoAdapter->MemoryBase)) & (IoAdapter->MemorySize - 1)) )
 {
  xdiFreeFile (File) ;
  DBG_FTL(("Protocol code '%s' too big (%ld)", FileName, FileLength))
  return (0) ;
 }
 addrHi =   IoAdapter->port
        + ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH) ;
 addrLo = IoAdapter->port + ADDR ;
 ioaddr = IoAdapter->port + DATA ;
/*
 * set start address for download (use autoincrement mode !)
 */
 outpp  (addrHi, 0) ;
 outppw (addrLo, 0) ;
 for ( i = 0 ; i < FileLength ; i += 2 )
 {
  if ( (i & 0x0000FFFF) == 0 )
  {
   outpp (addrHi, (byte)(i >> 16)) ;
  }
  outppw (ioaddr, File[i/2]) ;
 }
/*
 * memory test without second load of file
 */
 outpp  (addrHi, 0) ;
 outppw (addrLo, 0) ;
 for ( i = 0 ; i < FileLength ; i += 2 )
 {
  if ( (i & 0x0000FFFF) == 0 )
  {
   outpp (addrHi, (byte)(i >> 16)) ;
  }
  test = inppw (ioaddr) ;
  if ( test != File[i/2] )
  {
   DBG_FTL(("%s: Memory test failed! (%d - 0x%04X/0x%04X)",
            IoAdapter->Properties.Name, i, test, File[i/2]))
   xdiFreeFile (File) ;
   return (0) ;
  }
 }
 xdiFreeFile (File) ;
 return (FileLength) ;
}
/******************************************************************************/
typedef struct
{
 PISDN_ADAPTER IoAdapter ;
 byte*        AddrLo ;
 byte*        AddrHi ;
 word*       Data ;
 dword         DownloadPos ;
} bri_download_info ;
static long bri_download_buffer (OsFileHandle *fp, long length, void **addr) {
 int        buffer_size = 2048*sizeof(word);
 word       *buffer = (word*)diva_os_malloc (0, buffer_size);
 bri_download_info *info ;
 word       test ;
 long       i, len, page ;
 if (!buffer) {
  DBG_ERR(("A: out of memory, s_bri at %d", __LINE__))
  return (-1);
 }
 info = (bri_download_info *)fp->sysLoadDesc ;
 *addr = (void *)info->DownloadPos ;
 if ( ((dword) length) > info->IoAdapter->DspCodeBaseAddr +
                         info->IoAdapter->MaxDspCodeSize - info->DownloadPos )
 {
  DBG_FTL(("%s: out of card memory during DSP download (0x%X)",
           info->IoAdapter->Properties.Name,
           info->DownloadPos + length))
  diva_os_free (0, buffer);
  return (-1) ;
 }
 for ( len = 0 ; length > 0 ; length -= len )
 {
  len = (length > buffer_size ? buffer_size : length) ;
  page = ((long)(info->DownloadPos) + len) & 0xFFFF0000 ;
  if ( page != (long)(info->DownloadPos & 0xFFFF0000) )
  {
   len = 0x00010000 - (((long)info->DownloadPos) & 0x0000FFFF) ;
  }
  if ( fp->sysFileRead (fp, &buffer[0], len) != len ) {
   diva_os_free (0, buffer);
   return (-1) ;
  }
  outpp (info->AddrHi, (byte)(info->DownloadPos >> 16)) ;
  outppw (info->AddrLo, (word)info->DownloadPos) ;
  outppw_buffer (info->Data, &buffer[0], (len + 1)) ;
/*
 * memory test without second load of file
 */
  outpp (info->AddrHi, (byte)(info->DownloadPos >> 16)) ;
  outppw (info->AddrLo, (word)info->DownloadPos) ;
  for ( i = 0 ; i < len ; i += 2 )
  {
   if ( (test = inppw (info->Data)) != buffer[i/2] )
   {
    DBG_FTL(("%s: Memory test failed! (0x%lX - 0x%04X/0x%04X)",
             info->IoAdapter->Properties.Name,
             info->DownloadPos + i, test, buffer[i/2]))
    diva_os_free (0, buffer);
    return (-2) ;
   }
  }
  info->DownloadPos += len ;
 }
 info->DownloadPos = (info->DownloadPos + 3) & (~3) ;
 diva_os_free (0, buffer);
 return (0) ;
}
/******************************************************************************/
static dword bri_telindus_load (PISDN_ADAPTER IoAdapter) {
 bri_download_info    *pinfo =\
             (bri_download_info*)diva_os_malloc(0, sizeof(*pinfo));
 char                *error ;
 OsFileHandle        *fp ;
 t_dsp_portable_desc  download_table[DSP_MAX_DOWNLOAD_COUNT] ;
 word                 download_count ;
 dword                FileLength ;
 if (!pinfo) {
  DBG_ERR (("A: out of memory s_bri at %d", __LINE__))
  return (0);
 }
 if ( !(fp = OsOpenFile (DSP_TELINDUS_FILE)) ) {
  diva_os_free (0, pinfo);
  return (0) ;
 }
 FileLength     = fp->sysFileSize ;
 pinfo->IoAdapter = IoAdapter ;
 pinfo->AddrLo    = IoAdapter->port + ADDR ;
 pinfo->AddrHi    = IoAdapter->port +\
     (IoAdapter->Properties.Bus == BUS_PCI ? M_PCI_ADDRH : ADDRH);
 pinfo->Data = (word*)(IoAdapter->port + DATA) ;
 pinfo->DownloadPos = (IoAdapter->DspCodeBaseAddr +\
           sizeof(dword) + sizeof(download_table) + 3) & (~3) ;
 fp->sysLoadDesc = (void *)pinfo;
 fp->sysCardLoad = bri_download_buffer ;
 download_count = DSP_MAX_DOWNLOAD_COUNT ;
 memset (&download_table[0], '\0', sizeof(download_table)) ;
/*
 * set start address for download (use autoincrement mode !)
 */
 error = dsp_read_file (fp, (word)(IoAdapter->cardType),
                        &download_count, NULL, &download_table[0]) ;
 if ( error )
 {
  DBG_FTL(("download file error: %s", error))
  OsCloseFile (fp) ;
  diva_os_free (0, pinfo);
  return (0) ;
 }
 OsCloseFile (fp) ;
/*
 * store # of separate download files extracted from archive
 */
 pinfo->DownloadPos = IoAdapter->DspCodeBaseAddr ;
 outpp  (pinfo->AddrHi, (byte)(pinfo->DownloadPos >> 16)) ;
 outppw (pinfo->AddrLo, (word)pinfo->DownloadPos) ;
 outppw (pinfo->Data,   (word)download_count) ;
 outppw (pinfo->Data,   (word)0) ;
/*
 * copy download table to board
 */
 outppw_buffer (pinfo->Data, &download_table[0], sizeof(download_table)) ;
 diva_os_free (0, pinfo);
 return (FileLength) ;
}
/******************************************************************************/
static int load_bri_hardware (PISDN_ADAPTER IoAdapter) {
 dword   i ;
 byte*  addrHi, *addrLo, *ioaddr ;
 dword   test ;
 if ( IoAdapter->Properties.Card != CARD_MAE )
 {
  return (FALSE) ;
 }
 addrHi =   IoAdapter->port \
    + ((IoAdapter->Properties.Bus==BUS_PCI) ? M_PCI_ADDRH : ADDRH);
 addrLo = IoAdapter->port + ADDR ;
 ioaddr = IoAdapter->port + DATA ;
 reset_bri_hardware (IoAdapter) ;
 diva_os_wait (100);
/*
 * recover
 */
 outpp  (addrHi, (byte) 0) ;
 outppw (addrLo, (word) 0) ;
 outppw (ioaddr, (word) 0) ;
/*
 * clear shared memory
 */
 outpp  (addrHi, (byte)((BRI_UNCACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                            BRI_SHARED_RAM_SIZE)) >> 16)) ;
 outppw (addrLo, 0) ;
 for ( i = 0 ; i < 0x8000 ; outppw (ioaddr, 0), ++i ) ;
 diva_os_wait (100) ;
/*
 * download protocol and dsp files
 */
 if ( !bri_protocol_load (IoAdapter) )
  return (FALSE) ;
 if ( !bri_telindus_load (IoAdapter) )
  return (FALSE) ;
/*
 * clear signature
 */
 outpp  (addrHi, (byte)((BRI_UNCACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                            BRI_SHARED_RAM_SIZE)) >> 16)) ;
 outppw (addrLo, 0x1e) ;
 outpp (ioaddr, 0) ;
 outpp (ioaddr, 0) ;
/*
 * copy parameters
 */
 diva_configure_protocol (IoAdapter);
/*
 * start the protocol code
 */
 outpp (IoAdapter->ctlReg, 0x08) ;
/*
 * wait for signature (max. 3 seconds)
 */
 for ( i = 0 ; i < 300 ; ++i )
 {
  diva_os_wait (10) ;
  outpp (addrHi, (byte)((BRI_UNCACHED_ADDR (IoAdapter->MemoryBase + IoAdapter->MemorySize -
                                            BRI_SHARED_RAM_SIZE)) >> 16)) ;
  outppw (addrLo, 0x1e) ;
  test = (dword)inppw (ioaddr) ;
  if ( test == 0x4447 )
  {
   DBG_TRC(("Protocol startup time %d.%02d seconds",
            (i / 100), (i % 100) ))
   return (TRUE) ;
  }
 }
 DBG_FTL(("%s: Adapter selftest failed (0x%04X)!",
          IoAdapter->Properties.Name, test))
 bri_cpu_trapped (IoAdapter) ;
 return (FALSE) ;
}
#else /* } { */
static int load_bri_hardware (PISDN_ADAPTER IoAdapter) {
 return (0);
}
#endif /* } */
/******************************************************************************/
static int bri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
 if ( !(inpp (IoAdapter->ctlReg) & 0x01) )
  return (0) ;
 /*
  clear interrupt line
  */
 outpp (IoAdapter->ctlReg, 0x08) ;
 IoAdapter->IrqCount++ ;
 if ( IoAdapter->Initialized ) {
  diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
 }
 return (1) ;
}
/* --------------------------------------------------------------------------
  Disable IRQ in the card hardware
  -------------------------------------------------------------------------- */
static void disable_bri_interrupt (PISDN_ADAPTER IoAdapter) {
 if ( IoAdapter->reset )
 {
  outpp (IoAdapter->reset, 0x00) ; // disable interrupts !
 }
 outpp (IoAdapter->ctlReg, 0x00) ;    // clear int, halt cpu
}
/* -------------------------------------------------------------------------
  Fill card entry points
  ------------------------------------------------------------------------- */
void prepare_maestra_functions (PISDN_ADAPTER IoAdapter) {
 ADAPTER *a = &IoAdapter->a ;
 a->ram_in             = io_in ;
 a->ram_inw            = io_inw ;
 a->ram_in_buffer      = io_in_buffer ;
 a->ram_look_ahead     = io_look_ahead ;
 a->ram_out            = io_out ;
 a->ram_outw           = io_outw ;
 a->ram_out_buffer     = io_out_buffer ;
 a->ram_inc            = io_inc ;
 IoAdapter->MemoryBase = BRI_MEMORY_BASE ;
 IoAdapter->MemorySize = BRI_MEMORY_SIZE ;
 IoAdapter->out        = pr_out ;
 IoAdapter->dpc        = pr_dpc ;
 IoAdapter->tst_irq    = scom_test_int ;
 IoAdapter->clr_irq    = scom_clear_int ;
 IoAdapter->pcm        = (struct pc_maint *)MIPS_MAINT_OFFS ;
 IoAdapter->load       = load_bri_hardware ;
 IoAdapter->disIrq     = disable_bri_interrupt ;
 IoAdapter->rstFnc     = reset_bri_hardware ;
 IoAdapter->stop       = stop_bri_hardware ;
 IoAdapter->trapFnc    = bri_cpu_trapped ;
 IoAdapter->diva_isr_handler = bri_ISR;
 /*
  Prepare OS dependent functions
  */
 diva_os_prepare_maestra_functions (IoAdapter);
}
/* -------------------------------------------------------------------------- */
