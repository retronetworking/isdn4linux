/*
 * $Id$
 *
 * CAPI4Linux
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * 2001-02-05 : Module moved from avmb1 directory.
 *              Removed AVM specific code.
 *              Armin Schindler (mac@melware.de)
 * 2001-02-24 : added hook for /dev/capitty
 *              improve mod locking
 *              Kai Germaschewski (kai.germaschewski@gmx.de)
 * 2001-03-15 : big cleanup
 *              changed structure to being more modular
 *              Kai Germaschewski (kai.germaschewski@gmx.de
 *              
 *
 */

#ifdef CONFIG_ISDN_KCAPI_LEGACY_MODULE
#define CONFIG_ISDN_KCAPI_LEGACY
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <net/capi/kcapi.h>
#include <net/capi/util.h>
#include <net/capi/command.h>
#include "capi.h"
#include <linux/isdn_compat.h>

static char *revision = "$Revision$";

struct file_operations *capitty_dev_fops;
int (*capitty_ncci_connect)(struct capidev *cdev, struct ncci_connect_data *data);
int (*capi_ppptty_connect)(struct capidev *cdev, struct ncci_connect_data *data);


MODULE_AUTHOR("Carsten Paeth (calle@calle.in-berlin.de)");

#undef _DEBUG_TTYFUNCS		/* call to tty_driver */
#undef _DEBUG_DATAFLOW		/* data flow */

#ifdef CONFIG_ISDN_KCAPI_MIDDLEWARE
// FIXME later
#define CAPITTY_MINOR 32

EXPORT_SYMBOL(capitty_dev_fops);
EXPORT_SYMBOL(capitty_ncci_connect); // FIXME name
EXPORT_SYMBOL(capi_ppptty_connect); // FIXME name

EXPORT_SYMBOL(capincci_hijack);
EXPORT_SYMBOL(capincci_unhijack);
EXPORT_SYMBOL(capincci_send);

#ifdef CONFIG_ISDN_KCAPI_MIDDLE
EXPORT_SYMBOL(capiminor_ncciup_p);
EXPORT_SYMBOL(capiminor_ncci_getunit_p);
#endif

#endif /* CONFIG_ISDN_KCAPI_MIDDLEWARE */

/* -------- driver information -------------------------------------- */

int capi_major = 68;		/* allocated */

MODULE_PARM(capi_major, "i");

static struct capincci *capidev_find_ncci(struct capidev *cdev, u32 ncci);
static int capincci_recv_data_b3(struct capincci *np, struct sk_buff *skb);

/* -------- global variables ---------------------------------------- */

static spinlock_t capidev_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(capidev_list);

struct capi_interface *capifuncs; // FIXME

static kmem_cache_t *capidev_cachep;
static kmem_cache_t *capidh_cachep;

/* -------- struct capidev ------------------------------------------ */

struct capidev *capidev_alloc(void)
{
	struct capidev *cdev;

	cdev = (struct capidev *)kmem_cache_alloc(capidev_cachep, GFP_KERNEL);
	if (!cdev)
		return 0;
	memset(cdev, 0, sizeof(struct capidev));

	syncdev_r_ctor(&cdev->sdev);
	INIT_LIST_HEAD(&cdev->ncci_list);
	spin_lock_init(&cdev->lock);

        spin_lock(&capidev_list_lock);
	list_add_tail(&cdev->list, &capidev_list);
        spin_unlock(&capidev_list_lock);
        return cdev;
}

void capidev_free(struct capidev *cdev)
{
	struct capincci *np;
	struct list_head *p;

	if (cdev->applid)
		(*capifuncs->capi_release) (cdev->applid);
	cdev->applid = 0;

	spin_lock(&cdev->lock);
	list_for_each(p, &cdev->ncci_list) {
		np = list_entry(p, struct capincci, list);
		capincci_unhijack(np);
	}
	spin_unlock(&cdev->lock);
	
	syncdev_r_dtor(&cdev->sdev);

        spin_lock(&capidev_list_lock);
	list_del(&cdev->list);
        spin_unlock(&capidev_list_lock);

	kmem_cache_free(capidev_cachep, cdev);
}

#ifdef CONFIG_ISDN_KCAPI_LEGACY
static struct capidev *capidev_find(u16 applid)
{
	struct list_head *p;
	struct capidev *cdev = NULL;

