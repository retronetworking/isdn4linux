/* $Id$

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 * Revision 1.1.2.1  1997/11/15 18:57:38  keil
 * ARCOFI 2165 support
 *
 *
 */
 
#define ARCOFI_USE	1

extern int send_arcofi(struct IsdnCardState *cs, const u_char *msg, int bc);
