/*
 * $Id$
 * 
 * Headerfile for Compartibility between different kernel versions
 * 
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log$
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

#if LINUX_VERSION_CODE >= 0x020112	/* 2.1.18 */
#define HAS_NEW_SYMTAB
#endif

#endif				/* __COMPAT_H__ */