        spin_lock(&capidev_list_lock);
	list_for_each(p, &capidev_list) {
		cdev = list_entry(p, struct capidev, list);
		if (cdev->applid == applid)
			break;
	}
        spin_unlock(&capidev_list_lock);
	if (p == &capidev_list) {
		return 0;
	}
	return cdev;
}
#endif /* CONFIG_ISDN_KCAPI_LEGACY */

/* -------- function called by lower level -------------------------- */

static void capi_signal(u16 applid, void *param)
{
	struct capidev *cdev = (struct capidev *)param;
	struct sk_buff *skb = 0;
	struct capincci *np;
	u32 ncci;

	(void) (*capifuncs->capi_get_message) (applid, &skb);
	if (!skb) {
		printk(KERN_ERR "BUG: capi_signal: no skb\n");
		return;
	}

#ifdef CONFIG_ISDN_KCAPI_MIDDLEWARE
	if (CAPIMSG_COMMAND(skb->data) != CAPI_DATA_B3)
		goto queue;

	ncci = CAPIMSG_CONTROL(skb->data);
	np = capidev_find_ncci(cdev, ncci);
	if (!np)
		goto queue;

	if (capincci_recv_data_b3(np, skb) < 0)
		/* oops, let capi application handle it :-) */
		goto queue;

	return;

 queue:
#endif /* CONFIG_ISDN_KCAPI_MIDDLEWARE */

	syncdev_r_queue_tail(&cdev->sdev, skb);
}

/* -------- file_operations for capidev ----------------------------- */

static loff_t
capi_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t
capi_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	struct sk_buff *skb;
	ssize_t retval;

	if (!cdev->applid)
		return -ENODEV;

	retval = syncdev_r_read(&cdev->sdev, file, buf, count, ppos, &skb);
	if (retval < 0)
		return retval;

	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_IND) {
		cdev->nrecvdatapkt++;
	} else {
		cdev->nrecvctlpkt++;
	}
	kfree_skb(skb);
	return retval;
}

static int // FIXME
__capi_write(struct capidev *cdev, struct sk_buff *skb)
{
	CAPIMSG_SETAPPID(skb->data, cdev->applid);

	cdev->errcode = capifuncs->capi_put_message(cdev->applid, skb);

	if (cdev->errcode) {
		kfree_skb(skb);
		return -EIO;
	}
	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_REQ) {
		cdev->nsentdatapkt++;
	} else {
		cdev->nsentctlpkt++;
	}
	return 0;
}

static ssize_t
capi_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	struct sk_buff *skb;
	u16 mlen;

        if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!cdev->applid)
		return -ENODEV;

	skb = alloc_skb(count, GFP_USER);

	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	mlen = CAPIMSG_LEN(skb->data);
	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_REQ) {
		if (mlen + CAPIMSG_DATALEN(skb->data) != count) {
			kfree_skb(skb);
			return -EINVAL;
		}
	} else {
		if (mlen != count) {
			kfree_skb(skb);
			return -EINVAL;
		}
	}
	if (__capi_write(cdev, skb) < 0)
		return -EIO;

	return count;
}

static unsigned int
capi_poll(struct file *file, poll_table *wait)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	unsigned int mask = 0;

	if (!cdev->applid)
		return POLLERR;

	poll_wait(file, &(cdev->sdev.read_wait), wait);
	mask = POLLOUT | POLLWRNORM;
	if (!skb_queue_empty(&cdev->sdev.read_queue))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int
