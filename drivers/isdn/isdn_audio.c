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
 * Revision 1.1.1.1  1996/04/28 12:25:40  fritz
 * Taken under CVS control
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_audio.h"

/*
 * 8-bit-linear <-> alaw conversion stuff
 * This is simply done with lookup-tables.
 */

static unsigned char isdn_audio_a2ltable[] = {
         45,214,122,133,  0,255,107,149, 86,171,126,129,  0,255,117,138,
         13,246,120,135,  0,255, 99,157, 70,187,124,131,  0,255,113,142,
         61,198,123,132,  0,255,111,145, 94,163,127,128,  0,255,119,136,
         29,230,121,134,  0,255,103,153, 78,179,125,130,  0,255,115,140,
         37,222,122,133,  0,255,105,151, 82,175,126,129,  0,255,116,139,
          5,254,120,135,  0,255, 97,159, 66,191,124,131,  0,255,112,143,
         53,206,123,132,  0,255,109,147, 90,167,127,128,  0,255,118,137,
         21,238,121,134,  0,255,101,155, 74,183,125,130,  0,255,114,141,
         49,210,123,133,  0,255,108,148, 88,169,127,129,  0,255,118,138,
         17,242,121,135,  0,255,100,156, 72,185,125,131,  0,255,114,142,
         64,194,124,132,  0,255,112,144, 96,161,128,128,  1,255,120,136,
         33,226,122,134,  0,255,104,152, 80,177,126,130,  0,255,116,140,
         41,218,122,133,  0,255,106,150, 84,173,126,129,  0,255,117,139,
          9,250,120,135,  0,255, 98,158, 68,189,124,131,  0,255,113,143,
         57,202,123,132,  0,255,110,146, 92,165,127,128,  0,255,119,137,
         25,234,121,134,  0,255,102,154, 76,181,125,130,  0,255,115,141
};

static unsigned char isdn_audio_l2atable[] = {
        252,172,172,172,172, 80, 80, 80, 80,208,208,208,208, 16, 16, 16,
         16,144,144,144,144,112,112,112,112,240,240,240,240, 48, 48, 48,
         48,176,176,176,176, 64, 64, 64, 64,192,192,192,192,  0,  0,  0,
          0,128,128,128,128, 96, 96, 96, 96,224,224,224,224, 32, 32, 32,
        160,160, 88, 88,216,216, 24, 24,152,152,120,120,248,248, 56, 56,
        184,184, 72, 72,200,200,  8,  8,136,136,104,104,232,232, 40, 40,
        168, 86,214, 22,150,118,246, 54,182, 70,198,  6,134,102,230, 38,
        166,222,158,254,190,206,142,238,210,242,194,226,218,250,202,234,
        235,203,251,219,227,195,243,211,175,239,143,207,191,255,159,223,
        167, 39,231,103,135,  7,199, 71,183, 55,247,119,151, 23,215, 87,
         87,169,169, 41, 41,233,233,105,105,137,137,  9,  9,201,201, 73,
         73,185,185, 57, 57,249,249,121,121,153,153, 25, 25,217,217, 89,
         89, 89,161,161,161,161, 33, 33, 33, 33,225,225,225,225, 97, 97,
         97, 97,129,129,129,129,  1,  1,  1,  1,193,193,193,193, 65, 65,
         65, 65,177,177,177,177, 49, 49, 49, 49,241,241,241,241,113,113,
        113,113,145,145,145,145, 17, 17, 17, 17,209,209,209,209, 81,253
};

#if ((CPU == 386) || (CPU == 486) || (CPU == 586))
static inline void
isdn_audio_transasm(const void *table, void *buff, unsigned long n)
{
        __asm__("cld\n"
                "1:\tlodsb\n\t"
                "xlatb\n\t"
                "stosb\n\t"
                "loop 1b\n\t"
                ::"b" ((long)table), "c" (n), "D" ((long)buff), "S" ((long)buff)
                :"bx","cx","di","si","ax");
}

void
isdn_audio_a2l(unsigned char *buff, unsigned long len)
{
        isdn_audio_transasm(isdn_audio_a2ltable, buff, len);
}

void
isdn_audio_l2a(unsigned char *buff, unsigned long len)
{
        isdn_audio_transasm(isdn_audio_l2atable, buff, len);
}
#else
void
isdn_audio_a2l(unsigned char *buff, unsigned long len)
{
        while (len--) {
                *buff = isdn_audio_a2ltable[*buff];
                buff++;
        }
}

void
isdn_audio_l2a(unsigned char *buff, unsigned long len)
{
        while (len--) {
                *buff = isdn_audio_l2atable[*buff];
                buff++;
        }
}
#endif

