#include "capi.h"
#include "capiminor.h"
#include <linux/init.h>
#include <linux/module.h>

/* -------- driver information -------------------------------------- */

extern int capi_ttymajor;
extern int capi_rawmajor;

MODULE_PARM(capi_rawmajor, "i");
MODULE_PARM(capi_ttymajor, "i");

/* -------- global variables ---------------------------------------- */

static spinlock_t minor_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(minor_list);

static kmem_cache_t *capiminor_cachep = 0;

/* -------- handle data queue --------------------------------------- */

int
handle_recv_skb(struct capiminor *mp, struct sk_buff *skb)
{
	struct sk_buff *nskb;

	if (mp->tty) {
		if (mp->tty->ldisc.receive_buf == 0) {
			printk(KERN_ERR "capi: ldisc has no receive_buf function\n");
			return -1;
		}
		if (mp->ttyinstop) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
			printk(KERN_DEBUG "capi: recv tty throttled\n");
#endif
			return -1;
		}
		if (mp->tty->ldisc.receive_room &&
		    mp->tty->ldisc.receive_room(mp->tty) < skb->len) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
			printk(KERN_DEBUG "capi: no room in tty\n");
#endif
			return -1;
		}
		mp->tty->ldisc.receive_buf(mp->tty, skb->data, 0, skb->len);
#ifdef _DEBUG_DATAFLOW
		printk(KERN_DEBUG "capi: DATA_B3_RESP %u len=%d => ldisc\n",
					datahandle, skb->len);
#endif
		capincci_recv_ack(&mp->ncci, skb);
		return 0;

#ifdef CAPI_PPP_ON_RAW_DEVICE
	} else if (mp->chan_connected) {
		ppp_input(&mp->chan, skb);
		return 0;
#endif
	} else if (mp->file) {
		if (skb_queue_len(&mp->sdev.read_queue) > CAPINC_MAX_RECVQUEUE) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
			printk(KERN_DEBUG "capi: no room in raw queue\n");
#endif
			return -1;
		}
		// FIXME it would be better to ack the frame when it
		// acutally gets read from the device queue, would
		// also save the copy

		nskb = skb_copy(skb, GFP_ATOMIC);
		if (!nskb)
			return -1;

		syncdev_r_queue_tail(&mp->sdev, nskb);

		capincci_recv_ack(&mp->ncci, skb);
		return 0;
	}
#ifdef _DEBUG_DATAFLOW
	printk(KERN_DEBUG "capi: currently no receiver\n");
#endif
	return -1;
}

void handle_minor_recv(struct capiminor *mp)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&mp->inqueue)) != 0) {
		unsigned int len = skb->len;
		mp->inbytes -= len;
		if (handle_recv_skb(mp, skb) < 0) {
			skb_queue_head(&mp->inqueue, skb);
			mp->inbytes += len;
			return;
		}
	}
}

int handle_minor_send(struct capiminor *mp)
{
	struct sk_buff *skb;
	int count = 0;
	int retval;
	int len;
	
	if (mp->tty && mp->ttyoutstop) {
#if defined(_DEBUG_DATAFLOW) || defined(_DEBUG_TTYFUNCS)
		printk(KERN_DEBUG "capi: send: tty stopped\n");
#endif
		return 0;
	}

	while ((skb = skb_dequeue(&mp->outqueue)) != 0) {
		len = skb->len;
		retval = capincci_write(&mp->ncci, skb);
		if (retval == -EAGAIN) {
			// queue full
			skb_queue_head(&mp->outqueue, skb);
			break;
		} else if (retval < 0) {
			mp->outbytes -= len;
			kfree_skb(skb);
			continue;
		}
		count++;
		mp->outbytes -= len;
#ifdef _DEBUG_DATAFLOW
		printk(KERN_DEBUG "capi: DATA_B3_REQ %u len=%u\n",
		       datahandle, len);
#endif
	}
	if (count)
		wake_up_interruptible(&mp->sendwait);
	return count;
}

/* -------- struct capiminor ---------------------------------------- */

struct capiminor *capiminor_alloc(u16 applid, u32 ncci)
{
	struct capiminor *mp, *pp;
	struct list_head *p;
        unsigned int minor = 0;
#if defined(CONFIG_ISDN_KCAPI_CAPIFS) || defined(CONFIG_ISDN_KCAPI_CAPIFS_MODULE)
	kdev_t kdev;
#endif

	MOD_INC_USE_COUNT;
	mp = (struct capiminor *)kmem_cache_alloc(capiminor_cachep, GFP_ATOMIC);
	if (!mp) {
		MOD_DEC_USE_COUNT;
		printk(KERN_ERR "capi: can't alloc capiminor\n");
		return 0;
	}
#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capiminor_alloc %d\n", GET_USE_COUNT(THIS_MODULE));
#endif
	memset(mp, 0, sizeof(struct capiminor));
	atomic_set(&mp->ttyopencount,0);

	skb_queue_head_init(&mp->inqueue);
	skb_queue_head_init(&mp->outqueue);

	syncdev_r_ctor(&mp->sdev);
	init_waitqueue_head(&mp->sendwait);

