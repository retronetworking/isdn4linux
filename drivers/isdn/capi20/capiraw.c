#include <linux/fs.h>
#include <linux/skbuff.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/uaccess.h>
#include <net/capi/command.h>
#include <net/capi/util.h>
#include <net/capi/kcapi.h>

#include "capi.h"
#include "capiminor.h"

/* -------- defines ------------------------------------------------- */

/* -------- data structures ----------------------------------------- */

/* -------- global variables ---------------------------------------- */

/* -------- driver information -------------------------------------- */

int capi_rawmajor = 190;

/* -------- file_operations for capincci ---------------------------- */

static int
capinc_raw_open(struct inode *inode, struct file *file)
{
	struct capiminor *mp;

	if (file->private_data)
		return -EEXIST;
	if ((mp = capiminor_find(MINOR(file->f_dentry->d_inode->i_rdev))) == 0)
		return -ENXIO;
/* 	if (mp->nccip == 0) */
/* 		return -ENXIO; */
	if (mp->file)
		return -EBUSY;

#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capi_raw_open %d\n", GET_USE_COUNT(THIS_MODULE));
#endif

	mp->file = file;
	file->private_data = (void *)mp;
	handle_minor_recv(mp);
	return 0;
}

static loff_t
capinc_raw_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t
capinc_raw_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct capiminor *mp = (struct capiminor *)file->private_data;
	struct sk_buff *skb;
	size_t copied = 0;

        if (ppos != &file->f_pos)
		return -ESPIPE;

	//	if (!mp || !mp->nccip)
	if (!mp)
		return -EINVAL;

	if ((skb = skb_dequeue(&mp->sdev.read_queue)) == 0) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		for (;;) {
			interruptible_sleep_on(&mp->sdev.read_wait);
/* 			if (mp->nccip == 0) */
/* 				return 0; */
			if ((skb = skb_dequeue(&mp->sdev.read_queue)) != 0)
				break;
			if (signal_pending(current))
				break;
		}
		if (skb == 0)
			return -ERESTARTNOHAND;
	}
	do {
		if (count < skb->len) {
			if (copy_to_user(buf, skb->data, count)) {
				skb_queue_head(&mp->sdev.read_queue, skb);
				return -EFAULT;
			}
			skb_pull(skb, count);
			skb_queue_head(&mp->sdev.read_queue, skb);
			copied += count;
			return copied;
		} else {
			if (copy_to_user(buf, skb->data, skb->len)) {
				skb_queue_head(&mp->sdev.read_queue, skb);
				return -EFAULT;
			}
			copied += skb->len;
			count -= skb->len;
			buf += skb->len;
			kfree_skb(skb);
		}
	} while ((skb = skb_dequeue(&mp->sdev.read_queue)) != 0);

	return copied;
}

static ssize_t
capinc_raw_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct capiminor *mp = (struct capiminor *)file->private_data;
	struct sk_buff *skb;

        if (ppos != &file->f_pos)
		return -ESPIPE;

/* 	if (!mp || !mp->nccip) */
	if (!mp)
		return -EINVAL;

	skb = alloc_skb(CAPI_DATA_B3_REQ_LEN+count, GFP_USER);

	skb_reserve(skb, CAPI_DATA_B3_REQ_LEN);
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	while (skb_queue_len(&mp->outqueue) > CAPINC_MAX_SENDQUEUE) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		interruptible_sleep_on(&mp->sendwait);
/* 		if (mp->nccip == 0) { */
/* 			kfree_skb(skb); */
/* 			return -EIO; */
/* 		} */
		if (signal_pending(current))
			return -ERESTARTNOHAND;
	}
	skb_queue_tail(&mp->outqueue, skb);
	mp->outbytes += skb->len;
	(void)handle_minor_send(mp);
	return count;
}

static unsigned int
capinc_raw_poll(struct file *file, poll_table * wait)
{
	struct capiminor *mp = (struct capiminor *)file->private_data;
	unsigned int mask = 0;

	//	if (!mp || !mp->nccip)
	if (!mp)
		return POLLERR|POLLHUP;

	poll_wait(file, &(mp->sdev.read_wait), wait);
	if (!skb_queue_empty(&mp->sdev.read_queue))
		mask |= POLLIN | POLLRDNORM;
	poll_wait(file, &(mp->sendwait), wait);
	if (skb_queue_len(&mp->outqueue) > CAPINC_MAX_SENDQUEUE)
		mask = POLLOUT | POLLWRNORM;
	return mask;
}

static int
capinc_raw_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct capiminor *mp = (struct capiminor *)file->private_data;
/* 	if (!mp || !mp->nccip) */
	if (!mp)
		return -EINVAL;

	switch (cmd) {
#ifdef CAPI_PPP_ON_RAW_DEVICE
	case PPPIOCATTACH:
		{
			int retval, val;
			if (get_user(val, (int *) arg))
				break;
			if (mp->chan_connected)
				return -EALREADY;
			mp->chan.private = mp;
#if 1
			return -EINVAL;
#else
			mp->chan.ops = &ppp_ops;
#endif

			retval = ppp_register_channel(&mp->chan, val);
			if (retval)
				return retval;
			mp->chan_connected = 1;
			mp->chan_index = val;
		}
		return 0;
	case PPPIOCDETACH:
		{
			if (!mp->chan_connected)
				return -ENXIO;
			ppp_unregister_channel(&mp->chan);
			mp->chan_connected = 0;
		}
		return 0;
	case PPPIOCGUNIT:
		{
			if (!mp->chan_connected)
				return -ENXIO;
			if (put_user(mp->chan_index, (int *) arg))
				return -EFAULT;
		}
		return 0;
#endif
	}
	return -EINVAL;
}

static int
capinc_raw_release(struct inode *inode, struct file *file)
{
	struct capiminor *mp = (struct capiminor *)file->private_data;

	if (mp) {
		lock_kernel();
		mp->file = 0;
/* 		if (mp->nccip == 0) { */
/* 			capiminor_free(mp); */
/* 			file->private_data = NULL; */
/* 		} */
		unlock_kernel();
	}

#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capinc_raw_release %d\n", GET_USE_COUNT(THIS_MODULE));
#endif
	return 0;
}

static struct file_operations capinc_raw_fops =
{
	owner:		THIS_MODULE,
	llseek:		capinc_raw_llseek,
	read:		capinc_raw_read,
	write:		capinc_raw_write,
	poll:		capinc_raw_poll,
	ioctl:		capinc_raw_ioctl,
	open:		capinc_raw_open,
	release:	capinc_raw_release,
};

int __init capiraw_init(void)
{
	int retval;

	retval = devfs_register_chrdev(capi_rawmajor, "capi/r%d",
				       &capinc_raw_fops);
	if (retval < 0) {
		printk(KERN_ERR "capiraw: unable to get major %d\n",
		       capi_rawmajor);
		return retval;
	}
        devfs_register_series (NULL, "capi/r%u", CAPINC_NR_PORTS,
			      DEVFS_FL_DEFAULT,
                              capi_rawmajor, 0,
                              S_IFCHR | S_IRUSR | S_IWUSR,
                              &capinc_raw_fops, NULL);
	return 0;
}

/* currently can't be __exit FIXME */

void capiraw_exit(void)
{
	int j;

	devfs_unregister_chrdev(capi_rawmajor, "capi/r%d");
	for (j = 0; j < CAPINC_NR_PORTS; j++) {
		char devname[32];
		sprintf(devname, "capi/r%u", j);
		devfs_unregister(devfs_find_handle(NULL, devname, capi_rawmajor, j, DEVFS_SPECIAL_CHR, 0));
	}
}
