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

#ifndef __have_queue_h__
#define __have_queue_h__

#include <linux/skbuff.h>
#include "tables.h"

typedef long long	tag_t;

typedef struct __qitem {

    tag_t		key;
    struct sk_buff *	msg;
    struct __qitem *	succ;
    struct __qitem *	pred;
} qitem_t;

typedef struct __queue {

    qitem_t *		put;
    qitem_t *		get;
    qitem_t *		noconf;
    unsigned		num;
} queue_t;

extern void queue_init (queue_t ** q);
extern void queue_exit (queue_t ** q);

extern void queue_put (queue_t * q, struct sk_buff * msg);
extern struct sk_buff * queue_peek (queue_t * q);
extern struct sk_buff * queue_get (queue_t * q);
extern void queue_drop (queue_t * q);

extern int queue_size (queue_t * q);
extern int queue_is_empty (queue_t * q);
extern int queue_is_full (queue_t * q);

extern void queue_park (queue_t * q, unsigned appl, NCCI_t ncci, unsigned hand);
extern void queue_conf (queue_t * q, unsigned appl, NCCI_t ncci, unsigned hand);

#endif

