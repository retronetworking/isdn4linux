/*
 * Copyright (C) 2000 AVM GmbH. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, and WITHOUT 
 * ANY LIABILITY FOR ANY DAMAGES arising out of or in connection 
 * with the use or performance of this software. See the
 * GNU General Public License for further details.
 *
 */

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <linux/version.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <stdarg.h>
#include "ca.h"
#include "capilli.h"
#include "main.h"
#include "tables.h"
#include "queue.h"
#include "tools.h"
#include "defs.h"
#include "lib.h"
#include "driver.h"

#if defined (LOG_MESSAGES)
# define mlog		log
#else
# define mlog(f, a...)	
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcclassic__) 
# define	IO_RANGE	32
#elif defined (__fcpnp__)
# define	CARD_ID		9
# define	ID_OFFSET	0
# define	IO_RANGE	32
#elif defined (__fcpcmcia__)
# define	CARD_ID		1
# define	ID_OFFSET	6
# define	IO_RANGE	8
#elif defined (__fcpci__)
# define	CARD_ID		10
# define	ID_OFFSET	0
# define	IO_RANGE	32
#else
# error You must define a card identifier...
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static struct tq_struct		tq_dpc;
static int			irq;
static atomic_t 		crit_level	= ATOMIC_INIT (0);
card_t *			capi_card	= NULL;
lib_callback_t *		capi_lib	= NULL;
struct capi_driver_interface *	capi_driver	= NULL;
struct capi_ctr *		capi_controller	= NULL;
struct capi_driver		capi_interface  = {

    TARGET,
    "0.0",
    load_ware,
    reset_ctrl,
    remove_ctrl,
    register_appl,
    release_appl,
    send_msg,
    proc_info,
    ctr_info,
    drv_info,
    add_card,
    NULL,
    NULL,
    0,
    NULL,
    ""
} ;

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static char *	invalid_msg   = "Invalid application id #%d.\n";

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcpci__)
# include <linux/pci.h>

# define AVM_VENDOR_ID		0x1244
# define AVM_DEVICE_ID		0x0A00

# define PCI_NO_PCI_KERN	3
# define PCI_NO_CARD		2
# define PCI_NO_PCI		1
# define PCI_OK			0

static int find_card (unsigned * base, unsigned * irq) {
# ifndef CONFIG_PCI
    return PCI_NO_PCI_KERN;
# else
    struct pci_dev * dev = NULL;

    if (!pci_present ()) {
        return PCI_NO_PCI;
    }
    if (NULL == (dev = pci_find_device (AVM_VENDOR_ID, AVM_DEVICE_ID, dev))) {
        return PCI_NO_CARD;
    }
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 0)
    if (0 != pci_enable_device (dev)) {
	return PCI_NO_CARD;
    }
# endif
    *irq  = dev->irq;
    *base = GET_PCI_BASE (dev, 1) & PCI_BASE_ADDRESS_IO_MASK;
    log ("PCI: irq: %d, base: 0x%04x\n", *irq, *base);
    return PCI_OK;
# endif
} /* find_card */
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcpnp__) && defined (CONFIG_ISAPNP)
# include <linux/isapnp.h>

# define AVM_VENDOR_ID		ISAPNP_VENDOR('A','V','M')
# define AVM_DEVICE_ID		ISAPNP_DEVICE(0x0900)

# define PNP_NO_CARD		2
# define PNP_ERROR		1
# define PNP_OK			0

static int find_card (unsigned * base, unsigned * irq, struct pci_dev ** d) {
    struct pci_dev * dev = NULL;

    dev = isapnp_find_dev (NULL, AVM_VENDOR_ID, AVM_DEVICE_ID, dev);
    if (NULL == dev) {
	return PNP_NO_CARD;
    } else {
	if (dev->active) {
	    lprintf (KERN_ERR, "PnP device is already in use.\n");
	    return PNP_ERROR;
	}
	if ((*dev->prepare) (dev) < 0) {
	    lprintf (KERN_ERR, "PnP device preparation failed.\n");
	    return PNP_ERROR;
	}
	if ((*dev->activate) (dev) < 0) {
	    lprintf (KERN_ERR, "PnP device activation failed.\n");
	    return PNP_ERROR;
	}
	*base = dev->resource[0].start;
	*irq  = dev->irq_resource[0].start;
        *d    = dev;
	return PNP_OK;
    }
} /* find_card */
#endif
 
