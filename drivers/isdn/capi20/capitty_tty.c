/* -*- linux-c -*-  */

#include "capitty.h"
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/termios.h>
#include <asm/uaccess.h>
#include <net/capi/command.h>

static int capitty_refcount;

static struct tty_struct *capitty_table[CAPITTY_COUNT];
static struct termios *capitty_termios[CAPITTY_COUNT];
static struct termios *capitty_termios_locked[CAPITTY_COUNT];

static void
ctty_set_MCR(struct ctty_dev *d, unsigned int MCR)
{
	unsigned int old_MCR = d->MCR;
	struct sk_buff *skb;

	d->MCR = MCR;
	if ((old_MCR ^ MCR) & TIOCM_DTR) {
		skb = alloc_skb(1, GFP_ATOMIC);
		if (!skb)
			return;
		syncdev_r_queue_tail(&d->rdev, skb);
	}
}

static int 
ctty_get_modem_info(struct ctty_dev *d, unsigned int *value)
{
	unsigned int result;

	result =  (d->MCR & (TIOCM_RTS|TIOCM_DTR))
		| (d->MSR & (TIOCM_CAR|TIOCM_RNG|TIOCM_DSR|TIOCM_CTS));

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int
ctty_set_modem_info(struct ctty_dev *d, unsigned int cmd,
		    unsigned int *value)
{
	unsigned int arg;
	unsigned int MCR = d->MCR;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS: 
		MCR |= arg & (TIOCM_RTS|TIOCM_DTR);
		break;
	case TIOCMBIC:
		MCR &= ~ (arg & (TIOCM_RTS|TIOCM_DTR));
		break;
	case TIOCMSET:
		MCR = (MCR & ~(TIOCM_RTS|TIOCM_DTR)) |
		       (arg & (TIOCM_RTS|TIOCM_DTR));
		break;
	default:
		return -EINVAL;
	}
	ctty_set_MCR(d, MCR);
	return 0;
}

// tty_driver functions --------------------------------------------------

static void change_speed(struct ctty_dev *d,
			 struct termios *old_termios)
{
	unsigned int cflag;

	if (!d->tty || !d->tty->termios)
		return;

	cflag = d->tty->termios->c_cflag;

	/* CTS flow control flag and modem status interrupts */
	if (cflag & CRTSCTS)
		set_bit(CTTY_CTS_FLOW, &d->flags);
	else
		clear_bit(CTTY_CTS_FLOW, &d->flags);

	if (cflag & CLOCAL)
		clear_bit(CTTY_CHECK_CAR, &d->flags);
	else
		set_bit(CTTY_CHECK_CAR, &d->flags);

	/*
	 * !!! ignore all characters if CREAD is not set
	 */
/* 	if ((cflag & CREAD) == 0) */
/* 		info->ignore_status_mask |= UART_LSR_DR; */
}

static int
capitty_tty_open(struct tty_struct * tty, struct file * filp)
{
	DECLARE_WAITQUEUE(wait, current);
	struct ctty_dev *d;
	int line, retval = 0;

	MOD_INC_USE_COUNT;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= CAPITTY_COUNT))
		BUG();

	d = ctty_dev_table[line];
	if (!d)
		return -ENXIO;

	d->tty = tty;
	tty->driver_data = d;

	// currently, we only allow one user FIXME
	if (d->tty_count++ >= 1)
		return -EBUSY;

	if (d->tty_count == 1)
		change_speed(d, 0);

	// when opening non-blocking don't wait for carrier
	if (filp->f_flags & O_NONBLOCK)
		return 0;

	// wait for carrier
	add_wait_queue(&d->open_wait, &wait);
	for (;;) {
		if (tty->termios->c_cflag & CBAUD)
			ctty_set_MCR(d, d->MCR | (TIOCM_DTR|TIOCM_RTS));

		set_current_state(TASK_INTERRUPTIBLE);
		if (d->MSR & TIOCM_CAR || 
		    d->tty->termios->c_cflag & CLOCAL)
			break;

		schedule();

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&d->open_wait, &wait);

	return retval;
}

