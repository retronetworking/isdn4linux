/*
 * $Id$
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 */

#ifndef __CAPIMINOR_H__
#define __CAPIMINOR_H__

#define CAPINC_NR_PORTS 256
#define CAPINC_MAX_RECVQUEUE	10
#define CAPINC_MAX_SENDQUEUE	10

/* -------- struct capincci ----------------------------------------- */

struct capiminor {
	struct list_head list;
	struct capincci   ncci;
	unsigned int      minor;

	struct file      *file;
	struct tty_struct *tty;
	int                ttyinstop;
	int                ttyoutstop;
	struct sk_buff    *ttyskb;
	atomic_t           ttyopencount;

	struct sk_buff_head inqueue;
	int                 inbytes;
	struct sk_buff_head outqueue;
	int                 outbytes;

	struct syncdev_r sdev;
	wait_queue_head_t sendwait;
	
#ifdef CAPI_PPP_ON_RAW_DEVICE
	/* interface to generic ppp layer */
	struct ppp_channel	chan;
	int			chan_connected;
	int			chan_index;
#endif
};

struct capiminor *capiminor_alloc(u16 applid, u32 ncci);
void capiminor_free(struct capiminor *mp);
struct capiminor *capiminor_find(unsigned int minor);

void handle_minor_recv(struct capiminor *mp);
int handle_minor_send(struct capiminor *mp);

int capinc_tty_init(void);
void capinc_tty_exit(void);

int capiraw_init(void);
void capiraw_exit(void);

#endif /* __CAPIMINOR_H__ */
