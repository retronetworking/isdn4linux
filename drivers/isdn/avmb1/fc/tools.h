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

#ifndef __have_tools_h__
#define __have_tools_h__

#include <linux/types.h>
#include <stdarg.h>
#include "defs.h"

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#ifndef NDEBUG
# define assert(x)	(!(x)?message("%s(%d): assert (%s) failed\n", \
                                        __FILE__, __LINE__, #x):((void)0))
# define info(x)	(!(x)?message("%s(%d): info (%s) failed\n", \
					__FILE__, __LINE__, #x):((void)0))
# define log(f,x...)	message (f, ##x)

extern void message (const char * fmt, ...);
#else
# define assert(x)
# define info(x)
# define log(f,x...)
#endif

extern void lprintf  (const char * level, const char * fmt, ...);
extern void vlprintf (const char * level, const char * fmt, va_list args);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#ifndef NDEBUG
extern unsigned hallocated (void);
extern unsigned hallocator (void * mem);
extern int	hvalid (void * mem);
#endif

extern void *   hmalloc (unsigned size);
extern void *   hcalloc (unsigned size);

extern void     hfree (void * mem);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#ifdef NEED_MEM_STR_PROTOS

extern void * memcpy (void * d, const void * s, size_t n);
extern void * memmove (void * d, const void * s, size_t n);
extern void * memset (void * m, int x, size_t c);

extern int memcmp (const void * s1, const void * s2, size_t n);

extern char * strcpy (char * d, const char * s);
extern char * strcat (char * d, const char * s);
extern int strcmp (const char * s1, const char * s2);

extern size_t strlen (const char * s);

#endif
#endif
