/* $Id$
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 *
 * $Log$
 *
 *
 */
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include "hisax.h"

/*
 * This structure array contains one entry per card. An entry looks
 * like this:
 * 
 * { type, protocol, p0, p1, p2, NULL }
 *
 * type
 *    1 Teles 16.0   	p0=irq p1=membase p2=iobase
 *    2 Teles  8.0   	p0=irq p1=membase
 *    3 Teles 16.3   	p0=irq p1=iobase
 *    4 Creatix PNP   	p0=irq p1=IO0 (ISAC)  p2=IO1 (HSCX)
 *    5 AVM A1 (Fritz)  p0=irq p1=iobase
 *    6 ELSA PCC16 	[p0=iobase] or nothing (autodetect)
 *                    
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6
 *
 *
 */
 
#define EMPTY_CARD	{0, 0, {0, 0, 0}, NULL}

struct IsdnCard cards[] =
{
	{ISDN_CTYPE_16_0, ISDN_PTYPE_EURO,{15,0xd0000,0xd80}, NULL},	/* example */
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
};

extern char	*HiSax_id;
extern char	*l1_revision;
extern int	HiSax_Installed;
static char *HiSax_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

int             nrcards;

typedef struct {
	int             typ;
	unsigned int    protocol;
	unsigned int    para[3];
} io_type;

#define EMPTY_IO_TYPE	{0, 0, {0, 0, 0}}

io_type         io[] =
{
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
	EMPTY_IO_TYPE,
};

void
HiSax_mod_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}

void
HiSax_mod_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

#ifdef MODULE
#define HiSax_init init_module
#else
void HiSax_setup(char *str, int *ints)
{
        int  i, j, argc;
	static char sid[20];

        argc = ints[0];
        i = 0;
        j = 1;
        while (argc && (i<16)) {
                if (argc) {
                        io[i].typ    = ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].protocol = ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].para[0]  = ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].para[1]  = ints[j];
                        j++; argc--;
                }
                if (argc) {
                        io[i].para[2]  = ints[j];
                        j++; argc--;
                }
                i++;
        }
	if (strlen(str)) {
		strcpy(sid,str);
		HiSax_id = sid;
	}
}
#endif

int
HiSax_init(void)
{
	int             i,j;
	char		tmp[256];

	nrcards = 0;

	printk(KERN_NOTICE "HiSax: Driver for Siemens Chipset ISDN cards\n");
	printk(KERN_NOTICE "HiSax: Revision (");
	strcpy(tmp,l1_revision);
	printk("%s)\n",HiSax_getrev(tmp));
	

	for (i = 0; i < 16; i++) {
		if ((io[i].typ>0) && (io[i].typ<=ISDN_CTYPE_COUNT)) {
			cards[i].typ       = io[i].typ;
			cards[i].protocol  = io[i].protocol;
			for (j=0;j<3;j++)
				cards[i].para[j] = io[i].para[j];
		}
	}
	for (i = 0; i < 16; i++)
                if (cards[i].typ>0)
                        nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
               nrcards, (nrcards > 1) ? "s" : "");
	if (HiSax_inithardware()) {
                /* Install only, if at least one card found */
                Isdnl2New();
                TeiNew();
                CallcNew();
                ll_init();

		/* No symbols to export, hide all symbols */
		register_symtab(NULL);

#ifdef MODULE
                printk(KERN_NOTICE "HiSax: module installed\n");
#endif
		HiSax_Installed = 1;
                return (0);
        } else
                return -EIO;
}

#ifdef MODULE
void
cleanup_module(void)
{

	ll_stop();
	TeiFree();
	Isdnl2Free();
	CallcFree();
	HiSax_closehardware();
	HiSax_Installed = 0;
	ll_unload();
	printk(KERN_NOTICE "HiSax module removed\n");

}
#endif
