/* $Id$ */

#define CARDTYPE_H_WANT_DATA            1
#define CARDTYPE_H_WANT_IDI_DATA        0
#define CARDTYPE_H_WANT_RESOURCE_DATA   0
#define CARDTYPE_H_WANT_FILE_DATA       0

#include "platform.h"
#include "debuglib.h"
#include "diva_pci.h"
#include "cardtype.h"
#include "dlist.h"
#include "pc.h"
#include "di_defs.h"
#include "di.h"
#include "io.h"

#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "diva.h"

#include "os_pri.h"
#include "os_4bri.h"
#include "os_bri.h"

PISDN_ADAPTER IoAdapters[MAX_ADAPTER];
extern IDI_CALL Requests[MAX_ADAPTER];

#define DivaIdiReqFunc(N) \
static void DivaIdiRequest##N(ENTITY *e) \
{ if ( IoAdapters[N] ) (* IoAdapters[N]->DIRequest)(IoAdapters[N], e) ; }

/*
**  Create own 32 Adapters
*/
DivaIdiReqFunc(0)
DivaIdiReqFunc(1)
DivaIdiReqFunc(2)
DivaIdiReqFunc(3)
DivaIdiReqFunc(4)
DivaIdiReqFunc(5)
DivaIdiReqFunc(6)
DivaIdiReqFunc(7)
DivaIdiReqFunc(8)
DivaIdiReqFunc(9)
DivaIdiReqFunc(10)
DivaIdiReqFunc(11)
DivaIdiReqFunc(12)
DivaIdiReqFunc(13)
DivaIdiReqFunc(14)
DivaIdiReqFunc(15)
DivaIdiReqFunc(16)
DivaIdiReqFunc(17)
DivaIdiReqFunc(18)
DivaIdiReqFunc(19)
DivaIdiReqFunc(20)
DivaIdiReqFunc(21)
DivaIdiReqFunc(22)
DivaIdiReqFunc(23)
DivaIdiReqFunc(24)
DivaIdiReqFunc(25)
DivaIdiReqFunc(26)
DivaIdiReqFunc(27)
DivaIdiReqFunc(28)
DivaIdiReqFunc(29)
DivaIdiReqFunc(30)
DivaIdiReqFunc(31)

struct pt_regs;

/*
**  LOCALS
*/
diva_entity_queue_t adapter_queue;

typedef struct _diva_supported_cards_info {
  int CardOrdinal;
  diva_init_card_proc_t init_card;
} diva_supported_cards_info_t;

static diva_supported_cards_info_t divas_supported_cards [] = {
  /*
    PRI Cards
  */
  { CARDTYPE_DIVASRV_P_30M_PCI,	 diva_pri_init_card },
  /*
    PRI Rev.2 Cards
  */
  { CARDTYPE_DIVASRV_P_30M_V2_PCI, diva_pri_init_card },
  /*
    PRI Rev.2 VoIP Cards
  */
  { CARDTYPE_DIVASRV_VOICE_P_30M_V2_PCI, diva_pri_init_card },
  /*
    4BRI Rev 1 Cards
  */
  { CARDTYPE_DIVASRV_Q_8M_PCI,	 diva_4bri_init_card },
  { CARDTYPE_DIVASRV_VOICE_Q_8M_PCI, diva_4bri_init_card },
  /*
    4BRI Rev 2 Cards
  */
  { CARDTYPE_DIVASRV_Q_8M_V2_PCI,    diva_4bri_init_card },
  { CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI, diva_4bri_init_card },
  /*
    BRI
  */
  { CARDTYPE_MAESTRA_PCI,		diva_bri_init_card },

  /*
    EOL
  */
  { -1, 0 }
};

static void diva_detect_pci_cards (void);
static void divas_found_pci_card (int handle, unsigned char bus, unsigned char func, void* pci_dev_handle);
static void diva_init_request_array (void);

static diva_os_spin_lock_t adapter_lock;

