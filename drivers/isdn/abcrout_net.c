

/* $Id$

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
 * Fa. abc Agentur fuer
 * Bildschirm-Communication GmbH
 * Mercedesstrasse 14
 * 71063 Sindelfingen
 * Germany
 *
 * $Log$
 * Revision 1.1.2.10  1998/05/22 10:03:01  detabc
 * in case of a icmp-unreach condition the tcp-keepalive-entrys
 * will be dropped from the internal double-link-list (only abc-extension).
 *
 * Revision 1.1.2.9  1998/05/07 19:55:12  detabc
 * bugfix in abc_delayed_hangup
 * optimize keepalive-tests for abc_rawip
 *
 * Revision 1.1.2.8  1998/05/06 08:31:47  detabc
 * fixed a wrong response message in udp-control-packets
 *
 * Revision 1.1.2.7  1998/04/27 12:01:06  detabc
 * *** empty log message ***
 *
 * Revision 1.1.2.6  1998/04/26 19:53:24  detabc
 * remove unused code
 *
 * Revision 1.1.2.5  1998/04/26 11:26:47  detabc
 * add abc_tx_queues support.
 * remove some now unused code.
 *
 * Revision 1.1.2.4  1998/04/21 17:56:34  detabc
 * added code to reset secure-counter with a spezial udp-packets
 *
 * Revision 1.1.2.3  1998/03/20 12:25:50  detabc
 * Insert the timru recalc_timeout function in the abc_test_receive() function.
 * The timru function will be called after decrypt and decompress is done.
 * Note! Received pakets will be in same cases queued.
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

