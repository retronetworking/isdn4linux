
/*
 *
  Copyright (c) Eicon Networks, 2000.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    1.9
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __DIVA_DIDD_DADAPTER_INC__
#define __DIVA_DIDD_DADAPTER_INC__
void diva_didd_load_time_init (void);
void diva_didd_load_time_finit (void);
int diva_didd_add_descriptor (DESCRIPTOR* d);
int diva_didd_remove_descriptor (IDI_CALL request);
int diva_didd_read_adapter_array (DESCRIPTOR* buffer, int length);
#define OLD_MAX_DESCRIPTORS     16
#define NEW_MAX_DESCRIPTORS     64
#endif
