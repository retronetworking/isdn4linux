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

#ifndef __have_libdefs_h__
#define __have_libdefs_h__

#include <stdarg.h>

typedef void (* register_t) (void *, unsigned);
typedef void (* release_t) (void *);
typedef void (* down_t) (void);

typedef enum {

	timer_end	= 0,
	timer_restart	= 1
} restart_t;

typedef restart_t (* timer_t) (unsigned long);

typedef struct __data {

	unsigned	num;
	char *		buffer;
} appldata_t;

typedef struct __lib {

	void (* init) (unsigned, register_t, release_t, down_t);
	char * (* params) (void);
	int (* get_message) (unsigned char *);
	void (* put_message) (unsigned char *);
	unsigned char * (* get_data_block) (unsigned, unsigned long, unsigned);
	void (* free_data_block) (unsigned, unsigned char *);
	int (* new_ncci) (unsigned, unsigned long, unsigned, unsigned);
	void (* free_ncci) (unsigned, unsigned long);
	unsigned (* block_size) (unsigned);
	unsigned (* window_size) (unsigned);
	unsigned (* card) (void);
	void * (* appl_data) (unsigned);
	appldata_t * (* appl_1st_data) (appldata_t *);
	appldata_t * (* appl_next_data) (appldata_t *);
	void * (* malloc) (unsigned);
	void (* free) (void *);
	unsigned long (* msec) (void);
	int (* timer_new) (unsigned);
	void (* timer_delete) (void);
	int (* timer_start) (unsigned, unsigned long, unsigned long, timer_t);
	int (* timer_stop) (unsigned);
	void (* timer_poll) (void);
	void (* printf) (char *, va_list);
	void (* puts) (char *);
	void (* putl) (long);
	void (* puti) (int);
	void (* putc) (char);
	void (* putnl) (void);
	void (* _enter_critical) (const char *, int);
	void (* _leave_critical) (const char *, int);
	void (* enter_critical) (void);
	void (* leave_critical) (void);
	void (* enter_cache_sensitive_code) (void);
	void (* leave_cache_sensitive_code) (void);

	char *		name;
	unsigned	udata;
	void *		pdata;
} lib_interface_t;

typedef struct __cb {

	unsigned (* cm_start) (void);
	char * (* cm_init) (unsigned, unsigned);
	int (* cm_activate) (void);
	int (* cm_exit) (void);
	unsigned (* cm_handle_events) (void);
	void (* cm_schedule) (void);
	void (* cm_trigger_timer_irq) (void);
	unsigned (* check_controller) (unsigned, unsigned *);
} lib_callback_t;

#endif

