/*
 * $Id$
 *
 * CAPI 2.0 Interface for Linux
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log$
 * Revision 1.24  2000/03/03 15:50:42  calle
 * - kernel CAPI:
 *   - Changed parameter "param" in capi_signal from __u32 to void *.
 *   - rewrote notifier handling in kcapi.c
 *   - new notifier NCCI_UP and NCCI_DOWN
 * - User CAPI:
 *   - /dev/capi20 is now a cloning device.
 *   - middleware extentions prepared.
 * - capidrv.c
 *   - locking of list operations and module count updates.
 *
 * Revision 1.23  2000/02/26 01:00:53  keil
 * changes from 2.3.47
 *
 * Revision 1.22  1999/11/13 21:27:16  keil
 * remove KERNELVERSION
 *
 * Revision 1.21  1999/09/10 17:24:18  calle
 * Changes for proposed standard for CAPI2.0:
 * - AK148 "Linux Exention"
 *
 * Revision 1.20  1999/09/07 09:02:53  calle
 * SETDATA removed. Now inside the kernel the datapart of DATA_B3_REQ and
 * DATA_B3_IND is always directly after the CAPI message. The "Data" member
 * ist never used inside the kernel.
 *
 * Revision 1.19  1999/07/09 15:05:42  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.18  1999/07/06 07:42:01  calle
 * - changes in /proc interface
 * - check and changed calls to [dev_]kfree_skb and [dev_]alloc_skb.
 *
 * Revision 1.17  1999/07/01 15:26:30  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 * Revision 1.16  1999/07/01 08:22:57  keil
 * compatibility macros now in <linux/isdn_compat.h>
 *
 * Revision 1.15  1999/06/21 15:24:11  calle
 * extend information in /proc.
 *
 * Revision 1.14  1999/06/10 16:51:03  calle
 * Bugfix: open/release of control device was not handled correct.
 *
 * Revision 1.13  1998/08/28 04:32:25  calle
 * Added patch send by Michael.Mueller4@post.rwth-aachen.de, to get AVM B1
 * driver running with 2.1.118.
 *
 * Revision 1.12  1998/05/26 22:39:34  he
 * sync'ed with 2.1.102 where appropriate (CAPABILITY changes)
 * concap typo
 * cleared dev.tbusy in isdn_net BCONN status callback
 *
 * Revision 1.11  1998/03/09 17:46:37  he
 * merged in 2.1.89 changes
 *
 * Revision 1.10  1998/02/13 07:09:13  calle
 * change for 2.1.86 (removing FREE_READ/FREE_WRITE from [dev]_kfree_skb()
 *
 * Revision 1.9  1998/01/31 11:14:44  calle
 * merged changes to 2.0 tree, prepare 2.1.82 to work.
 *
 * Revision 1.8  1997/11/04 06:12:08  calle
 * capi.c: new read/write in file_ops since 2.1.60
 * capidrv.c: prepared isdnlog interface for d2-trace in newer firmware.
 * capiutil.c: needs config.h (CONFIG_ISDN_DRV_AVMB1_VERBOSE_REASON)
 * compat.h: added #define LinuxVersionCode
 *
 * Revision 1.7  1997/10/11 10:29:34  calle
 * llseek() parameters changed in 2.1.56.
 *
 * Revision 1.6  1997/10/01 09:21:15  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.5  1997/08/21 23:11:55  fritz
 * Added changes for kernels >= 2.1.45
 *
 * Revision 1.4  1997/05/27 15:17:50  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.3  1997/05/18 09:24:14  calle
 * added verbose disconnect reason reporting to avmb1.
 * some fixes in capi20 interface.
 * changed info messages for B1-PCI
 *
 * Revision 1.2  1997/03/05 21:17:59  fritz
 * Added capi_poll for compiling under 2.1.27
 *
 * Revision 1.1  1997/03/04 21:50:29  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#ifdef HAVE_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif /* HAVE_DEVFS_FS */
#include <linux/isdn_compat.h>
#include "capiutil.h"
#include "capicmd.h"
#ifdef COMPAT_HAS_kmem_cache
#include <linux/slab.h>
#endif

static char *revision = "$Revision$";

MODULE_AUTHOR("Carsten Paeth (calle@calle.in-berlin.de)");