        spin_lock(&minor_list_lock);
	list_for_each(p, &minor_list) {
		pp = list_entry(p, struct capiminor, list);
		if (pp->minor < minor)
			continue;
		if (pp->minor > minor)
			break;
		minor++;
	}
	list_add_tail(&mp->list, &minor_list);
        spin_unlock(&minor_list_lock);

#if defined(CONFIG_ISDN_KCAPI_CAPIFS) || defined(CONFIG_ISDN_KCAPI_CAPIFS_MODULE)
	kdev = MKDEV(capi_rawmajor, mp->minor);
	capifs_new_ncci('r', mp->minor, kdev);
	kdev = MKDEV(capi_ttymajor, mp->minor);
	capifs_new_ncci(0, mp->minor, kdev);
#endif
	return mp;
}

void capiminor_free(struct capiminor *mp)
{
	struct capincci *np = &mp->ncci;

#if defined(CONFIG_ISDN_KCAPI_CAPIFS) || defined(CONFIG_ISDN_KCAPI_CAPIFS_MODULE)
	capifs_free_ncci('r', mp->minor);
	capifs_free_ncci(0, mp->minor);
#endif
        spin_lock(&minor_list_lock);
	list_del(&mp->list);
        spin_unlock(&minor_list_lock);

	if (mp->ttyskb)
		kfree_skb(mp->ttyskb);

	mp->ttyskb = 0;
	syncdev_r_dtor(&mp->sdev);
	skb_queue_purge(&mp->inqueue);
	skb_queue_purge(&mp->outqueue);
	np->dtor = NULL; // FIXME...
	capincci_unhijack(np);

	kmem_cache_free(capiminor_cachep, mp);
	MOD_DEC_USE_COUNT;
#ifdef _DEBUG_REFCOUNT
	printk(KERN_DEBUG "capiminor_free %d\n", GET_USE_COUNT(THIS_MODULE));
#endif

}

struct capiminor *
capiminor_find(unsigned int minor)
{
	struct list_head *p;
	struct capiminor *mp = NULL;

        spin_lock(&minor_list_lock);
	list_for_each(p, &minor_list) {
		mp = list_entry(p, struct capiminor, list);
		if (mp->minor == minor)
			break;
	}
        spin_unlock(&minor_list_lock);
	if (p == &minor_list)
		return 0;

	return mp;
}

static void
capiminor_dtor(struct capincci *np)
{
	struct capiminor *mp = np->priv;

	if (mp->tty) {
//		mp->nccip = 0;
		tty_hangup(mp->tty);
	} else if (mp->file) {
//		mp->nccip = 0;
		wake_up_interruptible(&mp->sdev.read_wait);
		wake_up_interruptible(&mp->sendwait);
	} else {
		capiminor_free(mp);
	}

}

static int
capiminor_recv(struct capincci *np, struct sk_buff *skb)
{
	struct capiminor *mp = np->priv;

	skb_queue_tail(&mp->inqueue, skb);
	mp->inbytes += skb->len;
	handle_minor_recv(mp);
	
	return 0;
}

static void
capiminor_recv_wake_queue(struct capincci *np)
{
	struct capiminor *mp = np->priv;

#ifdef CAPI_PPP_ON_RAW_DEVICE
	if (mp->chan_connected) {
		ppp_output_wakeup(&mp->chan);
		return;
	}
#endif
	if (mp->tty) {
		if (mp->tty->ldisc.write_wakeup)
			mp->tty->ldisc.write_wakeup(mp->tty);
	} else {
		wake_up_interruptible(&mp->sendwait);
	}
	handle_minor_send(mp);
}

void
capiminor_ncciup(struct capidev *cdev, u32 ncci)
{
	struct capiminor *mp;
	struct capincci *np;

	mp = capiminor_alloc(cdev->applid, ncci);
	if (!mp)
		return;

	np = &mp->ncci;
	np->priv = mp;
	np->ncci = ncci;
	np->recv = capiminor_recv;
	np->recv_wake_queue = capiminor_recv_wake_queue;
	np->recv_stop_queue = NULL;
	np->dtor = capiminor_dtor;
	capincci_hijack(cdev, np);
}

int
capiminor_ncci_getunit(struct capidev *cdev, struct capincci *np)
{
	struct capiminor *mp;

	if (np->recv != capiminor_recv)
		return -EINVAL;

	mp = np->priv;
	if (!mp)
		return -ESRCH;
	return mp->minor;
}

/* -------- init / exit module -------------------------------------- */

int __init capiminor_init(void)
{
	int retval;

	retval = -ENOMEM;
	capiminor_cachep = kmem_cache_create("capi20_minor",
					 sizeof(struct capiminor),
					 0,
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!capiminor_cachep)
		goto out;

	retval = capiraw_init();
	if (retval)
		goto outf_capiminor;
	
	retval = capinc_tty_init();
	if (retval)
		goto outf_capiraw;
	
	capiminor_ncciup_p = &capiminor_ncciup;
	capiminor_ncci_getunit_p = &capiminor_ncci_getunit;
	retval = 0;
	goto out;

 outf_capiraw:
	capiraw_exit();
 outf_capiminor:
	kmem_cache_destroy(capiminor_cachep);
 out:
	return retval;
}

void __exit capiminor_exit(void)
{
	capiminor_ncciup_p = NULL;
	capiminor_ncci_getunit_p = NULL;
	capinc_tty_exit();
	capiraw_exit();
	kmem_cache_destroy(capiminor_cachep);
}

module_init(capiminor_init);
module_exit(capiminor_exit);
