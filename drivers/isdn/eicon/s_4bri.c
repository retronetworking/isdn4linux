
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
  abstract:   specific code for the DIVA Server 4BRI series, OS independent
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
#include "dsrv4bri.h"
#include "dsp_defs.h"
#define MAX_XLOG_SIZE (64 * 1024)
#define DIVA_4BRI_REVISION(__x__) \
     (((__x__)->cardType == CARDTYPE_DIVASRV_Q_8M_V2_PCI) ||  \
      ((__x__)->cardType == CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI))
/* --------------------------------------------------------------------------
  Recovery XLOG from QBRI Card
  -------------------------------------------------------------------------- */
static void qBri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
 byte  *base ;
 word *Xlog ;
 dword   regs[4], TrapID, offset ;
 Xdesc   xlogDesc ;
/*
 * check for trapped MIPS 46xx CPU, dump exception frame
 */
 offset = IoAdapter->ControllerNumber * (IoAdapter->MemorySize >> 2) ;
 base   = IoAdapter->ram - offset - ((IoAdapter->MemorySize >> 2) - MQ_SHARED_RAM_SIZE) ;
 TrapID = *(dword *)(&base[0x80]) ;
 if ( (TrapID == 0x99999999) || (TrapID == 0x99999901) )
 {
  dump_trap_frame (IoAdapter, &base[0x90]) ;
  IoAdapter->trapped = 1 ;
 }
 memcpy (&regs[0], &(base + offset)[0x70], sizeof(regs)) ;
 regs[0] &= IoAdapter->MemorySize - 1 ;
 if ( (regs[0] >= offset)
   && (regs[0] < offset + ((IoAdapter->MemorySize >> 2) - MQ_SHARED_RAM_SIZE)) )
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
/* --------------------------------------------------------------------------
  Reset QBRI Hardware
  -------------------------------------------------------------------------- */
static void reset_qBri_hardware (PISDN_ADAPTER IoAdapter) {
 word volatile *qBriReset ;
 dword  volatile *qBriCntrl ;
 qBriReset = (word volatile *)IoAdapter->prom ;
 qBriCntrl = (dword volatile *)(&IoAdapter->ctlReg[ \
                 DIVA_4BRI_REVISION(IoAdapter) ? \
                   (MQ2_BREG_RISC) : (MQ_BREG_RISC)]);
 *qBriReset |= PLX9054_SOFT_RESET ;
 diva_os_wait (1) ;
 *qBriReset &= ~PLX9054_SOFT_RESET ;
 diva_os_wait (1);
 *qBriReset |= PLX9054_RELOAD_EEPROM ;
 diva_os_wait (1) ;
 *qBriReset &= ~PLX9054_RELOAD_EEPROM ;
 diva_os_wait (1);
 *qBriCntrl = 0 ;
}
/* --------------------------------------------------------------------------
  Start Card CPU
  -------------------------------------------------------------------------- */
void start_qBri_hardware (PISDN_ADAPTER IoAdapter) {
 dword volatile *qBriReset ;
 qBriReset = (dword volatile *)(&IoAdapter->ctlReg[ \
     DIVA_4BRI_REVISION(IoAdapter) ? (MQ2_BREG_RISC) : (MQ_BREG_RISC)]);
 *qBriReset = MQ_RISC_COLD_RESET_MASK ;
 diva_os_wait (2) ;
 *qBriReset = MQ_RISC_WARM_RESET_MASK | MQ_RISC_COLD_RESET_MASK ;
 diva_os_wait (10) ;
}
/* --------------------------------------------------------------------------
  Stop Card CPU
  -------------------------------------------------------------------------- */
