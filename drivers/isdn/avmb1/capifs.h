/*
 * $Id$
 * 
 * (c) Copyright 2000 by Carsten Paeth (calle@calle.de)
 *
 * $Log$
 * Revision 1.1  2000/03/03 16:48:38  calle
 * - Added CAPI2.0 Middleware support (CONFIG_ISDN_CAPI)
 *   It is now possible to create a connection with a CAPI2.0 applikation
 *   and than to handle the data connection from /dev/capi/ (capifs) and also
 *   using async or sync PPP on this connection.
 *   The two major device number 190 and 191 are not confirmed yet,
 *   but I want to save the code in cvs, before I go on.
 *
 *
 */

void capifs_new_ncci(__u16 applid, __u32 ncci, char *type, kdev_t device);
void capifs_free_ncci(__u16 applid, __u32 ncci, char *type);
