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

#ifndef __have_driver_h__
#define __have_driver_h__

#include <linux/config.h>
#include <linux/skbuff.h>
#include <linux/capi.h>
#include "capilli.h"
#include "tables.h"
#include "queue.h"
#include "libdefs.h"

#define	SHARED_IRQ		/* For PCI only... */

#if defined (CONFIG_ISAPNP)
#include <linux/isapnp.h>
#endif

typedef struct __irq {

    unsigned	id;
} irq_t;

typedef struct __card {

    unsigned		base;
    unsigned		irq;
    unsigned		info;
    unsigned		data;
    char *		version;
    char *		string[8];
    unsigned		count;
    appltab_t *		appls;
    queue_t *		queue;
    unsigned		length;
    void	     (* reg_func) (void *, unsigned);
    void	     (* rel_func) (void *);
    void	     (* dwn_func) (void);
#if defined (CONFIG_ISAPNP)
    struct pci_dev *    dev;
#endif
} card_t;

extern card_t *				capi_card;
extern lib_callback_t *			capi_lib;
extern struct capi_driver_interface *	capi_driver;
extern struct capi_driver		capi_interface;
extern struct capi_ctr *		capi_controller;

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
extern int load_ware(struct capi_ctr * ctrl, capiloaddata * ware);
extern void reset_ctrl(struct capi_ctr * ctrl);
extern void remove_ctrl(struct capi_ctr * ctrl); 
extern void register_appl(struct capi_ctr * ctrl, __u16 appl, 
                                                  capi_register_params * args);
extern void release_appl(struct capi_ctr * ctrl, __u16 appl); 
extern void send_msg(struct capi_ctr * ctrl, struct sk_buff * skb); 
extern char * proc_info(struct capi_ctr * ctrl);
extern int ctr_info(char * page, char ** start, off_t ofs, int count, 
                                             int * eof, struct capi_ctr * ctr);
extern int drv_info(char * page, char ** start, off_t ofs, int count, 
                                          int * eof, struct capi_driver * drv); 
extern int add_card(struct capi_driver * drv, capicardparams *args); 

extern void * data_by_id (unsigned appl_id);
extern struct capi_ctr * card_by_id (unsigned appl_id);
extern void * first_data (int * res);
extern void * next_data (int * res);

extern int appl_profile (unsigned appl_id, unsigned * blksize, unsigned * blkcount);

extern int msg2stack (unsigned char * msg);
extern void msg2capi (unsigned char * msg);

extern void new_ncci (unsigned appl_id, __u32 ncci, unsigned winsize, unsigned blksize);
extern void free_ncci (unsigned appl_id, __u32 ncci);

extern unsigned char * data_block (unsigned appl_id, __u32 ncci, unsigned handle);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
extern void enter_critical (void);
extern void leave_critical (void);

extern int params_ok (card_t * card);

extern int install_card (card_t * card);
extern void remove_card (card_t * card);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
extern void init (unsigned len, 
		  void  (* reg) (void *, unsigned),
		  void  (* rel) (void *),
		  void  (* dwn) (void));

extern int start (card_t * card);
extern void stop (card_t * card);

#if defined (__fcpcmcia__)
extern int avm_a1pcmcia_addcard (unsigned int port, unsigned irq);
extern int avm_a1pcmcia_delcard (unsigned int port, unsigned irq);
#endif

extern int driver_init (void);
extern void driver_exit (void);

#endif

