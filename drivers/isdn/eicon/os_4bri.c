/* $Id$ */

#include "platform.h"
#include <stdarg.h>
#include "debuglib.h"
#include "cardtype.h"
#include "dlist.h"
#include "pc.h"
#include "pr_pc.h"
#include "di_defs.h"
#include "dsp_defs.h"
#include "di.h" 
#include "io.h"

#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "os_4bri.h"
#include "diva_pci.h"
#include "mi_pc.h"
#include "dsrv4bri.h"

void* diva_xdiLoadFileFile   = 0;
dword diva_xdiLoadFileLength = 0;

/*
**  IMPORTS
*/
extern void prepare_qBri_functions (PISDN_ADAPTER IoAdapter);
extern void prepare_qBri2_functions (PISDN_ADAPTER IoAdapter);
extern void diva_xdi_display_adapter_features (int card);
extern void diva_add_slave_adapter (diva_os_xdi_adapter_t* a);

extern int qBri_FPGA_download (PISDN_ADAPTER IoAdapter);
extern void start_qBri_hardware (PISDN_ADAPTER IoAdapter);

extern int diva_card_read_xlog (diva_os_xdi_adapter_t* a);

/*
**  LOCALS
*/
static unsigned long _4bri_bar_length[4] = {
  0x100,
  0x100, /* I/O */
  MQ_MEMORY_SIZE,
  0x2000
};
static unsigned long _4bri_v2_bar_length[4] = {
  0x100,
  0x100, /* I/O */
  MQ2_MEMORY_SIZE,
  0x10000
};

static int diva_4bri_cleanup_adapter (diva_os_xdi_adapter_t* a);
static int _4bri_get_serial_number (diva_os_xdi_adapter_t* a);
static int diva_4bri_cmd_card_proc (struct _diva_os_xdi_adapter* a,
                                    diva_xdi_um_cfg_cmd_t* cmd,
                                    int length);
static int diva_4bri_cleanup_slave_adapters (diva_os_xdi_adapter_t* a);
static int diva_4bri_write_fpga_image (diva_os_xdi_adapter_t* a,
                                       byte* data,
                                       dword length);
static int diva_4bri_reset_adapter (PISDN_ADAPTER IoAdapter);
static int diva_4bri_write_sdram_block (PISDN_ADAPTER IoAdapter,
                                        dword address,
                                        const byte* data,
                                        dword length,
                                        dword limit);
static int diva_4bri_start_adapter (PISDN_ADAPTER IoAdapter,
                                    dword start_address,
                                    dword features);
static int check_qBri_interrupt (PISDN_ADAPTER IoAdapter);
static int diva_4bri_stop_adapter (diva_os_xdi_adapter_t* a);

static int
_4bri_is_rev_2_card (int card_ordinal)
{
  switch (card_ordinal) {
    case CARDTYPE_DIVASRV_Q_8M_V2_PCI:
    case CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI:
      return (1);
  }
  return (0);
}

