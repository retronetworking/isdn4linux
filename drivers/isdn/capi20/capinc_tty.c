#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <net/capi/command.h>

#include "capi.h"
#include "capiminor.h"

/* -------- driver information -------------------------------------- */

int capi_ttymajor = 191;

/* -------- defines -----------------------a-------------------------- */

#define CAPI_MAX_BLKSIZE	2048

/* -------- tty_operations for capincci ----------------------------- */

int capinc_tty_open(struct tty_struct * tty, struct file * file)
{
	struct capiminor *mp;

	if ((mp = capiminor_find(MINOR(file->f_dentry->d_inode->i_rdev))) == 0)
		return -ENXIO;
/* 	if (mp->nccip == 0) */
/* 		return -ENXIO; */
	if (mp->file)
		return -EBUSY;

	syncdev_r_ctor(&mp->sdev);
	init_waitqueue_head(&mp->sendwait);
	tty->driver_data = (void *)mp;
#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capi_tty_open %d\n", GET_USE_COUNT(THIS_MODULE));
#endif
	if (atomic_read(&mp->ttyopencount) == 0)
		mp->tty = tty;
	atomic_inc(&mp->ttyopencount);
#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capinc_tty_open ocount=%d\n", atomic_read(&mp->ttyopencount));
#endif
	handle_minor_recv(mp);
	return 0;
}

void capinc_tty_close(struct tty_struct * tty, struct file * file)
{
	struct capiminor *mp;

	mp = (struct capiminor *)tty->driver_data;
	if (mp)	{
		if (atomic_dec_and_test(&mp->ttyopencount)) {
#ifdef _DEBUG_REFCOUNT
			printk(KERN_DEBUG "capinc_tty_close lastclose\n");
#endif
			tty->driver_data = (void *)0;
			mp->tty = 0;
		}
#ifdef _DEBUG_REFCOUNT
		printk(KERN_DEBUG "capinc_tty_close ocount=%d\n", atomic_read(&mp->ttyopencount));
#endif
/* 		if (mp->nccip == 0) */
/* 			capiminor_free(mp); */
	}

#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capinc_tty_close\n");
#endif
}

int capinc_tty_write(struct tty_struct * tty, int from_user,
		      const unsigned char *buf, int count)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	struct sk_buff *skb;

#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_write(from_user=%d,count=%d)\n",
				from_user, count);
#endif

/* 	if (!mp || !mp->nccip) { */
	if (!mp) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_write: mp or mp->ncci NULL\n");
#endif
		return 0;
	}

	skb = mp->ttyskb;
	if (skb) {
		mp->ttyskb = 0;
		skb_queue_tail(&mp->outqueue, skb);
		mp->outbytes += skb->len;
	}

	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+count, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "capinc_tty_write: alloc_skb failed\n");
		return -ENOMEM;
	}

	skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);
	if (from_user) {
		if (copy_from_user(skb_put(skb, count), buf, count)) {
			kfree_skb(skb);
#ifdef _DEBUG_TTYFUNCS
			printk(KERN_DEBUG "capinc_tty_write: copy_from_user\n");
#endif
			return -EFAULT;
		}
	} else {
		memcpy(skb_put(skb, count), buf, count);
	}

	skb_queue_tail(&mp->outqueue, skb);
	mp->outbytes += skb->len;
	(void)handle_minor_send(mp);
	(void)handle_minor_recv(mp);
	return count;
}

void capinc_tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	struct sk_buff *skb;

#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_put_char(%u)\n", ch);
#endif

/* 	if (!mp || !mp->nccip) { */
	if (!mp) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_put_char: mp or mp->ncci NULL\n");
#endif
		return;
	}

	skb = mp->ttyskb;
	if (skb) {
		if (skb_tailroom(skb) > 0) {
			*(skb_put(skb, 1)) = ch;
			return;
		}
		mp->ttyskb = 0;
		skb_queue_tail(&mp->outqueue, skb);
		mp->outbytes += skb->len;
		(void)handle_minor_send(mp);
	}
	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+CAPI_MAX_BLKSIZE, GFP_ATOMIC);
	if (skb) {
		skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);
		*(skb_put(skb, 1)) = ch;
		mp->ttyskb = skb;
	} else {
		printk(KERN_ERR "capinc_put_char: char %u lost\n", ch);
	}
}

void capinc_tty_flush_chars(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	struct sk_buff *skb;

#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_flush_chars\n");
#endif

/* 	if (!mp || !mp->nccip) { */
	if (!mp) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_flush_chars: mp or mp->ncci NULL\n");
#endif
		return;
	}

	skb = mp->ttyskb;
	if (skb) {
		mp->ttyskb = 0;
		skb_queue_tail(&mp->outqueue, skb);
		mp->outbytes += skb->len;
		(void)handle_minor_send(mp);
	}
	(void)handle_minor_recv(mp);
}

int capinc_tty_write_room(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
	int room;
/* 	if (!mp || !mp->nccip) { */
	if (!mp) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_write_room: mp or mp->ncci NULL\n");
#endif
		return 0;
	}
	room = CAPINC_MAX_SENDQUEUE-skb_queue_len(&mp->outqueue);
	room *= CAPI_MAX_BLKSIZE;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_write_room = %d\n", room);
#endif
	return room;
}