static void
capitty_tty_close(struct tty_struct * tty, struct file * filp)
{
	struct ctty_dev *d;

	MOD_DEC_USE_COUNT;
	d = tty->driver_data;
	tty->driver_data = NULL;

	// when daemon is not present
	// we'll get here with ctty = NULL;
	if (!d)
		return; 

	if (--d->tty_count == 0) {
		d->tty = NULL;
	}
}

static int 
capitty_tty_write_room(struct tty_struct *tty)
{
	struct ctty_dev *d = tty->driver_data;

	HDEBUG;
	if (!d) {
		HDEBUG;
		return 0;
	}

	if (d->MSR & TIOCM_CTS)
		return MAX_FRAME_SIZE;
	else
		return 0;
}

static int 
capitty_tty_write(struct tty_struct *tty, int from_user,
		  const unsigned char *buf, int count)
{
	struct ctty_dev *d = tty->driver_data;
	struct syncdev_r *rdev;
	struct sk_buff *skb;

	if (!d) {
		HDEBUG;
		return -EBUSY;
	}

	if (!(d->MSR & TIOCM_CTS))
		return 0;

	rdev = &d->rdev;

	skb = alloc_skb(count + CAPI_DATA_B3_REQ_LEN, from_user ? GFP_USER:GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	
	skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);

	if (from_user) {
		if (copy_from_user(skb_put(skb, count), buf, count)) {
			kfree_skb(skb);
			return -EFAULT;
		}
	} else {
		memcpy(skb_put(skb, count), buf, count);
	}
	
	if (d->ncci.ncci) {
		if (capincci_send(&d->ncci, skb) < 0) {
			// shouldn't happen
			kfree_skb(skb);
			HDEBUG;
			return 0;
		}
	} else {
		syncdev_r_queue_tail(rdev, skb);
		if (skb_queue_len(&rdev->read_queue) >= SYNCDEV_QUEUE_LEN) {
			d->MSR &= ~TIOCM_CTS;
		}
	}
	
	return count;
}

static int
capitty_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct ctty_dev *d = tty->driver_data;
	struct syncdev_r *rdev;

	if (!d) {
		HDEBUG;
		return 0;
	}
	rdev = &d->rdev;

	return MAX_FRAME_SIZE * skb_queue_len(&rdev->read_queue);
}

static int
capitty_tty_ioctl(struct tty_struct *tty, struct file * file,
			  unsigned int cmd, unsigned long arg)
{
	struct ctty_dev *d = tty->driver_data;
	
	if (!d) {
		HDEBUG;
		return 0;
	}

	switch (cmd) {
	case TIOCMGET:
		return ctty_get_modem_info(d, (unsigned int *) arg);
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		return ctty_set_modem_info(d, cmd, (unsigned int *) arg);
		// keep quiet about these FIXME
	case TCGETS: 
	case TCSETS: 
	case TCSETSW: 
	case TCSETSF: 
	case TCFLSH:
		return -ENOIOCTLCMD;
	default:
		printk(KERN_DEBUG "ioctl %#x\n", cmd);
	}
	return -ENOIOCTLCMD;
}

static void
capitty_tty_send_xchar(struct tty_struct *tty, char c)
{
	struct ctty_dev *d = tty->driver_data;

	HDEBUG;
	if (!d) {
		HDEBUG;
		return;
	}
}

static void
capitty_tty_throttle(struct tty_struct *tty)
{
	struct ctty_dev *d = tty->driver_data;

	HDEBUG;
	if (!d) {
		HDEBUG;
		return;
	}
	if (I_IXOFF(tty))
		capitty_tty_send_xchar(tty, STOP_CHAR(tty));

	if (tty->termios->c_cflag & CRTSCTS)
		ctty_set_MCR(d, d->MCR & ~TIOCM_RTS);
}

static void
capitty_tty_unthrottle(struct tty_struct *tty)
{
	struct ctty_dev *d = tty->driver_data;
	
	if (!d) {
		HDEBUG;
		return;
	}

	HDEBUG;
	if (I_IXOFF(tty))
		capitty_tty_send_xchar(tty, START_CHAR(tty));

	if (tty->termios->c_cflag & CRTSCTS)
		d->MCR |= TIOCM_RTS;

	capitty_run_write_queue(d);
}