capi_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	struct capidev *cdev = file->private_data;
	capi_ioctl_struct data;

	switch (cmd) {
	case CAPI_INSTALLED:
		if ((*capifuncs->capi_isinstalled)() == CAPI_NOERROR)
			return 0;
		return -ENXIO;
	case CAPI_REGISTER:
		if (copy_from_user((void *) &data.rparams,
				   (void *) arg,
				   sizeof(struct capi_register_params)))
			return -EFAULT;
		if (cdev->applid)
			return -EEXIST;
		cdev->errcode = (*capifuncs->capi_register) (&data.rparams,
							     &cdev->applid);
		if (cdev->errcode) {
			cdev->applid = 0;
			return -EIO;
		}
		(void) (*capifuncs->capi_set_signal) (cdev->applid, capi_signal, cdev);
		return (int)cdev->applid;
	case CAPI_GET_VERSION:
		if (copy_from_user((void *) &data.contr,
				   (void *) arg,
				   sizeof(data.contr)))
			return -EFAULT;
		cdev->errcode = (*capifuncs->capi_get_version) (data.contr, &data.version);
		if (cdev->errcode)
			return -EIO;
		if (copy_to_user((void *) arg,
				 (void *) &data.version,
				 sizeof(data.version)))
			return -EFAULT;
		return 0;
	case CAPI_GET_SERIAL:
		if (copy_from_user((void *) &data.contr,
				   (void *) arg,
				   sizeof(data.contr)))
			return -EFAULT;
		cdev->errcode = (*capifuncs->capi_get_serial) (data.contr, data.serial);
		if (cdev->errcode)
			return -EIO;
		if (copy_to_user((void *) arg,
				 (void *) data.serial,
				 sizeof(data.serial)))
			return -EFAULT;
		return 0;
	case CAPI_GET_PROFILE:
		if (copy_from_user((void *) &data.contr,
				   (void *) arg,
				   sizeof(data.contr)))
			return -EFAULT;
		
		if (data.contr == 0) {
			cdev->errcode = (*capifuncs->capi_get_profile) (data.contr, &data.profile);
			if (cdev->errcode)
				return -EIO;
			
			if (copy_to_user((void *) arg,
					 (void *) &data.profile.ncontroller,
					 sizeof(data.profile.ncontroller)))
				return -EFAULT;
		} else {
			cdev->errcode = (*capifuncs->capi_get_profile) (data.contr, &data.profile);
			if (cdev->errcode)
				return -EIO;
			
			if (copy_to_user((void *) arg,
					 (void *) &data.profile,
					 sizeof(data.profile)))
				return -EFAULT;
		}
		return 0;
	case CAPI_GET_MANUFACTURER:
		if (copy_from_user((void *) &data.contr,
				   (void *) arg,
				   sizeof(data.contr)))
			return -EFAULT;
		cdev->errcode = (*capifuncs->capi_get_manufacturer) (data.contr, data.manufacturer);
		if (cdev->errcode)
			return -EIO;
		
		if (copy_to_user((void *) arg, (void *) data.manufacturer,
				 sizeof(data.manufacturer)))
			return -EFAULT;
		
		return 0;
	case CAPI_GET_ERRCODE:
		data.errcode = cdev->errcode;
		cdev->errcode = CAPI_NOERROR;
		if (arg) {
			if (copy_to_user((void *) arg,
					 (void *) &data.errcode,
					 sizeof(data.errcode)))
				return -EFAULT;
		}
		return data.errcode; // FIXME?
	case CAPI_MANUFACTURER_CMD:
	{
		struct capi_manufacturer_cmd mcmd;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user((void *) &mcmd, (void *) arg,
				   sizeof(mcmd)))
			return -EFAULT;
		return (*capifuncs->capi_manufacturer) (mcmd.cmd, mcmd.data);
	}
#ifdef CONFIG_ISDN_KCAPI_MIDDLEWARE
	case CAPI_NCCI_CONNECT:
	{
		struct ncci_connect_data data;
		int retval;

		if (copy_from_user(&data,
				   (void *)arg,
				   sizeof(struct ncci_connect_data)))
			return -EFAULT;
		// FIXME add check to register to see if np already exists
		retval = -ESRCH;
		switch (data.type) {
		case CAPI_NCCI_TYPE_TTYI:
			if (!capitty_ncci_connect)
				return -ENXIO;
			retval = capitty_ncci_connect(cdev, &data);
			break;
		case CAPI_NCCI_TYPE_PPPTTY:
			if (!capi_ppptty_connect)
				return -ENXIO;
			retval = capi_ppptty_connect(cdev, &data);
			if (copy_to_user((void *)arg, &data,
				   sizeof(struct ncci_connect_data)))
				return -EFAULT;
			break;
		}
		return retval;
	}
	case CAPI_NCCI_DISCONNECT:
	{
		unsigned int ncci = arg;
		struct capincci *np = capidev_find_ncci(cdev, ncci);
		
		if (!np)
			return -ESRCH;

		capincci_unhijack(np);
	}
#ifdef CONFIG_ISDN_KCAPI_MIDDLE
	case CAPI_SET_FLAGS: // FIXME ???
	case CAPI_CLR_FLAGS:
	{
		unsigned userflags;
		if (copy_from_user((void *) &userflags,
				   (void *) arg,
				   sizeof(userflags)))
			return -EFAULT;
		if (cmd == CAPI_SET_FLAGS)
			cdev->userflags |= userflags;
		else
			cdev->userflags &= ~userflags;
	}
	case CAPI_GET_FLAGS:
	{
		if (copy_to_user((void *) arg,
				 (void *) &cdev->userflags,
				 sizeof(cdev->userflags)))
			return -EFAULT;
	}
