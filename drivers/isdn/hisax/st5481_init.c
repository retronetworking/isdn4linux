/* 
 *
 * Driver for ST5481 USB ISDN modem
 *
 * Author       Frode Isaksen (fisaksen@bewan.com)
 *
 *
 */

/* 
 * TODO:
 *
 * b layer1 delay?
 * d out fsm
 * hdlc as module
 * hotplug / unregister issues
 * mod_inc/dec_use_count
 * unify parts of d/b channel usb handling
 * file header
 * PH_PAUSE?
 * evt queue w/o arg?
 */

static const char *st5481_revision = "$Revision$";

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include "st5481.h"

MODULE_AUTHOR("Frode Isaksen <fisaksen@bewan.com>");
MODULE_DESCRIPTION("ST5481 USB ISDN modem");

static int protocol = 2;       /* EURO-ISDN Default */
MODULE_PARM(protocol, "i");

static int number_of_leds = 2;       /* 2 LEDs on the adpater default */
MODULE_PARM(number_of_leds, "i");

static LIST_HEAD(adapter_list);

/* ======================================================================
 * registration/deregistration with the USB layer
 */

/*
 * This function will be called when the adapter is plugged
 * into the USB bus.
 */
static void * __devinit probe_st5481(struct usb_device *dev,
				     unsigned int ifnum,
				     const struct usb_device_id *id)
{
	struct st5481_adapter *adapter;
	struct hisax_b_if *b_if[2];
	int retval, i;

	printk(KERN_INFO "st541: found adapter VendorId %04x, ProductId %04x, LEDs %d\n",
	     dev->descriptor.idVendor, dev->descriptor.idProduct,
	     number_of_leds);

	adapter = kmalloc(sizeof(struct st5481_adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	memset(adapter, 0, sizeof(struct st5481_adapter));

	adapter->number_of_leds = number_of_leds;
	adapter->usb_dev = dev;
	adapter->hisax_d_if.ifc.priv = adapter;
	adapter->hisax_d_if.ifc.l2l1 = st5481_d_l2l1;

	for (i = 0; i < 2; i++) {
		adapter->bcs[i].adapter = adapter;
		adapter->bcs[i].channel = i;
		adapter->bcs[i].b_if.ifc.priv = &adapter->bcs[i];
		adapter->bcs[i].b_if.ifc.l2l1 = st5481_b_l2l1;
	}
	list_add(&adapter->list, &adapter_list);

	retval = st5481_setup_usb(adapter);
	if (retval < 0)
		goto err;

	retval = st5481_setup_d(adapter);
	if (retval < 0)
		goto err_usb;

	retval = st5481_setup_b(&adapter->bcs[0]);
	if (retval < 0)
		goto err_d;

	retval = st5481_setup_b(&adapter->bcs[1]);
	if (retval < 0)
		goto err_b;

	for (i = 0; i < 2; i++)
		b_if[i] = &adapter->bcs[i].b_if;

	hisax_register(&adapter->hisax_d_if, b_if, "st5481_usb", protocol);
	st5481_start(adapter);

	return adapter;

 err_b:
	st5481_release_b(&adapter->bcs[0]);
 err_d:
	st5481_release_d(adapter);
 err_usb:
	st5481_release_usb(adapter);
 err:
	WARN("retval %d\n", retval);
	return NULL;
}

/*
 * This function will be called when the adapter is removed
 * from the USB bus.
 */
static void __devexit disconnect_st5481(struct usb_device *dev, void *arg)
{
	struct st5481_adapter *adapter = arg;

	DBG(1,"");

	list_del(&adapter->list);

	st5481_stop(adapter);
	st5481_release_b(&adapter->bcs[1]);
	st5481_release_b(&adapter->bcs[0]);
	st5481_release_d(adapter);
	// we would actually better wait for completion of outstanding urbs
	mdelay(2);
	st5481_release_usb(adapter);

	hisax_unregister(&adapter->hisax_d_if);

	kfree(adapter);
}

/*
 * The last 4 bits in the Product Id is set with 4 pins on the chip.
 */
static struct usb_device_id st5481_ids[] = {
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x0) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x1) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x2) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x3) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x4) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x5) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x6) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x7) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x8) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0x9) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0xA) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0xB) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0xC) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0xD) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0xE) },
	{ USB_DEVICE(ST_VENDOR_ID, ST5481_PRODUCT_ID+0xF) },
	{ }
};
MODULE_DEVICE_TABLE (usb, st5481_ids);

static struct usb_driver st5481_usb_driver = {
	name: "st5481_usb",
	probe: probe_st5481,
	disconnect: disconnect_st5481,
	id_table: st5481_ids,
};

static int __init st5481_usb_init(void)
{
	int retval;

	DBG(1,"");

	printk(KERN_INFO "st5481: ST5481 USB ISDN driver %s\n",
	       st5481_revision);

	retval = st5481_d_init();
	if (retval < 0)
		goto out;

	retval = usb_register(&st5481_usb_driver);
	if (retval < 0)
		goto out_d_exit;

	//	create_proc_read_entry("driver/st5481", 0, 0, proc_read_proc, NULL);
	return 0;

 out_d_exit:
	st5481_d_exit();
 out:
	return retval;
}

static void __exit st5481_usb_cleanup(void)
{
	DBG(1,"");

	usb_deregister(&st5481_usb_driver);
	//	remove_proc_entry("driver/st5481", NULL);
}

module_init(st5481_usb_init);
module_exit(st5481_usb_cleanup);
