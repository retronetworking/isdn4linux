#ifdef __KERNEL__
/* Compatibility for various Linux kernel versions */

#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
#include <linux/mm.h>

#define ioremap vremap
#define ioremap_nocache vremap
#define iounmap vfree

static inline unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
	int i;
	if ((i = verify_area(VERIFY_READ, from, n)) != 0)
		return i;
	memcpy_fromfs(to, from, n);
	return 0;
}

static inline unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
	int i;
	if ((i = verify_area(VERIFY_WRITE, to, n)) != 0)
		return i;
	memcpy_tofs(to, from, n);
	return 0;
}

#define GET_USER(x, addr) ( x = get_user(addr) )
#ifdef __alpha__ /* needed for 2.0.x with alpha-patches */
#define RWTYPE long
#define LSTYPE long
#define RWARG unsigned long
#else
#define RWTYPE int
#define LSTYPE int
#define RWARG int
#endif
#define LSARG off_t
#else
#define COMPAT_NEED_UACCESS
#define GET_USER get_user
#define PUT_USER put_user
#define RWTYPE long
#define LSTYPE long long
#define RWARG unsigned long
#define LSARG long long
#endif /* LINUX_VERSION_CODE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,15)
#define SET_SKB_FREE(x) ( x->free = 1 )
#define idev_kfree_skb(a,b) dev_kfree_skb(a,b)
#define idev_kfree_skb_irq(a,b) dev_kfree_skb(a,b)
#define idev_kfree_skb_any(a,b) dev_kfree_skb(a,b)
#else
#define SET_SKB_FREE(x)
#define idev_kfree_skb(a,b) dev_kfree_skb(a)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43)
#define idev_kfree_skb_irq(a,b) dev_kfree_skb(a)
#define idev_kfree_skb_any(a,b) dev_kfree_skb(a)
#else
#define idev_kfree_skb_irq(a,b) dev_kfree_skb_irq(a)
#define idev_kfree_skb_any(a,b) dev_kfree_skb_any(a)
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,18)
#define COMPAT_HAS_NEW_SYMTAB
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,31)
#define CLOSETYPE void
#define CLOSEVAL
#else
#define CLOSETYPE int
#define CLOSEVAL (0)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,37)
#define test_and_clear_bit clear_bit
#define test_and_set_bit set_bit
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,45)
#define MINOR_PART(f)	MINOR(f->f_inode->i_rdev)
#else
#define MINOR_PART(f)   MINOR(f->f_dentry->d_inode->i_rdev)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,81)
#define kstat_irqs( PAR ) kstat.interrupts[PAR]
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,89)
#define poll_wait(f,wq,w) poll_wait((wq),(w))
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,91)
#define COMPAT_HAS_NEW_PCI
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
#define get_pcibase(ps, nr) ps->base_address[nr]
#else
#define get_pcibase(ps, nr) ps->resource[nr].start
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,118)
#define FILEOP_HAS_FLUSH
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,127)
#define schedule_timeout(a) current->timeout = jiffies + (a); schedule ();
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,0)
#define COMPAT_HAS_NEW_WAITQ
#define BIG_PHONE_NUMBERS
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,12)
#define COMPAT_HAS_NEW_SETUP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,14)
#define net_device device
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,16)
#define set_current_state(sta) (current->state = sta)
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,22)
#define COMPAT_HAS_ISA_IOREMAP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,43)
#define COMPAT_NO_SOFTNET
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,47)
#define netif_running(d) test_bit(LINK_STATE_START, &d->state)
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,45)) && defined(CONFIG_DEVFS_FS)
#define HAVE_DEVFS_FS
#else
#define devfs_register_chrdev(m,n,f) register_chrdev(m,n,f)
#define devfs_unregister_chrdev(m,n) unregister_chrdev(m,n)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,99)
#define COMPAT_NEED_MPPP_DEFS
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
#define spin_lock_bh(lock)
#define spin_unlock_bh(lock)
#define COMPAT_NEED_SPIN_LOCK_BH
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */
