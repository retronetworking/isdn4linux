/* -*- linux-c -*-  */

#include "capitty.h"
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <net/capi/command.h>
#include <net/capi/kcapi.h>
#include <asm/uaccess.h>

struct ctty_dev *ctty_dev_table[CAPITTY_COUNT];

static void capitty_stop_queue(struct ctty_dev *d);
static void capitty_wake_queue(struct ctty_dev *d);

static int
ctty_set_MSR(struct ctty_dev *d, unsigned int MSR)
{
	unsigned int old_MSR = d->MSR;
	struct tty_struct *tty;

	d->MSR = MSR;
	if (test_bit(CTTY_CHECK_CAR, &d->flags) && 
	    (old_MSR ^ MSR) & TIOCM_CAR) {
		if (MSR & TIOCM_CAR) {
			// carrier set
			wake_up_interruptible(&d->open_wait);
		} else {
			// carrier cleared FIXME userspace?
			tty = d->tty;
			if (tty) {
				d->tty = NULL;
				tty_hangup(tty);
			}
		}
	}
	return 0;
}

// ----------------------------------------------------------------------
// /dev/capitty
// connection to userspace daemon

static inline int
capitty_dev_attach(struct ctty_dev *d, unsigned long arg)
{
	if (arg >= CAPITTY_COUNT)
		return -EINVAL;

	if (d->line != -1 || ctty_dev_table[arg])
		return -EBUSY;

	d->line = arg;
	ctty_dev_table[arg] = d;

	syncdev_r_ctor(&d->rdev);
	syncdev_w_ctor(&d->wdev);
	init_waitqueue_head(&d->open_wait);
	d->tty = NULL;
	d->tty_count = 0;
	d->MCR = 0;
	d->MSR = TIOCM_CTS;
	d->ncci.ncci = 0;
	d->flags = 0;

	return 0;
}

static int
capitty_dev_open(struct inode *inode, struct file *file)
{
	struct ctty_dev *d;

	d = kmalloc(sizeof(struct ctty_dev), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->line = -1;
	
	file->private_data = d;
	return 0;
}

static int
capitty_dev_release(struct inode *inode, struct file *file)
{
	struct ctty_dev *d = file->private_data;
	struct tty_struct *tty;
	
	if (d->line != -1) {
		ctty_dev_table[d->line] = NULL;
		tty = d->tty;
		if (tty) {
			d->tty = NULL;
			tty_hangup(tty);
		}
		syncdev_r_dtor(&d->rdev);
		syncdev_w_dtor(&d->wdev);
	}

	kfree(d);
	return 0;
}

static ssize_t 
capitty_dev_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct ctty_dev *d = file->private_data;
	struct syncdev_r *rdev = &d->rdev;
	int retval;
	
	if (d->line == -1) 
		return -EBUSY;

	retval = syncdev_r_read(rdev, file, buf, count, ppos, NULL);
	if (retval < 0)
		return retval;

	if (skb_queue_len(&rdev->read_queue) < SYNCDEV_QUEUE_LEN && 
	    !(d->MSR & TIOCM_CTS))
		capitty_wake_queue(d);

	return retval;
}


static ssize_t
capitty_dev_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct ctty_dev *d = file->private_data;
	struct syncdev_w *wdev = &d->wdev;
	int retval;

	if (d->line == -1) 
		return -EBUSY;

	if (!d->tty) 
		// discard frame FIXME? what about DTR
		return count;

	if (d->ncci.ncci)
		return -EBUSY;

	retval = syncdev_w_write(wdev, file, buf, count, ppos);
	if (retval < 0)
		return retval;

	capitty_run_write_queue(d);
	return retval;
}

static unsigned int
capitty_dev_poll(struct file *file, poll_table *wait)
{
	struct ctty_dev *d = file->private_data;
	struct syncdev_r *rdev = &d->rdev;
	struct syncdev_w *wdev = &d->wdev;
	unsigned int mask = 0;

	if (d->line == -1) 
		return POLLERR;

	syncdev_w_poll_wait(wdev, file, wait, &mask);
	syncdev_r_poll_wait(rdev, file, wait, &mask);
	return mask;
}

