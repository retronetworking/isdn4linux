/*
 * $Id$
 *
 * Common code for providing a synchronous char device interface
 *
 * (C) 2001 by Kai Germaschewski (kai.germaschewski@gmx.de)
 *
 */

#include <linux/skbuff.h>
#include <linux/poll.h>
#include <asm/uaccess.h>

#ifndef SYNCDEV_QUEUE_LEN
#define SYNCDEV_QUEUE_LEN 2
#endif

struct syncdev_r {
	struct sk_buff_head read_queue;
	wait_queue_head_t read_wait;
};

static inline void
syncdev_r_ctor(struct syncdev_r *sdev)
{
	skb_queue_head_init(&sdev->read_queue);
	init_waitqueue_head(&sdev->read_wait);
}

static inline void
syncdev_r_dtor(struct syncdev_r *sdev)
{
	skb_queue_purge(&sdev->read_queue);
}

static inline void
syncdev_r_queue_tail(struct syncdev_r *sdev, struct sk_buff *skb)
{
	skb_queue_tail(&sdev->read_queue, skb);
	wake_up_interruptible(&sdev->read_wait);
}

// skbp != NULL -> *skbp has to be kfree_skb'd by caller
//                  if retval >= 0

static inline ssize_t 
syncdev_r_read(struct syncdev_r *sdev, struct file *file, char *buf,
	     size_t count, loff_t *ppos, struct sk_buff **skbp)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	ssize_t retval = 0;
	int do_free = 0;

	if (!skbp) {
		skbp = &skb;
		do_free = 1;
	}

	if (ppos != &file->f_pos)
		return -ESPIPE;

	add_wait_queue(&sdev->read_wait, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		
		*skbp = skb_dequeue(&sdev->read_queue);
		if (*skbp)
			break;

		retval = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			break;

		retval = -ERESTARTSYS;
		if (signal_pending(current))
			break;

		schedule();
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&sdev->read_wait, &wait);

	if (!*skbp)
		goto out;

	retval = -EMSGSIZE;
	if ((*skbp)->len > count) {
		skb_queue_head(&sdev->read_queue, *skbp);
		goto out;
	}

	retval = -EFAULT;
	if (copy_to_user(buf, (*skbp)->data, (*skbp)->len))
		goto outf;

	retval = (*skbp)->len;
	if (!do_free)
		goto out;

 outf:
	kfree_skb(*skbp);
 out:
	return retval;
}

static inline void 
syncdev_r_poll_wait(struct syncdev_r *rdev, struct file *file, 
		    poll_table *wait, unsigned int *mask)
{
	poll_wait(file, &rdev->read_wait, wait);
	if (!skb_queue_empty(&rdev->read_queue))
		*mask |= POLLIN | POLLRDNORM;
}

// ----------------------------------------------------------------------

struct syncdev_w {
	struct sk_buff_head write_queue;
	wait_queue_head_t write_wait;
};

static inline void
syncdev_w_ctor(struct syncdev_w *sdev)
{
	skb_queue_head_init(&sdev->write_queue);
	init_waitqueue_head(&sdev->write_wait);
}

static inline void
syncdev_w_dtor(struct syncdev_w *sdev)
{
	skb_queue_purge(&sdev->write_queue);
}

static inline void 
syncdev_w_poll_wait(struct syncdev_w *wdev, struct file *file, 
		    poll_table *wait, unsigned int *mask)
{
	poll_wait(file, &wdev->write_wait, wait);
	if (skb_queue_len(&wdev->write_queue) < SYNCDEV_QUEUE_LEN)
		*mask |= POLLOUT | POLLWRNORM;
}

static inline void
syncdev_w_queue_tail(struct syncdev_w *wdev, struct sk_buff *skb)
{
	skb_queue_tail(&wdev->write_queue, skb);
}

static inline ssize_t
syncdev_w_write(struct syncdev_w *wdev, struct file *file, 
		const char *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	int retval;

	retval = -ESPIPE;
        if (ppos != &file->f_pos)
		goto out;

	retval = 0;
	if (count == 0)
		goto out;
	
	retval = -ENOMEM;
	skb = alloc_skb(count, GFP_USER);
	if (!skb)
		goto out;

	retval = -EFAULT;
	if (copy_from_user(skb_put(skb, count), buf, count))
		goto outf;

	add_wait_queue(&wdev->write_wait, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		
		retval = count;
		if (skb_queue_len(&wdev->write_queue) < SYNCDEV_QUEUE_LEN) {
			skb->pkt_type = 0;
			skb_queue_tail(&wdev->write_queue, skb);
			break;
		}
		retval = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			break;
		
		retval = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		
		schedule();
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&wdev->write_wait, &wait);

	if (retval >= 0)
		goto out;
	
 outf:
	kfree_skb(skb);
 out:
	return retval;
}

static inline struct sk_buff *
syncdev_w_dequeue(struct syncdev_w *wdev)
{
	struct sk_buff *skb;

	skb = skb_dequeue(&wdev->write_queue);
	if (!skb)
		return NULL;

	if (skb_queue_len(&wdev->write_queue) < SYNCDEV_QUEUE_LEN)
		wake_up_interruptible(&wdev->write_wait);
	
	return skb;
}