/*
**  BAR0 - MEM - 0x100    - CONFIG MEM
**  BAR1 - I/O - 0x100    - UNUSED
**  BAR2 - MEM - MQ_MEMORY_SIZE (MQ2_MEMORY_SIZE on Rev.2) - SDRAM
**  BAR3 - MEM - 0x2000 (0x10000 on Rev.2)   - CNTRL
**
**  Called by master adapter, that will initialize and add slave adapters
*/
int
diva_4bri_init_card (diva_os_xdi_adapter_t* a)
{
  int bar, i;
  PADAPTER_LIST_ENTRY quadro_list;
  diva_os_xdi_adapter_t* current;
  diva_os_xdi_adapter_t* adapter_list[4];
  PISDN_ADAPTER Slave;
  dword offset;
  unsigned long bar_length[sizeof(_4bri_bar_length)/sizeof(_4bri_bar_length[0])];
  int v2 = _4bri_is_rev_2_card (a->CardOrdinal);

  if (v2) {
    memcpy (bar_length, _4bri_v2_bar_length, sizeof(bar_length));
  } else {
    memcpy (bar_length, _4bri_bar_length, sizeof(bar_length));
  }

  /*
    Get Serial Number
    The serial number of 4BRI is accessible in accordance with PCI spec
    via command register located in configuration space, also we do not
    have to map any BAR before we can access it
  */
  if (!_4bri_get_serial_number (a)) {
    DBG_ERR(("A: 4BRI can't ger Serial Number"))
    diva_4bri_cleanup_adapter (a);
    return (-1);
  }

  /*
    Set properties
  */
  a->xdi_adapter.Properties = CardProperties[a->CardOrdinal];
  DBG_LOG(("Load %s, SN:%ld, bus:%02x, func:%02x",
            a->xdi_adapter.Properties.Name,
            a->xdi_adapter.serialNo,
            a->resources.pci.bus,
            a->resources.pci.func))

  /*
    First initialization step: get and check hardware resoures.
    Do not map resources and do not access card at this step
  */

  for (bar = 0; bar < 4; bar++) {
    a->resources.pci.bar[bar] = divasa_get_pci_bar (a->resources.pci.bus,
                                                    a->resources.pci.func,
                                                    bar,
                                                    a->resources.pci.hdev);
    if (!a->resources.pci.bar[bar] ||
        (a->resources.pci.bar[bar] == 0xFFFFFFF0)) {
      DBG_ERR(("A: invalid bar[%d]=%08x", bar, a->resources.pci.bar[bar]))
      return (-1);
    }
  }
  a->resources.pci.irq = (byte)divasa_get_pci_irq (a->resources.pci.bus,
                                                   a->resources.pci.func,
                                                   a->resources.pci.hdev);
  if (!a->resources.pci.irq) {
    DBG_ERR(("A: invalid irq"));
    return (-1);
  }

  /*
    Map all MEMORY BAR's
  */
  for (bar = 0; bar < 4; bar++) {
    if (bar != 1) { /* ignore I/O */
      a->resources.pci.addr[bar] = divasa_remap_pci_bar (a->resources.pci.bar[bar],
                                                         bar_length[bar]);
      if (!a->resources.pci.addr[bar]) {
        DBG_ERR(("A: 4BRI: can't map bar[%d]", bar))
        diva_4bri_cleanup_adapter (a);
        return (-1);
      }
    }
  }

  /*
    Register I/O port
  */
  sprintf (&a->port_name[0], "DIVA 4BRI %ld", (long)a->xdi_adapter.serialNo);

  if (diva_os_register_io_port (1, a->resources.pci.bar[1],
                                bar_length[1],
                                &a->port_name[0])) {
    DBG_ERR(("A: 4BRI: can't register bar[1]"))
    diva_4bri_cleanup_adapter (a);
    return (-1);
  }

  a->resources.pci.addr[1] = (void*)a->resources.pci.bar[1];

  /*
    Set cleanup pointer for base adapter only, so slave adapter
    will be unable to get cleanup
  */
  a->interface.cleanup_adapter_proc = diva_4bri_cleanup_adapter;

  /*
    Create slave adapters
  */
  if (!(a->slave_adapters[0] = (diva_os_xdi_adapter_t*)diva_os_malloc (0, sizeof(*a)))) {
    diva_4bri_cleanup_adapter (a);
    return (-1);
  }
  if (!(a->slave_adapters[1] = (diva_os_xdi_adapter_t*)diva_os_malloc (0, sizeof(*a)))) {
    diva_os_free (0, a->slave_adapters[0]);
    a->slave_adapters[0] = 0;
    diva_4bri_cleanup_adapter (a);
    return (-1);
  }
  if (!(a->slave_adapters[2] = (diva_os_xdi_adapter_t*)diva_os_malloc (0, sizeof(*a)))) {
    diva_os_free (0, a->slave_adapters[0]);
    diva_os_free (0, a->slave_adapters[1]);
    a->slave_adapters[0] = 0;
    a->slave_adapters[1] = 0;
    diva_4bri_cleanup_adapter (a);
    return (-1);
  }
  memset (a->slave_adapters[0], 0x00, sizeof(*a));
  memset (a->slave_adapters[1], 0x00, sizeof(*a));
  memset (a->slave_adapters[2], 0x00, sizeof(*a));

  adapter_list[0] = a;
  adapter_list[1] = a->slave_adapters[0];
  adapter_list[2] = a->slave_adapters[1];
  adapter_list[3] = a->slave_adapters[2];

  /*
    Allocate slave list
  */
  quadro_list = (PADAPTER_LIST_ENTRY)diva_os_malloc (0, sizeof(*quadro_list));
  if (!(a->slave_list = quadro_list)) {
    for (i = 0; i < 3; i++) {
      diva_os_free (0, a->slave_adapters[bar]);
      a->slave_adapters[bar] = 0;
    }
    diva_4bri_cleanup_adapter (a);
    return (-1);
  }
  memset (quadro_list, 0x00, sizeof(*quadro_list));

  /*
    Set interfaces
  */
  a->xdi_adapter.QuadroList = quadro_list;
  for (i = 0; i < 4; i++) {
    adapter_list[i]->xdi_adapter.ControllerNumber = i;
    quadro_list->QuadroAdapter[i] = &adapter_list[i]->xdi_adapter;
  }

  for (i = 0; i < 4; i++) {
    current = adapter_list[i];

    current->dsp_mask = 0x00000003;

    current->xdi_adapter.a.io = &current->xdi_adapter;
    current->xdi_adapter.DIRequest = request;
    current->interface.cmd_proc = diva_4bri_cmd_card_proc;
    current->xdi_adapter.Properties = CardProperties[a->CardOrdinal];
    current->CardOrdinal = a->CardOrdinal;

    current->xdi_adapter.Channels = CardProperties[a->CardOrdinal].Channels;
    current->xdi_adapter.e_max = CardProperties[a->CardOrdinal].E_info;
    current->xdi_adapter.e_tbl = diva_os_malloc (0, current->xdi_adapter.e_max * sizeof(E_INFO)); 

    if (!current->xdi_adapter.e_tbl) {
      diva_4bri_cleanup_slave_adapters (a);
      diva_4bri_cleanup_adapter (a);
      for (i = 1; i < 3; i++) { diva_os_free (0, adapter_list[i]); }
      return (-1);
    }
    memset (current->xdi_adapter.e_tbl, 0x00, current->xdi_adapter.e_max * sizeof(E_INFO));

    if (diva_os_initialize_spin_lock (&current->xdi_adapter.isr_spin_lock, "isr")) {
      diva_4bri_cleanup_slave_adapters (a);
      diva_4bri_cleanup_adapter (a);
      for (i = 1; i < 3; i++) { diva_os_free (0, adapter_list[i]); }
      return (-1);
    }
    if (diva_os_initialize_spin_lock (&current->xdi_adapter.data_spin_lock, "data")) {
      diva_4bri_cleanup_slave_adapters (a);
      diva_4bri_cleanup_adapter (a);
      for (i = 1; i < 3; i++) { diva_os_free (0, adapter_list[i]); }
      return (-1);
    }

    strcpy (current->xdi_adapter.req_soft_isr.dpc_thread_name, "kdivas4brid");

    if (diva_os_initialize_soft_isr (&current->xdi_adapter.req_soft_isr,
                                     DIDpcRoutine,
                                     &current->xdi_adapter)) {
      diva_4bri_cleanup_slave_adapters (a);
      diva_4bri_cleanup_adapter (a);
      for (i = 1; i < 3; i++) { diva_os_free (0, adapter_list[i]); }
      return (-1);
    }

    /*
     Do not initialize second DPC - only one thread will be created
    */
    current->xdi_adapter.isr_soft_isr.object = current->xdi_adapter.req_soft_isr.object;
  }

  if (v2) {
       prepare_qBri2_functions (&a->xdi_adapter);
  } else {
       prepare_qBri_functions (&a->xdi_adapter);
  }

  /*
    Set up hardware related pointers
  */
  a->xdi_adapter.cfg = (void*)a->resources.pci.bar[0]; /* BAR0 CONFIG */
  a->xdi_adapter.port = (void*)a->resources.pci.bar[1]; /* BAR1        */
  a->xdi_adapter.Address = a->resources.pci.addr[2];        /* BAR2 SDRAM  */
  a->xdi_adapter.ctlReg = (void*)a->resources.pci.bar[3]; /* BAR3 CNTRL  */

  a->xdi_adapter.reset = a->resources.pci.addr[0]; /* BAR0 CONFIG */
  a->xdi_adapter.ram = a->resources.pci.addr[2]; /* BAR2 SDRAM  */
  /*
    ctlReg contains the register address for the MIPS CPU reset control
  */
  a->xdi_adapter.ctlReg = a->resources.pci.addr[3];  /* BAR3 CNTRL  */
  /*
    prom contains the register address for FPGA and EEPROM programming
  */
  a->xdi_adapter.prom = &a->xdi_adapter.reset[0x6E];
  /*
    reset contains the base address for the PLX 9054 register set
  */
  a->xdi_adapter.reset[PLX9054_INTCSR] = 0x00 ;	/* disable PCI interrupts */

  /*
    Replicate addresses to all instances, set shared memory
    address for all instances
  */
  for (i = 0; i < 4; i++) {
    Slave = a->xdi_adapter.QuadroList->QuadroAdapter[i];
    offset = Slave->ControllerNumber * (a->xdi_adapter.MemorySize>>2);
    Slave->Address = &a->xdi_adapter.Address[offset];
    Slave->ram	= &a->xdi_adapter.ram[offset];
    Slave->reset = a->xdi_adapter.reset;
    Slave->ctlReg = a->xdi_adapter.ctlReg;
    Slave->prom	= a->xdi_adapter.prom;
    Slave->reset = a->xdi_adapter.reset;
  }
  for (i = 0; i < 4; i++) {
    Slave = a->xdi_adapter.QuadroList->QuadroAdapter[i];
    Slave->ram += ((a->xdi_adapter.MemorySize >> 2) - MQ_SHARED_RAM_SIZE);
  }
  for (i = 1; i < 4; i++) {
    Slave = a->xdi_adapter.QuadroList->QuadroAdapter[i];
    Slave->serialNo = ((dword)(Slave->ControllerNumber << 24)) | a->xdi_adapter.serialNo;
    Slave->cardType = a->xdi_adapter.cardType;
  }

  /*
    Set IRQ handler
  */
  a->xdi_adapter.irq_info.irq_nr = a->resources.pci.irq;
  sprintf (a->xdi_adapter.irq_info.irq_name, "DIVA 4BRI %ld", (long)a->xdi_adapter.serialNo);

  if (diva_os_register_irq (a, a->xdi_adapter.irq_info.irq_nr,
                            a->xdi_adapter.irq_info.irq_name)) {
    diva_4bri_cleanup_slave_adapters (a);
    diva_4bri_cleanup_adapter (a);
    for (i = 1; i < 3; i++) { diva_os_free (0, adapter_list[i]); }
    return (-1);
  }

  a->xdi_adapter.irq_info.registered = 1;

  /*
    Add three slave adapters
  */
  diva_add_slave_adapter (adapter_list[1]);
  diva_add_slave_adapter (adapter_list[2]);
  diva_add_slave_adapter (adapter_list[3]);

  diva_log_info("%s IRQ:%d SerNo:%d", a->xdi_adapter.Properties.Name,
                                      a->resources.pci.irq,
                                      a->xdi_adapter.serialNo);

  return (0);
}

