
/* $Id$
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * detlef@abcbtx.de
 *
 * $Log$
 */

#include <linux/config.h>

#ifdef CONFIG_ISDN_WITH_ABC
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include <linux/udp.h>
#include "isdn_common.h"
#include "isdn_net.h"


#define TABGR  3933		/* tabellen-groesse , bitte nicht aendern,
						** die abcrouter sind auf diese groesse abgestimmt
						*/


#define EMPF_TABGR	1800

static short pack_inuse = 0;
static short pack_mem_isvmalloc = 0;
static void *pack_mem = NULL;


#define C_NEXTBIT	256		/*	mehr ist nicht notwendig */
#define C_FIRSTE	257		


#define outbuf(z) out_buf((z),&dpoin,edpoin,&t_breite,&dbits)
static __inline int out_buf(register int z,
							u_char **dpoin,
							u_char *edpoin,
							short *t_breite,
							u_short *dbits)
{
	register int noch = *t_breite;

	while(noch > 0 && *dpoin < edpoin) {

		z &= (1 << noch ) - 1;

		if(!*dbits) {

			if(noch >= 8) {
				
				**dpoin = z >> (noch - 8);
				noch -= 8;
				(*dpoin)++;
			
			} else {
			
				**dpoin = z << (8 - noch);
				*dbits = noch;
				noch = 0;
			}

		} else {

			int step = 8 - *dbits;

			if(noch >= step) {

				**dpoin |= z >> (noch - step);
				noch -= step;
				*dbits = 0;
				(*dpoin)++;

			} else {

				**dpoin |= z << (step - noch);
				*dbits += noch;
				noch = 0;
			}
		}
	}

	if(*dpoin >= edpoin)
		return(1);

	return(0);
}


int abcgmbh_pack(u_char *src,u_char *dstpoin,int bytes)
/***************************************************************************

	compress von pointer src nach pointer dstpoin fuer bytes bytes
	der membereich ueber *dstpoin muss mindestens bytes bytes gross sein

	sollte die kompremierung  die orignal-groessee  erreichen,
	wird sofort abgebrochen.

	diese kompremierung ist fuer maximal 1500 bytes ausgelegt 
	( ethernet MTU )

	ACHTUNG: keine streamdaten !!! nur pakete.
	dies liegt daran, dass bei jedem call von hof_pack die internen
	tabellen wieder neu aufgebaut werden.

	das ist zwar nicht die absolute toploesung, aber ich kann
	dadurch xbeliebig viele logische verbindungen mit einem doch
	noch vernuenftigen memory-verbrauch compressen.


	returns:
		> 0 	==	anzahl bytes  an *dstpoin
					(kann maximal parameter bytes erreichen)

		< 1		==	abbruch weil kompremierung mehr overhead verursacht als
					die eigentlichen daten ausmachen


	ACHTUNG: der datenbereich  *dstpoin wird auf jedenfall ueberschrieben

******************************************************************************/
	