/*
 * linear <-> adpcm conversion stuff
 * Parts from the mgetty-package (C) by Gert Doering and Klaus Weidner
 * Used by permission of Gert Doering
 */

static int Mx[3][8] = {
        { 0x3800, 0x5600, 0,0,0,0,0,0 },
        { 0x399a, 0x3a9f, 0x4d14, 0x6607, 0,0,0,0 },
        { 0x3556, 0x3556, 0x399A, 0x3A9F, 0x4200, 0x4D14, 0x6607, 0x6607 },
};

static int bitmask[9] = {
        0, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff
}; 

static int
isdn_audio_get_bits (int nbits, adpcm_state *s, unsigned char *in, int *len)
{
        while( s->nleft < nbits) {
                int d = *in++;
                (*len)--;
                s->word = (s->word << 8) | d;
                s->nleft += 8;
        }
        s->nleft -= nbits;
        return (s->word >> s->nleft) & bitmask[nbits];
}

static void
isdn_audio_put_bits (int data, int nbits, adpcm_state *s,
                     unsigned char *out, int *len)
{
        s->word = (s->word << nbits) | (data & bitmask[nbits]);
        s->nleft += nbits;
        while(s->nleft >= 8) {
                int d = (s->word >> (s->nleft-8));
                *out++ = d & 255;
                (*len)++;
                s->nleft -= 8;
        }
}

adpcm_state *
isdn_audio_adpcm_init(int nbits)
{
        adpcm_state *s;

        s = (adpcm_state *) kmalloc(sizeof(adpcm_state), GFP_ATOMIC);
        if (s) {
                s->a     = 0;
                s->d     = 5;
                s->word  = 0;
                s->nleft = 0;
                s->nbits = nbits;
        }
        return s;
}

/*
 * Decompression of adpcm data to 8-bit linear
 *
 */
 
int
isdn_audio_adpcm2lin (adpcm_state *s, unsigned char *in,
                      unsigned char *out, int len)
{
        int a = s->a;
        int d = s->d;
        int nbits = s->nbits;
        int olen = 0;
        
        while (len) {
                int sign;
                int e = isdn_audio_get_bits(nbits, s, in, &len);

                if (nbits == 4 && e == 0)
                        d = 4;
                sign = (e >> (nbits-1))?-1:1;
                e &= bitmask[nbits-1];
#if 0                
                if (rom >= 610 && rom < 612) {
                        /* modified conversion algorithm for ROM >= 6.10 */
                        a = (a * 3973 + 2048) >>12;
                } else if (rom>=612) {
                        /* modified conversion algorithm for ROM >= 6.12 */
                        a = (a * 4093 + 2048) >>12;
                }
#endif                
                a += sign * ((e << 1) + 1) * d >> 1;
                if (d & 1)
                        a++;
                *out++ = (a << 6);
                olen++;
                d = (d * Mx[nbits-2][ e ] + 0x2000) >> 14;
                if ( d < 5 )
                        d = 5;     
        }
        return olen;
}

int
isdn_audio_lin2adpcm (adpcm_state *s, unsigned char *in,
                      unsigned char *out, int len)
{
        int a = s->a;
        int d = s->d;
        int nbits = s->nbits;
        int olen = 0;

        while (len--) {
                int e = 0, nmax = 1 << (nbits - 1);
                int sign, delta;
                
                delta = (*in++ << 6) - a;
                if (delta < 0) {
                        e = nmax;
                        delta = -delta;
                }
                while( --nmax && delta > d ) {
                        delta -= d;
                        e++;
                }
                if (nbits == 4 && ((e & 0x0f) == 0))
                        e = 8;
                isdn_audio_put_bits(e, nbits, s, out, &olen);
#if 0
                if(rom >= 610 && rom < 612) {
                        /* modified conversion algorithm for ROM >= 6.10 */
                        a = (a * 3973 + 2048) >>12;
                } else if(rom>=612) {
                        /* modified conversion algorithm for ROM >= 6.12 */
                        a = (a * 4093 + 2048) >>12;
                }
#endif                
                sign = (e >> (nbits-1))?-1:1 ;
                e &= bitmask[nbits-1];
                
                a += sign * ((e << 1) + 1) * d >> 1;
                if (d & 1)
                        a++;
                d = (d * Mx[nbits-2][ e ] + 0x2000) >> 14;
                if (d < 5)
                        d=5;
        }
        if (s->nleft)
                isdn_audio_put_bits(0, 8-s->nleft, s, out, &olen);
        return olen;
}
