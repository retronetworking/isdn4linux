
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
  abstract:   specific code for the DIVA Server PRI series, OS independent
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
#include "dsrv_pri.h"
#include "dsp_defs.h"
#define MAX_XLOG_SIZE  (64 * 1024)
/* -------------------------------------------------------------------------
  Does return offset between ADAPTER->ram and real begin of memory
  ------------------------------------------------------------------------- */
static dword pri_ram_offset (ADAPTER* a) {
 return ((dword)MP_SHARED_RAM_OFFSET);
}
/* -------------------------------------------------------------------------
  Recovery XLOG buffer from the card
  ------------------------------------------------------------------------- */
static void pri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
 byte  *base ;
 word *Xlog ;
 dword   regs[4], TrapID ;
 Xdesc   xlogDesc ;
/*
 * check for trapped MIPS 46xx CPU, dump exception frame
 */
 base   = IoAdapter->ram - MP_SHARED_RAM_OFFSET ;
 TrapID = *((dword *)&base[0x80]) ;
 if ( (TrapID == 0x99999999) || (TrapID == 0x99999901) )
 {
  dump_trap_frame (IoAdapter, &base[0x90]) ;
  IoAdapter->trapped = 1 ;
 }
 memcpy (&regs[0], &base[MP_PROTOCOL_OFFSET + 0x70], sizeof(regs)) ;
 regs[0] &= IoAdapter->MemorySize - 1 ;
 if ((regs[0] >= MP_PROTOCOL_OFFSET)
   &&(regs[0] < IoAdapter->MemorySize - MAX_XLOG_SIZE))
 {
  if ( !(Xlog = (word *)diva_os_malloc (0, MAX_XLOG_SIZE)) )
   return ;
  memcpy (Xlog, &base[regs[0]], MAX_XLOG_SIZE) ;
  xlogDesc.buf = Xlog ;
  xlogDesc.cnt = *((word *)&base[regs[1] & (IoAdapter->MemorySize - 1)]) ;
  xlogDesc.out = *((word *)&base[regs[2] & (IoAdapter->MemorySize - 1)]) ;
  dump_xlog_buffer (IoAdapter, &xlogDesc) ;
  diva_os_free (0, Xlog) ;
  IoAdapter->trapped = 2 ;
 }
}
/* -------------------------------------------------------------------------
  Hardware reset of PRI card
  ------------------------------------------------------------------------- */
static void reset_pri_hardware (PISDN_ADAPTER IoAdapter) {
 *IoAdapter->reset = _MP_RISC_RESET | _MP_LED1 | _MP_LED2 ;
 diva_os_wait (50) ;
 *IoAdapter->reset = 0x00 ;
 diva_os_wait (50) ;
}
/* -------------------------------------------------------------------------
  Stop Card Hardware
  ------------------------------------------------------------------------- */
static void stop_pri_hardware (PISDN_ADAPTER IoAdapter) {
 dword i;
 dword volatile *cfgReg = (dword volatile *)IoAdapter->cfg ;
 cfgReg[3] = 0x00000000 ;
 cfgReg[1] = 0x00000000 ;
 IoAdapter->a.ram_out (&IoAdapter->a, &RAM->SWReg, SWREG_HALT_CPU) ;
 i = 0 ;
 while ( (i < 100) && (IoAdapter->a.ram_in (&IoAdapter->a, &RAM->SWReg) != 0) )
 {
  diva_os_wait (1) ;
  i++ ;
 }
 DBG_TRC(("%s: PRI stopped (%d)", IoAdapter->Name, i))
 cfgReg[0] = (dword)(~0x03E00000) ;
 diva_os_wait (1) ;
 *IoAdapter->reset = _MP_RISC_RESET | _MP_LED1 | _MP_LED2 ;
}
#if !defined(DIVA_USER_MODE_CARD_CONFIG) /* { */
/* -------------------------------------------------------------------------
  Load protocol code to the PRI Card
  ------------------------------------------------------------------------- */
#define DOWNLOAD_ADDR(IoAdapter) \
 (&IoAdapter->ram[IoAdapter->downloadAddr & (IoAdapter->MemorySize - 1)])
