/*
 * $Id$
 * 
 * Headerfile for Compartibility between different kernel versions
 * 
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log$
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

#include <linux/version.h>
#include <linux/isdnif.h>

#ifndef LinuxVersionCode
#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))
#endif

#endif				/* __COMPAT_H__ */
