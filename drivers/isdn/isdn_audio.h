/* $Id$
 *
 * Linux ISDN subsystem, audio conversion and compression (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log$
 */

typedef struct adpcm_state {
        int a;
        int d;
        int word;
        int nleft;
        int nbits;
} adpcm_state;

extern void isdn_audio_a2l(unsigned char *, unsigned long);
extern void isdn_audio_l2a(unsigned char *, unsigned long);
extern adpcm_state *isdn_audio_adpcm_init(int);
extern int isdn_audio_adpcm2lin(adpcm_state *, unsigned char *, unsigned char *, int);
extern int isdn_audio_lin2adpcm(adpcm_state *, unsigned char *, unsigned char *, int);
