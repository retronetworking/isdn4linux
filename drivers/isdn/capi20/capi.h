/*
 * $Id$
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * 2001-03-15 : big cleanup
 *              changed structure to being more modular
 *              Kai Germaschewski (kai.germaschewski@gmx.de
 *
 */

#ifndef __CAPI_H__
#define __CAPI_H__

#define HDEBUG do { printk( "%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__); } while (0)

#include "syncdev.h"

/* 
 * Each open instance of /dev/capi20 will be associated with
 * a struct capidev via file->private.
 * The details should be private to capi20.c, but users of
 * the middleware extensions (see below), can access 
 */

struct capidev {
	spinlock_t       lock;
	struct list_head list;
	u16		 applid;
	u16		 errcode;
#ifdef CONFIG_ISDN_KCAPI_LEGACY
	unsigned         userflags;
#endif
	struct syncdev_r sdev;

	/* Statistic */
	unsigned long	 nrecvctlpkt;
	unsigned long	 nrecvdatapkt;
	unsigned long	 nsentctlpkt;
	unsigned long	 nsentdatapkt;

#ifdef CONFIG_ISDN_KCAPI_MIDDLEWARE
	struct list_head ncci_list;
#endif /* CONFIG_ISDN_KCAPI_MIDDLEWARE */
};

#ifdef CONFIG_ISDN_KCAPI_MIDDLEWARE

/*
 * The middleware interface
 *
 * We provide the possibility to handle the actual data transfer
 * via CAPI/ISDN in kernelspace, whereas the connection management
 * ist left to userspace via the usual /dev/capi20 interface.
 *
 * This is useful e.g. to write a Hayes Modem emulator, where
 * we will do parsing of AT commands etc in userspace and accordingly
 * set up a connection, but after we are connected we want to bypass
 * the userspace application and transfer data between tty and CAPI
 * directly.
 *
 * The basic idea is easy: When the userspace application setup a
 * connection and now has an NCCI in N-ACT (active) state, it calls
 * a special ioctl on its fd to /dev/capi20 and thus asks the kernel
 * to handle data flow by itself now.
 * (see include/net/capi.h for a description of these ioctls) FIXME
 * 
 * Interface description:
 * 
 * At this time, the possible middleware extensions capi_ppptty and
 * capitty explicitly activate their hook in capi20.c.
 * This will be changed to a more generic {,un}register_capimiddle. FIXME
 * 
 * An extension needs to define the following function:
 *
 *   static int
 *   ncci_connect(struct capidev *cdev, struct ncci_connect_data *data)
 * 
 * This function will be called by capi20.c when an CAPI_NCCI_CONNECT
 * ioctl is called for this extension.
 * 
 * The module should construct a capincci structure and set the 
 * ncci and callbacks. Then, it calls
 *
 *   capincci_hijack(cdev, &cp->ncci);
 *
 * and from there on, all data transfer on this ncci will go via the
 * supplied callbacks. This association will be released when the
 * userspace application calls the CAPI_NCCI_DISCONNECT ioctl or
 * the CAPI application goes down.
 *
 * receiving
 * ---------
 * For each DATA_B3_IND received, the recv callback will be called
 * with a skb which contains the actual data. When the module
 * is done with processing the skb, it should kfree_skb(). We are
 * using the skb->destructor mechanism to generate a corresponding 
 * DATA_B3_RESP. That means the flow control is taken care of automatically, 
 * because no more then 8 unacknowledged
 * frames will be given to the application.
 * NOTE: The module must not change the skb (skb_push/put/...), because
 * otherwise the destructor will produce unexpected behavior / crashes
 * 
 * sending
 * -------
 * The application simply calls capincci_send with an skb. An
 * DATA_B3_REQ message will be generated and sent. The caller
 * has to ensure that the skb has at least CAPI_DATA_B3_REQ_LEN
 * headroom left.
 * If the send queue overflows, the skb is not queued but -EAGAIN is
 * returned.
 * The corresponding DATA_B3_CONF messages are taken care of by
 * capi20.c. The application will only see its send_{wake,stop}_queue 
 * callbacks being called and should act accordingly.
 *
 */

struct datahandle {
	struct list_head list;
	u16              datahandle;
};

// bits for capincci->flags

#define NCCI_RECV_QUEUE_FULL 0 

struct capincci {
	/* to be set before calling capincci_hijack */
	void             *priv;
	u32		 ncci;
	int              (*recv)(struct capincci *np, struct sk_buff *skb);
	void             (*send_stop_queue)(struct capincci *np);
	void             (*send_wake_queue)(struct capincci *np);
	void             (*dtor)(struct capincci *np);

	/* private to capi20 */
	struct list_head list;
	struct capidev	 *cdev;
	unsigned long    flags;

	u16		 datahandle;
	u16		 msgid;

	/* transmit path */
	spinlock_t       lock;
	struct list_head ackqueue;
	int              nack;
};

extern int capincci_send(struct capincci *np, struct sk_buff *skb);
extern void capincci_hijack(struct capidev *cdev, struct capincci *np);
extern void capincci_unhijack(struct capincci *np);

#ifdef CONFIG_ISDN_KCAPI_LEGACY
/* legacy capiminor stuff */

void (*capiminor_ncciup_p)(struct capidev *cdev, u32 ncci);
int (*capiminor_ncci_getunit_p)(struct capidev *cdev, struct capincci *np);

#endif /* CONFIG_ISDN_KCAPI_LEGACY */

#endif /* CONFIG_ISDN_KCAPI_MIDDLEWARE */

// current interface for capitty - FIXME

extern struct file_operations *capitty_dev_fops;

#endif /* __CAPI_H__ */