/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static int nbchans (struct capi_ctr * ctrl) {
    card_t *        card;
    unsigned char * prf;
    int             temp = 2;
    
    assert (ctrl);
    card = (card_t *) ctrl->driverdata;
    prf = (unsigned char *) card->string[6];
    if (prf != NULL) {
	temp = prf[2] + 256 * prf[3];
    }
    return temp;
} /* nbchans */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static struct sk_buff * make_0xfe_request (unsigned appl) {
    unsigned char    request[8];
    struct sk_buff * skb;

    if (NULL == (skb = alloc_skb (8, GFP_ATOMIC))) {
        lprintf (KERN_ERR, "Unable to allocate message buffer.\n");
    } else {    
        request[0] = 8;
        request[1] = 0;
        request[2] = appl & 0xFF;
        request[3] = (appl >> 8) & 0xFF;
        request[4] = 0xFE;
        request[5] = 0x80;
        memcpy (skb_put (skb, 8), &request, 8);
   }
   return skb;
} /* make_0xfe_request */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void scan_version (card_t * card, const char * ver) {
    int    vlen, i;
    char * vstr;

    vlen = (unsigned char) ver[0];
    card->version = vstr = (char *) hmalloc (vlen);
    if (NULL == card->version) {
        log ("Could not allocate version buffer.\n");
        return;
    }
    memcpy (card->version, ver + 1, vlen);
    i = 0;
    for (i = 0; i < 8; i++) {
	card->string[i] = vstr + 1;
        vstr += 1 + *vstr;
    } 
#ifdef NDEBUG
    lprintf (KERN_INFO, "Stack version %s\n", card->string[0]);
#endif
    log ("Library version:    %s\n", card->string[0]);
    log ("Card type:          %s\n", card->string[1]);
    log ("Capabilities:       %s\n", card->string[4]);
    log ("D-channel protocol: %s\n", card->string[5]);
} /* scan_version */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void copy_version (struct capi_ctr * ctrl) {
    char *   tmp;
    card_t * card;

    assert (ctrl);
    card = (card_t *) ctrl->driverdata;
    if (NULL == (tmp = card->string[3])) {
        lprintf (KERN_ERR, "Do not have version information...\n");
        return;
    }
    strncpy (ctrl->serial, tmp, CAPI_SERIAL_LEN);
    memcpy (&ctrl->profile, card->string[6], sizeof (capi_profile));
    strncpy (ctrl->manu, "AVM GmbH", CAPI_MANUFACTURER_LEN);
    ctrl->version.majorversion = 2;
    ctrl->version.minorversion = 0;
    tmp = card->string[0];
    ctrl->version.majormanuversion = (((tmp[0] - '0') & 15) << 4)
                                   + ((tmp[2] - '0') & 15);
    ctrl->version.minormanuversion = ((tmp[3] - '0') << 4)
                                   + (tmp[5] - '0') * 10
                                   + ((tmp[6] - '0') & 15);
} /* copy_version */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void kill_version (card_t * card) {
    int i;

    for (i = 0; i < 8; i++) {
	card->string[i] = NULL;
    }
    if (card->version != NULL) {
	hfree (card->version);
	card->version = NULL;
    }
} /* kill_version */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void pprintf (char * page, int * len, const char * fmt, ...) {
    va_list args;

    va_start (args, fmt);
    *len += vsprintf (page + *len, fmt, args);
    va_end (args);
} /* pprintf */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int start (card_t * card) {
    char * version;

    card->count = 0;
    table_init (&card->appls);
    queue_init (&card->queue);
    (*capi_lib->cm_start) ();
    version = (*capi_lib->cm_init) (card->base, card->irq);
    scan_version (card, version);
    if (!install_card (card)) {
        (*capi_lib->cm_exit) ();
	return FALSE;
    }
    enter_critical ();
    if ((*capi_lib->cm_activate) ()) {
	log ("Activate failed.\n");
	leave_critical ();
	remove_card (card);
	return FALSE;
    }
    (*capi_lib->cm_handle_events) ();
    leave_critical ();
    return TRUE; 
} /* start */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void stop (card_t * card) {

    (*capi_lib->cm_exit) ();
    remove_card (card);
    queue_exit (&card->queue);
    table_exit (&card->appls);
    kill_version (card);
} /* stop */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int load_ware (struct capi_ctr * ctrl, capiloaddata * ware) {

    UNUSED_ARG (ctrl);
    UNUSED_ARG (ware);
    lprintf (KERN_ERR, "Cannot load firmware onto passive controller.\n");
    return -EIO;
} /* load_ware */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void reset_ctrl (struct capi_ctr * ctrl) {
    card_t * card;

    assert (ctrl);
    card = (card_t *) ctrl->driverdata;
    if (0 != card->count) {
	lprintf (KERN_INFO, "Removing registered applications!\n");
    }
    (*card->dwn_func) ();
    stop (card);
    (*ctrl->reseted) (ctrl);
} /* reset_ctrl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int add_card (struct capi_driver * drv, capicardparams * args) {
    card_t * card;

    UNUSED_ARG (drv);
    if (NULL != capi_controller) {
	lprintf (KERN_ERR, "Cannot handle two controllers!\n");
 	return -EBUSY;
    }
    if (NULL == (card = (card_t *) hmalloc (sizeof (card_t)))) {
	lprintf (KERN_ERR, "Card object allocation failed.\n");
	return -EIO;
    }
    capi_card = card;

    /*-----------------------------------------------------------------------*\
     * Card-dependent handling of I/O and IRQ values...
    \*-----------------------------------------------------------------------*/