/* -------- driver information -------------------------------------- */

int capi_major = 68;		/* allocated */

MODULE_PARM(capi_major, "i");

/* -------- defines ------------------------------------------------- */

#define CAPINC_MAX_RECVQUEUE	10
#define CAPINC_MAX_SENDQUEUE	10
#define CAPI_MAX_BLKSIZE	2048

/* -------- data structures ----------------------------------------- */

struct capidev;
struct capincci;

struct capincci {
	struct capincci *next;
	__u32		 ncci;
	struct capidev	*cdev;
};

struct capidev {
	struct capidev *next;
	struct file    *file;
	__u16		applid;
	__u16		errcode;
	unsigned int    minor;
	unsigned        userflags;

	struct sk_buff_head recvqueue;
#ifdef COMPAT_HAS_NEW_WAITQ
	wait_queue_head_t recvwait;
#else
	struct wait_queue *recvwait;
#endif

	/* Statistic */
	unsigned long	nrecvctlpkt;
	unsigned long	nrecvdatapkt;
	unsigned long	nsentctlpkt;
	unsigned long	nsentdatapkt;
	
	struct capincci *nccis;
};

/* -------- global variables ---------------------------------------- */

static struct capi_interface *capifuncs = 0;
static struct capidev *capidev_openlist = 0;

#ifdef COMPAT_HAS_kmem_cache
static kmem_cache_t *capidev_cachep = 0; 
static kmem_cache_t *capincci_cachep = 0; 
#endif

/* -------- struct capincci ----------------------------------------- */

static struct capincci *capincci_alloc(struct capidev *cdev, __u32 ncci)
{
	struct capincci *np, **pp;

#ifdef COMPAT_HAS_kmem_cache
	np = (struct capincci *)kmem_cache_alloc(capincci_cachep, GFP_ATOMIC);
#else
	np = (struct capincci *)kmalloc(sizeof(struct capincci), GFP_ATOMIC);
#endif
	if (!np)
		return 0;
	memset(np, 0, sizeof(struct capincci));
	np->ncci = ncci;
	np->cdev = cdev;
	for (pp=&cdev->nccis; *pp; pp = &(*pp)->next)
		;
	*pp = np;
        return np;
}

static void capincci_free(struct capidev *cdev, __u32 ncci)
{
	struct capincci *np, **pp;

	pp=&cdev->nccis;
	while (*pp) {
		np = *pp;
		if (ncci == 0xffffffff || np->ncci == ncci) {
			*pp = (*pp)->next;
#ifdef COMPAT_HAS_kmem_cache
			kmem_cache_free(capincci_cachep, np);
#else
			kfree(np);
#endif
			if (*pp == 0) return;
		} else {
			pp = &(*pp)->next;
		}
	}
}

struct capincci *capincci_find(struct capidev *cdev, __u32 ncci)
{
	struct capincci *p;

	for (p=cdev->nccis; p ; p = p->next) {
		if (p->ncci == ncci)
			break;
	}
	return p;
}

/* -------- struct capidev ------------------------------------------ */

static struct capidev *capidev_alloc(struct file *file)
{
	struct capidev *cdev;
	struct capidev **pp;

#ifdef COMPAT_HAS_kmem_cache
	cdev = (struct capidev *)kmem_cache_alloc(capidev_cachep, GFP_KERNEL);
#else
	cdev = (struct capidev *)kmalloc(sizeof(struct capidev), GFP_KERNEL);
#endif
	if (!cdev)
		return 0;
	memset(cdev, 0, sizeof(struct capidev));
	cdev->file = file;
	cdev->minor = MINOR_PART(file);

	skb_queue_head_init(&cdev->recvqueue);
#ifdef COMPAT_HAS_NEW_WAITQ
	init_waitqueue_head(&cdev->recvwait);
#endif
	pp=&capidev_openlist;
	while (*pp) pp = &(*pp)->next;
	*pp = cdev;
        return cdev;
}

