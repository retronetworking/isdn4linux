/* $Id$
 *
 * Header for the Linux ISDN abc-extension.
 *
 * Author: abc GmbH written by Detlef Wengorz <detlefw@isdn4linux.de> 
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
 * Revision 1.2  1999/09/14 10:16:21  keil
 * change ABC include
 *
 * Revision 1.1  1999/09/12 16:19:40  detabc
 * added abc features
 * low cost routing for net-interfaces (only the HL side).
 * need more implementation in the isdnlog-utility
 * udp info support (first part).
 * different EAZ on outgoing call's.
 * more checks on D-Channel callbacks (double use of channels).
 * tested and running with kernel 2.3.17
 *
 */

#ifndef ISDN_DWABC_H
#define ISDN_DWABC_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif

#define DWABC_LCR_FLG_NEWNUMBER		0x00000001L
#define DWABC_LCR_FLG_DISABLE		0x00000002L
#define DWABC_LCR_FLG_NEWHUPTIME	0x00000004L


struct ISDN_DWABC_LCR_IOCTL {

	int 	lcr_ioctl_sizeof;	/* mustbe sizeof(ISDN_DWABC_LCR_IOCTL)	*/
	u_short lcr_ioctl_onhtime;	/* new hanguptime						*/
	u_long 	lcr_ioctl_callid;	/* callid from lcr-subsystem			*/
	u_long 	lcr_ioctl_flags;	/* see above							*/
	char 	lcr_ioctl_nr[32];	/* new destination phonenumber			*/

};

#endif