/*
**  Called on driver load, MAIN, main, DriverEntry
*/
int
divasa_xdi_driver_entry(void)
{
  diva_os_xdi_adapter_t* a;
  int current = 1;
  diva_os_spin_lock_magic_t old_irql;

  diva_os_initialize_spin_lock (&adapter_lock, "adapter");
  diva_q_init(&adapter_queue);
  diva_init_request_array ();

  /*
    Detect PCI Cards
  */
  diva_detect_pci_cards ();

  /*
    Count the controllers
  */
  diva_os_enter_spin_lock (&adapter_lock, &old_irql, "driver_entry");
  a = (diva_os_xdi_adapter_t*)diva_q_get_head(&adapter_queue);
  while (a) {
    IoAdapters[current-1] = &a->xdi_adapter;
    a->controller = current++;
    a->xdi_adapter.ANum   = a->controller;
    a = (diva_os_xdi_adapter_t*)diva_q_get_next(&a->link);
  }
  diva_os_leave_spin_lock (&adapter_lock, &old_irql, "driver_entry");

  /*
    Return TRUE if no cards were found
  */
  return  (current == 1);
}

/*
** Called on driver unload FINIT, finit, Unload
*/
void
divasa_xdi_driver_unload(void)
{
  diva_os_spin_lock_magic_t old_irql;
  diva_os_xdi_adapter_t* a;

  diva_os_enter_spin_lock (&adapter_lock, &old_irql, "driver_unload");
  a = (diva_os_xdi_adapter_t*)diva_q_get_head(&adapter_queue);
  while (a) {
    diva_q_remove (&adapter_queue, &a->link);

    if (a->interface.cleanup_adapter_proc) {
      (*(a->interface.cleanup_adapter_proc))(a);
    }

    diva_os_free (0, a);

    a = (diva_os_xdi_adapter_t*)diva_q_get_head(&adapter_queue);
  }
  diva_os_leave_spin_lock (&adapter_lock, &old_irql, "driver_unload");
  diva_os_destroy_spin_lock (&adapter_lock, "adapter");
}

/*
**  Find and initialize all PCI cards supported by this driver
*/
static void
diva_detect_pci_cards(void)
{
  int i = 1;

  for (i = 0; divas_supported_cards[i].CardOrdinal != -1; i++) {
    divasa_find_card_by_type (CardProperties[divas_supported_cards[i].CardOrdinal].PnPId,
                              divas_found_pci_card, i);
  }
}

/*
**  Called if one PCI card was found
*/
static void
divas_found_pci_card (int handle, unsigned char bus, unsigned char func, void* pci_dev_handle)
{
  diva_os_xdi_adapter_t* a;
  diva_supported_cards_info_t* pI = &divas_supported_cards[handle];
  diva_os_spin_lock_magic_t old_irql;

  DBG_LOG(("found %d-%s bus: %d, func:%d", 
        pI->CardOrdinal, CardProperties[pI->CardOrdinal].Name, bus, func));

  if (!(a = (diva_os_xdi_adapter_t*)diva_os_malloc (0, sizeof(*a)))) {
    DBG_ERR(("A: can't alloc adapter"));
    return;
  }

  memset (a, 0x00, sizeof(*a));

  a->CardIndex = handle;
  a->CardOrdinal = pI->CardOrdinal;
  a->Bus = DIVAS_XDI_ADAPTER_BUS_PCI;
  a->xdi_adapter.cardType = a->CardOrdinal;
  a->resources.pci.bus	= bus;
  a->resources.pci.func = func;
  a->resources.pci.hdev = pci_dev_handle;

  /*
    Add master adapter first, so slave adapters will receive higher
    numbers as master adapter
  */
  diva_os_enter_spin_lock (&adapter_lock, &old_irql, "found_pci_card");
  diva_q_add_tail (&adapter_queue, &a->link);

  if ((*(pI->init_card))(a)) {
    diva_q_remove (&adapter_queue, &a->link);
    diva_os_free (0, a);
    DBG_ERR(("A: can't get adapter resources"));
  }
  diva_os_leave_spin_lock (&adapter_lock, &old_irql, "found_pci_card");
}

/*
**  Receive and process command from user mode utility
*/
static int
cmp_adapter_nr (const void* what, const diva_entity_link_t* p)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t*)p;
  dword nr = (dword)what;

  return (nr != a->controller);
}

