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

#include <linux/skbuff.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include "driver.h"
#include "queue.h"
#include "tables.h"
#include "main.h"
#include "defs.h"
#include "tools.h"

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void table_init (appltab_t ** tab) {

    if (NULL != (*tab = (appltab_t *) hmalloc (sizeof (appltab_t)))) {
	(*tab)->appl_root  = NULL;
	(*tab)->appl_count = 0;
    }
} /* table_init */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void table_exit (appltab_t ** tab) {
    appl_t * appp;
    appl_t * tmp;
    
    assert (*tab);
    appp = (*tab)->appl_root;
    while (appp != NULL) {
	tmp = appp->succ;
	dec_use_count ();
        capi_card->count--;
	remove_appl (*tab, appp);
	if (appp->data != NULL) {
	    hfree (appp->data);
	}
	appp = tmp;
    }
    hfree (*tab);
    *tab = NULL;
} /* table_exit */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appl_t * create_appl (appltab_t * tab, unsigned id, 
			  unsigned ncount, unsigned bcount, unsigned bsize) {
    appl_t * appp;

    if (NULL == (appp = (appl_t *) hmalloc (sizeof (appl_t)))) {
	lprintf (KERN_ERR, "Not enough memory for application record.\n");
	return NULL;
    }
    appp->id         = id;
    appp->ncci_count = ncount;
    appp->blk_count  = bcount;
    appp->blk_size   = bsize;
    appp->dying      = FALSE;
    appp->nncci      = 0;
    appp->root       = NULL;
    appp->pred       = NULL;
    appp->succ       = tab->appl_root;
    tab->appl_root = appp;
    if (NULL != appp->succ) {
	appp->succ->pred = appp;
    }
    tab->appl_count++;
    return appp;
} /* create_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void remove_appl (appltab_t * tab, appl_t * appp) {
    ncci_t * nccip;
    ncci_t * tmp;

    assert (appp);
    if (appp->pred != NULL) {
	appp->pred->succ = appp->succ;
    } else {
	tab->appl_root = appp->succ;
    }
    if (appp->succ != NULL) {
	appp->succ->pred = appp->pred;
    }
    if (appp->data != NULL) {
	hfree (appp->data);
	appp->data = NULL;
    }
    nccip = appp->root;
    while (nccip != NULL) {
	tmp = nccip->succ;
	remove_ncci (tab, appp, nccip);
	nccip = tmp;
    }
    hfree (appp);
    tab->appl_count--;
} /* remove_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appl_t * search_appl (appltab_t * tab, unsigned id) {
    appl_t * appp;

    appp = tab->appl_root;
    while (appp != NULL) {
	if (appp->id == id) {
	    break;
	}
	appp = appp->succ;
    }
    return appp;
} /* search_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appl_t * get_appl (appltab_t * tab, unsigned ix) {
    appl_t * appp = NULL;

    assert (ix < tab->appl_count);
    if (ix < tab->appl_count) {
	appp = tab->appl_root;
	while (ix > 0) {
	    appp = appp->succ;
	    --ix;
	}
    }
    return appp;
} /* get_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appl_t * next_appl (appltab_t * tab, appl_t * appp) {

    UNUSED_ARG (tab);
    assert (appp);
    return appp->succ;
} /* next_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appl_t * first_appl (appltab_t * tab) {

    return tab->appl_root;
} /* first_appl */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int handle_message (appltab_t * tab, appl_t * appp, struct sk_buff * msg) {
    int ok;

    UNUSED_ARG (tab);
    UNUSED_ARG (appp);
    assert (msg);
    if ((ok = !queue_is_full (capi_card->queue))) {
	queue_put (capi_card->queue, msg);
    } else {
	lprintf (KERN_ERR, "Message queue overflow. Message lost...\n");
	KFREE_SKB (msg);
    }
    return ok;
} /* handle_message */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
ncci_t * create_ncci (appltab_t * tab, unsigned appl, 
				NCCI_t ncci, unsigned wsize, unsigned bsize) {
    appl_t *         appp;
    ncci_t *         tmp;
    unsigned char ** data;

    if (NULL == (appp = search_appl (tab, appl))) {
	log ("Cannot create NCCIs for unknown applications (id #%u).\n", appl);
	return NULL;
    }
    if (NULL == (tmp = (ncci_t *) hmalloc (sizeof (ncci_t)))) {
	lprintf (KERN_ERR, "Failed to allocate NCCI record.\n");
	return NULL;
    }
    if (NULL == (data = (unsigned char **) hcalloc (sizeof (unsigned char *) * appp->blk_count))) {
	lprintf (KERN_ERR, "Failed to allocate data buffer directory.\n");
	hfree (tmp);
	return NULL;
    }
    tmp->ncci     = ncci;
    tmp->appl     = appl;
    tmp->win_size = wsize;
    tmp->blk_size = bsize;
    tmp->data     = data;
    tmp->pred     = NULL;
    tmp->succ     = appp->root;
    appp->root    = tmp;
    if (NULL != tmp->succ) {
	tmp->succ->pred = tmp;
    }
    appp->nncci++;
    return tmp;
} /* create_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void remove_ncci (appltab_t * tab, appl_t * appp, ncci_t * nccip) {
    unsigned i;

    UNUSED_ARG (tab);
    assert (appp);
    assert (nccip);
    if (nccip != NULL) {
	for (i = 0; i < appp->blk_count; i++) {
	    if (nccip->data[i] != NULL) {
		hfree (nccip->data[i]);
	    }
	}
	hfree (nccip->data);
	if (nccip->succ != NULL) {
	    nccip->succ->pred = nccip->pred;
	}
	if (nccip->pred != NULL) {
	    nccip->pred->succ = nccip->succ;
	} else {
	    appp->root = nccip->succ;
	}
	hfree (nccip);
	appp->nncci--;
    }
} /* remove_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned char * ncci_data_buffer (appltab_t * tab, unsigned appl, 
						NCCI_t ncci, unsigned index) {
    appl_t * appp;
    ncci_t * nccip;
    
    if (NULL == (appp = search_appl (tab, appl))) {
	log ("Data buffer request failed. Application not found.\n");
	return NULL;
    }
    if (NULL == (nccip = locate_ncci (appp, ncci))) {
	log ("Data buffer request failed. NCCI not found.\n");
	return NULL;
    }
    if (index >= appp->blk_count) {
	log ("Data buffer index out of range.\n");
	return NULL;
    }
    if (nccip->data[index] == NULL) {
	if (NULL == (nccip->data[index] = (unsigned char *) hmalloc (appp->blk_size))) {
	    lprintf (KERN_ERR, "Not enough memory for data buffer.\n");
	}
    }
    return nccip->data[index];
} /* ncci_data_buffer */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
ncci_t * locate_ncci (appl_t * appp, NCCI_t ncci) {
    ncci_t * tmp = appp->root;

    while ((tmp != NULL) && (tmp->ncci != ncci)) {
	tmp = tmp->succ;
    }
    return tmp;
} /* locate_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
ncci_t * search_ncci (appltab_t * tab, unsigned appl, NCCI_t ncci) {
    appl_t * appp;

    appp = search_appl (tab, appl);
    return appl ? locate_ncci (appp, ncci) : NULL;
} /* search_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/