/*
**  Cleanup function will be called for master adapter only
**  this is garanteed by design: cleanup callback is set
**  by master adapter only
*/
static int
diva_4bri_cleanup_adapter (diva_os_xdi_adapter_t* a)
{
  int bar;

  /*
    Stop adapter if running
  */
  if (a->xdi_adapter.Initialized) {
    diva_4bri_stop_adapter (a);
  }

  /*
    Remove IRQ handler
  */
  if (a->xdi_adapter.irq_info.registered) {
    diva_os_remove_irq (a, a->xdi_adapter.irq_info.irq_nr);
  }
  a->xdi_adapter.irq_info.registered = 0;

  /*
    Free DPC's and spin locks on all adapters
  */
  diva_4bri_cleanup_slave_adapters (a);

  /*
    Unmap all BARS
  */
  for (bar = 0; bar < 4; bar++) {
    if (bar != 1) {
      if (a->resources.pci.bar[bar] && a->resources.pci.addr[bar]) {
        divasa_unmap_pci_bar (a->resources.pci.addr[bar]);
        a->resources.pci.bar[bar]  = 0;
        a->resources.pci.addr[bar] = 0;
      }
    }
  }

  /*
    Unregister I/O
  */
  if (a->resources.pci.bar[1] && a->resources.pci.addr[1]) {
    diva_os_register_io_port (0, a->resources.pci.bar[1], 
      _4bri_is_rev_2_card (a->CardOrdinal) ? _4bri_v2_bar_length[1] : _4bri_bar_length[1],
      &a->port_name[0]);
    a->resources.pci.bar[1]  = 0;
    a->resources.pci.addr[1] = 0;
  }

  if (a->slave_list) {
    diva_os_free (0, a->slave_list);
    a->slave_list = 0;
  }

  return (0);
}

