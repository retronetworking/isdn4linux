

/* $Id$

 *
 * ONLY FOR KERNEL  > 2.1.0  !!!!!!!!!!!!!!!
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
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
 *
 * Fa. abc Agentur fuer
 * Bildschirm-Communication GmbH
 * Mercedesstrasse 14
 * 71063 Sindelfingen
 * Germany
 *
 *
 * $Log$
 * Revision 1.9  1998/05/22 10:01:20  detabc
 * in case of a icmp-unreach condition the tcp-keepalive-entrys
 * will be dropped from the internal double-link-list (only abc-extension).
 * send icmp unreach only if the skb->protocol == ETH_P_IP
 * speedup abc-no-dchan  redial
 *
 * Revision 1.8  1998/05/07 19:58:48  detabc
 * bugfix in abc_delayed_hangup
 * optimize keepalive-tests for abc_rawip
 *
 * Revision 1.7  1998/05/06 08:35:00  detabc
 * fixed a wrong response-message in udp-control-packets
 *
 * Revision 1.6  1998/04/27 12:00:23  detabc
 * *** empty log message ***
 *
 * Revision 1.5  1998/04/26 20:01:17  detabc
 * add new abc-extension-code from 2.0.xx kernels
 * remove some unused code
 *
 * Revision 1.4  1998/04/21 18:03:40  detabc
 * changes for 2.1.x kernels
 *
 * Revision 1.3  1998/03/08 14:26:06  detabc
 * change kfree_skb to dev_kfree_skb
 * remove SET_SKB_FREE
 *
 * Revision 1.2  1998/03/08 13:14:23  detabc
 * abc-extension support for kernels > 2.1.x
 * first try (sorry experimental)
 *
 * Revision 1.1.2.2  1998/03/08 11:35:06  detabc
 * Add cvs header-controls an remove unused funktions
 *
 */

/*
** wegen einstweiliger verfuegung gegen DW ist zur zeit
** die abc-extension bis zur klaerung der rechtslage nicht
** im internet verfuegbar
*/