static void capidev_free(struct capidev *cdev)
{
	struct capidev **pp;
	struct sk_buff *skb;

	if (cdev->applid)
		(*capifuncs->capi_release) (cdev->applid);
	cdev->applid = 0;

	while ((skb = skb_dequeue(&cdev->recvqueue)) != 0) {
		kfree_skb(skb);
	}
	
	pp=&capidev_openlist;
	while (*pp && *pp != cdev) pp = &(*pp)->next;
	if (*pp)
		*pp = cdev->next;

#ifdef COMPAT_HAS_kmem_cache
	kmem_cache_free(capidev_cachep, cdev);
#else
	kfree(cdev);
#endif
}

static struct capidev *capidev_find(__u16 applid)
{
	struct capidev *p;
	for (p=capidev_openlist; p; p = p->next) {
		if (p->applid == applid)
			break;
	}
	return p;
}

/* -------- function called by lower level -------------------------- */

static void capi_signal(__u16 applid, void *param)
{
	struct capidev *cdev = (struct capidev *)param;
	struct capincci *np;
	struct sk_buff *skb = 0;
	__u32 ncci;

	(void) (*capifuncs->capi_get_message) (applid, &skb);
	if (!skb) {
		printk(KERN_ERR "BUG: capi_signal: no skb\n");
		return;
	}

	if (CAPIMSG_COMMAND(skb->data) != CAPI_DATA_B3) {
		skb_queue_tail(&cdev->recvqueue, skb);
		wake_up_interruptible(&cdev->recvwait);
		return;
	}
	ncci = CAPIMSG_CONTROL(skb->data);
	for (np = cdev->nccis; np && np->ncci != ncci; np = np->next)
		;
	if (!np) {
		printk(KERN_ERR "BUG: capi_signal: ncci not found\n");
		skb_queue_tail(&cdev->recvqueue, skb);
		wake_up_interruptible(&cdev->recvwait);
		return;
	}
	skb_queue_tail(&cdev->recvqueue, skb);
	wake_up_interruptible(&cdev->recvwait);
}

/* -------- file_operations for capidev ----------------------------- */

static long long capi_llseek(struct file *file,
			     long long offset, int origin)
{
	return -ESPIPE;
}

static ssize_t capi_read(struct file *file, char *buf,
		      size_t count, loff_t *ppos)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	struct sk_buff *skb;
	int retval;
	size_t copied;

	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!cdev->applid)
		return -ENODEV;

	if ((skb = skb_dequeue(&cdev->recvqueue)) == 0) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		for (;;) {
			interruptible_sleep_on(&cdev->recvwait);
			if ((skb = skb_dequeue(&cdev->recvqueue)) != 0)
				break;
			if (signal_pending(current))
				break;
		}
		if (skb == 0)
			return -ERESTARTNOHAND;
	}
	if (skb->len > count) {
		skb_queue_head(&cdev->recvqueue, skb);
		return -EMSGSIZE;
	}
	retval = copy_to_user(buf, skb->data, skb->len);
	if (retval) {
		skb_queue_head(&cdev->recvqueue, skb);
		return retval;
	}
	copied = skb->len;

	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_IND) {
		cdev->nrecvdatapkt++;
	} else {
		cdev->nrecvctlpkt++;
	}

	kfree_skb(skb);

	return copied;
}

static ssize_t capi_write(struct file *file, const char *buf,
		       size_t count, loff_t *ppos)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	struct sk_buff *skb;
	int retval;
	__u16 mlen;

        if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!cdev->applid)
		return -ENODEV;

	skb = alloc_skb(count, GFP_USER);

	if ((retval = copy_from_user(skb_put(skb, count), buf, count))) {
		kfree_skb(skb);
		return retval;
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
	CAPIMSG_SETAPPID(skb->data, cdev->applid);

	cdev->errcode = (*capifuncs->capi_put_message) (cdev->applid, skb);

	if (cdev->errcode) {
		kfree_skb(skb);
		return -EIO;
	}
	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_REQ) {
		cdev->nsentdatapkt++;
	} else {
		cdev->nsentctlpkt++;
	}
	return count;
}