static int
capitty_dev_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	struct ctty_dev *d = file->private_data;

	switch (cmd) {
	case CAPI_TTY_ATTACH:
		return capitty_dev_attach(d, arg);
	case CAPI_TTY_SET_MSR:
		return ctty_set_MSR(d, arg);
	case CAPI_TTY_SBIT_MSR:
		return ctty_set_MSR(d, d->MSR | arg);
	case CAPI_TTY_CBIT_MSR:
		return ctty_set_MSR(d, d->MSR & ~arg);
	case CAPI_TTY_GET_MCR:
		if (copy_to_user((void *)arg, &d->MCR, sizeof(unsigned int)))
			return  -EFAULT;
		return 0;
	default:
		return -EINVAL;
	}
}

static loff_t
capitty_dev_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static struct file_operations capitty_fops =
{
	owner:		THIS_MODULE,
	open:		capitty_dev_open,
	release:	capitty_dev_release,
	read:		capitty_dev_read,
	write:		capitty_dev_write,
	poll:		capitty_dev_poll,
	ioctl:		capitty_dev_ioctl,
	llseek:		capitty_dev_llseek,
};

/* ---------------------------------------------------------------------- */

static void capitty_dtor(struct capincci *np)
{
 	struct ctty_dev *d = np->priv;

	HDEBUG;
	d->ncci.ncci = 0;
}

static void
capitty_wake_queue(struct ctty_dev *d)
{
	struct tty_struct *tty = d->tty;

	d->MSR |= TIOCM_CTS;
	if (tty) {
		if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

static void
capitty_recv_wake_queue(struct capincci *np)
{
	struct ctty_dev *d = np->priv;

	capitty_wake_queue(d);
}

static void
capitty_stop_queue(struct ctty_dev *d)
{
	d->MSR &= ~TIOCM_CTS;
}

static void
capitty_recv_stop_queue(struct capincci *np)
{
	struct ctty_dev *d = np->priv;

	capitty_stop_queue(d);
}

static int
capitty_recv(struct capincci *np, struct sk_buff *skb)
{
	struct ctty_dev *d = np->priv;

	// we don't need to do flow control here,
	// because it'll happen automatically (CAPI
	// won't send more than 8 unacknowledged messages)

	syncdev_w_queue_tail(&d->wdev, skb);
	capitty_run_write_queue(d);
	return 0;
}

static int
ncci_connect(struct capidev *cdev, struct ncci_connect_data *data)
{
	struct ctty_dev *d;

	HDEBUG;

	if ((data->data < 0) || (data->data >= CAPITTY_COUNT))
		return -EINVAL;

	d = ctty_dev_table[data->data];
	if (!d)
		return -ESRCH;

	d->ncci.priv = d;
	d->ncci.ncci = data->ncci;
	d->ncci.recv = capitty_recv;
	d->ncci.send_wake_queue = capitty_recv_wake_queue;
	d->ncci.send_stop_queue = capitty_recv_stop_queue;
	d->ncci.dtor = capitty_dtor;
	capincci_hijack(cdev, &d->ncci);

	return 0;
}

/* ---------------------------------------------------------------------- */

extern int (*capitty_ncci_connect)(struct capidev *cdev, struct ncci_connect_data *data);

static int __init capitty_init(void) 
{
	int retval;

	HDEBUG;

	MOD_INC_USE_COUNT;

	capitty_dev_fops = &capitty_fops;
	capitty_ncci_connect = &ncci_connect;

	retval = capitty_tty_init();
	if (retval)
		capitty_dev_fops = NULL;

	MOD_DEC_USE_COUNT;
	return retval;
}

static void __exit capitty_exit(void) 
{
	capitty_tty_exit();

	capitty_dev_fops = NULL;
	capitty_ncci_connect = NULL;
}

module_init(capitty_init);
module_exit(capitty_exit);
