/*
 * $Id$
 *
 * CAPI4Linux
 * 
 * Kernel CAPI 2.0 Driver Interface for Linux
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * 2001-02-06 : Moved from avmb1 directory.
 *              Armin Schindler (mac@melware.de)
 * 
 */
#ifndef __CAPI_DRIVER_H__
#define __CAPI_DRIVER_H__

#include <net/capi/capi.h>
#include <linux/skbuff.h>

#define CARD_FREE	0
#define CARD_DETECTED	1
#define CARD_LOADING	2
#define CARD_RUNNING	3

struct capi_driver;

struct capi_ctr {
        struct capi_ctr *next;			/* next ctr of same driver */
        struct capi_driver *driver;
	int cnr;				/* controller number */
	char name[32];				/* name of controller */
	volatile unsigned short cardstate;	/* controller state */
	volatile int blocked;			/* output blocked */
	int traceflag;				/* capi trace */

	void *driverdata;			/* driver specific */

	/* filled before calling ready callback */
	__u8 manu[CAPI_MANUFACTURER_LEN];	/* CAPI_GET_MANUFACTURER */
	capi_version version;			/* CAPI_GET_VERSION */
	capi_profile profile;			/* CAPI_GET_PROFILE */
	__u8 serial[CAPI_SERIAL_LEN];		/* CAPI_GET_SERIAL */

	/* functions */
        void (*ready)(struct capi_ctr * card);
        void (*reseted)(struct capi_ctr * card);
        void (*suspend_output)(struct capi_ctr * card);
        void (*resume_output)(struct capi_ctr * card);
        void (*handle_capimsg)(struct capi_ctr * card,
			   	__u16 appl, struct sk_buff *skb);
	void (*appl_registered)(struct capi_ctr * card, __u16 appl);
	void (*appl_released)(struct capi_ctr * card, __u16 appl);

        void (*new_ncci)(struct capi_ctr * card,
			   	__u16 appl, __u32 ncci, __u32 winsize);
        void (*free_ncci)(struct capi_ctr * card, __u16 appl, __u32 ncci);

	/* management information for kcapi */

	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;

	struct proc_dir_entry *procent;
        char procfn[128];
};

struct capi_driver_interface {
	struct capi_ctr *(*attach_ctr)(struct capi_driver *driver, char *name, void *data);
	int (*detach_ctr)(struct capi_ctr *);
};

struct capi_driver {
	struct module *owner;
	
	char name[32];				/* driver name */
	char revision[32];
	void (*register_appl)(struct capi_ctr *, __u16 appl,
			      capi_register_params *);
	void (*release_appl)(struct capi_ctr *, __u16 appl);
	void (*send_message)(struct capi_ctr *, struct sk_buff *skb);
	
	char *(*procinfo)(struct capi_ctr *);
	int (*ctr_read_proc)(char *page, char **start, off_t off,
			     int count, int *eof, struct capi_ctr *card);
	int (*driver_read_proc)(char *page, char **start, off_t off,
				int count, int *eof, struct capi_driver *driver);

	/* data in user space */
	int (*conf_driver)(struct capi_driver *driver, int cmd, void *data);
	int (*conf_controller)(struct capi_ctr *ctr, int cmd, void *data);
	
	
	/* intitialized by kcapi */
	struct capi_ctr	*controller;		/* list of controllers */
	struct capi_driver *next;
	int ncontroller;
	struct proc_dir_entry *procent;
	char procfn[128];
};

struct capi_driver_interface *attach_capi_driver(struct capi_driver *driver);
void detach_capi_driver(struct capi_driver *driver);

#endif				/* __CAPI_DRIVER_H__ */