#endif
#if 0
	case CAPI_NCCI_OPENCOUNT:
	{
		struct capincci *nccip;
		struct capiminor *mp;
		unsigned ncci;
		int count = 0;
		if (copy_from_user((void *) &ncci,
				   (void *) arg,
				   sizeof(ncci)))
			return -EFAULT;
		nccip = capincci_find(cdev, (u32) ncci);
		if (!nccip)
			return 0;
		if ((mp = nccip->minorp) != 0) {
			count += atomic_read(&mp->ttyopencount);
			if (mp->file)
				count++;
			}
		return count;
	}
#endif
#ifdef CONFIG_ISDN_KCAPI_MIDDLE
	case CAPI_NCCI_GETUNIT:
	{
		unsigned ncci;
		struct capincci *np;
	
		if (copy_from_user((void *) &ncci,
				   (void *) arg,
				   sizeof(ncci)))
			return -EFAULT;
		np = capidev_find_ncci(cdev, ncci);
		if (!np)
			return -ESRCH;
		if (!capiminor_ncci_getunit_p)
			break;

		return capiminor_ncci_getunit_p(cdev, np);
	}
#endif
#endif /* CONFIG_ISDN_KCAPI_MIDDLEWARE */
	}
	 
	return -EINVAL;
}

// partly copied from misc.c FIXME?

static int
capi_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct file_operations *old_fops, *new_fops = NULL;
	int retval;

#ifndef COMPAT_HAS_FILEOP_OWNER
	MOD_INC_USE_COUNT;
#endif
	if (minor == CAPITTY_MINOR) {
		new_fops = fops_get(capitty_dev_fops);
		retval = -ENODEV;
		if (!new_fops)
			goto outf;

		retval = 0;
		old_fops = file->f_op;
		file->f_op = new_fops;
		if (file->f_op->open) {
			retval=file->f_op->open(inode,file);
			if (retval) {
				fops_put(file->f_op);
				file->f_op = fops_get(old_fops);
			}
		}
		fops_put(old_fops);
		goto outf;
	}

	retval = -ENOMEM;
	file->private_data = capidev_alloc();
	if (!file->private_data)
		goto outf;

	return 0;
 outf:
#ifndef COMPAT_HAS_FILEOP_OWNER
	MOD_DEC_USE_COUNT;
#endif
	return retval;
}

