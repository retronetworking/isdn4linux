/* $Id$

 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 *
 * $Log$
 * Revision 1.2  1998/04/15 16:47:17  keil
 * new interface
 *
 * Revision 1.1  1997/10/29 18:51:20  keil
 * New files
 *
 */
 
#define ARCOFI_USE	1

extern int send_arcofi(struct IsdnCardState *cs, const u_char *msg, int bc, int receive);