static int pri_protocol_load (PISDN_ADAPTER IoAdapter) {
 dword  FileLength ;
 dword *File ;
 dword *sharedRam ;
 dword  Addr ;
 File = (dword *)xdiLoadArchive (IoAdapter, &FileLength) ;
 if ( !File )
  return (0) ;
 IoAdapter->features = diva_get_protocol_file_features ((byte*)File,
                                        OFFS_PROTOCOL_ID_STRING,
                                        IoAdapter->ProtocolIdString,
                                        sizeof(IoAdapter->ProtocolIdString)) ;
 DBG_LOG(("Loading %s", IoAdapter->ProtocolIdString))
 Addr = ((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR]))
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 1])) << 8)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 2])) << 16)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 3])) << 24) ;
        if ( Addr != 0 )
 {
  IoAdapter->DspCodeBaseAddr = (Addr + 3) & (~3) ;
  IoAdapter->MaxDspCodeSize = (MP_UNCACHED_ADDR (IoAdapter->MemorySize)
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
  if ( IoAdapter->features & PROTCAP_VOIP )
   IoAdapter->MaxDspCodeSize = MP_VOIP_MAX_DSP_CODE_SIZE ;
  else if ( IoAdapter->features & PROTCAP_V90D )
   IoAdapter->MaxDspCodeSize = MP_V90D_MAX_DSP_CODE_SIZE ;
  else
   IoAdapter->MaxDspCodeSize = MP_ORG_MAX_DSP_CODE_SIZE ;
  IoAdapter->DspCodeBaseAddr = MP_CACHED_ADDR (IoAdapter->MemorySize -
                                               IoAdapter->MaxDspCodeSize) ;
  IoAdapter->InitialDspInfo = (IoAdapter->MaxDspCodeSize
                            - MP_ORG_MAX_DSP_CODE_SIZE) >> 14 ;
 }
 DBG_LOG(("DSP code base 0x%08lx, max size 0x%08lx (%08lx,%02x)",
          IoAdapter->DspCodeBaseAddr, IoAdapter->MaxDspCodeSize,
          Addr, IoAdapter->InitialDspInfo))
 if ( FileLength > ((IoAdapter->DspCodeBaseAddr -
                     MP_CACHED_ADDR (MP_PROTOCOL_OFFSET)) & (IoAdapter->MemorySize - 1)) )
 {
  xdiFreeFile (File) ;
  DBG_FTL(("Protocol code '%s' too long (%ld)",
           &IoAdapter->Protocol[0], FileLength))
  return (0) ;
 }
 IoAdapter->downloadAddr = MP_UNCACHED_ADDR (MP_PROTOCOL_OFFSET) ;
 sharedRam = (dword *)DOWNLOAD_ADDR(IoAdapter) ;
 memcpy (sharedRam, File, FileLength) ;
 if ( memcmp (sharedRam, File, FileLength) )
 {
  DBG_FTL(("%s: Memory test failed!", IoAdapter->Properties.Name))
  xdiFreeFile (File) ;
  return (0) ;
 }
 xdiFreeFile (File) ;
 return (1) ;
}
/* -------------------------------------------------------------------------
  helper used to download dsp code toi PRI Card
  ------------------------------------------------------------------------- */
static long pri_download_buffer (OsFileHandle *fp, long length, void **addr) {
 PISDN_ADAPTER IoAdapter = (PISDN_ADAPTER)fp->sysLoadDesc ;
 dword        *sharedRam ;
 *addr = (void *)IoAdapter->downloadAddr ;
 if ( ((dword) length) > IoAdapter->DspCodeBaseAddr +
                         IoAdapter->MaxDspCodeSize - IoAdapter->downloadAddr )
 {
  DBG_FTL(("%s: out of card memory during DSP download (0x%X)",
           IoAdapter->Properties.Name,
           IoAdapter->downloadAddr + length))
  return (-1) ;
 }
 sharedRam = (dword *)DOWNLOAD_ADDR(IoAdapter) ;
 if ( fp->sysFileRead (fp, sharedRam, length) != length )
  return (-1) ;
 IoAdapter->downloadAddr += length ;
 IoAdapter->downloadAddr  = (IoAdapter->downloadAddr + 3) & (~3) ;
 return (0) ;
}
/* -------------------------------------------------------------------------
  Download DSP code to PRI Card
  ------------------------------------------------------------------------- */
static dword pri_telindus_load (PISDN_ADAPTER IoAdapter) {
 char                *error ;
 OsFileHandle        *fp ;
 t_dsp_portable_desc  download_table[DSP_MAX_DOWNLOAD_COUNT] ;
 word                 download_count ;
 dword               *sharedRam ;
 dword                FileLength ;
 if ( !(fp = OsOpenFile (DSP_TELINDUS_FILE)) )
  return (0) ;
 IoAdapter->downloadAddr = (IoAdapter->DspCodeBaseAddr
                         + sizeof(dword) + sizeof(download_table) + 3) & (~3) ;
 FileLength      = fp->sysFileSize ;
 fp->sysLoadDesc = (void *)IoAdapter ;
 fp->sysCardLoad = pri_download_buffer ;
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
  return (0) ;
 }
 OsCloseFile (fp) ;
/*
 * store # of separate download files extracted from archive
 */
 IoAdapter->downloadAddr = IoAdapter->DspCodeBaseAddr ;
 sharedRam    = (dword *)DOWNLOAD_ADDR(IoAdapter) ;
 sharedRam[0] = (dword)download_count ;
 memcpy (&sharedRam[1], &download_table[0], sizeof(download_table)) ;
 return (FileLength) ;
}
/* -------------------------------------------------------------------------
  Download PRI Card
  ------------------------------------------------------------------------- */
