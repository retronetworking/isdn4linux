/* $Id$ */
/*======================================================================

    A teles S0 PCMCIA client driver

    Based on skeleton by David Hinds, dhinds@allegro.stanford.edu
    Written by Christof Petig, christof.petig@wtal.de
    
    Also inspired by ELSA PCMCIA driver 
    by Klaus Lichtenwalder <Lichtenwalder@ACM.org>
    
    Extentions to new hisax_pcmcia by Karsten Keil

    minor changes to be compatible with kernel 2.4.x
    by Jan.Schubert@GMX.li

======================================================================*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/bus_ops.h>

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/
#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args);
static char *version =
"teles_cs.c 2.10 2002/07/30 22:23:34 kkeil";
#else
#define DEBUG(n, args...)
#endif

MODULE_LICENSE("GPL");

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* Bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 6, 5, 4, and 3 */

static u_long irq_mask = 0xdeb8;
MODULE_PARM(irq_mask, "i");

static int protocol = 2;	/* EURO-ISDN Default */
MODULE_PARM(protocol, "i");

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card insertion
   and ejection events.  They are invoked from the teles event
   handler.
*/

static void teles_config(dev_link_t *link);
static void teles_release(u_long arg);
static int teles_event(event_t event, int priority,
			  event_callback_args_t *args);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static dev_link_t *teles_attach(void);
static void teles_detach(dev_link_t *);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'pcmem_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/

static dev_info_t dev_info = "teles_cs";

/*
   A linked list of "instances" of the teles device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

static dev_link_t *dev_list = NULL;

/*
   A dev_link_t structure has fields for most things that are needed
   to keep track of a socket, but there will usually be some device
   specific information that also needs to be kept track of.  The
   'priv' pointer in a dev_link_t structure can be used to point to
   a device-specific private data structure, like this.

   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a dev_link_t
   structure.  We allocate them in the card's private data structure,
   because they generally can't be allocated dynamically.
*/
   
typedef struct local_info_t {
    dev_node_t	node;
    int		busy;
    int		id;
} local_info_t;

/*====================================================================*/

static void cs_error(client_handle_t handle, int func, int ret)
{
    error_info_t err = { func, ret };
    CardServices(ReportError, handle, &err);
}

/*======================================================================

    teles_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.
    
======================================================================*/

static dev_link_t *teles_attach(void)
{
    client_reg_t client_reg;
    dev_link_t *link;
    local_info_t *local;
    int ret;

    DEBUG(0, "teles_attach()\n");    

    /* Initialize the dev_link_t structure */
    link = kmalloc(sizeof(struct dev_link_t), GFP_KERNEL);
    memset(link, 0, sizeof(struct dev_link_t));
    link->release.function = &teles_release;
    link->release.data = (u_long)link;

    /* The io structure describes IO port mapping */
    link->io.NumPorts1 = 96;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 5;

    /* Interrupt setup */
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_INFO2_VALID|IRQ_LEVEL_ID;
    link->irq.IRQInfo2 = irq_mask;
    
    /* General socket configuration */
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.Vcc = 50;
    link->conf.Vpp1 = link->conf.Vpp2 = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;
    link->conf.ConfigIndex = 1;
    link->conf.Present = PRESENT_OPTION;

    /* Allocate space for private device-specific data */
    local = kmalloc(sizeof(local_info_t), GFP_KERNEL);
    memset(local, 0, sizeof(local_info_t));
    link->priv = local;
    
    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.Attributes = INFO_IO_CLIENT;
    client_reg.EventMask =
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &teles_event;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = CardServices(RegisterClient, &link->handle, &client_reg);
    if (ret != 0) {
	cs_error(link->handle, RegisterClient, ret);
	teles_detach(link);
	return NULL;
    }

    return link;
} /* teles_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void teles_detach(dev_link_t *link)
{
    dev_link_t **linkp;

    DEBUG(0,"teles_detach(0x%p)\n", link);
    
    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    /*
       If the device is currently configured and active, we won't
       actually delete it yet.  Instead, it is marked so that when
       the release() function is called, that will trigger a proper
       detach().
    */
    if (link->state & DEV_CONFIG) {
#ifdef PCMCIA_DEBUG
	printk(KERN_DEBUG "teles_cs: detach postponed, '%s' "
	       "still locked\n", link->dev->dev_name);
#endif
	link->state |= DEV_STALE_LINK;
	return;
    }

    /* Break the link with Card Services */
    if (link->handle)
	CardServices(DeregisterClient, link->handle);
    
    /* Unlink device structure, free pieces */
    *linkp = link->next;
    if (link->priv) {
	kfree(link->priv);
    }
    kfree(link);
    
} /* teles_detach */