static unsigned int
capi_poll(struct file *file, poll_table * wait)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	unsigned int mask = 0;

	if (!cdev->applid)
		return POLLERR;

	poll_wait(file, &(cdev->recvwait), wait);
	mask = POLLOUT | POLLWRNORM;
	if (!skb_queue_empty(&cdev->recvqueue))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int capi_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct capidev *cdev = (struct capidev *)file->private_data;
	capi_ioctl_struct data;
	int retval = -EINVAL;

	switch (cmd) {
	case CAPI_REGISTER:
		{
			retval = copy_from_user((void *) &data.rparams,
						(void *) arg, sizeof(struct capi_register_params));
			if (retval)
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
		}
		return (int)cdev->applid;

	case CAPI_GET_VERSION:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;
		        cdev->errcode = (*capifuncs->capi_get_version) (data.contr, &data.version);
			if (cdev->errcode)
				return -EIO;
			retval = copy_to_user((void *) arg,
					      (void *) &data.version,
					      sizeof(data.version));
			if (retval)
				return -EFAULT;
		}
		return 0;

	case CAPI_GET_SERIAL:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;
			cdev->errcode = (*capifuncs->capi_get_serial) (data.contr, data.serial);
			if (cdev->errcode)
				return -EIO;
			retval = copy_to_user((void *) arg,
					      (void *) data.serial,
					      sizeof(data.serial));
			if (retval)
				return -EFAULT;
		}
		return 0;
	case CAPI_GET_PROFILE:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;

			if (data.contr == 0) {
				cdev->errcode = (*capifuncs->capi_get_profile) (data.contr, &data.profile);
				if (cdev->errcode)
					return -EIO;

				retval = copy_to_user((void *) arg,
				      (void *) &data.profile.ncontroller,
				       sizeof(data.profile.ncontroller));

			} else {
				cdev->errcode = (*capifuncs->capi_get_profile) (data.contr, &data.profile);
				if (cdev->errcode)
					return -EIO;

				retval = copy_to_user((void *) arg,
						  (void *) &data.profile,
						   sizeof(data.profile));
			}
			if (retval)
				return -EFAULT;
		}
		return 0;

	case CAPI_GET_MANUFACTURER:
		{
			retval = copy_from_user((void *) &data.contr,
						(void *) arg,
						sizeof(data.contr));
			if (retval)
				return -EFAULT;
			cdev->errcode = (*capifuncs->capi_get_manufacturer) (data.contr, data.manufacturer);
			if (cdev->errcode)
				return -EIO;

			retval = copy_to_user((void *) arg, (void *) data.manufacturer,
					      sizeof(data.manufacturer));
			if (retval)
				return -EFAULT;

		}
		return 0;
	case CAPI_GET_ERRCODE:
		data.errcode = cdev->errcode;
		cdev->errcode = CAPI_NOERROR;
		if (arg) {
			retval = copy_to_user((void *) arg,
					      (void *) &data.errcode,
					      sizeof(data.errcode));
			if (retval)
				return -EFAULT;
		}
		return data.errcode;

	case CAPI_INSTALLED:
		if ((*capifuncs->capi_isinstalled)() == CAPI_NOERROR)
			return 0;
		return -ENXIO;

	case CAPI_MANUFACTURER_CMD:
		{
			struct capi_manufacturer_cmd mcmd;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			retval = copy_from_user((void *) &mcmd, (void *) arg,
						sizeof(mcmd));
			if (retval)
				return -EFAULT;
			return (*capifuncs->capi_manufacturer) (mcmd.cmd, mcmd.data);
		}
		return 0;

	case CAPI_SET_FLAGS:
	case CAPI_CLR_FLAGS:
		{
			unsigned userflags;
			retval = copy_from_user((void *) &userflags,
						(void *) arg,
						sizeof(userflags));
			if (retval)
				return -EFAULT;
			if (cmd == CAPI_SET_FLAGS)
				cdev->userflags |= userflags;
			else
				cdev->userflags &= ~userflags;
		}
		return 0;

	case CAPI_GET_FLAGS:
		{
			retval = copy_to_user((void *) arg,
					      (void *) &cdev->userflags,
					      sizeof(cdev->userflags));
			if (retval)
				return -EFAULT;
		}
		return 0;

	case CAPI_NCCI_OPENCOUNT:
		{
			struct capincci *nccip;
			unsigned ncci;
			int count = 0;
			retval = copy_from_user((void *) &ncci,
						(void *) arg,
						sizeof(ncci));
			if (retval)
				return -EFAULT;
			nccip = capincci_find(cdev, (__u32) ncci);
			if (!nccip)
				return 0;
			return count;
		}
		return 0;
	}
	return -EINVAL;
}