void*
diva_xdi_open_adapter (void* os_handle, const void* src,
                       int length, divas_xdi_copy_from_user_fn_t cp_fn)
{
  diva_xdi_um_cfg_cmd_t	msg;
  diva_os_xdi_adapter_t* a;
  diva_os_spin_lock_magic_t old_irql;

  if (length < sizeof(diva_xdi_um_cfg_cmd_t)) {
    DBG_ERR(("A: A(?) open, msg too small (%d < %d)",
            length, sizeof(diva_xdi_um_cfg_cmd_t)))
    return (0);
  }
  if ((*cp_fn)(os_handle, &msg, src, sizeof(msg)) <= 0) {
    DBG_ERR(("A: A(?) open, write error"))
    return (0);
  }
  diva_os_enter_spin_lock (&adapter_lock, &old_irql, "open_adapter");
  a = (diva_os_xdi_adapter_t*)diva_q_find (&adapter_queue,
                                           (void*)msg.adapter, cmp_adapter_nr);
  diva_os_leave_spin_lock (&adapter_lock, &old_irql, "open_adapter");

  if (!a) {
    DBG_ERR(("A: A(%d) open, adapter not found", msg.adapter))
  }
	
  return (a);
}

/*
**  Easy cleanup mailbox status
*/
void
diva_xdi_close_adapter (void* adapter, void* os_handle)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t*)adapter;

  a->xdi_mbox.status &= ~DIVA_XDI_MBOX_BUSY;
  if (a->xdi_mbox.data) {
    diva_os_free (0, a->xdi_mbox.data);
    a->xdi_mbox.data = 0;
  }
}

int
diva_xdi_write (void* adapter, void* os_handle, const void* src,
                int length,divas_xdi_copy_from_user_fn_t cp_fn)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t*)adapter;
  void* data;

  if (a->xdi_mbox.status & DIVA_XDI_MBOX_BUSY) {
    DBG_ERR(("A: A(%d) write, mbox busy", a->controller))
    return (-1);
  }

  if (length < sizeof(diva_xdi_um_cfg_cmd_t)) {
    DBG_ERR(("A: A(%d) write, message too small (%d < %d)",
          a->controller, length, sizeof(diva_xdi_um_cfg_cmd_t)))
    return (-3);
  }

  if (!(data = diva_os_malloc (0, length))) {
    DBG_ERR(("A: A(%d) write, ENOMEM", a->controller))
    return (-2);
  }

  length = (*cp_fn)(os_handle, data, src, length);
  if (length > 0) {
    if ((*(a->interface.cmd_proc))(a, (diva_xdi_um_cfg_cmd_t*)data, length)) {
      length = -3;
    }
  } else {
    DBG_ERR(("A: A(%d) write error (%d)", a->controller, length))
  }

  diva_os_free (0, data);

  return (length);
}

/*
**  Write answers to user mode utility, if any
*/
int
diva_xdi_read (void* adapter, void* os_handle, void* dst,
               int max_length, divas_xdi_copy_to_user_fn_t cp_fn)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t*)adapter;
  int ret;

  if (!(a->xdi_mbox.status & DIVA_XDI_MBOX_BUSY)) {
    DBG_ERR(("A: A(%d) rx mbox empty", a->controller))
    return (-1);
  }
  if (!a->xdi_mbox.data) {
    a->xdi_mbox.status &= ~DIVA_XDI_MBOX_BUSY;
    DBG_ERR(("A: A(%d) rx ENOMEM", a->controller))
    return (-2);
  }

  if (max_length < a->xdi_mbox.data_length) {
    DBG_ERR(("A: A(%d) rx buffer too short(%d < %d)", 
            a->controller, max_length, a->xdi_mbox.data_length))
    return (-3);
  }

  ret = (*cp_fn)(os_handle, dst, a->xdi_mbox.data, a->xdi_mbox.data_length);
  if (ret > 0) {
    diva_os_free (0, a->xdi_mbox.data);
    a->xdi_mbox.data = 0;
    a->xdi_mbox.status &= ~DIVA_XDI_MBOX_BUSY;
  }

  return (ret);
}


