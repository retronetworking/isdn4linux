/*
 * $Id$
 *
 * CAPI 2.0 Interface for Linux
 *
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log$
 * Revision 1.2  1999/06/21 15:24:13  calle
 * extend information in /proc.
 *
 * Revision 1.1  1997/03/04 21:50:30  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

struct capidev {
	int is_open;
	int is_registered;
	__u16 applid;
	struct sk_buff_head recv_queue;
#ifdef COMPAT_HAS_NEW_WAITQ
	wait_queue_head_t recv_wait;
#else
	struct wait_queue *recv_wait;
#endif
	__u16 errcode;
	/* Statistic */
	unsigned long nopen;
	unsigned long nrecvdroppkt;
	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;
};

#define CAPI_MAXMINOR	CAPI_MAXAPPL
