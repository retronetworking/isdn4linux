#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/capi/capi.h>
#include <net/capi/command.h>
#include "capi.h"

// ----------------------------------------------------------------------

#define CAPINC_NR_PORTS 256
#define CAPINC_MAX_RECVQUEUE	10
#define CAPINC_MAX_SENDQUEUE	10

#define CAPI_MAX_BLKSIZE	2048

// ----------------------------------------------------------------------

struct capi_ppptty {
	struct list_head    list;
	struct capincci     ncci;
	unsigned int        minor;

	struct tty_struct  *tty;
	int                 ttyinstop;
	int                 ttyoutstop;
	atomic_t            ttyopencount;

	struct sk_buff_head inqueue;
	int                 inbytes;
	struct sk_buff_head outqueue;
	int                 outbytes;
};

// ----------------------------------------------------------------------

static int capi_ttymajor = 191;
MODULE_PARM(capi_ttymajor, "i");

// ----------------------------------------------------------------------

static spinlock_t ppptty_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(ppptty_list);

// ----------------------------------------------------------------------

static void handle_minor_recv(struct capi_ppptty *cp);

// ----------------------------------------------------------------------

static void
capi_ppptty_send_wake_queue(struct capincci *np)
{
	struct capi_ppptty *cp = np->priv;

	if (!cp->tty)
		return;

	if (cp->tty->ldisc.write_wakeup)
		cp->tty->ldisc.write_wakeup(cp->tty);
}

static int
capi_ppptty_recv(struct capincci *np, struct sk_buff *skb)
{
	struct capi_ppptty *cp = np->priv;

	skb_queue_tail(&cp->inqueue, skb);
	cp->inbytes += skb->len;
	handle_minor_recv(cp);

	return 0;
}

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

	HDEBUG();
	MOD_INC_USE_COUNT;
	
	cp = kmalloc(sizeof(struct capi_ppptty), GFP_ATOMIC);
	if (!cp)
		return -ENOMEM;

	memset(cp, 0, sizeof(struct capi_ppptty));
	atomic_set(&cp->ttyopencount,0);

	skb_queue_head_init(&cp->inqueue);
	skb_queue_head_init(&cp->outqueue);

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
	cp->ncci.send_wake_queue = capi_ppptty_send_wake_queue;
	cp->ncci.dtor = capi_ppptty_dtor;
	capincci_hijack(cdev, &cp->ncci);

	return 0;
}

// ----------------------------------------------------------------------

static int
handle_recv_skb(struct capi_ppptty *cp, struct sk_buff *skb)
{
	if (!cp->tty) {
		capincci_recv_ack(&cp->ncci, skb);
		return 0;
	}

	if (cp->ttyinstop) {
		return -1;
	}
	if (cp->tty->ldisc.receive_room(cp->tty) < skb->len) {
		return -1;
	}
	cp->tty->ldisc.receive_buf(cp->tty, skb->data, 0, skb->len);
	capincci_recv_ack(&cp->ncci, skb);
	return 0;
}

static void
handle_minor_recv(struct capi_ppptty *cp)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&cp->inqueue)) != 0) {
		unsigned int len = skb->len;
		cp->inbytes -= len;
		if (handle_recv_skb(cp, skb) < 0) {
			skb_queue_head(&cp->inqueue, skb);
			cp->inbytes += len;
			return;
		}
	}
}

int handle_minor_send(struct capi_ppptty *cp)
{
	struct sk_buff *skb;
	int count = 0;
	int retval;
	int len;

	if (cp->ttyoutstop) {
		return 0;
	}

	while ((skb = skb_dequeue(&cp->outqueue)) != 0) {
		len = skb->len;
		retval = capincci_send(&cp->ncci, skb);
		if (retval == -EAGAIN) {
			// queue full
			skb_queue_head(&cp->outqueue, skb);
			break;
		} else if (retval < 0) {
			cp->outbytes -= len;
			kfree_skb(skb);
			continue;
		}
		count++;
		cp->outbytes -= len;
	}
	return count;
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

// ----------------------------------------------------------------------

static int
capinc_tty_open(struct tty_struct * tty, struct file * file)
{
	struct capi_ppptty *cp;

	cp = find_capi_ppptty(MINOR(file->f_dentry->d_inode->i_rdev));
	if (!cp)
		return -ENXIO;

	tty->driver_data = cp;
	cp->tty = tty;
	atomic_inc(&cp->ttyopencount);
	handle_minor_recv(cp);
	return 0;
}

static void
capinc_tty_close(struct tty_struct * tty, struct file * file)
{
	struct capi_ppptty *cp;

	cp = tty->driver_data;
	if (!cp)
		return;

	if (atomic_dec_and_test(&cp->ttyopencount)) {
		tty->driver_data = NULL;
		cp->tty = NULL;
	}
}

static int
capinc_tty_write(struct tty_struct * tty, int from_user,
		 const unsigned char *buf, int count)
{
	struct capi_ppptty *cp = tty->driver_data;
	struct sk_buff *skb;

	if (!cp)
		return 0;

	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+count, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "capinc_tty_write: alloc_skb failed\n");
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

	skb_queue_tail(&cp->outqueue, skb);
	cp->outbytes += skb->len;
	handle_minor_send(cp);
	handle_minor_recv(cp);
	return count;
}