/*======================================================================

    teles_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.
    
======================================================================*/

#define CS_CHECK(fn, args...) \
while ((last_ret=CardServices(last_fn=(fn),args))!=0) goto cs_failed

struct IsdnCard {
	int typ;
	int protocol;
	unsigned int para[4];
	void *cs;
};
extern int hisax_init_pcmcia(void *, int *, struct IsdnCard *);
extern void HiSax_closecard(int);

static void teles_config(dev_link_t *link)
{
    client_handle_t handle;
    tuple_t tuple;
    cisparse_t parse;
    local_info_t *dev;
    int i=0,j,last_ret,last_fn;
    u_char buf[255];
    struct IsdnCard cs = {8,2,{0,0,0,0}, NULL};
    handle = link->handle;
    dev = link->priv;

    DEBUG(0, "teles_config(0x%p)\n", link);

    /*
       This reads the card's CONFIG tuple to find its configuration
       registers.
    */
    tuple.DesiredTuple = CISTPL_CONFIG;
    CS_CHECK(GetFirstTuple, handle, &tuple);
    tuple.TupleData = buf;
    tuple.TupleDataMax = sizeof buf;
    tuple.TupleOffset = 0;
    CS_CHECK(GetTupleData, handle, &tuple);
    CS_CHECK(ParseTuple, handle, &tuple, &parse);
    link->conf.ConfigBase = parse.config.base;
    /* link->conf.Present= parse.config.rmask[0]; */
    
/* card accepts the following codes
   CISTPL_DEVICE CISTPL_NO_LINK CISTPL_VERS_1 CISTPL_CONFIG 
   CISTPL_CFTABLE_ENTRY CISTPL_FUNCID CISTPL_END */

    /* Configure card */
    link->state |= DEV_CONFIG;

    /*
       Try allocating IO ports.  This tries a few fixed addresses.
       If you want, you can also read the card's config table to
       pick addresses -- see the serial driver for an example.
    */
    for (j = 0; j < 0x400; j += 0x80) {
        /* TODO: Check for presence of card! */
	/* The '^0x300' is so that we probe 0x300-0x3ff first, then
	   0x200-0x2ff, and so on, because this seems safer */
	link->io.BasePort1 = j^0x300;
	i = CardServices(RequestIO, link->handle, &link->io);
	if (i == CS_SUCCESS) break;
	}
    if (i != CS_SUCCESS) {
	cs_error(link->handle, RequestIO, i);
	goto failed;
    }
    
    /*
       Now allocate an interrupt line.  Note that this does not
       actually assign a handler to the interrupt.
    */
    CS_CHECK(RequestIRQ, link->handle, &link->irq);
	
    /*
       This actually configures the PCMCIA socket -- setting up
       the I/O windows and the interrupt mapping.
    */
    CS_CHECK(RequestConfiguration, link->handle, &link->conf);

    outb(0x41,link->io.BasePort1+0x18); /* activate card & irq */

    /* At this point, the dev_node_t structure(s) should be
       initialized and arranged in a linked list at link->dev. */
    sprintf(dev->node.dev_name, "teles0");
    dev->node.major = 45;
    dev->node.minor = 0;
    link->dev = &dev->node;
    
    link->state &= ~DEV_CONFIG_PENDING;
    printk(KERN_DEBUG "teles device loaded\n");
    release_region(link->io.BasePort1,96);  /* will be reallocated by hisax */
    cs.protocol=protocol;
    cs.para[0]=link->irq.AssignedIRQ;
    cs.para[1]=link->io.BasePort1;
    dev->id=hisax_init_pcmcia(link,&dev->busy,&cs);
    if (dev->id <0)
    	goto failed;
    return;

cs_failed:
    cs_error(link->handle, last_fn, last_ret);
failed:
    teles_release((u_long)link);

} /* teles_config */