static int
capi_release(struct inode *inode, struct file *file)
{
	struct capidev *cdev = (struct capidev *)file->private_data;

	capidev_free(cdev);
	file->private_data = NULL;
	
#ifndef COMPAT_HAS_FILEOP_OWNER
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

static struct file_operations capi_fops =
{
#ifdef COMPAT_HAS_FILEOP_OWNER
	owner:		THIS_MODULE,
#endif
	llseek:		capi_llseek,
	read:		capi_read,
	write:		capi_write,
	poll:		capi_poll,
	ioctl:		capi_ioctl,
	open:		capi_open,
	release:	capi_release,
};

/* ---------------------------------------------------------------------- */

static struct capincci *
capidev_find_ncci(struct capidev *cdev, u32 ncci)
{
	struct list_head *p;
	struct capincci *np = NULL;

        spin_lock(&cdev->lock);
	list_for_each(p, &cdev->ncci_list) {
		np = list_entry(p, struct capincci, list);
		if (np->ncci == ncci)
			break;
	}
        spin_unlock(&cdev->lock);
	if (p == &cdev->ncci_list)
		return 0;

	return np;
}

static int
capincci_add_ack(struct capincci *mp, u16 datahandle)
{
	struct datahandle *n;

	n = kmem_cache_alloc(capidh_cachep, GFP_ATOMIC);
	if (!n) {
	   printk(KERN_ERR "capi: alloc datahandle failed\n");
	   return -1;
	}
	n->datahandle = datahandle;
        spin_lock(&mp->lock);
	list_add_tail(&n->list, &mp->ackqueue);
	mp->nack++;
        spin_unlock(&mp->lock);
	return 0;
}

static int
capincci_del_ack(struct capincci *np, u16 datahandle)
{
	struct list_head *p;
	struct datahandle *ack;
	int retval = -ESRCH;

        spin_lock(&np->lock);
	list_for_each(p, &np->ackqueue) {
		ack = list_entry(p, struct datahandle, list);
 		if (ack->datahandle == datahandle) {
			list_del(p);
			kmem_cache_free(capidh_cachep, ack);
			np->nack--;
			retval = 0;
			break;
		}
	}
        spin_unlock(&np->lock);
	return retval;
}

static void
capincci_purge_ack(struct capincci *np)
{
	struct list_head *p;
	struct datahandle *ack;

        spin_lock(&np->lock);
	while (!list_empty(&np->ackqueue)) {
		p = np->ackqueue.next;
		ack = list_entry(p, struct datahandle, list);
		list_del(p);
		kmem_cache_free(capidh_cachep, ack);
		np->nack--;
	}
        spin_unlock(&np->lock);
}

int
capincci_send(struct capincci *np, struct sk_buff *skb)
{
	int len;
	u16 datahandle;
	u16 errcode;
	
	len = skb->len;
	datahandle = np->datahandle++;

	skb_push(skb, CAPI_DATA_B3_REQ_LEN);
	capimsg_setu16(skb->data, 0, CAPI_DATA_B3_REQ_LEN);
	capimsg_setu16(skb->data, 2, np->cdev->applid);
	capimsg_setu8 (skb->data, 4, CAPI_DATA_B3);
	capimsg_setu8 (skb->data, 5, CAPI_REQ);
	capimsg_setu16(skb->data, 6, np->msgid++);
	capimsg_setu32(skb->data, 8, np->ncci);	/* NCCI */
	capimsg_setu32(skb->data, 12, 0); /* Data32 */
	capimsg_setu16(skb->data, 16, len);	/* Data length */
	capimsg_setu16(skb->data, 18,datahandle);
	capimsg_setu16(skb->data, 20, 0);	/* Flags */
	
	if (capincci_add_ack(np, datahandle) < 0) {
		skb_pull(skb, CAPI_DATA_B3_REQ_LEN);
		return -EAGAIN;
	}
	errcode = (*capifuncs->capi_put_message) (np->cdev->applid, skb);
	if (errcode != CAPI_NOERROR) {
		capincci_del_ack(np, datahandle);
		skb_pull(skb, CAPI_DATA_B3_REQ_LEN);
		if (errcode == CAPI_SENDQUEUEFULL) {
			// shouldn't happen
			HDEBUG;
			return -EAGAIN;
		}
		return -EIO;
	}
	
	if (np->nack >= 3) { // FIXME tune
		if (!test_and_set_bit(NCCI_RECV_QUEUE_FULL, &np->flags) &&
		    np->send_stop_queue)
			np->send_stop_queue(np);
	}
	return len;
}

/*
 * here we use the skb->destructor method to send a corresponding
 * DATA_B3_RESP back to kcapi when an skb which was handed to a module
 * via capincci->recv is kfree_skb'ed
 */

static void
capincci_skb_destructor(struct sk_buff *skb)
{
	struct capincci *np = (struct capincci *) skb->sk;
	unsigned char *p;
	struct sk_buff *nskb;
	u16 errcode;

	nskb = alloc_skb(CAPI_DATA_B3_RESP_LEN, GFP_ATOMIC);
	if (!nskb) {
		HDEBUG;
		return;
	}
	skb_push(skb, CAPI_DATA_B3_IND_LEN);
	p = skb_put(nskb, CAPI_DATA_B3_RESP_LEN);
	memcpy(p, skb->data, CAPI_DATA_B3_RESP_LEN); 
	capimsg_setu16(p, 0, CAPI_DATA_B3_RESP_LEN);
	capimsg_setu8 (p, 5, CAPI_RESP);
	capimsg_setu16(p, 12, CAPIMSG_U16(skb->data, CAPIMSG_BASELEN+4+4+2)); // datahandle
	
	errcode = (*capifuncs->capi_put_message)(np->cdev->applid, nskb);
	if (errcode != CAPI_NOERROR) {
		printk(KERN_ERR "capi: send DATA_B3_RESP failed=%x\n",
		       errcode);
		kfree_skb(nskb);
	}
}

static int
capincci_recv_data_b3(struct capincci *np, struct sk_buff *skb)
{
	u16 datahandle;
	int retval;

	if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_IND) {
		if (!np->recv)
			return -ESRCH;
		
		if (CAPIMSG_LEN(skb->data) != CAPI_DATA_B3_IND_LEN)
			BUG();

		skb_pull(skb, CAPI_DATA_B3_IND_LEN);
		if (skb->destructor || skb->sk)
			BUG();
		skb->destructor = capincci_skb_destructor;
		skb->sk = (struct sock *) np;
		retval = np->recv(np, skb);
		if (retval < 0) 
			skb->destructor = NULL;
		return retval;
	} 
	if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_CONF) {
		datahandle = CAPIMSG_U16(skb->data, CAPIMSG_BASELEN+4);
		if (capincci_del_ack(np, datahandle)) {
			HDEBUG;
			return -ESRCH;
		}
		kfree_skb(skb);
		if (test_and_clear_bit(NCCI_RECV_QUEUE_FULL, &np->flags))
			np->send_wake_queue(np);
		return 0;
	} 
	return -EINVAL;
}