static int
_4bri_get_serial_number (diva_os_xdi_adapter_t* a)
{
  dword data[64] ;
  dword serNo ;
  word addr, status, i, j ;
  byte Bus, Slot;
  void* hdev;

  Bus = a->resources.pci.bus;
  Slot = a->resources.pci.func;
  hdev = a->resources.pci.hdev;

  for ( i = 0 ; i < 64 ; ++i )
  {
    addr = i * 4 ;
    for ( j = 0 ; j < 5; ++j )
    {
      PCIwrite (Bus, Slot, 0x4E, &addr, sizeof(addr), hdev) ;
      diva_os_wait (1) ;
      PCIread (Bus, Slot, 0x4E, &status, sizeof(status), hdev) ;
      if (status & 0x8000)
        break ;
    }
    if (j >= 5){
      DBG_ERR(("EEPROM[%d] read failed (0x%x)", i * 4, addr))
      return(0) ;
    }
    PCIread (Bus, Slot, 0x50, &data[i], sizeof (data[i]), hdev) ;
  }
  DBG_BLK(( (char *)&data[0], sizeof(data) ))

  serNo  = data[32] ;
  if (serNo == 0 || serNo == 0xffffffff)
    serNo  = data[63] ;

  if (!serNo) {
    DBG_LOG(("W: Serial Number == 0, create one serial number"));
    serNo  = a->resources.pci.bar[1] & 0xffff0000;
    serNo |= a->resources.pci.bus << 8;
    serNo |= a->resources.pci.func;
  }
	
  a->xdi_adapter.serialNo = serNo;

  DBG_REG(("Serial No.          : %ld", a->xdi_adapter.serialNo))

  return (serNo) ;
}

