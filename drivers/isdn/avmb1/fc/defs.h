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

#ifndef __have_defs_h__
#define __have_defs_h__

#define OSDEBUG
#define LOG_MESSAGES

#ifndef LINUX_VERSION_CODE
# include <linux/version.h>
#endif

#ifndef TRUE
# define TRUE		(1==1)
# define FALSE		(1==0)
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (__fcclassic__)
# define PRODUCT_LOGO   "AVM FRITZ!Card Classic"
# define INTERFACE	"isa"
#elif defined (__fcpnp__)
# define PRODUCT_LOGO	"AVM FRITZ!Card PnP"
# define INTERFACE	"pnp"
#elif defined (__fcpcmcia__)
# define PRODUCT_LOGO	"AVM FRITZ!Card PCMCIA"
# define INTERFACE	"pcmcia"
#elif defined (__fcpci__)
# define PRODUCT_LOGO   "AVM FRITZ!Card PCI"
# define INTERFACE	"pci"
#endif
#define  DRIVER_LOGO    PRODUCT_LOGO " driver"
#define  SHORT_LOGO	"fritz-" INTERFACE

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if defined (OSDEBUG) && defined (NDEBUG)
# undef NDEBUG
#endif

#define	UNUSED_ARG(x)	(x)=(x)

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 13)
# define GET_PCI_BASE(d, r)     (d)->base_address[r]
#else
# define GET_PCI_BASE(d, r)     (d)->resource[r].start
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 43)
# define KFREE_SKB(x)	dev_kfree_skb(x)
#else
# include <linux/netdevice.h>
# define KFREE_SKB(x)	dev_kfree_skb_any(x)
#endif

#endif

