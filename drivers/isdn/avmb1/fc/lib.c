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

#include <asm/param.h>
#include <asm/io.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <stdarg.h>
#include "driver.h"
#include "queue.h"
#include "defs.h"
#include "tools.h"
#include "libstub.h"
#include "lib.h"

#ifdef __SMP__
#error The code has not yet been prepared for SMP machines...
#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os__enter_critical (const char * f, int l) { 

    f = (void *) l;
    enter_critical (); 
} /* os__enter_critical */

void os__leave_critical (const char * f, int l) {

    f = (void *) l;
    leave_critical ();
} /* os__leave_critical */

void os_enter_critical (void) { enter_critical (); }

void os_leave_critical (void) { leave_critical (); } 

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_enter_cache_sensitive_code (void) { /* NOP */ }

void os_leave_cache_sensitive_code (void) { /* NOP */ }

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_init (unsigned len, register_t reg, release_t rel, down_t down) {

    init (len, reg, rel, down);
} /* os_init */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
char * os_params (void) {

    return NULL;
} /* os_params */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int os_get_message (unsigned char * msg) {

    assert (msg);
    return msg2stack (msg); 
} /* os_get_message */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_put_message (unsigned char * msg) {

    assert (msg);
    msg2capi (msg);
} /* os_put_message */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned char * os_get_data_block (unsigned      appl, 
					    unsigned long ncci, 
					    unsigned      index) 
{
    return data_block (appl, ncci, index);
} /* os_get_data_block */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_free_data_block (unsigned appl, unsigned char * data) {

    data = (void *) appl;	/* Avoid "unused arg" warning */
} /* os_free_data_block */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int os_new_ncci (unsigned appl, unsigned long ncci, 
					unsigned win_size, unsigned blk_size) 
{
    new_ncci (appl, ncci, win_size, blk_size); 
    return 1;
} /* os_new_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_free_ncci (unsigned appl, unsigned long ncci) {

    free_ncci (appl, ncci);
} /* os_free_ncci */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned os_block_size (unsigned appl) {
    unsigned bs;

    appl_profile (appl, &bs, NULL);
    return bs;
} /* os_block_size */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned os_window_size (unsigned appl) {
    unsigned bc;

    appl_profile (appl, NULL, &bc);
    return bc;
} /* os_window_size */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned os_card (void) {

    return 0;
} /* os_card */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * os_appl_data (unsigned appl) {

    return data_by_id (appl);
} /* os_appl_data */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appldata_t * os_appl_1st_data (appldata_t * s) {
    int		num;
    void *	data;
    static char e[10]; 

    memset (&e, 0, 10);
    data = first_data (&num);
    s->num    = num;
    s->buffer = (NULL == data) ? e : data;
    return s;
} /* os_appl_1st_data */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
appldata_t * os_appl_next_data (appldata_t * s) {
    int		num;
    void *	data;
    static char e[10]; 

    memset (&e, 0, 10);
    if ((num = s->num) < 0) {	
        return NULL;
    };
    data = next_data (&num);
    s->num    = num;
    s->buffer = (NULL == data) ? e : data;
    return s;
} /* os_appl_next_data */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void * os_malloc (unsigned len) {

    return hcalloc (len); 
} /* os_malloc */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_free (void * p) {

    assert (p);
    hfree (p);
} /* os_free */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned long os_msec (void) {

    return (((unsigned long long) jiffies) * 1000) / HZ;
} /* os_msec */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
typedef struct {

	unsigned long	lstart;
	unsigned long	start;
	unsigned long	tics;
	unsigned long	arg;
	timer_t		func;
} timer_rec_t;

#define	TIC_PER_SEC		250
#define	TIC_PER_MSEC		4
#define	Time()			(avm_time_base * TIC_PER_MSEC)

static volatile timer_rec_t *	timer = 0;
static unsigned			timer_count = 0;

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int os_timer_new (unsigned ntimers) {
    unsigned size;

    assert (ntimers > 0); 
    timer_count = ntimers;
    
    size = sizeof (timer_rec_t) * ntimers;
    timer = (timer_rec_t *) hcalloc (size);
    info (timer != NULL);
    if (NULL == timer) {
        timer_count = 0;
    } 
    return timer == NULL;
} /* os_timer_new */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_timer_delete (void) {

    hfree ((void *) timer);
    timer = NULL;
    timer_count = 0;
} /* os_timer_delete */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int os_timer_start (unsigned      index,
		    unsigned long timeout,
		    unsigned long arg,
		    timer_t	  func)	
{
    assert (index < timer_count);
    if (index >= timer_count) {
        return 1;
    }
    enter_critical ();
    timer[index].start = avm_time_base;
    timer[index].tics  = (timeout + TIC_PER_MSEC) / TIC_PER_MSEC;
    timer[index].arg   = arg;
    timer[index].func  = func;
    leave_critical ();
    return 0;
} /* os_timer_start */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int os_timer_stop (unsigned index) {

    assert (index < timer_count);
    if (index >= timer_count) {
        return 1;
    }
    enter_critical ();
    timer[index].func = NULL;
    leave_critical ();
    return 0;
} /* os_timer_stop */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_timer_poll (void) {
    unsigned  i;
    restart_t flag;

    if (NULL == timer) {
        return;
    }
    enter_critical ();
    for (i = 0; i < timer_count; i++) {
        if (timer[i].func != 0) {
            if ((avm_time_base - timer[i].start) >= timer[i].tics) {
                leave_critical ();
                assert (timer[i].func);
		flag = (*timer[i].func) (timer[i].arg);
		enter_critical ();
                if (timer_restart == flag) {
                    timer[i].start = avm_time_base;
                } else {
                    timer[i].func = 0;
                }
            }
        }
    }
    leave_critical ();
} /* os_timer_poll */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static int	nl_needed = 0;

void os_printf (char * s, va_list args) {
#if !defined (NDEBUG)
    char   buffer[500];
    char * bufptr = buffer;
    int    count;

    if (nl_needed) {
	nl_needed = 0;
	printk ("\n");
    }	
    count = vsprintf (bufptr, s, args);
    if ('\n' == buffer[0]) {
	bufptr++;
    }
    if ('\n' != buffer[count - 1]) {
	assert (count < 498);
	buffer[count++] = '\n';
	buffer[count]   = (char) 0;
    }
    lprintf (KERN_INFO, bufptr);
#else
    s = s;
#endif
} /* os_printf */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void os_puts (char * str, ...) { 
    va_list dummy;

    va_start (dummy, str);
    os_printf (str, dummy);
    va_end (dummy);
} /* os_puts */
 
void os_putl (long l) { 

    nl_needed = 1; 
    lprintf (KERN_INFO, "%ld", l); 
}  /* os_putl */

void os_puti (int i) { 

    nl_needed = 1; 
    lprintf (KERN_INFO, "%d", i); 
} /* os_puti */

void os_putnl (void) { 

    nl_needed = 0; 
    lprintf (KERN_INFO, "\n"); 
}  /* os_putnl */

void os_putc (char c) {
    char buffer[10];
    
    nl_needed = 1;
    if ((31 < c) && (c < 127)) {
        sprintf (buffer, "'%c' (0x%02x)", c, (unsigned char) c);
    } else {
        sprintf (buffer, "0x%02x", (unsigned char) c);
    }
    lprintf (KERN_INFO, "%s", buffer);
} /* os_putc */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
static lib_callback_t *	lib	= NULL;
static lib_interface_t	libif	= {

	init:				&os_init,
	params:				&os_params,
	get_message:			&os_get_message,
	put_message:			&os_put_message,
	get_data_block:			&os_get_data_block,
	free_data_block:		&os_free_data_block,
	new_ncci:			&os_new_ncci,
	free_ncci:			&os_free_ncci,
	block_size:			&os_block_size,
	window_size:			&os_window_size,
	card:				&os_card,
	appl_data:			&os_appl_data,
	appl_1st_data:			&os_appl_1st_data,
	appl_next_data:			&os_appl_next_data,
	malloc:				&os_malloc,
	free:				&os_free,
	msec:				&os_msec,	
	timer_new:			&os_timer_new,
	timer_delete:			&os_timer_delete,
	timer_start:			&os_timer_start,
	timer_stop:			&os_timer_stop,
	timer_poll:			&os_timer_poll,
	printf:				&os_printf,
	puts:				(void (*) (char *)) &os_puts,
	putl:				&os_putl,
	puti:				&os_puti,
	putc:				&os_putc,
	putnl:				&os_putnl,
	_enter_critical:		&os__enter_critical,
	_leave_critical:		&os__leave_critical,
	enter_critical:			&os_enter_critical,
	leave_critical:			&os_leave_critical,
	enter_cache_sensitive_code:	&os_enter_cache_sensitive_code,	
	leave_cache_sensitive_code:	&os_leave_cache_sensitive_code,
	name:				TARGET,
	udata:				0,
	pdata:				NULL
} ;

lib_callback_t * get_library (void) {

	return lib;
} /* get_library */

lib_callback_t * link_library (void) {

	return (lib = avm_lib_attach (&libif));
} /* bind_library */

void free_library (void) {

	if (lib != NULL) {
		lib = 0;
		avm_lib_detach (&libif);
	}
} /* free_library */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/