static void
capitty_tty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct ctty_dev *d = tty->driver_data;
	unsigned int cflag = tty->termios->c_cflag;

	HDEBUG;

	if (cflag == old_termios->c_cflag)
		return;

	change_speed(d, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) &&
	    !(cflag & CBAUD)) {
		ctty_set_MCR(d, d->MCR & ~(TIOCM_DTR|TIOCM_RTS));
	}
	
	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
	    (cflag & CBAUD)) {
		ctty_set_MCR(d, d->MCR | TIOCM_DTR);
		if (!(tty->termios->c_cflag & CRTSCTS) || 
		    !test_bit(TTY_THROTTLED, &tty->flags)) {
			ctty_set_MCR(d, d->MCR | TIOCM_RTS);
		}
	}
	
#if 0
	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
#endif
}

static inline int
line_info(char *p, struct ctty_dev *d)
{
	int retval;

	if (!d)
		return 0;
	
	retval = sprintf(p, "%3d: MSR %#x MCR %#x flags %#lx\n",
			 d->line, d->MSR, d->MCR, d->flags);
	if (d->tty) 
		retval += sprintf(p + retval, "     tty %p cflag %#x\n",
				  d->tty, d->tty->termios->c_cflag);

	return retval;
}

static int
capitty_tty_read_proc(char *page, char **start, off_t off, int count,
		      int *eof, void *data)
{
	int i, len = 0, l;
	off_t	begin = 0;

	len += sprintf(page, "serinfo:1.0 driver:%s%s revision:%s\n",
		       "", "", "");
	for (i = 0; i < CAPITTY_COUNT && len < 4000; i++) {
		l = line_info(page + len, ctty_dev_table[i]);
		len += l;
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

static struct tty_driver capitty_driver = {
	magic:          TTY_DRIVER_MAGIC,
	driver_name:    "capitty",
	name:           "ttyI",
	major:          CAPITTY_MAJOR,
	minor_start:    CAPITTY_START,
	num:            CAPITTY_COUNT,
	type:           TTY_DRIVER_TYPE_SERIAL,
	subtype:        SERIAL_TYPE_NORMAL,
	flags:          TTY_DRIVER_REAL_RAW,
	refcount:       &capitty_refcount,
	table:          capitty_table,
	termios:        capitty_termios,
	termios_locked: capitty_termios_locked,

	open:           capitty_tty_open,
	close:          capitty_tty_close,
	write_room:     capitty_tty_write_room,
	write:          capitty_tty_write,
	//	capitty_driver.flush_chars = tty_dummy;
	chars_in_buffer:capitty_tty_chars_in_buffer,
	//	capitty_driver.flush_buffer = tty_dummy;
	ioctl:          capitty_tty_ioctl,
	throttle:       capitty_tty_throttle,
	unthrottle:     capitty_tty_unthrottle,
	send_xchar:     capitty_tty_send_xchar,
	//	capitty_driver.break_ctl = tty_dummy;
	//	capitty_driver.wait_until_sent = capitty_wait_until_sent;
	set_termios:    capitty_tty_set_termios,
	//	capitty_driver.stop = tty_dummy;
	//	capitty_driver.start = tty_dummy;
	read_proc:      capitty_tty_read_proc,
};

/* ---------------------------------------------------------------------- */

static int
capitty_tty_receive_buf(struct ctty_dev *d, char *buf, int len)
{
	struct tty_struct *tty = d->tty;

	if (!tty) {
		// discard frame
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

/*
 * capitty_run_write_queue is called by the function which just queued a 
 * frame on wdev, and by tty_unthrottle.
 * It pushes pending frames to the tty layer.
 */

void
capitty_run_write_queue(struct ctty_dev *d)
{
	struct tty_struct *tty = d->tty;
	struct sk_buff *skb;

	if (tty && test_bit(TTY_THROTTLED, &tty->flags))
		return;

	while ((skb = syncdev_w_dequeue(&d->wdev))) {
		if (capitty_tty_receive_buf(d, skb->data, skb->len) < 0) {
			// tty throttled
			HDEBUG;
			syncdev_w_queue_head(&d->wdev, skb);
			return;
		}
		kfree_skb(skb);
	}
}

int __init capitty_tty_init(void)
{
	capitty_driver.init_termios = tty_std_termios;

	return tty_register_driver(&capitty_driver);
}

void __exit capitty_tty_exit(void)
{
	tty_unregister_driver(&capitty_driver);
}
