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
 * Revision 1.2  1996/05/10 08:48:32  fritz
 * Corrected adpcm bugs.
 *
 * Revision 1.1  1996/04/30 09:29:06  fritz
 * Taken under CVS control.
 *
 */

#define DTMF_NPOINTS 205       /* Number of samples for DTMF recognition */
typedef struct audio_state {
        int  a;
        int  d;
        int  word;
        int  nleft;
        int  nbits;
        char dtmf_last;
        int  dtmf_idx;
        int  dtmf_buf[DTMF_NPOINTS];
} audio_state;

extern void isdn_audio_ulaw2alaw(unsigned char *, unsigned long);
extern void isdn_audio_alaw2ulaw(unsigned char *, unsigned long);
extern audio_state *isdn_audio_state_init(int);
extern int  isdn_audio_adpcm2xlaw(audio_state *, int, unsigned char *, unsigned char *, int);
extern int  isdn_audio_xlaw2adpcm(audio_state *, int, unsigned char *, unsigned char *, int);
extern int  isdn_audio_2adpcm_flush(audio_state *s, unsigned char *out);
extern void isdn_audio_calc_dtmf(modem_info *, unsigned char *, int, int);
extern void isdn_audio_eval_dtmf(modem_info *);
