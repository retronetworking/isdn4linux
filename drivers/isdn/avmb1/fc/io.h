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

#ifndef __have_io_h__
#define __have_io_h__

#define	BYTE	unsigned char
#define	DWORD	unsigned long

extern void outBYTE  (unsigned port, BYTE  value);
extern void outDWORD (unsigned port, DWORD value);

extern BYTE  inBYTE  (unsigned port);
extern DWORD inDWORD (unsigned port);

extern void outBYTEblock  (unsigned port, BYTE *  buffer, unsigned count);
extern void outDWORDblock (unsigned port, DWORD * buffer, unsigned count);

extern void inBYTEblock  (unsigned port, BYTE *  buffer, unsigned count);
extern void inDWORDblock (unsigned port, DWORD * buffer, unsigned count);

#endif