static void stop_qBri_hardware (PISDN_ADAPTER IoAdapter) {
 dword volatile *qBriReset ;
 dword volatile *qBriIrq ;
 dword volatile *qBriIsacDspReset ;  /* IHe 12.8.1999 */
 int rev2 = DIVA_4BRI_REVISION(IoAdapter);
 int reset_offset = rev2 ? (MQ2_BREG_RISC)      : (MQ_BREG_RISC);
 int irq_offset   = rev2 ? (MQ2_BREG_IRQ_TEST)  : (MQ_BREG_IRQ_TEST);
 int hw_offset    = rev2 ? (MQ2_ISAC_DSP_RESET) : (MQ_ISAC_DSP_RESET);
 if ( IoAdapter->ControllerNumber > 0 )
  return ;
 qBriReset = (dword volatile *)(&IoAdapter->ctlReg[reset_offset]) ;
 qBriIrq   = (dword volatile *)(&IoAdapter->ctlReg[irq_offset]) ;
 /*
  IHe 12.8.1999
  */
 qBriIsacDspReset = (dword volatile *)(&IoAdapter->ctlReg[hw_offset]);
/*
 * clear interrupt line (reset Local Interrupt Test Register)
 */
 *qBriReset = 0 ;
  *qBriIsacDspReset = 0 ; /* IHe 12.8.1999 */
 IoAdapter->reset[PLX9054_INTCSR] = 0x00 ; /* disable PCI interrupts */
 *qBriIrq   = MQ_IRQ_REQ_OFF ;
}
/* --------------------------------------------------------------------------
  FPGA download
  -------------------------------------------------------------------------- */
#define NAME_OFFSET         0x10
static byte * qBri_check_FPGAsrc (char *FileName, dword *Length, dword *code) {
 byte *File ;
 char  *fpgaType, *fpgaFile, *fpgaDate, *fpgaTime ;
 dword  fpgaTlen,  fpgaFlen,  fpgaDlen, cnt, i ;
 File = (byte *)xdiLoadFile (FileName, Length) ;
 if ( !File )
  return (NULL) ;
/*
 *  scan file until FF and put id string into buffer
 */
 for ( i = 0 ; File[i] != 0xff ; )
 {
  if ( ++i >= *Length )
  {
   DBG_FTL(("FPGA download: start of data header not found"))
   xdiFreeFile (File) ;
   return (NULL) ;
  }
 }
 *code = i++ ;
 if ( (File[i] & 0xF0) != 0x20 )
 {
  DBG_FTL(("FPGA download: data header corrupted"))
  xdiFreeFile (File) ;
  return (NULL) ;
 }
 fpgaFlen = (dword)  File[NAME_OFFSET - 1] ;
 fpgaFile = (char *)&File[NAME_OFFSET] ;
 fpgaTlen = (dword)  fpgaFile[fpgaFlen + 2] ;
 fpgaType = (char *)&fpgaFile[fpgaFlen + 3] ;
 fpgaDlen = (dword)  fpgaType[fpgaTlen + 2] ;
 fpgaDate = (char *)&fpgaType[fpgaTlen + 3] ;
 fpgaTime = (char *)&fpgaDate[fpgaDlen + 3] ;
 cnt = (dword)(((File[  i  ] & 0x0F) << 20) + (File[i + 1] << 12)
              + (File[i + 2]         <<  4) + (File[i + 3] >>  4)) ;
 if ( (dword)(i + (cnt / 8)) > *Length )
 {
  DBG_FTL(("FPGA download: '%s' file too small (%ld < %ld)",
           FileName, *Length, code + ((cnt + 7) / 8) ))
  xdiFreeFile (File) ;
  return (NULL) ;
 }
 DBG_LOG(("FPGA[%s] file %s (%s %s) len %d",
          fpgaType, fpgaFile, fpgaDate, fpgaTime, cnt))
 return (File) ;
}
static byte * qBri2_check_FPGAsrc (char *FileName, dword *Length, dword *code) {
#if 1
 byte *File ;
 dword  i ;
 File = (byte *)xdiLoadFile (FileName, Length) ;
 if ( !File )
  return (NULL) ;
/*
 *  scan file until FF and put id string into buffer
 */
 for ( i = 0 ; File[i] != 0xff ; )
 {
  if ( ++i >= *Length )
  {
   DBG_FTL(("FPGA download: start of data header not found"))
   xdiFreeFile (File) ;
   return (NULL) ;
  }
 }
 *code = i ;
 return (File) ;
#else
 return (qBri_check_FPGAsrc (FileName, Length, code)) ;
#endif
}
/******************************************************************************/
#define FPGA_PROG   0x0001  // PROG enable low
#define FPGA_BUSY   0x0002  // BUSY high, DONE low
#define FPGA_CS     0x000C  // Enable I/O pins
#define FPGA_CCLK   0x0100
#define FPGA_DOUT   0x0400
#define FPGA_DIN    FPGA_DOUT   // bidirectional I/O
int qBri_FPGA_download (PISDN_ADAPTER IoAdapter) {
 int            bit ;
 byte           *File ;
 dword          code, FileLength ;
 word volatile *addr = (word volatile *)IoAdapter->prom ;
 word           val, baseval = FPGA_CS | FPGA_PROG ;
 if ( (IoAdapter->cardType == CARDTYPE_DIVASRV_Q_8M_V2_PCI)
   || (IoAdapter->cardType == CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI) )
 {
  File = qBri2_check_FPGAsrc ("ds4bri2.bit", &FileLength, &code) ;
 }
 else
 {
  File = qBri_check_FPGAsrc ("ds4bri.bit", &FileLength, &code) ;
 }
 if ( !File )
  return (0) ;
/*
 * prepare download, pulse PROGRAM pin down.
 */
 *addr = baseval & ~FPGA_PROG ; // PROGRAM low pulse
 *addr = baseval ;              // release
 diva_os_wait (50) ;  // wait until FPGA finished internal memory clear
/*
 * check done pin, must be low
 */
 if ( *addr & FPGA_BUSY )
 {
  DBG_FTL(("FPGA download: acknowledge for FPGA memory clear missing"))
  xdiFreeFile (File) ;
  return (0) ;
 }
/*
 * put data onto the FPGA
 */
 while ( code < FileLength )
 {
  val = ((word)File[code++]) << 3 ;
  for ( bit = 8 ; bit-- > 0 ; val <<= 1 ) // put byte onto FPGA
  {
   baseval &= ~FPGA_DOUT ;             // clr  data bit
   baseval |= (val & FPGA_DOUT) ;      // copy data bit
   *addr    = baseval ;
   *addr    = baseval | FPGA_CCLK ;     // set CCLK hi
   *addr    = baseval | FPGA_CCLK ;     // set CCLK hi
   *addr    = baseval ;                 // set CCLK lo
  }
 }
 xdiFreeFile (File) ;
 diva_os_wait (100) ;
 val = *addr ;
 if ( !(val & FPGA_BUSY) )
 {
  DBG_FTL(("FPGA download: chip remains in busy state (0x%04x)", val))
  return (0) ;
 }
 return (1) ;
}
#if !defined(DIVA_USER_MODE_CARD_CONFIG) /* { */
/* --------------------------------------------------------------------------
  Download protocol code to the adapter
  -------------------------------------------------------------------------- */
#define DOWNLOAD_ADDR(IoAdapter) \
 (&IoAdapter->ram[IoAdapter->downloadAddr & (IoAdapter->MemorySize - 1)])
static int qBri_protocol_load (PISDN_ADAPTER BaseIoAdapter, PISDN_ADAPTER IoAdapter) {
 PISDN_ADAPTER HighIoAdapter;
 dword  FileLength ;
 dword *sharedRam, *File ;
 dword  Addr, ProtOffset, SharedRamOffset, i ;
 File = (dword *)xdiLoadArchive (IoAdapter, &FileLength) ;
 if ( !File )
  return (0) ;
 IoAdapter->features = diva_get_protocol_file_features ((byte*)File,
                                        OFFS_PROTOCOL_ID_STRING,
                                        IoAdapter->ProtocolIdString,
                                        sizeof(IoAdapter->ProtocolIdString)) ;
 DBG_LOG(("Loading %s", IoAdapter->ProtocolIdString))
 ProtOffset = IoAdapter->ControllerNumber * (IoAdapter->MemorySize >> 2);
 SharedRamOffset = (IoAdapter->MemorySize >> 2) - MQ_SHARED_RAM_SIZE;
 Addr = ((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR]))
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 1])) << 8)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 2])) << 16)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 3])) << 24) ;
        if ( Addr != 0 )
 {
  IoAdapter->DspCodeBaseAddr = (Addr + 3) & (~3) ;
  IoAdapter->MaxDspCodeSize = (MQ_UNCACHED_ADDR (ProtOffset + SharedRamOffset)
                            - IoAdapter->DspCodeBaseAddr) & ((IoAdapter->MemorySize >> 2) - 1) ;
  i = 0 ;
  while ( BaseIoAdapter->QuadroList->QuadroAdapter[i]->ControllerNumber != MQ_INSTANCE_COUNT - 1 )
   i++ ;
  HighIoAdapter = BaseIoAdapter->QuadroList->QuadroAdapter[i] ;
  Addr = HighIoAdapter->DspCodeBaseAddr ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR] = (byte) Addr ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 1] = (byte)(Addr >> 8) ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 2] = (byte)(Addr >> 16) ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 3] = (byte)(Addr >> 24) ;
  IoAdapter->InitialDspInfo = 0x80 ;
 }
 else
 {
  if ( IoAdapter->features & PROTCAP_VOIP )
  {
   IoAdapter->DspCodeBaseAddr = MQ_CACHED_ADDR (ProtOffset + SharedRamOffset -
                                                MQ_VOIP_MAX_DSP_CODE_SIZE) ;
   IoAdapter->MaxDspCodeSize = MQ_VOIP_MAX_DSP_CODE_SIZE ;
  }
  else if ( IoAdapter->features & PROTCAP_V90D )
  {
   IoAdapter->DspCodeBaseAddr = MQ_CACHED_ADDR (ProtOffset + SharedRamOffset -
                                                MQ_V90D_MAX_DSP_CODE_SIZE) ;
   IoAdapter->MaxDspCodeSize = (IoAdapter->ControllerNumber == MQ_INSTANCE_COUNT - 1) ?
                               MQ_V90D_MAX_DSP_CODE_SIZE : 0 ;
  }
  else
  {
   IoAdapter->DspCodeBaseAddr = MQ_CACHED_ADDR (ProtOffset + SharedRamOffset -
                                                MQ_ORG_MAX_DSP_CODE_SIZE) ;
   IoAdapter->MaxDspCodeSize = (IoAdapter->ControllerNumber == MQ_INSTANCE_COUNT - 1) ?
                               MQ_ORG_MAX_DSP_CODE_SIZE : 0 ;
  }
  IoAdapter->InitialDspInfo = (MQ_CACHED_ADDR (ProtOffset + SharedRamOffset - MQ_ORG_MAX_DSP_CODE_SIZE)
                            - IoAdapter->DspCodeBaseAddr) >> 14 ;
 }
 DBG_LOG(("%d: DSP code base 0x%08lx, max size 0x%08lx (%08lx,%02x)",
          IoAdapter->ControllerNumber,
          IoAdapter->DspCodeBaseAddr, IoAdapter->MaxDspCodeSize,
          Addr, IoAdapter->InitialDspInfo))
 if (FileLength > ((IoAdapter->DspCodeBaseAddr -
                     MQ_CACHED_ADDR (ProtOffset)) & (IoAdapter->MemorySize - 1)) )
 {
  xdiFreeFile (File) ;
  DBG_FTL(("Protocol code '%s' too long (%ld)",
           &IoAdapter->Protocol[0], FileLength))
  return (0) ;
 }
 IoAdapter->downloadAddr = 0 ;
 sharedRam = (dword *)DOWNLOAD_ADDR(IoAdapter) ;
 memcpy (sharedRam, File, FileLength) ;
 DBG_TRC(("Download addr 0x%08x len %ld - virtual 0x%08x",
          IoAdapter->downloadAddr, FileLength, sharedRam))
 if ( memcmp (sharedRam, File, FileLength) )
 {
  DBG_FTL(("%s: Memory test failed!", IoAdapter->Properties.Name))
  xdiFreeFile (File) ;
  return (0) ;
 }
 xdiFreeFile (File) ;
 return (1) ;
}
/* --------------------------------------------------------------------------
  DSP Code download
  -------------------------------------------------------------------------- */
static long qBri_download_buffer (OsFileHandle *fp, long length, void **addr) {
 PISDN_ADAPTER BaseIoAdapter = (PISDN_ADAPTER)fp->sysLoadDesc ;
 PISDN_ADAPTER IoAdapter;
 word        i ;
 dword       *sharedRam ;
 i = 0 ;
 do
 {
  IoAdapter = BaseIoAdapter->QuadroList->QuadroAdapter[i++] ;
 } while ( (i < MQ_INSTANCE_COUNT)
        && (((dword) length) > IoAdapter->DspCodeBaseAddr +
                 IoAdapter->MaxDspCodeSize - IoAdapter->downloadAddr) );
 *addr = (void *)IoAdapter->downloadAddr ;
 if ( ((dword) length) > IoAdapter->DspCodeBaseAddr +
                         IoAdapter->MaxDspCodeSize - IoAdapter->downloadAddr )
 {
  DBG_FTL(("%s: out of card memory during DSP download (0x%X)",
           IoAdapter->Properties.Name,
           IoAdapter->downloadAddr + length))
  return (-1) ;
 }
 sharedRam = (dword*)(&BaseIoAdapter->ram[IoAdapter->downloadAddr &
                                          (IoAdapter->MemorySize - 1)]) ;
 if ( fp->sysFileRead (fp, sharedRam, length) != length )
  return (-1) ;
 IoAdapter->downloadAddr += length ;
 IoAdapter->downloadAddr  = (IoAdapter->downloadAddr + 3) & (~3) ;
 return (0) ;
}
/******************************************************************************/
static dword qBri_telindus_load (PISDN_ADAPTER BaseIoAdapter) {
 PISDN_ADAPTER        IoAdapter ;
 PISDN_ADAPTER        HighIoAdapter = NULL ;
 char                *error ;
 OsFileHandle        *fp ;
 t_dsp_portable_desc  download_table[DSP_MAX_DOWNLOAD_COUNT] ;
 word                 download_count, i ;
 dword               *sharedRam ;
 dword                FileLength ;
 if ( !(fp = OsOpenFile (DSP_TELINDUS_FILE)) )
  return (0) ;
 for ( i = 0 ; i < MQ_INSTANCE_COUNT ; ++i )
 {
  IoAdapter = BaseIoAdapter->QuadroList->QuadroAdapter[i] ;
  IoAdapter->downloadAddr = IoAdapter->DspCodeBaseAddr ;
  if ( IoAdapter->ControllerNumber == MQ_INSTANCE_COUNT - 1 )
  {
   HighIoAdapter = IoAdapter ;
   HighIoAdapter->downloadAddr = (HighIoAdapter->downloadAddr
              + sizeof(dword) + sizeof(download_table) + 3) & (~3) ;
  }
 }
 FileLength      = fp->sysFileSize ;
 fp->sysLoadDesc = (void *)BaseIoAdapter ;
 fp->sysCardLoad = qBri_download_buffer ;
 download_count = DSP_MAX_DOWNLOAD_COUNT ;
 memset (&download_table[0], '\0', sizeof(download_table)) ;
/*
 * set start address for download
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
 * store # of download files extracted from the archive and download table
 */
 HighIoAdapter->downloadAddr = HighIoAdapter->DspCodeBaseAddr ;
 sharedRam = (dword *)(&BaseIoAdapter->ram[HighIoAdapter->downloadAddr &
                                           (IoAdapter->MemorySize - 1)]) ;
 sharedRam[0] = (dword)download_count ;
 memcpy (&sharedRam[1], &download_table[0], sizeof(download_table)) ;
 return (FileLength) ;
}
/* --------------------------------------------------------------------------
  Load Card
  -------------------------------------------------------------------------- */
static int load_qBri_hardware (PISDN_ADAPTER IoAdapter) {
 dword         i, offset, controller ;
 word         *signature ;
 PISDN_ADAPTER Slave ;
 if ( !IoAdapter->QuadroList
   || ( (IoAdapter->cardType != CARDTYPE_DIVASRV_Q_8M_PCI)
     && (IoAdapter->cardType != CARDTYPE_DIVASRV_VOICE_Q_8M_PCI)
     && (IoAdapter->cardType != CARDTYPE_DIVASRV_Q_8M_V2_PCI)
     && (IoAdapter->cardType != CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI) ) )
 {
  return (0) ;
 }
/*
 * Check for first instance
 */
 if ( IoAdapter->ControllerNumber > 0 )
  return (1) ;
/*
 * first initialize the onboard FPGA
 */
 if ( !qBri_FPGA_download (IoAdapter) )
  return (0) ;
/*
 * download protocol code for all instances
 */
 controller = MQ_INSTANCE_COUNT ;
 do
 {
  controller-- ;
  i = 0 ;
  while ( IoAdapter->QuadroList->QuadroAdapter[i]->ControllerNumber != controller )
   i++ ;
/*
 * calculate base address for instance
 */
  Slave          = IoAdapter->QuadroList->QuadroAdapter[i] ;
  offset         = Slave->ControllerNumber * (IoAdapter->MemorySize >> 2) ;
  Slave->Address = &IoAdapter->Address[offset] ;
  Slave->ram     = &IoAdapter->ram[offset] ;
  Slave->reset   = IoAdapter->reset ;
  Slave->ctlReg  = IoAdapter->ctlReg ;
  Slave->prom    = IoAdapter->prom ;
  Slave->reset   = IoAdapter->reset ;
  if ( !qBri_protocol_load (IoAdapter, Slave) )
   return (0) ;
 } while (controller != 0) ;
/*
 * download only one copy of the DSP code
 */
 if ( !qBri_telindus_load (IoAdapter) )
  return (0) ;
/*
 * copy configuration parameters
 */
 for ( i = 0 ; i < 4 ; ++i )
 {
  Slave = IoAdapter->QuadroList->QuadroAdapter[i] ;
  Slave->ram += (IoAdapter->MemorySize >> 2) - MQ_SHARED_RAM_SIZE ;
  DBG_TRC(("Configure instance %d shared memory @ 0x%08lx",
           Slave->ControllerNumber, Slave->ram))
  memset (Slave->ram, '\0', 256) ;
  diva_configure_protocol (Slave);
 }
/*
 * start adapter
 */
 start_qBri_hardware (IoAdapter) ;
 signature = (word *)(&IoAdapter->ram[0x1E]) ;
/*
 * wait for signature in shared memory (max. 3 seconds)
 */
 for ( i = 0 ; i < 300 ; ++i )
 {
  diva_os_wait (10) ;
  if ( signature[0] == 0x4447 )
  {
   DBG_TRC(("Protocol startup time %d.%02d seconds",
            (i / 100), (i % 100) ))
   return (1) ;
  }
 }
 DBG_FTL(("%s: Adapter selftest failed (0x%04X)!",
          IoAdapter->Properties.Name, signature[0] >> 16))
 qBri_cpu_trapped (IoAdapter) ;
 return (FALSE) ;
}
#else  /* } { */
static int load_qBri_hardware (PISDN_ADAPTER IoAdapter) {
 return (0);
}
#endif /* } */
/* --------------------------------------------------------------------------
  Card ISR
  -------------------------------------------------------------------------- */
