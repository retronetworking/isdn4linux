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

#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include "defs.h"
#include "tools.h"

typedef struct __header {
    unsigned		type;
#if !defined (NDEBUG)
    unsigned		size;
    unsigned		from;
    unsigned		tag;
#endif
} header_t;

#define	TYPE_NONE	'?'
#define TYPE_KMALLOCED	'k'
#define	TYPE_VMALLOCED	'v'
#define	PRIORITY	GFP_ATOMIC
#define KMALLOC_LIMIT	131072

#if !defined (NDEBUG)
#include <asm/atomic.h>

#define	FENCE_TAG	0xDEADBEEF

static atomic_t		alloc_count = ATOMIC_INIT (0);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned hallocated (void) {
    
    return atomic_read (&alloc_count);
} /* hallocated */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned hallocator (void * mem) {
    header_t * hdr;

    if (mem != NULL) {
	hdr = ((header_t *) mem) - 1;
	return (hdr->tag != FENCE_TAG) ? 0 : hdr->from;
    } else {
	return 0;
    }
} /* hallocator */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int hvalid (void * mem) {
    header_t * hdr;
    int        flag = TRUE;

    if (mem != NULL) {
	hdr  = ((header_t *) mem) - 1;
	flag = (hdr->tag == FENCE_TAG)
	    && (* (unsigned *) (((char *) mem) + hdr->size) == FENCE_TAG);
    } 
    return flag;
} /* hvalid */
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void * ALLOCATE (unsigned n, unsigned * t) {
    void *   temp;
    unsigned type;

    if (n <= KMALLOC_LIMIT) {
    	temp = kmalloc (n, PRIORITY);
	type = TYPE_KMALLOCED;
    } else {
	temp = vmalloc (n);
	type = TYPE_VMALLOCED;
    }
    *t = (temp != NULL) ? type : TYPE_NONE;
    return temp;
} /* ALLOCATE */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if !defined (NDEBUG)
#define	PATCH(n)	sizeof(header_t)+sizeof(unsigned)+((n)?(n):1)
#else
#define	PATCH(n)	sizeof(header_t)+((n)?(n):1)
#endif

static void * halloc (unsigned size, unsigned addr) {
    unsigned   n, t = TYPE_NONE;
    void *     mem;
    header_t * hdr;

    n = PATCH(size);
    if (NULL == (hdr = (header_t *) ALLOCATE (n, &t))) {
	log ("Memory request (%u/%u bytes) failed.\n", size, PATCH(size));
	mem = NULL;
    } else {
	mem = (void *) (hdr + 1);
	hdr->type = t;
#if !defined (NDEBUG)
	hdr->size = size ? size : 1;
	hdr->from = addr;
	hdr->tag  = FENCE_TAG;
	* (unsigned *) (((char *) mem) + size) = FENCE_TAG;
	atomic_add (size, &alloc_count);
#else
	UNUSED_ARG (addr);
#endif
    }
    return mem;
} /* halloc */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * hmalloc (unsigned size) {

    return halloc (size, *(((unsigned *) &size) - 1));
} /* hmalloc */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * hcalloc (unsigned size) {
    void * mem;

    mem = halloc (size, *(((unsigned *) &size) - 1));
    if ((mem != NULL) && (size != 0)) {
	memset (mem, 0, size);
    }
    return mem;
} /* hcalloc */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void hfree (void * mem) {
    header_t * hdr;
#if !defined (NDEBUG)
    int        fence1, fence2;
#endif

    if (mem != NULL) {
	hdr = ((header_t *) mem) - 1;
#if !defined (NDEBUG)
	fence1 = (hdr->tag == FENCE_TAG);
	fence2 = (* (unsigned *) (((char *) mem) + hdr->size) == FENCE_TAG);
	if (!(fence1 && fence2)) {
	    log ("FENCE VIOLATED (%u/0x%08X)!\n", hdr->size, hdr->from);
	}
	atomic_sub (hdr->size, &alloc_count);
#endif
	assert ((hdr->type == TYPE_KMALLOCED) || (hdr->type == TYPE_VMALLOCED));
	if (TYPE_KMALLOCED == hdr->type) {
	    kfree (hdr);
	} else {
	    vfree (hdr);
	}
    }
} /* hfree */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void vlprintf (const char * level, const char * fmt, va_list args) {
    static char line[256];

    vsprintf (line, fmt, args);
    printk ("%s%s: %s", level, TARGET, line);
} /* vlprintf */
 
/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void lprintf (const char * level, const char * fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vlprintf(level, fmt, args);
    va_end(args);
} /* lprintf */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void message (const char * fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vlprintf(KERN_INFO, fmt, args);
    va_end(args);
} /* message */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
