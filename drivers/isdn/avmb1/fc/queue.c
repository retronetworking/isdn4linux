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

#include "defs.h"
#include "tools.h"
#include "tables.h"
#include "queue.h"
#include "driver.h"

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#define	MAKE_KEY(a,n,h)		(((tag_t)(a)<<48)+((tag_t)(n)<<16)+((tag_t)(h)))

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void queue_init (queue_t ** q) { 
    
    if (NULL == (*q = (queue_t *) hmalloc (sizeof (queue_t)))) {
	lprintf (KERN_ERR, "Not enough memory for queue struct.\n");
    } else {
	(*q)->noconf = (*q)->put = (*q)->get = NULL;
	(*q)->num = 0;
    }
} /* queue_init */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void queue_exit (queue_t ** q) { 
    qitem_t * item;
    
    assert (q != NULL);
    assert (*q != NULL);
    while ((*q)->get != NULL) {
	item = (*q)->get->succ;
	KFREE_SKB ((*q)->get->msg);
	hfree ((*q)->get);
	(*q)->get = item;
    }
    while ((*q)->noconf != NULL) {
	item = (*q)->noconf->succ;
	KFREE_SKB ((*q)->noconf->msg);
	hfree ((*q)->noconf);
	(*q)->noconf = item;
    }
    hfree (*q);
    *q = NULL;
} /* queue_exit */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static void enqueue (queue_t * q, struct sk_buff * msg) {
    qitem_t * item;

    if (NULL == (item = (qitem_t *) hmalloc (sizeof (qitem_t)))) {
	lprintf (KERN_ERR, "Not enough memory for internal message queue.\n");
	lprintf (KERN_ERR, "Message lost.\n");
	return;
    }
    item->succ = NULL;
    item->msg  = msg;
    item->key  = 0;
    enter_critical ();
    assert (q != NULL);
    if (q->num != 0) {
	q->put->succ = item;
    } else {
	q->get = item;
    }
    q->put = item;
    q->num++;
    leave_critical ();
} /* enqueue */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static struct sk_buff * dequeue (queue_t * q) {
    struct sk_buff * res;
    qitem_t *        item;
    qitem_t *        tmp;

    enter_critical ();
    assert (q != NULL);
    assert (q->get != NULL);
    res = q->get->msg;
    item = q->get->succ;
    tmp = q->get;
    q->get = item;
    q->num--;
    leave_critical ();
    hfree (tmp);
    return res;
} /* dequeue */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void queue_put (queue_t * q, struct sk_buff * msg) {

    assert (q != NULL);
    if (!queue_is_full (q)) {
	enqueue (q, msg);
    } else {
	lprintf (KERN_ERR, "Queue overflow.\n");
    }
} /* queue_put */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
struct sk_buff * queue_peek (queue_t * q) {
    struct sk_buff * tmp = NULL;

    assert (q != NULL);
    if (!queue_is_empty (q)) {
	assert (q->get != NULL);
	tmp = q->get->msg;
    } else {
	lprintf (KERN_ERR, "Queue underflow.\n");
    }
    return tmp;
} /* queue_peek */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
struct sk_buff * queue_get (queue_t * q) {
    struct sk_buff * tmp = NULL;

    assert (q != NULL);
    if (!queue_is_empty (q)) {
	tmp = dequeue (q);
    } else {
	lprintf (KERN_ERR, "Cannot read empty queue.\n");
    }
    return tmp;
} /* queue_get */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void queue_drop (queue_t * q) {
    struct sk_buff * tmp;

    assert (q != NULL);
    if (!queue_is_empty (q)) {
	tmp = queue_get (q);
	KFREE_SKB (tmp);
    } else {
	lprintf (KERN_ERR, "Cannot read empty queue.\n");
    }
} /* queue_drop */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int queue_size (queue_t * q) { 

    assert (q != NULL);
    return (int) q->num; 
} /* queue_size */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int queue_is_empty (queue_t * q) { 

    assert (q != NULL);
    return (q->num == 0) ? TRUE : FALSE; 
} /* queue_is_empty */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int queue_is_full (queue_t * q) { 

    assert (q != NULL);
    return FALSE; 
} /* queue_is_full */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void queue_park (queue_t * q, unsigned appl, NCCI_t ncci, unsigned hand) {
    struct sk_buff * eoq;
    qitem_t *        item;

    assert (q != NULL);
    if (NULL == (eoq = queue_get (q))) {
	lprintf (KERN_ERR, "Cannot park from empty queue.\n");
	return;
    }
    if (NULL == (item = (qitem_t *) hmalloc (sizeof (qitem_t)))) {
	lprintf (KERN_ERR, "Not enough memory for queue item.\n");
	return;
    }
    item->key  = MAKE_KEY (appl, ncci, hand);
    item->msg  = eoq;
    item->succ = q->noconf;
    item->pred = NULL;
    if (q->noconf != NULL) {
	q->noconf->pred = item;
    }
    q->noconf = item;
} /* queue_park */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void queue_conf (queue_t * q, unsigned appl, NCCI_t ncci, unsigned hand) {
    qitem_t * item;
    tag_t     key = MAKE_KEY (appl, ncci, hand);

    assert (q != NULL);
    item = q->noconf;
    while ((item != NULL) && (item->key != key)) {
	item = item->succ;
    }
    if (item != NULL) {
	if (item->succ != NULL) {
	    item->succ->pred = item->pred;
	}
	if (item->pred != NULL) {
	    item->pred->succ = item->succ;
	} else {
	    q->noconf = item->succ;
	}
	assert (item->msg);
	KFREE_SKB (item->msg);
	hfree (item);
    } else {
	log ("Tried to confirm unknown data b3 message.\n");
    }
} /* queue_conf */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/

