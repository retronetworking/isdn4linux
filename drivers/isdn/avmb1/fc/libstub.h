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

#ifndef __have_libstub_h__
#define __have_libstub_h__

#include "libdefs.h"

#if defined (OSDEBUG) && defined (NDEBUG)
# undef NDEBUG
#endif

extern lib_callback_t * avm_lib_attach (lib_interface_t * lif);
extern void avm_lib_detach (lib_interface_t * lif);
extern volatile unsigned long avm_time_base;

#endif