void
diva_os_irq_wrapper (int irq, void* context, struct pt_regs* regs)
{
  diva_os_xdi_adapter_t* a = (diva_os_xdi_adapter_t*)context;
  diva_xdi_clear_interrupts_proc_t clear_int_proc;

  if (!a || !a->xdi_adapter.diva_isr_handler) {
    return;
  }

  if ((clear_int_proc = a->clear_interrupts_proc)) {
    (*clear_int_proc)(a);
    a->clear_interrupts_proc = 0;
    return;
  }

  (*(a->xdi_adapter.diva_isr_handler))(&a->xdi_adapter);
}

static void
diva_init_request_array (void)
{
  Requests[0 ] = DivaIdiRequest0;
  Requests[1 ] = DivaIdiRequest1;
  Requests[2 ] = DivaIdiRequest2;
  Requests[3 ] = DivaIdiRequest3;
  Requests[4 ] = DivaIdiRequest4;
  Requests[5 ] = DivaIdiRequest5;
  Requests[6 ] = DivaIdiRequest6;
  Requests[7 ] = DivaIdiRequest7;
  Requests[8 ] = DivaIdiRequest8;
  Requests[9 ] = DivaIdiRequest9;
  Requests[10] = DivaIdiRequest10;
  Requests[11] = DivaIdiRequest11;
  Requests[12] = DivaIdiRequest12;
  Requests[13] = DivaIdiRequest13;
  Requests[14] = DivaIdiRequest14;
  Requests[15] = DivaIdiRequest15;
  Requests[16] = DivaIdiRequest16;
  Requests[17] = DivaIdiRequest17;
  Requests[18] = DivaIdiRequest18;
  Requests[19] = DivaIdiRequest19;
  Requests[20] = DivaIdiRequest20;
  Requests[21] = DivaIdiRequest21;
  Requests[22] = DivaIdiRequest22;
  Requests[23] = DivaIdiRequest23;
  Requests[24] = DivaIdiRequest24;
  Requests[25] = DivaIdiRequest25;
  Requests[26] = DivaIdiRequest26;
  Requests[27] = DivaIdiRequest27;
  Requests[28] = DivaIdiRequest28;
  Requests[29] = DivaIdiRequest29;
  Requests[30] = DivaIdiRequest30;
  Requests[31] = DivaIdiRequest31;
}

void
diva_xdi_display_adapter_features (int card)
{
  dword features;
  if (!card || ((card-1) > MAX_ADAPTER) || !IoAdapters[card-1]) {
    return;
  }
  card--;
  features = IoAdapters[card]->Properties.Features;

  DBG_LOG(("FEATURES FOR ADAPTER: %d", card+1))
  DBG_LOG((" DI_FAX3          :  %s", (features & DI_FAX3)   ? "Y" : "N"))
  DBG_LOG((" DI_MODEM         :  %s", (features & DI_MODEM)  ? "Y" : "N"))
  DBG_LOG((" DI_POST          :  %s", (features & DI_POST)   ? "Y" : "N"))
  DBG_LOG((" DI_V110          :  %s", (features & DI_V110)   ? "Y" : "N"))
  DBG_LOG((" DI_V120          :  %s", (features & DI_V120)   ? "Y" : "N"))
  DBG_LOG((" DI_POTS          :  %s", (features & DI_POTS)   ? "Y" : "N"))
  DBG_LOG((" DI_CODEC         :  %s", (features & DI_CODEC)  ? "Y" : "N"))
  DBG_LOG((" DI_MANAGE        :  %s", (features & DI_MANAGE) ? "Y" : "N"))
  DBG_LOG((" DI_V_42          :  %s", (features & DI_V_42)   ? "Y" : "N"))
  DBG_LOG((" DI_EXTD_FAX      :  %s", (features & DI_EXTD_FAX) ? "Y" : "N"))
  DBG_LOG((" DI_AT_PARSER     :  %s", (features & DI_AT_PARSER) ? "Y" : "N"))
  DBG_LOG((" DI_VOICE_OVER_IP :  %s", (features & DI_VOICE_OVER_IP) ? "Y" : "N"))
}

void
diva_add_slave_adapter (diva_os_xdi_adapter_t* a)
{
  diva_os_spin_lock_magic_t old_irql;

  diva_os_enter_spin_lock (&adapter_lock, &old_irql, "add_slave");
  diva_q_add_tail (&adapter_queue, &a->link);
  diva_os_leave_spin_lock (&adapter_lock, &old_irql, "add_slave");
}