static int capi_open(struct inode *inode, struct file *file)
{
	if (file->private_data)
		return -EEXIST;

	if ((file->private_data = capidev_alloc(file)) == 0)
		return -ENOMEM;

	MOD_INC_USE_COUNT;
	return 0;
}

static int capi_release(struct inode *inode, struct file *file)
{
	struct capidev *cdev = (struct capidev *)file->private_data;

	capincci_free(cdev, 0xffffffff);
	capidev_free(cdev);

	MOD_DEC_USE_COUNT;
	return 0;
}

static struct file_operations capi_fops =
{
	llseek:         capi_llseek,
	read:           capi_read,
	write:          capi_write,
	poll:           capi_poll,
	ioctl:          capi_ioctl,
	open:           capi_open,
	release:        capi_release,                                           
};

/* -------- /proc functions ----------------------------------------- */

/*
 * /proc/capi/capi20:
 *  minor applid nrecvctlpkt nrecvdatapkt nsendctlpkt nsenddatapkt
 */
static int proc_capidev_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct capidev *cdev;
	int len = 0;
	off_t begin = 0;

	for (cdev=capidev_openlist; cdev; cdev = cdev->next) {
		len += sprintf(page+len, "%d %d %lu %lu %lu %lu\n",
			cdev->minor,
			cdev->applid,
			cdev->nrecvctlpkt,
			cdev->nrecvdatapkt,
			cdev->nsentctlpkt,
			cdev->nsentdatapkt);
		if (len+begin > off+count)
			goto endloop;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
endloop:
	if (cdev == 0)
		*eof = 1;
	if (off >= len+begin)
		return 0;
	*start = page + (begin-off);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/*
 * /proc/capi/capi20ncci:
 *  applid ncci
 */
static int proc_capincci_read_proc(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct capidev *cdev;
        struct capincci *np;
	int len = 0;
	off_t begin = 0;

	for (cdev=capidev_openlist; cdev; cdev = cdev->next) {
		for (np=cdev->nccis; np; np = np->next) {
			len += sprintf(page+len, "%d 0x%x%s\n",
				cdev->applid,
				np->ncci,
				"");
			if (len+begin > off+count)
				goto endloop;
			if (len+begin < off) {
				begin += len;
				len = 0;
			}
		}
	}
endloop:
	if (cdev == 0)
		*eof = 1;
	if (off >= len+begin)
		return 0;
	*start = page + (begin-off);
	return ((count < begin+len-off) ? count : begin+len-off);
}

static struct procfsentries {
  char *name;
  mode_t mode;
  int (*read_proc)(char *page, char **start, off_t off,
                                       int count, int *eof, void *data);
  struct proc_dir_entry *procent;
} procfsentries[] = {
   /* { "capi",		  S_IFDIR, 0 }, */
   { "capi/capi20", 	  0	 , proc_capidev_read_proc },
   { "capi/capi20ncci",   0	 , proc_capincci_read_proc },
};

static void proc_init(void)
{
    int nelem = sizeof(procfsentries)/sizeof(procfsentries[0]);
    int i;

    for (i=0; i < nelem; i++) {
        struct procfsentries *p = procfsentries + i;
	p->procent = create_proc_entry(p->name, p->mode, 0);
	if (p->procent) p->procent->read_proc = p->read_proc;
    }
}

static void proc_exit(void)
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

#ifdef COMPAT_HAS_kmem_cache

static void alloc_exit(void)
{
	if (capidev_cachep) {
		(void)kmem_cache_destroy(capidev_cachep);
		capidev_cachep = 0;
	}
	if (capincci_cachep) {
		(void)kmem_cache_destroy(capincci_cachep);
		capincci_cachep = 0;
	}
}

static int alloc_init(void)
{
	capidev_cachep = kmem_cache_create("capi20_dev", 
					 sizeof(struct capidev),
					 0, 
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!capidev_cachep) {
		alloc_exit();
		return -ENOMEM;
	}

	capincci_cachep = kmem_cache_create("capi20_ncci", 
					 sizeof(struct capincci),
					 0, 
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!capincci_cachep) {
		alloc_exit();
		return -ENOMEM;
	}
	return 0;
}
#endif

static void lower_callback(unsigned int cmd, __u32 contr, void *data)
{
	struct capi_ncciinfo *np;
	struct capidev *cdev;

	switch (cmd) {
	case KCI_CONTRUP:
		printk(KERN_INFO "capi: controller %hu up\n", contr);
		break;
	case KCI_CONTRDOWN:
		printk(KERN_INFO "capi: controller %hu down\n", contr);
		break;
	case KCI_NCCIUP:
		np = (struct capi_ncciinfo *)data;
		if ((cdev = capidev_find(np->applid)) == 0)
			return;
		(void)capincci_alloc(cdev, np->ncci);
		break;
	case KCI_NCCIDOWN:
		np = (struct capi_ncciinfo *)data;
		if ((cdev = capidev_find(np->applid)) == 0)
			return;
		(void)capincci_free(cdev, np->ncci);
		break;
	}
}

#ifdef MODULE
#define	 capi_init	init_module
#endif

static struct capi_interface_user cuser = {
	"capi20",
	lower_callback,
};

static char rev[10];

int capi_init(void)
{
	char *p;

	MOD_INC_USE_COUNT;

	if ((p = strchr(revision, ':'))) {
		strcpy(rev, p + 2);
		p = strchr(rev, '$');
		*(p-1) = 0;
	} else
		strcpy(rev, "???");

	if (devfs_register_chrdev(capi_major, "capi20", &capi_fops)) {
		printk(KERN_ERR "capi20: unable to get major %d\n", capi_major);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

#ifdef HAVE_DEVFS_FS
	devfs_register (NULL, "isdn/capi20", 0, DEVFS_FL_DEFAULT,
			capi_major, 0, S_IFCHR | S_IRUSR | S_IWUSR, 0, 0,
			&capi_fops, NULL);
#endif
	printk(KERN_NOTICE "capi20: started up with major %d\n", capi_major);

	if ((capifuncs = attach_capi_interface(&cuser)) == 0) {

		MOD_DEC_USE_COUNT;
		devfs_unregister_chrdev(capi_major, "capi20");
#ifdef HAVE_DEVFS_FS
		devfs_unregister(devfs_find_handle(NULL, "capi20", 0,
						   capi_major, 0,
						   DEVFS_SPECIAL_CHR, 0));
#endif
		return -EIO;
	}

#ifdef COMPAT_HAS_kmem_cache
	if (alloc_init() < 0) {
		(void) detach_capi_interface(&cuser);
		devfs_unregister_chrdev(capi_major, "capi20");
		MOD_DEC_USE_COUNT;
#ifdef HAVE_DEVFS_FS
		devfs_unregister(devfs_find_handle(NULL, "capi20", 0,
						   capi_major, 0,
						   DEVFS_SPECIAL_CHR, 0));
		return -ENOMEM;
	}
#endif

	(void)proc_init();

	printk(KERN_NOTICE "capi20: Rev%s: started up with major %d\n",
				rev, capi_major);

	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
#ifdef HAVE_DEVFS_FS
	int i;
	char devname[32];

#endif
#ifdef COMPAT_HAS_kmem_cache
	alloc_exit();
#endif
	(void)proc_exit();
	devfs_unregister_chrdev(capi_major, "capi20");
#ifdef HAVE_DEVFS_FS
	devfs_unregister(devfs_find_handle(NULL, "isdn/capi20", 0, capi_major, 0, DEVFS_SPECIAL_CHR, 0));
	for (i = 0; i < 10; i++) {
		sprintf (devname, "isdn/capi20.0%i", i);
		devfs_unregister(devfs_find_handle(NULL, devname, 0, capi_major, i + 1, DEVFS_SPECIAL_CHR, 0));
		sprintf (devname, "isdn/capi20.1%i", i);
		devfs_unregister(devfs_find_handle(NULL, devname, 0, capi_major, i + 11, DEVFS_SPECIAL_CHR, 0));
	}
#endif
	(void) detach_capi_interface(&cuser);
	printk(KERN_NOTICE "capi: Rev%s: unloaded\n", rev);
}

#endif
