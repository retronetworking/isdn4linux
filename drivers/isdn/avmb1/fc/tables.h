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

#ifndef __have_appl_h__
#define __have_appl_h__

#include <asm/types.h>
#include <linux/skbuff.h>
#include <linux/capi.h>
#include "capilli.h"

typedef __u32	NCCI_t;

typedef struct __ncci {

    NCCI_t		ncci;
    unsigned		appl;
    unsigned		win_size;
    unsigned		blk_size;
    unsigned char **	data;
    struct __ncci *	pred;
    struct __ncci *	succ;
} ncci_t;

typedef struct __appl {

    unsigned		id;
    unsigned		dying;
    struct capi_ctr *	ctrl;
    void *		data;
    unsigned		blk_size;
    unsigned		blk_count;
    unsigned		ncci_count;
    unsigned		nncci;
    ncci_t *		root;
    struct __appl *	pred;
    struct __appl *	succ;
} appl_t;

typedef struct __appltab {

    appl_t *		appl_root;
    unsigned		appl_count;
} appltab_t;

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
extern void table_init (appltab_t ** tab);
extern void table_exit (appltab_t ** tab);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
extern appl_t * create_appl (appltab_t * tab, 
			     unsigned    id, 
			     unsigned    ncount, 
			     unsigned    bcount, 
			     unsigned    bsize);
extern void remove_appl (appltab_t * tab, appl_t * appp);

extern appl_t * search_appl (appltab_t * tab, unsigned id);
extern appl_t * get_appl (appltab_t * tab, unsigned ix);
extern appl_t * first_appl (appltab_t * tab);
extern appl_t * next_appl (appltab_t * tab, appl_t * appp);

extern int handle_message (appltab_t * tab, appl_t * appp, struct sk_buff * msg);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
extern ncci_t * create_ncci (appltab_t * tab, 
			     unsigned    appl, 
			     NCCI_t      ncci, 
			     unsigned    wsize, 
			     unsigned    bsize);
extern void remove_ncci (appltab_t * tab, appl_t * appp, ncci_t * nccip);

extern unsigned char * ncci_data_buffer (appltab_t * tab,
					 unsigned    appl,
					 NCCI_t      ncci,
					 unsigned    index);

extern ncci_t * locate_ncci (appl_t * appp, NCCI_t ncci);
extern ncci_t * search_ncci (appltab_t * tab, unsigned appl, NCCI_t ncci);

#endif