#if defined (__fcclassic__) || defined (__fcpcmcia__)
    card->base = args->port;
    card->irq  = args->irq;
#elif defined (__fcpnp__) 
# if !defined (CONFIG_ISAPNP)
    card->base = args->port;
    card->irq  = args->irq;
# else
    UNUSED_ARG (args);
    switch (find_card (&card->base, &card->irq, &card->dev)) {
	case PNP_OK:
	    break;
	case PNP_ERROR:
	    return -ESRCH;
	case PNP_NO_CARD:
	    lprintf (KERN_ERR, "Could not locate any FRITZ!Card PnP.\n");
	    return -ESRCH;
        default:
            lprintf (KERN_ERR, "Unknown PnP related problem...\n");
            return -EINVAL;
    }
# endif
#else
    UNUSED_ARG (args);
    switch (find_card (&card->base, &card->irq)) {
        case PCI_OK:
            break;
        case PCI_NO_PCI_KERN:
            lprintf (KERN_ERR, "No PCI kernel support available.\n");
            return -ESRCH;
        case PCI_NO_CARD:
            lprintf (KERN_ERR, "Could not locate any FRITZ!Card PCI.\n");
            return -ESRCH;
        case PCI_NO_PCI:
            lprintf (KERN_ERR, "No PCI busses available.\n");
            return -ESRCH;
        default:
            lprintf (KERN_ERR, "Unknown PCI related problem...\n");
            return -EINVAL;
    }