int capinc_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
/* 	if (!mp || !mp->nccip) { */
	if (!mp) {
#ifdef _DEBUG_TTYFUNCS
		printk(KERN_DEBUG "capinc_tty_chars_in_buffer: mp or mp->ncci NULL\n");
#endif
		return 0;
	}
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_chars_in_buffer = %d nack=%d sq=%d rq=%d\n",
			mp->outbytes, mp->nack,
			skb_queue_len(&mp->outqueue),
			skb_queue_len(&mp->inqueue));
#endif
	return mp->outbytes;
}

int capinc_tty_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error = 0;
	switch (cmd) {
	default:
		error = n_tty_ioctl (tty, file, cmd, arg);
		break;
	}
	return error;
}

void capinc_tty_set_termios(struct tty_struct *tty, struct termios * old)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_set_termios\n");
#endif
}

void capinc_tty_throttle(struct tty_struct * tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_throttle\n");
#endif
	if (mp)
		mp->ttyinstop = 1;
}

void capinc_tty_unthrottle(struct tty_struct * tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_unthrottle\n");
#endif
	if (mp) {
		mp->ttyinstop = 0;
		handle_minor_recv(mp);
	}
}

void capinc_tty_stop(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_stop\n");
#endif
	if (mp) {
		mp->ttyoutstop = 1;
	}
}

void capinc_tty_start(struct tty_struct *tty)
{
	struct capiminor *mp = (struct capiminor *)tty->driver_data;
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_start\n");
#endif
	if (mp) {
		mp->ttyoutstop = 0;
		(void)handle_minor_send(mp);
	}
}

void capinc_tty_hangup(struct tty_struct *tty)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_hangup\n");
#endif
}

void capinc_tty_break_ctl(struct tty_struct *tty, int state)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_break_ctl(%d)\n", state);
#endif
}

void capinc_tty_flush_buffer(struct tty_struct *tty)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_flush_buffer\n");
#endif
}

void capinc_tty_set_ldisc(struct tty_struct *tty)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_set_ldisc\n");
#endif
}

void capinc_tty_send_xchar(struct tty_struct *tty, char ch)
{
#ifdef _DEBUG_TTYFUNCS
	printk(KERN_DEBUG "capinc_tty_send_xchar(%d)\n", ch);
#endif
}

int capinc_tty_read_proc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	return 0;
}

int capinc_write_proc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	return 0;
}

static struct tty_driver capinc_tty_driver;
static int capinc_tty_refcount;
static struct tty_struct *capinc_tty_table[CAPINC_NR_PORTS];
static struct termios *capinc_tty_termios[CAPINC_NR_PORTS];
static struct termios *capinc_tty_termios_locked[CAPINC_NR_PORTS];

int __init capinc_tty_init(void)
{
	struct tty_driver *drv = &capinc_tty_driver;

	/* Initialize the tty_driver structure */
	
	memset(drv, 0, sizeof(struct tty_driver));
	drv->magic = TTY_DRIVER_MAGIC;
#if (LINUX_VERSION_CODE > 0x20100)
	drv->driver_name = "capi_nc";
#endif
	drv->name = "capi/%d";
	drv->major = capi_ttymajor;
	drv->minor_start = 0;
	drv->num = CAPINC_NR_PORTS;
	drv->type = TTY_DRIVER_TYPE_SERIAL;
	drv->subtype = SERIAL_TYPE_NORMAL;
	drv->init_termios = tty_std_termios;
	drv->init_termios.c_iflag = ICRNL;
	drv->init_termios.c_oflag = OPOST | ONLCR;
	drv->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	drv->init_termios.c_lflag = 0;
	drv->flags = TTY_DRIVER_REAL_RAW|TTY_DRIVER_RESET_TERMIOS;
	drv->refcount = &capinc_tty_refcount;
	drv->table = capinc_tty_table;
	drv->termios = capinc_tty_termios;
	drv->termios_locked = capinc_tty_termios_locked;

	drv->open = capinc_tty_open;
	drv->close = capinc_tty_close;
	drv->write = capinc_tty_write;
	drv->put_char = capinc_tty_put_char;
	drv->flush_chars = capinc_tty_flush_chars;
	drv->write_room = capinc_tty_write_room;
	drv->chars_in_buffer = capinc_tty_chars_in_buffer;
	drv->ioctl = capinc_tty_ioctl;
	drv->set_termios = capinc_tty_set_termios;
	drv->throttle = capinc_tty_throttle;
	drv->unthrottle = capinc_tty_unthrottle;
	drv->stop = capinc_tty_stop;
	drv->start = capinc_tty_start;
	drv->hangup = capinc_tty_hangup;
	drv->break_ctl = capinc_tty_break_ctl;
	drv->flush_buffer = capinc_tty_flush_buffer;
	drv->set_ldisc = capinc_tty_set_ldisc;
	drv->send_xchar = capinc_tty_send_xchar;
	drv->read_proc = capinc_tty_read_proc;
	if (tty_register_driver(drv)) {
		printk(KERN_ERR "Couldn't register capi_nc driver\n");
		return -1;
	}
	return 0;
}

// FIXME __exit?

void capinc_tty_exit(void)
{
	struct tty_driver *drv = &capinc_tty_driver;
	int retval;
	if ((retval = tty_unregister_driver(drv)))
		printk(KERN_ERR "capi: failed to unregister capi_nc driver (%d)\n", retval);
}