/*
**  Release resources of slave adapters
*/
static int
diva_4bri_cleanup_slave_adapters (diva_os_xdi_adapter_t* a)
{
  diva_os_xdi_adapter_t* adapter_list[4];
  diva_os_xdi_adapter_t* current;
  int i;

  adapter_list[0] = a;
  adapter_list[1] = a->slave_adapters[0];
  adapter_list[2] = a->slave_adapters[1];
  adapter_list[3] = a->slave_adapters[2];

  for (i = 0; i < 4; i++) {
    current = adapter_list[i];
    if (current) {
      diva_os_destroy_spin_lock (&current->xdi_adapter.isr_spin_lock, "unload");
      diva_os_destroy_spin_lock (&current->xdi_adapter.data_spin_lock,"unload");

      diva_os_cancel_soft_isr (&current->xdi_adapter.req_soft_isr);
      diva_os_cancel_soft_isr (&current->xdi_adapter.isr_soft_isr);

      diva_os_remove_soft_isr (&current->xdi_adapter.req_soft_isr);
      current->xdi_adapter.isr_soft_isr.object = 0;

      if (current->xdi_adapter.e_tbl) {
        diva_os_free (0, current->xdi_adapter.e_tbl);
      }
      current->xdi_adapter.e_tbl   = 0;
      current->xdi_adapter.e_max   = 0;
      current->xdi_adapter.e_count = 0;
    }
  }

  return (0);
}