#endif

    if (!params_ok (card)) {
	lprintf (
		KERN_INFO, 
		"Error: Invalid module parameters; base=0x%04x, irq=%d\n", 
		card->base, 
		card->irq
	);
	return -EINVAL;
    }
    inc_use_count ();
    if (!start (card)) {
	dec_use_count ();
        lprintf (KERN_INFO, "Error: Initialization failed.\n");
        return -EIO;
    }
    capi_controller = (*capi_driver->attach_ctr) 
					(&capi_interface, SHORT_LOGO, NULL);
    if (NULL == capi_controller) {
	dec_use_count ();
	stop (card);
        lprintf (KERN_INFO, "Error: Could not attach the controller.\n");
 	return -EBUSY;
    }
    capi_controller->driverdata = (void *) card;
    copy_version (capi_controller);
    (*capi_controller->ready) (capi_controller);
    return 0;
} /* add_card */ 

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void remove_ctrl (struct capi_ctr * ctrl) {

    UNUSED_ARG (ctrl);
    assert (capi_controller != NULL);
    (*capi_driver->detach_ctr) (capi_controller);
    dec_use_count ();
    ctrl->driverdata = NULL;
    hfree (capi_card);
    capi_controller = NULL;
    capi_card       = NULL;
} /* remove_ctrl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void register_appl (struct capi_ctr * ctrl, u16 appl, capi_register_params * args) {
    card_t * card;
    appl_t * appp;
    void *   ptr;
    unsigned nc;

    mlog ("REGISTER(appl:%u)\n", appl);
    assert (ctrl);
    assert (args);
    card = (card_t *) ctrl->driverdata;
    if ((int) args->level3cnt < 0) {
	nc = nbchans (ctrl) * -((int) args->level3cnt);
    } else {
	nc = args->level3cnt;
    }
    if (0 == nc) {
	nc = nbchans (ctrl);
    }
    appp = create_appl (card->appls, appl, nc, args->datablkcnt, args->datablklen);
    if (NULL == appp) {
	log ("Unable to create application record.\n");
        return;
    }
    ptr = hcalloc (card->length);
    if (NULL == ptr) {
        lprintf (KERN_ERR, "Not enough memory for application data.\n");
	remove_appl (card->appls, appp);
    } else {
	inc_use_count ();
	card->count++;
	appp->data = ptr;
	appp->ctrl = ctrl;
	(*card->reg_func) (ptr, appl);
	(*ctrl->appl_registered) (ctrl, appl);
    }
} /* register_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void release_appl (struct capi_ctr * ctrl, u16 appl) {
    card_t *         card;
    struct sk_buff * skb;
    appl_t *         appp;

    mlog ("RELEASE(appl:%u)\n", appl);
    assert (ctrl);
    card = (card_t *) ctrl->driverdata;
    if (NULL == (appp = search_appl (card->appls, appl))) {
	log ("Attempt to release unknown application (id #%u)\n", appl);
	return;
    }
    skb = make_0xfe_request (appl);
    handle_message (card->appls, appp, skb);
    (*capi_lib->cm_trigger_timer_irq) ();
    appp->dying = TRUE;
} /* release_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void send_msg (struct capi_ctr * ctrl, struct sk_buff * skb) {
    card_t *        card;
    unsigned char * byte;
    unsigned        appl;
    appl_t *        appp;

    assert (ctrl);
    assert (skb);
    card = (card_t *) ctrl->driverdata;
    byte = skb->data;
    appl = byte[2] + 256 * byte[3];
    appp = search_appl (card->appls, appl);
    if ((NULL == appp) || appp->dying) {
        log ("Message for unknown application (id %u).\n", appl);
        return;
    }
    handle_message (card->appls, appp, skb);
    (*capi_lib->cm_trigger_timer_irq) ();
} /* send_msg */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
char * proc_info (struct capi_ctr * ctrl) {
    card_t *    card;
    static char text[80];

    assert (ctrl);
    card = (card_t *) ctrl->driverdata;
    sprintf (
	text, 
	"%s %s 0x%04x %u",
	card->version ? card->string[1] : "A1",
	card->version ? card->string[0] : "-",
	card->base, card->irq
    );
    return text;
} /* proc_info */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int ctr_info (char * page, char ** start, off_t ofs, int count, 
                                             int * eof, struct capi_ctr * ctrl) 
{
    card_t *      card;
    char *        temp;
    unsigned char flag;
    int           len = 0;

    assert (ctrl);
    card = (card_t *) ctrl->driverdata;
    pprintf (page, &len, "%-16s %s\n", "name", SHORT_LOGO);
    pprintf (page, &len, "%-16s 0x%04x\n", "io", card->base);
    pprintf (page, &len, "%-16s %d\n", "irq", card->irq);
    temp = card->version ? card->string[1] : "A1";
    pprintf (page, &len, "%-16s %s\n", "type", temp);
    temp = card->version ? card->string[0] : "-";
#if defined (__fcclassic__) || defined (__fcpcmcia__)
    pprintf (page, &len, "%-16s 0x%04x\n", "revision", card->info);
#endif
    pprintf (page, &len, "%-16s %s\n", "ver_driver", temp);
    pprintf (page, &len, "%-16s %s\n", "ver_cardtype", SHORT_LOGO);

    flag = ((unsigned char *) (ctrl->profile.manu))[3];
    if (flag) {
	pprintf(page, &len, "%-16s%s%s%s%s%s%s%s\n", "protocol",
	    (flag & 0x01) ? " DSS1" : "",
	    (flag & 0x02) ? " CT1" : "",
	    (flag & 0x04) ? " VN3" : "",
	    (flag & 0x08) ? " NI1" : "",
	    (flag & 0x10) ? " AUSTEL" : "",
	    (flag & 0x20) ? " ESS" : "",
	    (flag & 0x40) ? " 1TR6" : ""
	);
    }
    flag = ((unsigned char *) (ctrl->profile.manu))[5];
    if (flag) {
        pprintf(page, &len, "%-16s%s%s%s%s\n", "linetype",
	    (flag & 0x01) ? " point to point" : "",
	    (flag & 0x02) ? " point to multipoint" : "",
	    (flag & 0x08) ? " leased line without D-channel" : "",
	    (flag & 0x04) ? " leased line with D-channel" : ""
	);
    }
    if (len < ofs) 
        return 0;
    *eof = 1;
    *start = page - ofs;
    return ((count < len - ofs) ? count : len - ofs);
} /* ctr_info */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int drv_info (char * page, char ** start, off_t ofs, int count, 
                                          int * eof, struct capi_driver * drv)
{
    int len = 0;

    UNUSED_ARG (drv);
    pprintf (page, &len, "%-16s %s\n", "name", TARGET);
    pprintf (page, &len, "%-16s %s\n", "revision", capi_interface.revision);
    if (len < ofs) 
        return 0;
    *eof = 1;
    *start = page - ofs;
    return ((count < len - ofs) ? count : len - ofs);
} /* drv_info */
 
/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * data_by_id (unsigned appl_id) {
    appl_t * appp;

    appp = search_appl (capi_card->appls, appl_id);
    return (appp != NULL) ? appp->data : NULL;
} /* data_by_id */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
struct capi_ctr * card_by_id (unsigned appl_id) {
    appl_t * appp;

    appp = search_appl (capi_card->appls, appl_id);
    return (appp != NULL) ? appp->ctrl : NULL;
} /* card_by_id */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * first_data (int * res) {
    appl_t * appp;

    assert (res);
    appp = first_appl (capi_card->appls);
    *res = (appp != NULL) ? 0 : -1;
    return (appp != NULL) ? appp->data  : NULL;
} /* first_data */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * next_data (int * res) {
    appl_t * appp;

    assert (res);
    if (NULL != (appp = get_appl (capi_card->appls, *res))) {
	appp = next_appl (capi_card->appls, appp);
    }
    *res = (appp != NULL) ? 1 + *res : -1;
    return (appp != NULL) ? appp->data  : NULL;
} /* next_data */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int appl_profile (unsigned appl_id, unsigned * bs, unsigned * bc) {
    appl_t * appp;

    appp = search_appl (capi_card->appls, appl_id);
    if (NULL == appp) {
        return 0;
    }
    if (bs) *bs = appp->blk_size;
    if (bc) *bc = appp->blk_count;
    return 1;
} /* appl_profile */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int msg2stack (unsigned char * msg) {
    unsigned         mlen;
    unsigned	     appl;
    __u32            ncci;
    unsigned	     hand;
    unsigned char *  mptr;
    unsigned char *  dptr;
    struct sk_buff * skb;
    unsigned         temp;
    int              res = 0;

    assert (msg);
    if (!queue_is_empty (capi_card->queue)) {
        res = 1;
        skb = queue_peek (capi_card->queue);	
	assert (skb);
        mptr = (unsigned char *) skb->data;
        mlen = mptr[0] + 256 * mptr[1]; 
	appl = mptr[2] + 256 * mptr[3];
	mlog ("PUT_MESSAGE(appl:%u,cmd:0x%02X,subcmd:0x%02X)\n", appl, mptr[4], mptr[5]);

        if ((0x86 == mptr[4]) && (0x80 == mptr[5])) {	/* DATA_B3_REQ */
            ncci = mptr[8] + 256 * (mptr[9] + 256 * (mptr[10] + 256 * mptr[11]));
            hand = mptr[18] + 256 * mptr[19];
            temp = (unsigned) (mptr + mlen);
            dptr = mptr + 12;
	    *dptr++ = temp & 0xFF;	temp >>= 8;
	    *dptr++ = temp & 0xFF;	temp >>= 8;
	    *dptr++ = temp & 0xFF;	temp >>= 8;
	    *dptr++ = temp & 0xFF;
	    queue_park (capi_card->queue, appl, ncci, hand);
            memcpy (msg, mptr, mlen);
        } else {
	    queue_drop (capi_card->queue);
            memcpy (msg, mptr, mlen);
        }
    }
    return res;
} /* msg2stack */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void msg2capi (unsigned char * msg) {
    unsigned          mlen; 
    unsigned          appl;
    __u32             ncci;
    unsigned          hand;
    unsigned	      dlen;
    unsigned char *   dptr;
    unsigned          temp;
    struct sk_buff *  skb;
    struct capi_ctr * card;

    assert (msg);
    mlen = msg[0] + 256 * msg[1];
    appl = msg[2] + 256 * msg[3];
    if ((0x86 == msg[4]) && (0x81 == msg[5])) {		/* DATA_B3_CONF */
	hand = msg[12] + 256 * msg[13];
	ncci = msg[8] + 256 * (msg[9] + 256 * (msg[10] + 256 * msg[11]));
	queue_conf (capi_card->queue, appl, ncci, hand);
    }

    if ((0x86 == msg[4]) && (0x82 == msg[5])) {		/* DATA_B3_IND */
	dlen = msg[16] + 256 * msg[17];
	if (NULL == (skb = alloc_skb (mlen + dlen + ((mlen < 30) ? (30 - mlen) : 0), GFP_ATOMIC))) {
	    lprintf (KERN_ERR, "Unable to build CAPI message skb. Message lost.\n");
	    return;
	}
        /* Messages are expected to come with 32 bit data pointers. The kernel
	 * CAPI works with extended (64 bit ready) message formats so that the
	 * incoming message needs to be fixed, i.e. the length gets adjusted
	 * and the required 64 bit data pointer is added.
	 */
	temp = msg[12] + 256 * (msg[13] + 256 * (msg[14] + 256 * msg[15]));
	dptr = (unsigned char *) temp;
	if (mlen < 30) {
	    msg[0] = 30;
	    ncci   = 0;					/* 32 bit zero */
	    memcpy (skb_put (skb, mlen), msg, mlen);
	    memcpy (skb_put (skb, 4), &ncci, 4);	/* Writing a 64 bit zero */
	    memcpy (skb_put (skb, 4), &ncci, 4);
	} else {
	    memcpy (skb_put (skb, mlen), msg, mlen);
        }
	memcpy (skb_put (skb, dlen), dptr, dlen); 
    } else {
	if (NULL == (skb = alloc_skb (mlen, GFP_ATOMIC))) {
	    lprintf (KERN_ERR, "Unable to build CAPI message skb. Message lost.\n");
	    return;
	}
	memcpy (skb_put (skb, mlen), msg, mlen);
    }
    mlog ("GET_MESSAGE(appl:%d,cmd:0x%02X,subcmd:0x%02X)\n", appl, msg[4], msg[5]);
    card = card_by_id (appl);
    if (card != NULL) {
        (*card->handle_capimsg) (card, appl, skb);
    }
} /* msg2capi */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void new_ncci (unsigned appl_id, __u32 ncci, unsigned winsize, unsigned blksize) {
    appl_t * appp;
    ncci_t * nccip;

    mlog ("NEW NCCI(appl:%u,ncci:0x%08X)\n", appl_id, ncci);
    if (NULL == (appp = search_appl (capi_card->appls, appl_id))) {
        lprintf (KERN_ERR, invalid_msg, appl_id);
	return;
    }
    if (NULL == (nccip = create_ncci (capi_card->appls, appl_id, (NCCI_t) ncci, winsize, blksize))) {
	log ("Cannot handle new NCCI...\n");
	return;
    }
    assert (appp->ctrl);
    (*appp->ctrl->new_ncci) (appp->ctrl, appl_id, ncci, winsize);
} /* new_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void free_ncci (unsigned appl_id, __u32 ncci) {
    appl_t * appp;
    ncci_t * nccip;

    appp = search_appl (capi_card->appls, appl_id);
    if (NULL == appp) {
        lprintf (KERN_ERR, invalid_msg, appl_id);
        return;
    }
    assert (appp->ctrl);
    if (0xFFFFFFFF == ncci) {			/* 2nd phase RELEASE */
	assert (appp->dying);
	dec_use_count ();
	capi_card->count--;
	(*appp->ctrl->appl_released) (appp->ctrl, appl_id);
	(*capi_card->rel_func) (appp->data);
	remove_appl (capi_card->appls, appp);
    } else if (NULL != (nccip = locate_ncci (appp, ncci))) {
        mlog ("FREE NCCI(appl:%u,ncci:0x%08X)\n", appl_id, ncci);
        (*appp->ctrl->free_ncci) (appp->ctrl, appl_id, ncci);
	remove_ncci (capi_card->appls, appp, nccip);
    } else {
	lprintf (KERN_ERR, "Attempt to free unknown NCCI.\n");
    }
} /* free_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned char * data_block (unsigned appl_id, __u32 ncci, unsigned handle) {
    appl_t * appp;
 
    appp = search_appl (capi_card->appls, appl_id);
    if (NULL == appp) {
        lprintf (KERN_ERR, invalid_msg, appl_id);
        return NULL;
    }
    return ncci_data_buffer (capi_card->appls, appl_id, ncci, handle);
} /* data_block */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void enter_critical (void) {
    
    atomic_inc (&crit_level);
    if (atomic_read (&crit_level) == 1) {
	disable_irq (irq);
    }
} /* enter_critical */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void leave_critical (void) {

    assert (atomic_read (&crit_level) > 0);
    atomic_dec (&crit_level);
    if (atomic_read (&crit_level) == 0) {
	enable_irq (irq);
    }
} /* leave_critical */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int params_ok (card_t * card) {
    int  chk;

    assert (card);
    if (0 == card->irq) {
	lprintf (
		KERN_ERR, 
		"IRQ not assigned by BIOS. Please check BIOS"
		   "settings/manual for a proper PnP/PCI-Support.\n"
	);
	return FALSE;
    }
    if (0 == card->base) {
        lprintf (KERN_ERR, "Base address has not been set.\n");
        return FALSE;
    }
#if defined (__fcclassic__)
    switch (card->base) {
        case 0x200: case 0x240: case 0x300: case 0x340:
	    log ("Base address valid.\n");
	    break;
	default:
	    log ("Invalid base address.\n");
	    return FALSE;
    }
#endif
#if !defined (__fcpcmcia__)
    if ((chk = check_region (card->base, IO_RANGE)) < 0) {
	log ("I/O range 0x%04x-0x%04x busy. [%d]\n", 
				card->base, card->base + IO_RANGE - 1, chk);
	return FALSE;
    }
#endif
    if (!(chk = (*capi_lib->check_controller) (card->base, &card->info))) {
	return TRUE;
    } else {
	log ("Controller check failed.\n");
	return FALSE;
    }
} /* params_ok */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void dpc (void * data) {

    UNUSED_ARG (data);
    os_timer_poll ();
    (*capi_lib->cm_schedule) (); 
} /* dpc */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void irq_handler (int irq, void * args, struct pt_regs * regs) {
#if defined (__fcpci__)
    card_t * card = (card_t *) args;

    if ((NULL == card) || (card->data != (unsigned) &irq_handler)) {
	return;
    }
#else
    UNUSED_ARG (args);
#endif
    UNUSED_ARG (irq);
    UNUSED_ARG (regs);
    atomic_inc (&crit_level);
    (void) (*capi_lib->cm_handle_events) ();
    atomic_dec (&crit_level);
    queue_task (&tq_dpc, &tq_immediate);
    mark_bh (IMMEDIATE_BH);
} /* irq_handler */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/ 
int install_card (card_t * card) {
    int result = 4711;

    assert (card);
#if !defined (__fcpcmcia__)
    request_region (card->base, IO_RANGE, TARGET);
    log ("I/O range 0x%04x-0x%04x assigned to " TARGET " driver.\n",
                                        card->base, card->base + IO_RANGE - 1);
#endif
#if !defined (__fcclassic__) 
    if (CARD_ID != inb (card->base + ID_OFFSET)) {
	release_region (card->base, IO_RANGE);
	lprintf (KERN_ERR, "Card identification test failed.\n");
	return FALSE;
    }
#endif
    card->data = (unsigned) &irq_handler;
    irq = card->irq;
#if defined (__fcpci__) && defined (SHARED_IRQ)
    result = request_irq (card->irq, &irq_handler, SA_INTERRUPT|SA_SHIRQ, TARGET, card);
#else
    result = request_irq (card->irq, &irq_handler, SA_INTERRUPT, TARGET, card);
#endif
    if (result) {
	release_region (card->base, IO_RANGE);
        lprintf (KERN_ERR, "Could not install irq handler.\n");
	return FALSE;
    } else {
	log ("IRQ #%d assigned to " TARGET " driver.\n", card->irq);
    }
    tq_dpc.next    = NULL;
    tq_dpc.sync    = 0;
    tq_dpc.routine = &dpc;
    tq_dpc.data    = card;
    return TRUE;
} /* install_card */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void remove_card (card_t * card) {

    log ("Releasing IRQ #%d...\n", card->irq);
    free_irq (card->irq, card);
    log ("Releasing I/O range 0x%04x-0x%04x...\n", 
					card->base, card->base + IO_RANGE - 1);
#if !defined (__fcpcmcia__)
    release_region (card->base, IO_RANGE);
#endif
#if defined (__fcpnp__) && defined (CONFIG_ISAPNP)
    (*card->dev->deactivate) (card->dev);
#endif
} /* remove_card */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void init (unsigned len, 
	   void  (* reg) (void *, unsigned),
	   void  (* rel) (void *),
	   void  (* dwn) (void)) 
{
    assert (reg);
    assert (rel);
    assert (dwn);

    capi_card->length   = len;
    capi_card->reg_func = reg;
    capi_card->rel_func = rel;
    capi_card->dwn_func = dwn;
} /* init */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcpcmcia__)
int avm_a1pcmcia_addcard (unsigned int port, unsigned irq) {
    capicardparams args;

    args.port = port;
    args.irq  = irq;
    return add_card (&capi_interface, &args);
} /* avm_a1pcmcia_addcard */

int avm_a1pcmcia_delcard (unsigned int port, unsigned irq) {

    UNUSED_ARG (port);
    UNUSED_ARG (irq);
    if (NULL != capi_controller) { 
	reset_ctrl (capi_controller);
	remove_ctrl (capi_controller);
    }
    return 0; 
} /* avm_a1pcmcia_delcard */
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int driver_init (void) {

    strncpy (capi_interface.revision, REVISION, 
					sizeof (capi_interface.revision));
    return (NULL != (capi_lib = link_library ()));
} /* driver_init */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void driver_exit (void) {

    assert (capi_lib);
    free_library ();
    capi_lib = NULL;
} /* driver_exit */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/

