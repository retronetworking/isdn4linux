/*
 * $Id$
 * 
 * Headerfile for Compartibility between different kernel versions
 * 
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log$
 * Revision 1.5  1999/07/01 08:24:12  keil
 * compatibility macros now in <linux/isdn_compat.h>
 *
 * Revision 1.4  1998/10/25 14:39:02  fritz
 * Backported from MIPS (Cobalt).
 *
 * Revision 1.3  1997/11/04 06:12:15  calle
 * capi.c: new read/write in file_ops since 2.1.60
 * capidrv.c: prepared isdnlog interface for d2-trace in newer firmware.
 * capiutil.c: needs config.h (CONFIG_ISDN_DRV_AVMB1_VERBOSE_REASON)
 * compat.h: added #define LinuxVersionCode
 *
 * Revision 1.2  1997/10/01 09:21:22  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.1  1997/03/04 21:50:36  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 * 
 */
#ifndef __COMPAT_H__
#define __COMPAT_H__

#include <linux/isdn_compat.h>

#endif				/* __COMPAT_H__ */