static void
capinc_tty_flush_chars(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (!cp)
		return;

	handle_minor_recv(cp);
}

static int
capinc_tty_write_room(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;
	int room;

	if (!cp)
		return 0;

	room = CAPINC_MAX_SENDQUEUE-skb_queue_len(&cp->outqueue);
	room *= CAPI_MAX_BLKSIZE;
	return room;
}

#if 0
static int
capinc_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (!cp)
		return 0;

	return cp->outbytes;
}
#endif

static void
capinc_tty_throttle(struct tty_struct * tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (cp)
		cp->ttyinstop = 1;
}

static void
capinc_tty_unthrottle(struct tty_struct * tty)
{
	struct capi_ppptty *cp = tty->driver_data;
	if (cp) {
		cp->ttyinstop = 0;
		handle_minor_recv(cp);
	}
}

static void
capinc_tty_stop(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (cp) {
		cp->ttyoutstop = 1;
	}
}

static void
capinc_tty_start(struct tty_struct *tty)
{
	struct capi_ppptty *cp = tty->driver_data;

	if (cp) {
		cp->ttyoutstop = 0;
		handle_minor_send(cp);
	}
}

// ----------------------------------------------------------------------

static int capinc_tty_refcount;
static struct tty_struct *capinc_tty_table[CAPINC_NR_PORTS];
static struct termios *capinc_tty_termios[CAPINC_NR_PORTS];
static struct termios *capinc_tty_termios_locked[CAPINC_NR_PORTS];

static struct tty_driver capinc_tty_driver = {
	magic:           TTY_DRIVER_MAGIC,
	driver_name:     "capi_nc",
	name:            "capi/%d",
	minor_start:     0,
	num:             CAPINC_NR_PORTS,
	type:            TTY_DRIVER_TYPE_SERIAL,
	subtype:         SERIAL_TYPE_NORMAL,
	flags:           TTY_DRIVER_REAL_RAW|TTY_DRIVER_RESET_TERMIOS,
	refcount:        &capinc_tty_refcount,
	table:           capinc_tty_table,
	termios:         capinc_tty_termios,
	termios_locked:  capinc_tty_termios_locked,
	
	open:            capinc_tty_open,
	close:           capinc_tty_close,
	write:           capinc_tty_write,
	flush_chars:     capinc_tty_flush_chars,
	write_room:      capinc_tty_write_room,
//	chars_in_buffer: capinc_tty_chars_in_buffer,
	throttle:        capinc_tty_throttle,
	unthrottle:      capinc_tty_unthrottle,
	stop:            capinc_tty_stop,
	start:           capinc_tty_start,
};

extern int (*capi_ppptty_connect)(struct capidev *cdev, struct ncci_connect_data *data);

static int __init capi_ppptty_init(void) 
{
	int retval;
	
	HDEBUG();

	MOD_INC_USE_COUNT;

	capi_ppptty_connect = &ncci_connect;

	capinc_tty_driver.major = capi_ttymajor,
	capinc_tty_driver.init_termios = tty_std_termios;
	capinc_tty_driver.init_termios.c_iflag = ICRNL;
	capinc_tty_driver.init_termios.c_oflag = OPOST | ONLCR;
	capinc_tty_driver.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	capinc_tty_driver.init_termios.c_lflag = 0;

	retval = tty_register_driver(&capinc_tty_driver);
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
	tty_unregister_driver(&capinc_tty_driver);
}

module_init(capi_ppptty_init);
module_exit(capi_ppptty_exit);
