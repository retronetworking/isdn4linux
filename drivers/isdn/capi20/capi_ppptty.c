/*
 * $Id$
 *
 * CAPI4Linux
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 2001 by Kai Germaschewski (kai.germaschewski@gmx.de)
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * based up on the middleware extensions by Carsten Paeth
 * 
 * This module, along with the userspace pppd plugin 
 * "capipppdplugin" allows to establish sync/async PPP connections
 * via the /dev/capi20 interface, whereas the actual data connection
 * will be handled by an emulated tty.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/capi/capi.h>
#include <net/capi/command.h>
#include "capi.h"
#include <linux/isdn_compat.h>

// ----------------------------------------------------------------------

#define CAPI_PPPTTY_NR_PORTS 256

#define CAPI_MAX_BLKSIZE	2048

// bits for capi_ppptty->flags
#define XMIT_QUEUE_FULL         0

// ----------------------------------------------------------------------

struct capi_ppptty {
	struct list_head    list;

	struct capincci     ncci;
	unsigned int        minor;
	unsigned long       flags;
	struct tty_struct  *tty;
	atomic_t            ttyopencount;

	struct sk_buff_head recv_queue;
};

// ----------------------------------------------------------------------

static int capi_ttymajor = 191;
MODULE_PARM(capi_ttymajor, "i");

// ----------------------------------------------------------------------

static spinlock_t ppptty_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(ppptty_list);

// ------------------------------------------------- receiving ---------

static int
receive_buf(struct capi_ppptty *cp, char *buf, int len)
{
	struct tty_struct *tty = cp->tty;

	if (!tty) {
		// discard frame FIXME?
		HDEBUG;
		return 0;
	}
	
 	if (tty->ldisc.receive_room(tty) < len) {
		/* check TTY_THROTTLED first so it indicates our state */
		if (!test_and_set_bit(TTY_THROTTLED, &tty->flags) &&
		    tty->driver.throttle)
			tty->driver.throttle(tty);
		
		HDEBUG;
		return -EBUSY;
	}
	tty->ldisc.receive_buf(tty, buf, 0, len);
	return 0;
}

static void
run_recv_queue(struct capi_ppptty *cp)
{
	struct tty_struct *tty = cp->tty;
	struct sk_buff *skb;

	if (tty && test_bit(TTY_THROTTLED, &tty->flags))
		return;

	while ((skb = skb_dequeue(&cp->recv_queue))) {
		if (receive_buf(cp, skb->data, skb->len) < 0) {
			HDEBUG;
			skb_queue_head(&cp->recv_queue, skb);
			return;
		}
		kfree_skb(skb);
	}
}

static int
capi_ppptty_recv(struct capincci *np, struct sk_buff *skb)
{
	struct capi_ppptty *cp = np->priv;

	// we don't need to do flow control here,
	// because it'll happen automatically (CAPI
	// won't send more than 8 unacknowledged messages)
	skb_queue_tail(&cp->recv_queue, skb);
	run_recv_queue(cp);

	return 0;
}

// ------------------------------------------------------ sending -------

static int
capi_ppptty_write_room(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (!cp) {
		HDEBUG;
		return 0;
	}
	if (test_bit(XMIT_QUEUE_FULL, &cp->flags))
		return 0;
	else
		return CAPI_MAX_BLKSIZE;
}

static int
capi_ppptty_write(struct tty_struct * tty, int from_user,
		 const unsigned char *buf, int count)
{
	struct capi_ppptty *cp = tty->driver_data;
	struct sk_buff *skb;
	int retval;

	if (!cp) {
		HDEBUG;
		return 0;
	}

	if (test_bit(XMIT_QUEUE_FULL, &cp->flags))
		return 0;

	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+count, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "capi_ppptty_write: alloc_skb failed\n");
		return -ENOMEM;
	}

	skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);
	if (from_user) {
		if (copy_from_user(skb_put(skb, count), buf, count)) {
			kfree_skb(skb);
			return -EFAULT;
		}
	} else {
		memcpy(skb_put(skb, count), buf, count);
	}

	retval = capincci_send(&cp->ncci, skb);
	if (retval < 0)
		return retval;

	return count;
}