static int
diva_4bri_cmd_card_proc (struct _diva_os_xdi_adapter* a,
                         diva_xdi_um_cfg_cmd_t* cmd,
                         int length)
{
  int ret = -1;

  if (cmd->adapter != a->controller) {
    DBG_ERR(("A: 4bri_cmd, invalid controller=%d != %d",
                  cmd->adapter, a->controller))
    return (-1);
  }

  switch (cmd->command) {
    case DIVA_XDI_UM_CMD_GET_CARD_ORDINAL:
      a->xdi_mbox.data_length = sizeof(dword);
      a->xdi_mbox.data = diva_os_malloc (0, a->xdi_mbox.data_length);
      if (a->xdi_mbox.data) {
        *(dword*)a->xdi_mbox.data = (dword)a->CardOrdinal;
        a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
        ret = 0;
      }
      break;

    case DIVA_XDI_UM_CMD_GET_SERIAL_NR:
      a->xdi_mbox.data_length = sizeof(dword);
      a->xdi_mbox.data = diva_os_malloc (0, a->xdi_mbox.data_length);
      if (a->xdi_mbox.data) {
        *(dword*)a->xdi_mbox.data = (dword)a->xdi_adapter.serialNo;
        a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
        ret = 0;
      }
      break;

    case DIVA_XDI_UM_CMD_GET_PCI_HW_CONFIG:
      if (!a->xdi_adapter.ControllerNumber) {
        /*
          Only master adapter can access hardware config
        */
        a->xdi_mbox.data_length = sizeof(dword)*9;
        a->xdi_mbox.data = diva_os_malloc (0, a->xdi_mbox.data_length);
        if (a->xdi_mbox.data) {
          int i;
          dword* data = (dword*)a->xdi_mbox.data;

          for (i = 0; i < 8; i++) {
            *data++ = a->resources.pci.bar[i];
          }
          *data++ = (dword)a->resources.pci.irq;
          a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
          ret = 0;
        }
      }
      break;

    case DIVA_XDI_UM_CMD_GET_CARD_STATE:
      if (!a->xdi_adapter.ControllerNumber) {
        a->xdi_mbox.data_length = sizeof(dword);
        a->xdi_mbox.data = diva_os_malloc (0, a->xdi_mbox.data_length);
        if (a->xdi_mbox.data) {
          dword* data = (dword*)a->xdi_mbox.data;
          if (!a->xdi_adapter.ram || !a->xdi_adapter.reset ||
              !a->xdi_adapter.cfg) {
            *data = 3;
          } else if (a->xdi_adapter.trapped) {
            *data = 2;
          } else if (a->xdi_adapter.Initialized) {
            *data = 1;
          } else {
            *data = 0;
          }
          a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
          ret = 0;
        }
      }
      break;

    case DIVA_XDI_UM_CMD_WRITE_FPGA:
      if (!a->xdi_adapter.ControllerNumber) {
        ret = diva_4bri_write_fpga_image (a, (byte*)&cmd[1], 
                                          cmd->command_data.write_fpga.image_length);
      }
      break;

    case DIVA_XDI_UM_CMD_RESET_ADAPTER:
      if (!a->xdi_adapter.ControllerNumber) {
        ret = diva_4bri_reset_adapter (&a->xdi_adapter);
      }
      break;

    case DIVA_XDI_UM_CMD_WRITE_SDRAM_BLOCK:
      if (!a->xdi_adapter.ControllerNumber) {
        ret = diva_4bri_write_sdram_block (&a->xdi_adapter,
                                           cmd->command_data.write_sdram.offset,
                                           (byte*)&cmd[1],
                                           cmd->command_data.write_sdram.length,
                                           a->xdi_adapter.MemorySize);
      }
      break;

    case DIVA_XDI_UM_CMD_START_ADAPTER:
      if (!a->xdi_adapter.ControllerNumber) {
        ret = diva_4bri_start_adapter (&a->xdi_adapter,
                                       cmd->command_data.start.offset,
                                       cmd->command_data.start.features);
      }
      break;

    case DIVA_XDI_UM_CMD_STOP_ADAPTER:
      if (!a->xdi_adapter.ControllerNumber) {
        ret = diva_4bri_stop_adapter (a);
      }
      break;

    case DIVA_XDI_UM_CMD_READ_XLOG_ENTRY:
      ret = diva_card_read_xlog (a);
      break;

    case DIVA_XDI_UM_CMD_READ_SDRAM:
      if (!a->xdi_adapter.ControllerNumber && a->xdi_adapter.Address) {
        if ((a->xdi_mbox.data_length = cmd->command_data.read_sdram.length)) {
          if ((a->xdi_mbox.data_length+cmd->command_data.read_sdram.offset) < a->xdi_adapter.MemorySize) {
            a->xdi_mbox.data = diva_os_malloc (0, a->xdi_mbox.data_length);
            if (a->xdi_mbox.data) {
              byte* src = a->xdi_adapter.Address;
              byte* dst = a->xdi_mbox.data;
              dword len = a->xdi_mbox.data_length;

              src += cmd->command_data.read_sdram.offset;

              while (len--) {
                *dst++ = *src++;
              }
              a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
              ret = 0;
            }
          }
        }
      }
      break;

    default:
      DBG_ERR(("A: A(%d) invalid cmd=%d", a->controller, cmd->command))
  }

  return (ret);
}