{
	register int prev_code;

	short t_breite;
	int  t_counter;
	u_short  dbits = 0;

	u_char *dpoin;
	u_char *edpoin;
	u_char *srcpoin;
	u_char *esrcpoin;

	u_char 	*t_inuse;
	short 	*t_code_wert;
	short 	*t_eltern_code;
	u_char 	*t_zeichen;
	void 	*tp;

	u_long flags;

	if(bytes < 5 || bytes > 1550)
		return(0);

	save_flags(flags);
	cli();

	if(pack_inuse) {

		restore_flags(flags);
		return(0);
	}

	pack_inuse = 1;
	restore_flags(flags);

	if(pack_mem == NULL) {

		flags = sizeof(u_char) * (TABGR + ((TABGR + 7) / 8));
		flags += sizeof(short) * TABGR ;
		flags += sizeof(short) * TABGR ;

		pack_mem = kmalloc(flags,GFP_ATOMIC);

		if(pack_mem  == NULL) {

			printk(KERN_DEBUG "abc-pack: no memory for %ld bytes\n",flags);
			pack_inuse = 0;
			return(0);
		}

		printk(KERN_DEBUG "abc-pack: get memory for %ld bytes\n",flags);
	}

/****************************************
	u_char 	t_inuse[(TABGR + 7) / 8];
	short 	t_code_wert[TABGR];
	short 	t_eltern_code[TABGR];
	u_char 	t_zeichen[TABGR];
****************************************/

	tp = pack_mem;
	t_inuse = (u_char *)tp;
	tp += sizeof(u_char ) * ((TABGR + 7) / 8);
	t_code_wert = (short *)tp;
	tp += sizeof(short) * TABGR;
	t_eltern_code = (short *)tp;
	tp += sizeof(short) * TABGR;
	t_zeichen = (u_char *)tp;

	t_breite = 9;
	t_counter = C_FIRSTE;
	dbits = 0;

	memset(t_inuse,0,(TABGR + 7 ) / 8);
	srcpoin = src;
	esrcpoin = src + bytes;
	dpoin = dstpoin;
	edpoin = dstpoin + bytes;
	prev_code = *(srcpoin++);

	while(srcpoin < esrcpoin) {

		int ncode = *(srcpoin++);

		register unsigned int index = ((ncode << 7) ^ prev_code) % TABGR;
		register unsigned int findex;
		int by;
		int bi;

		findex = index;

		for(;;) {

			by = index >> 3;
			bi = index & 7;

			if(!(t_inuse[by] & (1 << bi))) 
				break;

			if(t_eltern_code[index] == prev_code && 
				t_zeichen[index] == ncode) {

				ncode = t_code_wert[index];
				goto ende_while;
			}

			if((index = (index + 1 ) % TABGR) == findex) {

				printk(KERN_WARNING
					"abccompress DEADLOOP in t_suche detected\n");

				pack_inuse = 0;
				return(0);
			}
		} 

		t_eltern_code[index] = prev_code;
		t_zeichen[index] = ncode;
		t_code_wert[index] = t_counter++;
		t_inuse[by] |= 1 << bi;

		if(outbuf(prev_code)) {

			pack_inuse = 0;
			return(0);
		}
	 
		if(t_counter >= (1 << t_breite)) {

			if(outbuf(C_NEXTBIT)) {

				pack_inuse = 0;
				return(0);
			}

			t_breite++;
		}

ende_while:;

		prev_code = ncode;
	}

	if(outbuf(prev_code)) {

		pack_inuse = 0;
		return(0);
	}

	flags = (dpoin - dstpoin) + !!dbits;
	pack_inuse = 0;
	return(flags);
}



static __inline int get_a_word(short *t_breite,
								u_char **srcpoin,
								u_char *esrcpoin,
								u_short *dbits)
{

	register int w;
	register int noch ;

once_more:;
	noch = *t_breite;
	w = 0;

	while(noch > 0 && *srcpoin < esrcpoin) {

		int step = 8 - *dbits;
		int rstep = 0;

		if(noch < step) {

			rstep = step - noch;
			step = noch;
		}

		w <<= step;
		(uint)w |= (**srcpoin >> rstep) & (( 1 << step) - 1);
		noch -= step;

		if((*dbits += step) >= 8) {

			*dbits = 0;
			(*srcpoin)++;
		}
	}

	if(noch > 0)
		return(-1);

	if(w == C_NEXTBIT) {

		(*t_breite)++;
		goto once_more;
	}

	return(w);
}


static volatile short meminuse 		= 0;
static volatile short memisvmalloc 	= 0;
static void *depackmem;


int abcgmbh_freepack_mem(void)
{

	ulong flags;

	save_flags(flags);
	cli();

	if(meminuse || pack_inuse) {

		restore_flags(flags);

		printk(KERN_DEBUG "abc-freepakcmem: memory inuse %d %d\n",
			meminuse, pack_inuse);

		return(-1);
	}

	meminuse = 1;
	pack_inuse = 1;
	restore_flags(flags);

	if(depackmem != NULL) {

		if(memisvmalloc)
			vfree((void *)depackmem);
		else
			kfree((void *)depackmem);
	}
	

	depackmem = NULL;
	memisvmalloc = 0;

	if(pack_mem != NULL) {

		if(pack_mem_isvmalloc)
			vfree(pack_mem);
		else
			kfree(pack_mem);
	}

	pack_mem_isvmalloc = 0;
	pack_mem = NULL;
	meminuse = 0;
	pack_inuse = 0;

	return(0);
}
	

int abcgmbh_getpack_mem(void)
{
	ulong flags;

	save_flags(flags);
	cli();

	if(meminuse) {

		restore_flags(flags);
		return(-1);
	}

	meminuse = 1;
	restore_flags(flags);

	if(depackmem == NULL) {

		depackmem = vmalloc(sizeof(short) * (EMPF_TABGR << 1) + 800);
		memisvmalloc = depackmem != NULL;
	}

	if(pack_mem == NULL) {

		int l;

		l = sizeof(u_char) * (TABGR + ((TABGR + 7) / 8));
		l += sizeof(short) * TABGR ;
		l += sizeof(short) * TABGR ;

		pack_mem = vmalloc(l);
		pack_mem_isvmalloc = pack_mem != NULL;
	}

	meminuse = 0;
	return(0);
}

	
			
int abcgmbh_depack(u_char *msrcpoin,int srcbytes,u_char *dstpoin,int bytes)