static void
send_wake_queue(struct capincci *np)
{
	struct capi_ppptty *cp = np->priv;
	struct tty_struct *tty = cp->tty;

	clear_bit(XMIT_QUEUE_FULL, &cp->flags);
	if (tty) {
		if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

static void
send_stop_queue(struct capincci *np)
{
	struct capi_ppptty *cp = np->priv;

	set_bit(XMIT_QUEUE_FULL, &cp->flags);
}

static int
capi_ppptty_chars_in_buffer(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (!cp)
		return 0;

	if (test_bit(XMIT_QUEUE_FULL, &cp->flags))
		return CAPI_MAX_BLKSIZE;
	else
		return 0;
}

// ----------------------------------------------------------------------

static void
capi_ppptty_dtor(struct capincci *np)
{
	struct capi_ppptty *cp = np->priv;

	if (cp->tty) {
		cp->tty->driver_data = NULL;
		tty_hangup(cp->tty);
	}

        spin_lock(&ppptty_list_lock);
	list_del(&cp->list);
        spin_unlock(&ppptty_list_lock);
	kfree(cp);

	MOD_DEC_USE_COUNT;
}

static int
ncci_connect(struct capidev *cdev, struct ncci_connect_data *data)
{
	struct capi_ppptty *cp, *pp;
	struct list_head *p;
        unsigned int minor = 0;

	MOD_INC_USE_COUNT;
	
	cp = kmalloc(sizeof(struct capi_ppptty), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	memset(cp, 0, sizeof(struct capi_ppptty));
	atomic_set(&cp->ttyopencount, 0);

	skb_queue_head_init(&cp->recv_queue);

        spin_lock(&ppptty_list_lock);
	list_for_each(p, &ppptty_list) {
		pp = list_entry(p, struct capi_ppptty, list);
		if (pp->minor < minor)
			continue;
		if (pp->minor > minor)
			break;
		minor++;
	}
	list_add_tail(&cp->list, &ppptty_list);
        spin_unlock(&ppptty_list_lock);

	data->data = minor;
	cp->minor = minor;
	cp->ncci.priv = cp;
	cp->ncci.ncci = data->ncci;
	cp->ncci.recv = capi_ppptty_recv;
	cp->ncci.send_wake_queue = send_wake_queue;
	cp->ncci.send_stop_queue = send_stop_queue;
	cp->ncci.dtor = capi_ppptty_dtor;
	capincci_hijack(cdev, &cp->ncci);

	return 0;
}

// ----------------------------------------------------------------------

static struct capi_ppptty *
find_capi_ppptty(int minor)
{
	struct list_head *p;
	struct capi_ppptty *cp = NULL;

        spin_lock(&ppptty_list_lock);
	list_for_each(p, &ppptty_list) {
		cp = list_entry(p, struct capi_ppptty, list);
		if (cp->minor == minor)
			break;
	}
        spin_unlock(&ppptty_list_lock);
	if (p == &ppptty_list) {
		return 0;
	}
	return cp;
}

static int
capi_ppptty_open(struct tty_struct * tty, struct file * file)
{
	struct capi_ppptty *cp;

	cp = find_capi_ppptty(MINOR(file->f_dentry->d_inode->i_rdev));
	if (!cp)
		return -ENXIO;

	tty->driver_data = cp;
	cp->tty = tty;
	atomic_inc(&cp->ttyopencount);
	MOD_INC_USE_COUNT;
	run_recv_queue(cp);

	return 0;
}

static void
capi_ppptty_close(struct tty_struct * tty, struct file * file)
{
	struct capi_ppptty *cp;

	cp = tty->driver_data;
	if (!cp)
		return;

	if (atomic_dec_and_test(&cp->ttyopencount)) {
		tty->driver_data = NULL;
		cp->tty = NULL;
	}
	MOD_DEC_USE_COUNT;
}

// ----------------------------------------------------------------------

static int capi_ppptty_refcount;
static struct tty_struct *capi_ppptty_table[CAPI_PPPTTY_NR_PORTS];
static struct termios *capi_ppptty_termios[CAPI_PPPTTY_NR_PORTS];
static struct termios *capi_ppptty_termios_locked[CAPI_PPPTTY_NR_PORTS];

static struct tty_driver capi_ppptty_driver = {
	magic:           TTY_DRIVER_MAGIC,
	driver_name:     "capi_ppptty",
	name:            "capi/%d",
	minor_start:     0,
	num:             CAPI_PPPTTY_NR_PORTS,
	type:            TTY_DRIVER_TYPE_SERIAL,
	subtype:         SERIAL_TYPE_NORMAL,
	flags:           TTY_DRIVER_REAL_RAW|TTY_DRIVER_RESET_TERMIOS,
	refcount:        &capi_ppptty_refcount,
	table:           capi_ppptty_table,
	termios:         capi_ppptty_termios,
	termios_locked:  capi_ppptty_termios_locked,
	
	open:            capi_ppptty_open,
	close:           capi_ppptty_close,
	write:           capi_ppptty_write,
	write_room:      capi_ppptty_write_room,
	chars_in_buffer: capi_ppptty_chars_in_buffer,
};

extern int (*capi_ppptty_connect)(struct capidev *cdev, struct ncci_connect_data *data);

static int __init capi_ppptty_init(void) 
{
	int retval;
	
	MOD_INC_USE_COUNT;

	capi_ppptty_connect = &ncci_connect;

	capi_ppptty_driver.major = capi_ttymajor,
	capi_ppptty_driver.init_termios = tty_std_termios;
	capi_ppptty_driver.init_termios.c_iflag = ICRNL;
	capi_ppptty_driver.init_termios.c_oflag = OPOST | ONLCR;
	capi_ppptty_driver.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	capi_ppptty_driver.init_termios.c_lflag = 0;

	retval = tty_register_driver(&capi_ppptty_driver);
	if (retval < 0) {
		printk(KERN_ERR "Couldn't register capi_nc driver\n");
		goto out;
	}

	retval = 0;
 out:
	MOD_DEC_USE_COUNT;
	return retval;
}

static void __exit capi_ppptty_exit(void) 
{
	capi_ppptty_connect = NULL;
	tty_unregister_driver(&capi_ppptty_driver);
}

module_init(capi_ppptty_init);
module_exit(capi_ppptty_exit);