void *
xdiLoadFile (char *FileName, unsigned long *FileLength)
{
  void* ret = diva_xdiLoadFileFile;

  if (FileLength) {
    *FileLength = diva_xdiLoadFileLength;
  }
  diva_xdiLoadFileFile	 = 0;
  diva_xdiLoadFileLength = 0;

  return (ret);
}

void
diva_os_set_qBri_functions (PISDN_ADAPTER IoAdapter)
{
}

void
diva_os_set_qBri2_functions (PISDN_ADAPTER IoAdapter)
{
}

static int
diva_4bri_write_fpga_image (diva_os_xdi_adapter_t* a, byte* data, dword length)
{
  int ret;

  diva_xdiLoadFileFile = data;
  diva_xdiLoadFileLength = length;

  ret = qBri_FPGA_download (&a->xdi_adapter);

  diva_xdiLoadFileFile = 0;
  diva_xdiLoadFileLength = 0;

  return (ret ? 0 : -1);
}

static int
diva_4bri_reset_adapter (PISDN_ADAPTER IoAdapter)
{
  PISDN_ADAPTER Slave;
  int i;

  if (!IoAdapter->Address || !IoAdapter->reset) {
    return (-1);
  }
  if (IoAdapter->Initialized) {
    DBG_ERR(("A: A(%d) can't reset 4BRI adapter - please stop first", IoAdapter->ANum))
    return (-1);
  }
#if 0
  /*
    Reset erases interrupt setting from the
    PCI configuration space
  */
  (*(IoAdapter->rstFnc))(IoAdapter);
#endif

  /*
    Forget all entities on all adapters
  */
  for (i = 0; ((i < 4) && IoAdapter->QuadroList); i++) {
    Slave = IoAdapter->QuadroList->QuadroAdapter[i];
    Slave->e_count =  0;
    if (Slave->e_tbl) {
      memset (Slave->e_tbl, 0x00, Slave->e_max * sizeof(E_INFO));
    }
    Slave->head	= 0;
    Slave->tail = 0;
    Slave->assign = 0;
    Slave->trapped = 0;
  }

  return (0);
}


static int
diva_4bri_write_sdram_block (PISDN_ADAPTER IoAdapter,
                             dword address,
                             const byte* data,
                             dword length,
                             dword limit)
{
  byte* mem = IoAdapter->Address;

  if (((address + length) >= limit) || !mem) {
    DBG_ERR(("A: A(%d) write PRI address=0x%08lx", IoAdapter->ANum, address+length))
    return (-1);
  }
  mem += address;

  while (length--) {
    *mem++ = *data++;
  }

  return (0);
}

static int
diva_4bri_start_adapter (PISDN_ADAPTER IoAdapter,
                         dword start_address,
                         dword features)
{
  volatile word *signature;
  int started = 0;
  int i;

  /*
    start adapter
  */
  start_qBri_hardware (IoAdapter);

  /*
    wait for signature in shared memory (max. 3 seconds)
  */
  signature = (volatile word *)(&IoAdapter->ram[0x1E]) ;

  for ( i = 0 ; i < 300 ; ++i ) {
    diva_os_wait (10) ;
    if (signature[0] == 0x4447) {
      DBG_TRC(("Protocol startup time %d.%02d seconds", (i / 100), (i % 100) ))
      started = 1;
      break;
    }
  }

  if (!started) {
    DBG_FTL(("%s: Adapter selftest failed, signature=%04x",
              IoAdapter->Properties.Name, signature[0]))
      (*(IoAdapter->trapFnc))(IoAdapter);
    IoAdapter->stop(IoAdapter);
    return (-1);
  }

  for (i = 0; i < 4; i++) {
    IoAdapter->QuadroList->QuadroAdapter[i]->Initialized = 1;
    IoAdapter->QuadroList->QuadroAdapter[i]->IrqCount = 0;
  }

  if (check_qBri_interrupt (IoAdapter)) {
    DBG_ERR(("A: A(%d) interrupt test failed", IoAdapter->ANum))
    for (i = 0; i < 4; i++) {
      IoAdapter->QuadroList->QuadroAdapter[i]->Initialized = 0;
    }
    IoAdapter->stop(IoAdapter);
    return (-1);
  }

  IoAdapter->Properties.Features = (word)features;
  diva_xdi_display_adapter_features (IoAdapter->ANum);

  for (i = 0; i < 4; i++) {
    DBG_LOG(("A(%d) 4BRI adapter successfull started",
              IoAdapter->QuadroList->QuadroAdapter[i]->ANum))
    diva_xdi_didd_register_adapter (IoAdapter->QuadroList->QuadroAdapter[i]->ANum);
    IoAdapter->QuadroList->QuadroAdapter[i]->Properties.Features = (word)features;
  }

  return (0);
}

static int
check_qBri_interrupt (PISDN_ADAPTER IoAdapter)
{
#ifdef	SUPPORT_INTERRUPT_TEST_ON_4BRI
  int i ;
  ADAPTER *a = &IoAdapter->a ;

  IoAdapter->IrqCount = 0 ;

  if ( IoAdapter->ControllerNumber > 0 )
    return (-1) ;

  IoAdapter->reset[PLX9054_INTCSR] = PLX9054_INT_ENABLE ;
  /*
    interrupt test
  */
  a->ReadyInt = 1 ;
  a->ram_out (a, &PR_RAM->ReadyInt, 1) ;

  for ( i = 100 ; !IoAdapter->IrqCount && (i-- > 0) ; diva_os_wait(10)) ;

  return ((IoAdapter->IrqCount > 0) ? 0 : -1);
#else
  dword volatile *qBriIrq ;
  /*
    Reset on-board interrupt register
  */
  IoAdapter->IrqCount = 0 ;
  qBriIrq = (dword volatile *)(&IoAdapter->ctlReg[_4bri_is_rev_2_card (IoAdapter->cardType) ? (MQ2_BREG_IRQ_TEST) : (MQ_BREG_IRQ_TEST)]);

  *qBriIrq = MQ_IRQ_REQ_OFF ;

  IoAdapter->reset[PLX9054_INTCSR] = PLX9054_INT_ENABLE ;

  return (0) ;
#endif	/* SUPPORT_INTERRUPT_TEST_ON_4BRI */
}

static void
diva_4bri_clear_interrupts (diva_os_xdi_adapter_t* a)
{
  PISDN_ADAPTER IoAdapter = &a->xdi_adapter;

  /*
    clear any pending interrupt
  */
  IoAdapter->disIrq (IoAdapter) ;

  IoAdapter->tst_irq (&IoAdapter->a) ;
  IoAdapter->clr_irq (&IoAdapter->a) ;
  IoAdapter->tst_irq (&IoAdapter->a) ;

  /*
    kill pending dpcs
  */
  diva_os_cancel_soft_isr (&IoAdapter->req_soft_isr);
  diva_os_cancel_soft_isr (&IoAdapter->isr_soft_isr);
}

static int
diva_4bri_stop_adapter (diva_os_xdi_adapter_t* a)
{
  PISDN_ADAPTER IoAdapter = &a->xdi_adapter;
  int i;

  if (!IoAdapter->ram) {
    return (-1);
  }

  if (!IoAdapter->Initialized) {
    DBG_ERR(("A: A(%d) can't stop PRI adapter - not running", IoAdapter->ANum))
    return (-1); /* nothing to stop */
  }

  for (i = 0; i < 4; i++) {
    IoAdapter->QuadroList->QuadroAdapter[i]->Initialized = 0;
  }

  /*
    Disconnect Adapters from DIDD
  */
  for (i = 0; i < 4; i++) {
    diva_xdi_didd_remove_adapter(IoAdapter->QuadroList->QuadroAdapter[i]->ANum);
  }

  i = 100;

  /*
    Stop interrupts
  */
  a->clear_interrupts_proc = diva_4bri_clear_interrupts;
  IoAdapter->a.ReadyInt = 1;
  IoAdapter->a.ram_inc (&IoAdapter->a, &PR_RAM->ReadyInt) ;
  do {
    diva_os_sleep (10);
  } while (i-- && a->clear_interrupts_proc);
  if (a->clear_interrupts_proc) {
    diva_4bri_clear_interrupts (a);
    a->clear_interrupts_proc = 0;
    DBG_ERR(("A: A(%d) no final interrupt from 4BRI adapter", IoAdapter->ANum))
  }
  IoAdapter->a.ReadyInt = 0;

  /*
    Stop and reset adapter
  */
  IoAdapter->stop (IoAdapter) ;

  return (0);
}