void
capincci_hijack(struct capidev *cdev, struct capincci *np)
{
	np->cdev = cdev;
	np->datahandle = 0;
	np->msgid = 0;
	np->flags = 0;
	INIT_LIST_HEAD(&np->ackqueue);
	
        spin_lock(&cdev->lock);
        list_add_tail(&np->list, &cdev->ncci_list);
	spin_unlock(&cdev->lock);
}

void
capincci_unhijack(struct capincci *np)
{
	list_del(&np->list);
	if (np->dtor) 
		np->dtor(np);
	capincci_purge_ack(np);
}

/* -------- /proc functions ----------------------------------------- */

/*
 * /proc/capi/capi20:
 *  minor applid nrecvctlpkt nrecvdatapkt nsendctlpkt nsenddatapkt
 */
static int proc_capidev_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
	struct list_head *p;
        struct capidev *cdev;
	int len = 0;

        spin_lock(&capidev_list_lock);
	list_for_each(p, &capidev_list) {
		cdev = list_entry(p, struct capidev, list);
		len += sprintf(page+len, "%d %d %lu %lu %lu %lu\n",
			0,
			cdev->applid,
			cdev->nrecvctlpkt,
			cdev->nrecvdatapkt,
			cdev->nsentctlpkt,
			cdev->nsentdatapkt);
		if (len <= off) {
			off -= len;
			len = 0;
		} else {
			if (len-off > count)
				goto endloop;
		}
	}
endloop:
        spin_unlock(&capidev_list_lock);
	if (len < count)
		*eof = 1;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

#ifdef CONFIG_ISDN_KCAPI_MIDDLEWARE
/*
 * /proc/capi/capi20ncci:
 *  applid ncci
 */
static int proc_capincci_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
	struct list_head *p, *pp;
        struct capidev *cdev;
        struct capincci *np;
	int len = 0;

        spin_lock(&capidev_list_lock);
	list_for_each(p, &capidev_list) {
		cdev = list_entry(p, struct capidev, list);
		spin_lock(&cdev->lock);
		list_for_each(pp, &cdev->ncci_list) {
			np = list_entry(pp, struct capincci, list);
			len += sprintf(page+len, "%d 0x%x%s\n",
				cdev->applid,
				np->ncci,
// FIXME?
//				np->minorp && np->minorp->file ? " open" : "");
			        "");
			if (len <= off) {
				off -= len;
				len = 0;
			} else {
				if (len-off > count) {
					goto endloop;
                                }
			}
		}
	}
endloop:
        spin_unlock(&capidev_list_lock);
	*start = page+off;
	if (len < count)
		*eof = 1;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}
#endif /* CONFIG_ISDN_KCAPI_MIDDLEWARE */

static struct procfsentries {
	char *name;
	mode_t mode;
	int (*read_proc)(char *page, char **start, off_t off,
                                       int count, int *eof, void *data);
	struct proc_dir_entry *procent;
} procfsentries[] = {
	{ "capi/capi20",     0, proc_capidev_read_proc },
	{ "capi/capi20ncci", 0, proc_capincci_read_proc },
};

static void __init proc_init(void)
{
    int nelem = sizeof(procfsentries)/sizeof(procfsentries[0]);
    int i;

    for (i=0; i < nelem; i++) {
        struct procfsentries *p = procfsentries + i;
	p->procent = create_proc_entry(p->name, p->mode, 0);
	if (p->procent) p->procent->read_proc = p->read_proc;
    }
}

static void __exit proc_exit(void)
{
    int nelem = sizeof(procfsentries)/sizeof(procfsentries[0]);
    int i;

    for (i=nelem-1; i >= 0; i--) {
        struct procfsentries *p = procfsentries + i;
	if (p->procent) {
	   remove_proc_entry(p->name, 0);
	   p->procent = 0;
	}
    }
}

/* -------- init function and module interface ---------------------- */


static int __init alloc_init(void)
{
	capidev_cachep = kmem_cache_create("capi20_dev",
					 sizeof(struct capidev),
					 0,
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!capidev_cachep)
		goto outf;

	capidh_cachep = kmem_cache_create("capi20_dh",
					 sizeof(struct datahandle),
					 0,
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!capidh_cachep)
		goto outf_capidev;

	return 0;

 outf_capidev:
	kmem_cache_destroy(capidev_cachep);
 outf:
	return -ENOMEM;
}

static void __exit alloc_exit(void)
{
	kmem_cache_destroy(capidev_cachep);
	kmem_cache_destroy(capidh_cachep);
}

static void lower_callback(unsigned int cmd, u32 contr, void *data)
{
#ifdef CONFIG_ISDN_KCAPI_LEGACY
	struct capi_ncciinfo *nip;
	struct capincci *np;
	struct capidev *cdev;
#endif /* CONFIG_ISDN_KCAPI_LEGACY */

	switch (cmd) {
	case KCI_CONTRUP:
		printk(KERN_INFO "capi: controller %hu up\n", contr);
		break;
	case KCI_CONTRDOWN:
		printk(KERN_INFO "capi: controller %hu down\n", contr);
		break;
#ifdef CONFIG_ISDN_KCAPI_LEGACY
	case KCI_NCCIUP:
		nip = (struct capi_ncciinfo *)data;
		if (!(cdev = capidev_find(nip->applid)))
			break;
		if (!(cdev->userflags & CAPIFLAG_HIGHJACKING))
			break;

		if (capiminor_ncciup_p)
			capiminor_ncciup_p(cdev, nip->ncci);
		break;
	case KCI_NCCIDOWN:
		nip = (struct capi_ncciinfo *)data;
		cdev = capidev_find(nip->applid);
		if (!cdev)
			break;
		if (!(cdev->userflags & CAPIFLAG_HIGHJACKING))
			break;
		np = capidev_find_ncci(cdev, nip->ncci);
		if (!np)
			break;

		capincci_unhijack(np);
		break;
#endif /* CONFIG_ISDN_KCAPI_LEGACY */
	}
}

static struct capi_interface_user cuser = {
	name: "capi20",
	callback: lower_callback,
};

static char rev[10];
static devfs_handle_t devfs_handle;

static int __init capi_init(void)
{
	char *p;
	int retval;
	MOD_INC_USE_COUNT;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 2);
		p = strchr(rev, '$');
		*(p-1) = 0;
	} else
		strcpy(rev, "???");

	retval = -EBUSY;
	capifuncs = attach_capi_interface(&cuser);
	if (!capifuncs)
		goto out;

	retval = devfs_register_chrdev(capi_major, "capi20", &capi_fops);
	if (retval) {
		printk(KERN_ERR "capi20: unable to get major %d\n", capi_major);
		goto outf_capi;
	}
	// FIXME why?
	retval = -EBUSY;
	devfs_handle = devfs_register (NULL, "isdn/capi20", DEVFS_FL_DEFAULT,
				       capi_major, 0, S_IFCHR | S_IRUSR | S_IWUSR,
				       &capi_fops, NULL);

	retval = alloc_init();
	if (retval < 0)
		goto outf_chrdev;

	proc_init();

	printk(KERN_NOTICE "capi20: Rev %s started up\n", rev);

	retval = 0;
	goto out;

 outf_chrdev:
	devfs_unregister(devfs_handle);
	devfs_unregister_chrdev(capi_major, "capi20");
 outf_capi:
	detach_capi_interface(&cuser);
 out:
	MOD_DEC_USE_COUNT;
	return retval;
}

static void __exit capi_exit(void)
{
	alloc_exit();
	(void)proc_exit();

	devfs_unregister_chrdev(capi_major, "capi20");
	devfs_unregister(devfs_handle);
	detach_capi_interface(&cuser);
	printk(KERN_NOTICE "capi: Rev %s: unloaded\n", rev);
}

module_init(capi_init);
module_exit(capi_exit);