static int load_pri_hardware (PISDN_ADAPTER IoAdapter) {
 dword           i ;
 struct mp_load *boot = (struct mp_load *)IoAdapter->ram ;
 if ( IoAdapter->Properties.Card != CARD_MAEP )
  return (0) ;
 boot->err = 0 ;
 IoAdapter->rstFnc (IoAdapter) ;
/*
 * check if CPU is alive
 */
 diva_os_wait (10) ;
 i = boot->live ;
 diva_os_wait (10) ;
 if ( i == boot->live )
 {
  DBG_FTL(("%s: CPU is not alive!", IoAdapter->Properties.Name))
  return (0) ;
 }
 if ( boot->err )
 {
  DBG_FTL(("%s: Board Selftest failed!", IoAdapter->Properties.Name))
  return (0) ;
 }
/*
 * download protocol and dsp files
 */
 if ( !pri_protocol_load (IoAdapter) )
  return (0) ;
 if ( !pri_telindus_load (IoAdapter) )
  return (0) ;
/*
 * copy configuration parameters
 */
 IoAdapter->ram += MP_SHARED_RAM_OFFSET ;
 memset (IoAdapter->ram, '\0', 256) ;
 diva_configure_protocol (IoAdapter);
/*
 * start adapter
 */
 boot->addr = MP_UNCACHED_ADDR (MP_PROTOCOL_OFFSET) ;
 boot->cmd  = 3 ;
/*
 * wait for signature in shared memory (max. 3 seconds)
 */
 for ( i = 0 ; i < 300 ; ++i )
 {
  diva_os_wait (10) ;
  if ( (boot->signature >> 16) == 0x4447 )
  {
   DBG_TRC(("Protocol startup time %d.%02d seconds",
            (i / 100), (i % 100) ))
   return (1) ;
  }
 }
 DBG_FTL(("%s: Adapter selftest failed (0x%04X)!",
          IoAdapter->Properties.Name, boot->signature >> 16))
 pri_cpu_trapped (IoAdapter) ;
 return (0) ;
}
#else /* } { */
static int load_pri_hardware (PISDN_ADAPTER IoAdapter) {
 return (0);
}
#endif /* } */
/* --------------------------------------------------------------------------
  PRI Adapter interrupt Service Routine
   -------------------------------------------------------------------------- */
static int pri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
 if ( !(*((dword *)IoAdapter->cfg) & 0x80000000) )
  return (0) ;
 IoAdapter->IrqCount++ ;
 if ( IoAdapter->Initialized )
 {
  diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
 }
 /*
  clear interrupt line
  */
 *((dword *)IoAdapter->cfg) = (dword)~0x03E00000 ;
 return (1) ;
}
/* -------------------------------------------------------------------------
  Disable interrupt in the card hardware
  ------------------------------------------------------------------------- */
static void disable_pri_interrupt (PISDN_ADAPTER IoAdapter) {
 dword volatile *cfgReg = (dword volatile *)IoAdapter->cfg ;
 cfgReg[3] = 0x00000000 ;
 cfgReg[1] = 0x00000000 ;
 cfgReg[0] = (dword)(~0x03E00000) ;
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Adapter
  ------------------------------------------------------------------------- */
static void prepare_common_pri_functions (PISDN_ADAPTER IoAdapter) {
 ADAPTER *a = &IoAdapter->a ;
 a->ram_in           = mem_in ;
 a->ram_inw          = mem_inw ;
 a->ram_in_buffer    = mem_in_buffer ;
 a->ram_look_ahead   = mem_look_ahead ;
 a->ram_out          = mem_out ;
 a->ram_outw         = mem_outw ;
 a->ram_out_buffer   = mem_out_buffer ;
 a->ram_inc          = mem_inc ;
 a->ram_offset       = pri_ram_offset ;
 a->ram_out_dw    = mem_out_dw;
 a->ram_in_dw    = mem_in_dw;
  a->istream_wakeup   = pr_stream;
 IoAdapter->out      = pr_out ;
 IoAdapter->dpc      = pr_dpc ;
 IoAdapter->tst_irq  = scom_test_int ;
 IoAdapter->clr_irq  = scom_clear_int ;
 IoAdapter->pcm      = (struct pc_maint *)(MIPS_MAINT_OFFS
                                        - MP_SHARED_RAM_OFFSET) ;
 IoAdapter->load     = load_pri_hardware ;
 IoAdapter->disIrq   = disable_pri_interrupt ;
 IoAdapter->rstFnc   = reset_pri_hardware ;
 IoAdapter->stop     = stop_pri_hardware ;
 IoAdapter->trapFnc  = pri_cpu_trapped ;
 IoAdapter->diva_isr_handler = pri_ISR;
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Adapter
  ------------------------------------------------------------------------- */
void prepare_pri_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MP_MEMORY_SIZE ;
 prepare_common_pri_functions (IoAdapter) ;
 diva_os_prepare_pri_functions (IoAdapter);
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Rev.2 Adapter
  ------------------------------------------------------------------------- */
void prepare_pri2_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MP2_MEMORY_SIZE ;
 prepare_common_pri_functions (IoAdapter) ;
 diva_os_prepare_pri2_functions (IoAdapter);
}
/* ------------------------------------------------------------------------- */