/*======================================================================

    After a card is removed, teles_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

static void teles_release(u_long arg)
{
    dev_link_t *link = (dev_link_t *)arg;
    local_info_t *local = link->priv;

    DEBUG(0, "teles_release(0x%p)\n", link);

    /*
       If the device is currently in use, we won't release until it
       is actually closed.
    */
    if (local->id >= 0) {
    	HiSax_closecard(local->id);
    	local->id = -1;
    }
    if (link->open) {
    	DEBUG(1, "teles_cs: release postponed, '%s' still open\n",
	      link->dev->dev_name);
	link->state |= DEV_STALE_CONFIG;
	return;
    }

    /* Unlink the device chain */
    link->dev = NULL;
    
    /* Don't bother checking to see if these succeed or not */
    CardServices(ReleaseWindow, link->win);
    CardServices(ReleaseConfiguration, link->handle);
    CardServices(ReleaseIO, link->handle, &link->io);
    CardServices(ReleaseIRQ, link->handle, &link->irq);
    link->state &= ~DEV_CONFIG;
    
    if (link->state & DEV_STALE_LINK)
	teles_detach(link);
    
} /* teles_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.  A CARD_REMOVAL event
    also sets some flags to discourage the net drivers from trying
    to talk to the card any more.

    When a CARD_REMOVAL event is received, we immediately set a flag
    to block future accesses to this device.  All the functions that
    actually access the device should check this flag to make sure
    the card is still present.
    
======================================================================*/

static int teles_event(event_t event, int priority,
			  event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;

    DEBUG(1, "teles_event(0x%06x)\n", event);
    
    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	((local_info_t*)link->priv)->busy = 1;
	if (link->state & DEV_CONFIG) {
	    mod_timer(&link->release, jiffies + HZ/20);
	}
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	teles_config(link);
	break;
    case CS_EVENT_PM_SUSPEND:
	link->state |= DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
	if (link->state & DEV_CONFIG)
	    CardServices(ReleaseConfiguration, link->handle);
	break;
    case CS_EVENT_PM_RESUME:
	link->state &= ~DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_CARD_RESET:
	if (link->state & DEV_CONFIG)
	    CardServices(RequestConfiguration, link->handle, &link->conf);
	outb(0x41,link->io.BasePort1+0x18); /* re-activate card & irq ??? */
	break;
    }
    return 0;
} /* teles_event */

/*====================================================================*/

int init_module(void)
{
    servinfo_t serv;
    DEBUG(0, "%s\n", version);
    CardServices(GetCardServicesInfo, &serv);
    if (serv.Revision != CS_RELEASE_CODE) {
	printk(KERN_NOTICE "teles: Card Services release "
	       "does not match!\n");
	return -1;
    }
    register_pcmcia_driver(&dev_info, &teles_attach, &teles_detach);
    return 0;
}

void cleanup_module(void)
{
    DEBUG(0, "teles_cs: unloading\n");
    unregister_pcmcia_driver(&dev_info);
    while (dev_list != NULL) {
	if (dev_list->state & DEV_CONFIG)
	    teles_release((u_long)dev_list);
	teles_detach(dev_list);
    }
}