static int qBri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
 dword volatile     *qBriIrq ;
 PADAPTER_LIST_ENTRY QuadroList = IoAdapter->QuadroList ;
 word               i ;
 int               serviced = 0 ;
 if ( !(IoAdapter->reset[PLX9054_INTCSR] & 0x80) )
  return (0) ;
 /*
  * clear interrupt line (reset Local Interrupt Test Register)
  */
 qBriIrq = (dword volatile *)(&IoAdapter->ctlReg[ \
              DIVA_4BRI_REVISION(IoAdapter) ? \
                (MQ2_BREG_IRQ_TEST)  : (MQ_BREG_IRQ_TEST)]) ;
 *qBriIrq = MQ_IRQ_REQ_OFF ;
 for ( i = 0 ; i < 4 ; ++i )
 {
  IoAdapter = QuadroList->QuadroAdapter[i] ;
  if ( IoAdapter && IoAdapter->Initialized
    && IoAdapter->tst_irq (&IoAdapter->a) )
  {
   IoAdapter->IrqCount++ ;
   serviced = 1 ;
   diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
  }
 }
 return (serviced) ;
}
/* --------------------------------------------------------------------------
  Does disable the interrupt on the card
  -------------------------------------------------------------------------- */
static void disable_qBri_interrupt (PISDN_ADAPTER IoAdapter) {
 dword volatile *qBriIrq ;
 if ( IoAdapter->ControllerNumber > 0 )
  return ;
 qBriIrq = (dword volatile *)(&IoAdapter->ctlReg[ \
              DIVA_4BRI_REVISION(IoAdapter) ? \
                 (MQ2_BREG_IRQ_TEST)  : (MQ_BREG_IRQ_TEST)]) ;
/*
 * clear interrupt line (reset Local Interrupt Test Register)
 */
 IoAdapter->reset[PLX9054_INTCSR] = 0x00 ; // disable PCI interrupts
 *qBriIrq   = MQ_IRQ_REQ_OFF ;
}
/* --------------------------------------------------------------------------
  Install Adapter Entry Points
  -------------------------------------------------------------------------- */
static void set_common_qBri_functions (PISDN_ADAPTER IoAdapter) {
 ADAPTER *a;
 a = &IoAdapter->a ;
 a->ram_in           = mem_in ;
 a->ram_inw          = mem_inw ;
 a->ram_in_buffer    = mem_in_buffer ;
 a->ram_look_ahead   = mem_look_ahead ;
 a->ram_out          = mem_out ;
 a->ram_outw         = mem_outw ;
 a->ram_out_buffer   = mem_out_buffer ;
 a->ram_inc          = mem_inc ;
 IoAdapter->out      = pr_out ;
 IoAdapter->dpc      = pr_dpc ;
 IoAdapter->tst_irq  = scom_test_int ;
 IoAdapter->clr_irq  = scom_clear_int ;
 IoAdapter->pcm      = (struct pc_maint *)MIPS_MAINT_OFFS ;
 IoAdapter->load     = load_qBri_hardware ;
 IoAdapter->disIrq   = disable_qBri_interrupt ;
 IoAdapter->rstFnc   = reset_qBri_hardware ;
 IoAdapter->stop     = stop_qBri_hardware ;
 IoAdapter->trapFnc  = qBri_cpu_trapped ;
 IoAdapter->diva_isr_handler = qBri_ISR;
 IoAdapter->a.io       = (void*)IoAdapter ;
}
static void set_qBri_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MQ_MEMORY_SIZE ;
 set_common_qBri_functions (IoAdapter) ;
 diva_os_set_qBri_functions (IoAdapter) ;
}
static void set_qBri2_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MQ2_MEMORY_SIZE ;
 set_common_qBri_functions (IoAdapter) ;
 diva_os_set_qBri2_functions (IoAdapter) ;
}
/******************************************************************************/
void
prepare_qBri_functions (PISDN_ADAPTER IoAdapter)
{
 set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[0]) ;
 set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[1]) ;
 set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[2]) ;
 set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[3]) ;
}
void
prepare_qBri2_functions (PISDN_ADAPTER IoAdapter)
{
 set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[0]) ;
 set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[1]) ;
 set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[2]) ;
 set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[3]) ;
}
/* -------------------------------------------------------------------------- */
