
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
 */

#ifndef ISDN_DWABC_H
#define ISDN_DWABC_H

#define DWABC_LCR_FLG_NEWNUMBER		0x00000001L
#define DWABC_LCR_FLG_DISABLE		0x00000002L
#define DWABC_LCR_FLG_NEWHUPTIME	0x00000004L


struct ISDN_DWABC_LCR_IOCTL {

	int 	lcr_ioctl_sizeof;	/* mustbe sizeof(ISDN_DWABC_LCR_IOCTL)		*/
	u_short lcr_ioctl_onhtime;	/* new hanguptime							*/
	u_long 	lcr_ioctl_callid;	/* callid from lcr-subsystem				*/
	u_long 	lcr_ioctl_flags;	/* see above								*/
	char 	lcr_ioctl_nr[32];	/* new destination phonenumber				*/

};

#endif