/***************************************************************************


	decompress der mit abcgmbh_pack kompremierten daten

	parameter:

		msrcpopin		==	pointer auf sourcedaten daten (1 paket)
		srcbytes		==	anzahl bytes im sourcebuffer
		dstpoin			==	pointer auf den zielbuffer 
		bytes			==	groesse des zielbuffers in bytes


	return 	>= 0		==	anzahl abgelegter bytes im zielbuffer

	return < 0			== no unpack memory




*****************************************************************************/
{
	register int	last_code;
	register int now_code;
	int retw = 0;
	int last_zeich;
	void *mymem = NULL;
	u_char *estack;
	u_char 	*t_stack;
	u_char *t_stpoin;

	u_char *dpoin;
	u_char *edpoin;
	u_char *srcpoin;
	u_char *esrcpoin;
	ulong flags;

	short t_breite;
	int  t_counter;
	u_short  dbits = 0;

	short 	*t_code_wert;
	short 	*t_eltern_code;

	save_flags(flags);
	cli();

	if(meminuse) {

		restore_flags(flags);
		return(-1);
	}

	meminuse = 1;
	restore_flags(flags);

	if((mymem = depackmem) == NULL) {

		mymem = (void *)
			kmalloc(sizeof(short) * (EMPF_TABGR << 1) + 800,GFP_ATOMIC);

		if((depackmem = mymem) == NULL) {

			printk(KERN_DEBUG "abc-depack: no memory for %d\n",meminuse);
			meminuse = 0;
			return(-1);
		}

		printk(KERN_DEBUG "abc-depack: get memory for %d\n",meminuse);
	}

	t_code_wert = (short *)mymem;
	t_eltern_code = (short *)(mymem + sizeof(short) * EMPF_TABGR);
	t_stack = (u_char *)(mymem + (sizeof(short) * (EMPF_TABGR << 1)));

	estack = t_stack + 800;

	t_breite = 9;
	t_counter = C_FIRSTE;
	dbits = 0;

	srcpoin = msrcpoin;
	esrcpoin = srcpoin + srcbytes;
	dpoin = dstpoin;

	if((edpoin = dstpoin + bytes) <= dpoin)
		goto ausgang;

	if((last_code = get_a_word(&t_breite,&srcpoin,esrcpoin,&dbits))  < 0)
		goto ausgang;

	*(dpoin++) = last_zeich = last_code ;

	while((now_code = get_a_word(&t_breite,&srcpoin,esrcpoin,&dbits)) >= 0 
		&& dpoin < edpoin) {

		int pcode; 
		int em;

		if(now_code < 256) {

			if(t_counter >= EMPF_TABGR) {

				printk(KERN_DEBUG "abc-depack tcounter-overflow\n");
				goto ausgang;
			}

			*(dpoin++) = now_code;
			t_code_wert[t_counter] = now_code;
			t_eltern_code[t_counter] = last_code;
			t_counter++;

			last_code = last_zeich = now_code;
			continue;
		} 

		pcode = last_code; 
		last_code = now_code;
		em = 1;

		if(now_code >= t_counter) {

			if(t_counter >= EMPF_TABGR) {

				printk(KERN_DEBUG "abc-depack tcounter-ovewrflow\n");
				goto ausgang;
			}

			em = 0;
			t_eltern_code[t_counter] = pcode;
			t_code_wert[t_counter] = last_zeich;
			t_counter++;

			if(now_code > t_counter) {

				printk(KERN_DEBUG 
					"abc-depack  now_code %d > t_counter %d (meminuse %d)\n",
					now_code,t_counter,meminuse);

				goto ausgang;
			}
		} 

		t_stpoin = t_stack;

		do {

			*(t_stpoin++) = t_code_wert[now_code];
			now_code = t_eltern_code[now_code];

			if(t_stpoin >= estack) {

				printk(KERN_DEBUG "abc-depack depack-stack-overflow\n");
				goto ausgang;
			}

		} while(now_code >= C_FIRSTE);

		last_zeich = *t_stpoin = now_code;

		if(em) {

			if(t_counter >= EMPF_TABGR) {

				printk(KERN_DEBUG "abc-depack tcounter-overflow\n");
				goto ausgang;
			}

			t_eltern_code[t_counter] = pcode;
			t_code_wert[t_counter] = last_zeich; 
			t_counter++;
		}

		while(t_stpoin >= t_stack && dpoin < edpoin) 
			*(dpoin++) = *(t_stpoin--);
	}

	if(now_code >= 0)
		goto ausgang;

	retw = dpoin - dstpoin;

ausgang:;

	meminuse = 0;
	return(retw);
}
	
#endif
