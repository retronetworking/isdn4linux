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

#include <asm/io.h>

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned char InpByte (unsigned port) {

	return inb (port);
} /* InpByte */

void OutpByte (unsigned port, unsigned char data) { 

	outb (data, port); 
} /* OutpByte */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned long InpDWord (unsigned port) { 

	return inl (port); 
} /* InpDWord */

void OutpDWord (unsigned port, unsigned long data) { 

	outl (data, port); 
} /* OutpDWord */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void InpByteBlock (unsigned port, unsigned char * buffer, unsigned length) {

	insb (port, buffer, length);
} /* InpByteBlock */

void OutpByteBlock (unsigned port, unsigned char * buffer, unsigned length) {

	outsb (port, buffer, length);
} /* OutpByteBlock */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void InpDWordBlock (unsigned port, unsigned char * buffer, unsigned length) {

	insl (port, buffer, (length + 3) / 4);
} /* InpDWordBlock */

void OutpDWordBlock (unsigned port, unsigned char * buffer, unsigned length) {

	outsl (port, buffer, (length + 3) / 4);
} /* OutpDWordBlock */

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/

