/* 
 *
 * HiSax ISDN driver - usb specific routines for ST5481 USB ISDN modem
 *
 * Author       Frode Isaksen (fisaksen@bewan.com)
 *
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/isdn_compat.h>
#include "hisax.h"
#include "st5481.h"

MODULE_AUTHOR("Frode Isaksen <fisaksen@bewan.com>");
MODULE_DESCRIPTION("ST5481 USB ISDN modem");

/* Parameters that can be set with 'insmod' */
static int protocol=2;       /* EURO-ISDN Default */
MODULE_PARM(protocol, "i");

static int number_of_leds=2;       /* 2 LEDs on the adpater default */
MODULE_PARM(number_of_leds, "i");

void HiSax_closecard(int cardnr);
int st5481_init_usb(struct usb_device *dev, int type, int prot, int *cardnr);

#define MAX_ADAPTERS 4   /* Each adapter needs about 1.3 Mbs of isoc BW */

struct st5481_usb_adapter {
	int active;
	int protocol;
	int number_of_leds;
	int cardnr;
};
static struct st5481_usb_adapter usb_adapter_instances[MAX_ADAPTERS];
	
static struct st5481_usb_adapter *
get_usb_adapter(void)
{
	struct st5481_usb_adapter *usb_adapter;

	for (usb_adapter = usb_adapter_instances; 
	     usb_adapter < &usb_adapter_instances[MAX_ADAPTERS];
	     usb_adapter++) {
		if (!test_and_set_bit(0, &usb_adapter->active)) {
			//MOD_INC_USE_COUNT;     			
			usb_adapter->protocol = protocol;
			usb_adapter->number_of_leds = number_of_leds;
			usb_adapter->cardnr = -1;
			return usb_adapter;
		}
	}
	return NULL;
}

static void
free_usb_adapter(struct st5481_usb_adapter *usb_adapter)
{
	if (usb_adapter) {
		if (test_and_clear_bit(0, &usb_adapter->active)) {
			printk( KERN_DEBUG __FUNCTION__ ": cardnr %d active\n",
				usb_adapter->cardnr);
			if (usb_adapter->cardnr != -1) {
				HiSax_closecard(usb_adapter->cardnr);			
			}
			//MOD_DEC_USE_COUNT;     			
		}
	}
}

/*
  This function will be called when the adapter is plugged
  into the USB bus.
  Call init_usb_st5481 to tell HiSax that USB device is
  ready.
*/
static void *
probe_st5481(struct usb_device *dev, unsigned int ifnum
#ifdef COMPAT_HAS_USB_IDTAB
	     ,const struct usb_device_id *id
#endif
     )
{
	struct st5481_usb_adapter *usb_adapter;

	printk( KERN_INFO __FILE__ ": "__FUNCTION__ ": VendorId %04x,ProductId %04x\n",
		dev->descriptor.idVendor,dev->descriptor.idProduct);

	if (dev->descriptor.idVendor != ST_VENDOR_ID) {
		return NULL;
	}

	if ((dev->descriptor.idProduct & ST5481_PRODUCT_ID_MASK) != 
	    ST5481_PRODUCT_ID) {
		return NULL;
	}

	usb_adapter = get_usb_adapter();
	if (!usb_adapter) {
		printk( KERN_WARNING __FILE__ ": " __FUNCTION__ ": too many adapters\n");
		return NULL;
	}

	if (st5481_init_usb(dev, ISDN_CTYPE_ST5481, usb_adapter->protocol,
			    &usb_adapter->cardnr) < 0) {
		printk( KERN_WARNING __FILE__ ": " __FUNCTION__ ": st5481_init_usb failed\n");
		free_usb_adapter(usb_adapter);
		return NULL;
	}
	return usb_adapter;
}

/*
  This function will be called when the adapter is removed
  from the USB bus.
  Call HiSax_closecard via free_sub_adapter to tell HiSax that USB 
  device has been removed.
*/
static void 
disconnect_st5481(struct usb_device *dev,void *arg)
{
	struct st5481_usb_adapter *usb_adapter = arg;

	printk( KERN_DEBUG __FUNCTION__ ": disconnect driver\n");
	
	free_usb_adapter(usb_adapter);
}

#ifdef COMPAT_HAS_USB_IDTAB
/*
  The last 4 bits in the Product Id is set with 4 pins on the chip.
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
#endif

static struct
usb_driver st5481_usb_driver = {
	name: "st5481_usb",
	probe: probe_st5481,
	disconnect: disconnect_st5481,
#ifdef COMPAT_HAS_USB_IDTAB
	id_table: st5481_ids,
#endif
};

static int __init
st5481_usb_init(void)
{
 	printk( KERN_INFO __FILE__ ": " __FUNCTION__ ": register driver\n");

	memset(usb_adapter_instances, 0, sizeof(usb_adapter_instances));

	if (usb_register(&st5481_usb_driver) < 0) {
		printk( KERN_WARNING __FILE__ ": " __FUNCTION__ ": usb_register failed\n");
		return -1;
	}

	return 0;
}

static void __exit
st5481_usb_cleanup(void)
{
	struct st5481_usb_adapter *usb_adapter;

	for (usb_adapter = usb_adapter_instances; 
	     usb_adapter < &usb_adapter_instances[MAX_ADAPTERS];
	     usb_adapter++) {
		
		free_usb_adapter(usb_adapter);
	}
	usb_deregister(&st5481_usb_driver);
}

module_init(st5481_usb_init);
module_exit(st5481_usb_cleanup);

