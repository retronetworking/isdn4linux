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

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
 
#include <stdarg.h>
#include <asm/uaccess.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/capi.h>
#include <linux/ctype.h>
#include "capilli.h"
#include "driver.h"
#include "tools.h"
#include "defs.h"

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static char * REVCONST = "$Revision$";
char          REVISION[32];

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#ifndef NDEBUG
static void base_address (void) {

    lprintf (KERN_INFO, "Base adress: 0x%08X\n", &base_address);
} /* base_address */
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcpcmcia__)
EXPORT_SYMBOL (avm_a1pcmcia_addcard);
EXPORT_SYMBOL (avm_a1pcmcia_delcard);
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcpci__) || (defined (__fcpnp__) && defined (CONFIG_ISAPNP))
static int auto_attach (void) {
    struct capicardparams args;

    lprintf (KERN_INFO, "Auto-attaching...\n");
    return add_card (&capi_interface, &args);
} /* auto_attach */
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void inc_use_count (void) { MOD_INC_USE_COUNT; }
void dec_use_count (void) { MOD_DEC_USE_COUNT; }

#if defined (__fcclassic__)
# define FRITZ_INIT	fcclassic_init
#elif defined (__fcpnp__)
# define FRITZ_INIT	fcpnp_init
#elif defined (__fcpcmcia__)
# define FRITZ_INIT	fcpcmcia_init
#elif defined (__fcpci__)
# define FRITZ_INIT	fcpci_init
#else
# define FRITZ_INIT
#endif

#if defined (MODULE)
int init_module (void) { 
    int FRITZ_INIT (void);

    return FRITZ_INIT (); 
} /* init_module */
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int FRITZ_INIT (void) {
    char * tmp;

#define	RETURN(x)	MOD_DEC_USE_COUNT; return (x);

    EXPORT_NO_SYMBOLS;
#ifndef NDEBUG
    base_address ();
#endif

    if ((NULL != (tmp = strchr (REVCONST, ':'))) && isdigit (*(tmp + 2))) {
        strncpy (REVISION, tmp + 1, sizeof (REVISION));
        tmp = strchr (REVISION, '$');
        *tmp = 0;
    } else {
	strcpy (REVISION, "0.0");
    }
    lprintf (KERN_INFO, "%s, revision %s\n", DRIVER_LOGO, REVISION);

    /*-----------------------------------------------------------------------*\
     * 64 bit CAPI is not supported yet.
    \*-----------------------------------------------------------------------*/
    if (sizeof (char *) > 4) {
        lprintf (KERN_ERR, "Cannot deal with 64 bit CAPI messages...\n");
        return -ENOSYS;
    }

    MOD_INC_USE_COUNT;			/* Protect attachment procedure */
    lprintf (KERN_INFO, "Loading...\n");
    if (!driver_init ()) {
        lprintf (KERN_INFO, "Error: Driver library not available.\n");
        lprintf (KERN_INFO, "Not loaded.\n");
        RETURN (-ENOSYS);
    }
    capi_driver = attach_capi_driver (&capi_interface);
    if (NULL == capi_driver) {
        lprintf (KERN_INFO, "Error: Could not attach the driver.\n");
	lprintf (KERN_INFO, "Not loaded.\n");
	driver_exit ();
        RETURN (-EIO);
    } 
#if defined (__fcpci__) || (defined (__fcpnp__) && defined (CONFIG_ISAPNP))
    if (0 != auto_attach ()) {
	lprintf (KERN_INFO, "Not loaded.\n");
	detach_capi_driver (&capi_interface);
	driver_exit ();
	RETURN (-EIO);
    }
#endif    
    lprintf (KERN_INFO, "Loaded.\n");
    RETURN (0);

#undef RETURN

} /* init_module */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void cleanup_module(void) {

    if (capi_controller != NULL) {
	lprintf (KERN_INFO, "Shutting down controller...\n");
	stop (capi_card);
	(*capi_driver->detach_ctr) (capi_controller);
    }
    lprintf (KERN_INFO, "Removing...\n");
    detach_capi_driver (&capi_interface);
    driver_exit ();
#ifndef NDEBUG
    if (hallocated() != 0) {
	lprintf (KERN_ERR, "%u bytes leaked.\n", hallocated());
    }
    /* hleaklist (); */
#endif
    lprintf (KERN_INFO, "Removed.\n");
} /* cleanup_module */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/

