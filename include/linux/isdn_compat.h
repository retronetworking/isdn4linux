#ifdef __KERNEL__
/* Compatibility for various Linux kernel versions */

#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

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
#define COMPAT_HAS_2_2_PCI
#define get_pcibase(ps, nr) ps->base_address[nr]
#define pci_resource_start_io(pdev, nr) ((pdev)->base_address[nr] & PCI_BASE_ADDRESS_IO_MASK)
#define pci_resource_start_mem(pdev, nr) ((pdev)->base_address[nr] & PCI_BASE_ADDRESS_MEM_MASK)
#else
#define pci_resource_start_io(pdev, nr) pci_resource_start(pdev, nr)
#define pci_resource_start_mem(pdev, nr) pci_resource_start(pdev, nr)
#define get_pcibase(ps, nr) ps->resource[nr].start
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#define pci_get_sub_vendor(pdev, id)	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &id)
#define pci_get_sub_system(pdev, id)	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &id)
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
#else
#define __exit
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,12)
#define COMPAT_HAS_NEW_SETUP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,14)
#define net_device device
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,99)
#define i_count_read(ic) ic
#define i_count_inc(ic)  ic++
#else
#define i_count_read(ic) atomic_read(&ic)
#define i_count_inc(ic)  atomic_inc(&ic)
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,0)
#define COMPAT_HAS_FILEOP_OWNER
#define COMPAT_HAVE_NEW_FILLDIR
#else
#define COMPAT_USE_MODCOUNT_LOCK
#endif

#define COMPAT_PCI_COMMON_ID

#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */
