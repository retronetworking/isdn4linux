/* $Id$

 * Linux ISDN subsystem, V.110 related functions (linklevel).
 *
 * Copyright by Thomas Pfeiffer (pfeiffer@pds.de)
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
 * $Log$
 */
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <linux/isdn.h>
#include "isdn_v110.h"

char *isdn_v110_revision = "$Revision$";

#define V110_38400 255
#define V110_19200  15
#define V110_9600    3

/* Die folgenden Daten sind fertig kodierte Matrizen, jeweils
   als online und offline matrix für 9600, 19200 und 38400
 */
unsigned char V110_OnMatrix_9600[] =
{0xfc, 0xfc, 0xfc, 0xfc, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff,
 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd,
 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff,
 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xfd};

unsigned char V110_OffMatrix_9600[] =
{0xfc, 0xfc, 0xfc, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

unsigned char V110_OnMatrix_19200[] =
{0xf0, 0xf0, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7,
 0xfd, 0xff, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7, 0xff, 0xf7};

unsigned char V110_OffMatrix_19200[] =
{0xf0, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

unsigned char V110_OnMatrix_38400[] =
{0x00, 0x7f, 0x7f, 0x7f, 0x7f, 0xfd, 0x7f, 0x7f, 0x7f, 0x7f};

unsigned char V110_OffMatrix_38400[] =
{0x00, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xff};


#ifdef DEBUG
static int anz = 0;
static void
pr_bin(unsigned char c)
{
	printk(KERN_DEBUG "%.2x %d %d %d %d %d %d %d %d\n", c, c & 128. c & 64, c & 32, c & 16, c & 8, c & 4, c & 2, c & 1);
}

#endif


/* FlipBits dreht die Reihenfolge von jeweils keylen bits in einem byte um.
   Aus der Bitreihenfolge 76543210 werden bei keylen=4 die bits 45670123,
   bei keylen=2 die bits 67452301. Dies ist notwendig, weil die reihenfolge
   auf der isdn-leitung falsch herum ist.
 */

static unsigned char
FlipBits(unsigned char c, int keylen)
{
	unsigned char b = c;
	unsigned char bit = 128;
	int i;
	int j;
	int hunks = (8 / keylen);

	c = 0;
	for (i = 0; i < hunks; i++) {
		for (j = 0; j < keylen; j++) {
			if (b & (bit >> j))
				c |= bit >> (keylen - j - 1);
		}
		bit >>= keylen;
	}
	return c;
}


/* isdn_v110_open allocates and initializes private V.110 data
 * structures and returns a pointer to these.
 */
static isdn_v110_stream *
isdn_v110_open(unsigned char key, int hdrlen, int maxsize)
{
	int i;
	isdn_v110_stream *v;

	if ((v = kmalloc(sizeof(isdn_v110_stream), GFP_KERNEL)) == NULL)
		return NULL;
	memset(v, 0, sizeof(isdn_v110_stream));
	v->key = key;
	v->nbits = 0;
	for (i = 0; key & (1 << i); i++)
		v->nbits++;

	v->nbytes = 8 / v->nbits;
	v->decodelen = 0;
	v->encodelen = 0;

	switch (key) {
		case V110_38400:
			v->OnlineFrame = V110_OnMatrix_38400;
			v->OfflineFrame = V110_OffMatrix_38400;
			break;
		case V110_19200:
			v->OnlineFrame = V110_OnMatrix_19200;
			v->OfflineFrame = V110_OffMatrix_19200;
			break;
		default:
			v->OnlineFrame = V110_OnMatrix_9600;
			v->OfflineFrame = V110_OffMatrix_9600;
			break;
	}
	v->framelen = v->nbytes * 10;
	v->SyncInit = 5;
	v->introducer = 0;
	v->dbit = 1;
	v->b = 0;
	v->skbres = hdrlen;
	v->maxsize = maxsize - hdrlen;
#if (DEBUG > 1)
	printk(KERN_DEBUG "keylen=%d, bytes in stream=%d\n", v->nbits, v->nbytes);
#endif
	return v;
}

/* isdn_v110_close frees private V.110 data structures */
static void
isdn_v110_close(isdn_v110_stream * v)
{
	if (v == NULL)
		return;
	printk(KERN_DEBUG "v110 close\n");
#if (DEBUG > 1)
	printk(KERN_DEBUG "isdn_v110_close: nbytes=%d\n", v->nbytes);
	printk(KERN_DEBUG "isdn_v110_close: nbits=%d\n", v->nbits);
	printk(KERN_DEBUG "isdn_v110_close: key=%d\n", v->key);
	printk(KERN_DEBUG "isdn_v110_close: SyncInit=%d\n", v->SyncInit);
	printk(KERN_DEBUG "isdn_v110_close: encodelen=%d\n", v->encodelen);
	printk(KERN_DEBUG "isdn_v110:close: decodelen=%d\n", v->decodelen);
	printk(KERN_DEBUG "isdn_v110_close: framelen=%d\n", v->framelen);
#endif
	kfree(v);
}


/* ValidHeaderBytes prüft, wieviele bytes in v->decodebuf gültig sind */

static int
ValidHeaderBytes(isdn_v110_stream * v)
{
	int i;
	for (i = 0; (i < v->decodelen) && (i < v->nbytes); i++)
		if ((v->decodebuf[i] & v->key) != 0)
			break;
	return i;
}

/* SyncHeader schiebt den decodebuf pointer auf den nächsten gültigen header */

static void
SyncHeader(isdn_v110_stream * v)
{
	unsigned char *rbuf = v->decodebuf;
	int len = v->decodelen;

	if (len == 0)
		return;
	for (rbuf++, len--; len > 0; len--, rbuf++)	/* such den SyncHeader in buf ! */
		if ((*rbuf & v->key) == 0)	/* erstes byte gefunden ?       */
			break;  /* jupp!                        */
	if (len)
		memcpy(v->decodebuf, rbuf, len);

	v->decodelen = len;
	printk(KERN_DEBUG "isdn_v110: Header resync\n");
}

/* DecodeMatrix takes n (n>=1) matrices (v110 frames, 10 bytes) where
   len is the number of matrix-lines. len must be a multiple of 10, i.e.
   only complete matices must be given.
   From these, netto data is extracted and returned in buf. The return-value
   is the bytecount of the decoded data.
 */
static int
DecodeMatrix(isdn_v110_stream * v, unsigned char *m, int len, unsigned char *buf)
{
	int line = 0;
	int buflen = 0;
	int mbit = 64;
	int introducer = v->introducer;
	int dbit = v->dbit;
	unsigned char b = v->b;

	while (line < len) {    /* sind schon alle matrizenzeilen abgearbeitet? */
		if ((line % 10) == 0) {	/* die 0. zeile der matrix ist immer null ! */
			if (m[line] != 0x00) {	/* nicht 0 ? dann fehler! */
				printk(KERN_DEBUG "isdn_v110: DecodeMatrix, V110 Bad Header\n");

/* dann einen return zu machen, ist auch irgendwie nicht das richtige! :-(
   v->introducer = 0; v->dbit = 1; v->b = 0;
   return buflen;                                                                                                                                                                                                                                                                                                                                                                                               // anzahl schon erzeugter daten zurückgeben!
 */
			}
			line++; /* sonst die nächste matrixzeile nehmen */
			continue;
		} else if ((line % 10) == 5) {	/* in zeile 5 stehen nur e-bits ! */
			if ((m[line] & 0x70) != 0x30) {	/* 011 muß am anfang stehen! */
				printk(KERN_DEBUG "isdn_v110: DecodeMatrix, V110 Bad 5th line\n");
/* dann einen return zu machen, ist auch irgendwie nicht das richtige! :-(
   v->introducer = 0; v->dbit = 1; v->b = 0;
   return buflen;
 */
			}
			line++; /* alles klar, nächste zeile */
			continue;
		} else if (introducer != 0x06) {	/* vor jedem datenbyte kommt "110" ! */
			introducer <<= 1;	/* shifte die schon vorhanden bits */
			introducer &= 0x07;	/* nur die letzten drei sind relevant */
			introducer |= (m[line] & mbit) ? 1 : 0;	/* aktuelles bit der matrix */
		      next_byte:
			if (mbit > 2) {	/* war es das letzte bit dieser matrixzeile ? */
				mbit >>= 1;	/* nein, nimm das nächste in dieser zeile */
				continue;
			}       /* sonst links in der nächsten zeile anfangen */
			mbit = 64;
			line++;
			continue;
		} else {        /* sonst müssen wir ein datenbit setzen */
			if (m[line] & mbit)	/* war das bit in der matrix gesetzt ? */
				b |= dbit;	/* ja, dann setz es auch im datenbyte  */
			else
				b &= dbit - 1;	/* nein, lösch bit im datenbyte */
			if (dbit < 128)	/* haben wir schon ein ganzes byte voll ? */
				dbit <<= 1;	/* nein, auf zum nächsten datenbit */
			else {  /* ein ganzes datenbyte ist voll */
				buf[buflen++] = b;	/* byte in den output buffer kopieren */
				introducer = b = 0;	/* Init der Introsequenz und des datenbytes */
				dbit = 1;	/* als nächstes suchen wir das nullte bit */
			}
			goto next_byte;	/* suche das nächste bit in der matrix */
		}
	}
	v->introducer = introducer;
	v->dbit = dbit;
	v->b = b;
	return buflen;          /* return anzahl der bytes im output buffer */
}

/* DecodeStream erhält vom input stream V110 kodierte Daten, die zu den
   V110 frames zusammengepackt werden müssen. Die Daten können an diese
   Schnittstelle so übergeben werden, wie sie von der Leitung kommen, ohne
   darauf achten zu müssen, das frames usw. eingehalten werden.
 */
struct sk_buff *
isdn_v110_decode(isdn_v110_stream * v, struct sk_buff *skb)
{
	int i;
	int j;
	int len;
	unsigned char *v110_buf;
	unsigned char *rbuf;

	if (!skb) {
		printk(KERN_WARNING "isdn_v110_decode called with NULL skb!\n");
		return NULL;
	}
	rbuf = skb->data;
	len = skb->len;
	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_decode called with NULL stream!\n");
		dev_kfree_skb(skb);
		return NULL;
	}
	if (v->decodelen == 0)  /* cache empty?               */
		for (; len > 0; len--, rbuf++)	/* scan for SyncHeader in buf */
			if ((*rbuf & v->key) == 0)
				break;	/* found first byte           */
	if (len == 0) {
		dev_kfree_skb(skb);
		return NULL;
	}
	/* copy new data to decode-buffer */
	memcpy(&(v->decodebuf[v->decodelen]), rbuf, len);
	v->decodelen += len;
      ReSync:
	if (v->decodelen < v->nbytes) {	/* got a new header ? */
		dev_kfree_skb(skb);
		return NULL;    /* no, try later      */
	}
	if (ValidHeaderBytes(v) != v->nbytes) {	/* ist es ein ungültiger header ? */
		SyncHeader(v);  /* nein, such einen header */
		goto ReSync;
	}
	len = (v->decodelen - (v->decodelen % (10 * v->nbytes))) / v->nbytes;
	if ((v110_buf = kmalloc(4096, GFP_ATOMIC)) == NULL) {
		printk(KERN_WARNING "isdn_v110_decode: Couldn't allocate v110_buf\n");
		dev_kfree_skb(skb);
		return NULL;
	}
	for (i = 0; i < len; i++) {
		v110_buf[i] = 0;
		for (j = 0; j < v->nbytes; j++)
			v110_buf[i] |= (v->decodebuf[(i * v->nbytes) + j] & v->key) << (8 - ((j + 1) * v->nbits));
		v110_buf[i] = FlipBits(v110_buf[i], v->nbits);
#if (DEBUG > 1)
		pr_bin(v110_buf[i]);
#endif
	}
	v->decodelen = (v->decodelen % (10 * v->nbytes));
	memcpy(v->decodebuf, &(v->decodebuf[len * v->nbytes]), v->decodelen);

	skb_trim(skb, DecodeMatrix(v, v110_buf, len, skb->data));
	kfree(v110_buf);
	return skb;
}

#define TEMP_BUFSIZE 4000

/* EncodeMatrix takes input data in buf, len is the bytecount.
   Data is encoded into v110 frames in m. Return value is the number of
   matrix-lines generated.
 */
static int
EncodeMatrix(unsigned char *buf, int len, unsigned char *m, int mlen)
{
	int line = 0;
	int i = 0;
	int mbit = 128;
	int dbit = 1;
	int introducer = 3;
	int ibit[] = {0, 1, 1};

	while ((i < len) && (line < mlen)) {	/* solange noch input da ist */
		switch (line % 10) {	/* in welcher matrixzeile sind wir ? */
			case 0:
				m[line++] = 0x00;	/* zeile 0 ist immer 0 */
				mbit = 128;	/* und es geht mit dem 7. bit weiter */
				break;
			case 5:
				m[line++] = 0xbf;	/* zeile 5 ist immer 10111111 */
				mbit = 128;	/* und es geht mit dem 7. bit weiter */
				break;
		}
		if (line >= mlen) {
			printk(KERN_DEBUG "isdn_v110: EncodeMatrix, matrix buffer full!\n");
			return line;
		}
	next_bit:
		switch (mbit) { /* ganz linkes oder rechtes bit ? */
			case 1:
				line++;	/* ganz rechts ! dann in die nächste */
				if (line >= mlen) {
					printk(KERN_DEBUG "isdn_v110: EncodeMatrix, matrix buffer full!\n");
					return line;
				}
			case 128:
				m[line] = 128;	/* ganz links byte auf 1000000 setzen */
				mbit = 64;	/* aktuelles bit in der matrixzeile */
				continue;
		}
		if (introducer) {	/* 110 sequenz setzen ? */
			introducer--;	/* ein digit weniger setzen */
			m[line] |= ibit[introducer] ? mbit : 0;	/* entsprechendes bit setzen */
			mbit >>= 1;	/* bit der matrixzeile >> 1 */
			goto next_bit;	/* und dort weiter machen */
		}               /* else datenbits in die matrix packen! */
		m[line] |= (buf[i] & dbit) ? mbit : 0;	/* datenbit in matrix setzen */
		if (dbit == 128) {	/* war es das letzte datenbit ? */
			dbit = 1;	/* dann mach beim nächsten weiter */
			i++;    /* nächste datenbyte des input buffers */
			if (i < len)	/* war es schon das letzte ? */
				introducer = 3;	/* nein, schreib den introducer 110 */
			else {  /* war das letzte datenbyte ! */
				m[line] |= (mbit - 1) & 0xfe;	/* setz restliche bits der zeile auf 1 */
				break;
			}
		} else          /* nicht das letzte datenbit */
			dbit <<= 1;	/* dann gehe zum nächsten datenbit */
		mbit >>= 1;     /* und setz bit der matrix weiter */
		goto next_bit;

	}
	/* evtl. noch restliche zeilen in der matrix generieren... */
	if ((line) && ((line + 10) < mlen))
		switch (++line % 10) {
			case 1:
				m[line++] = 0xfe;
			case 2:
				m[line++] = 0xfe;
			case 3:
				m[line++] = 0xfe;
			case 4:
				m[line++] = 0xfe;
			case 5:
				m[line++] = 0xbf;
			case 6:
				m[line++] = 0xfe;
			case 7:
				m[line++] = 0xfe;
			case 8:
				m[line++] = 0xfe;
			case 9:
				m[line++] = 0xfe;
		}
	return line;            /* soviele matrixzeilen sind es */
}

/*
 * Build a sync frame.
 */
struct sk_buff *
isdn_v110_sync(isdn_v110_stream *v)
{
	struct sk_buff *skb = NULL;

	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_sync called with NULL stream!\n");
		return NULL;
	}
	if ((skb = dev_alloc_skb(v->framelen + v->skbres))) {
		skb_reserve(skb, v->skbres);
		memcpy(skb_put(skb, v->framelen), v->OfflineFrame, v->framelen);
	}
	return skb;
}

/*
 * Build an idle frame.
 */
struct sk_buff *
isdn_v110_idle(isdn_v110_stream *v)
{
	struct sk_buff *skb = NULL;

	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_sync called with NULL stream!\n");
		return NULL;
	}
	if ((skb = dev_alloc_skb(v->framelen + v->skbres))) {
		skb_reserve(skb, v->skbres);
		memcpy(skb_put(skb, v->framelen), v->OnlineFrame, v->framelen);
	}
	return skb;
}

struct sk_buff *
isdn_v110_encode(isdn_v110_stream * v, struct sk_buff *skb)
{
	int i;
	int j;
	int mlen = 0;
	int n = 0;
	int olen = 0;
	int space;
	int size;
	int len;
	unsigned char *v110buf = NULL;
	unsigned char *rbuf;

	if (v == NULL) {
		/* invalid handle, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_encode called with NULL stream!\n");
		return NULL;
	}
	if (!skb) {
		/* invalid skb, no chance to proceed */
		printk(KERN_WARNING "isdn_v110_encode called with NULL skb!\n");
		return NULL;
	}
	sti();
	size = v->maxsize;
	len = skb->len;
	if ((v110buf = kmalloc(TEMP_BUFSIZE, GFP_ATOMIC)) == NULL) {
		printk(KERN_WARNING "isdn_v110_encode: Couldn't alloc v110buf\n");
		return NULL;
	}
	mlen = EncodeMatrix(skb->data, skb->len, v110buf, TEMP_BUFSIZE);
	dev_kfree_skb(skb);
	if (!(skb = dev_alloc_skb(size + v->skbres))) {
		printk(KERN_WARNING "isdn_v110_encode: Couldn't alloc skb\n");
		kfree(v110buf);
		return NULL;
	}
	skb_reserve(skb, v->skbres);
	if (v->encodelen) {
		n = size > v->encodelen ? v->encodelen : size;
		memcpy(skb_put(skb, n), v->encodebuf, n);
		v->encodelen -= n;
#if (DEBUG > 3)
		printk(KERN_DEBUG "isdn_v110_encode: cping encodebuf %d bytes size=%d\n", n, size)
#endif
		if (v->encodelen)
			memcpy(v->encodebuf, &(v->encodebuf[n]), v->encodelen);
		size -= n;
	} else if (len == 0) {
		memcpy(skb_put(skb, v->framelen), v->OnlineFrame, v->framelen);
		kfree(v110buf);
		return skb;
	}
	/* jetzt noch jeweils 2 oder 4 bits auf den output stream verteilen! */
	space = size + sizeof(v->encodebuf) - v->encodelen;
	if (size) {
		rbuf = skb_put(skb, size);
	} else
		rbuf = &(v->encodebuf[v->encodelen]);
	for (i = 0; i < mlen; i++) {
		v110buf[i] = FlipBits(v110buf[i], v->nbits);
		for (j = 0; j < v->nbytes; j++) {
			if (space--)
				*rbuf++ = ~v->key | (((v110buf[i] << (j * v->nbits)) & (v->key << (8 - v->nbits))) >> (8 - v->nbits));
			else {
				printk(KERN_DEBUG "isdn_v110_encode: all buffers full!\n");
				goto buffer_full;
			}
			if (++olen == size)
				rbuf = &(v->encodebuf[v->encodelen]);
		}
	}
buffer_full:
	if (olen > size) {
		v->encodelen += olen - size;
		printk(KERN_DEBUG "isdn_v110_encode: using encodebuf %d bytes, size=%d!\n", v->encodelen, size);
	} else
		skb_trim(skb, olen + n);
	kfree(v110buf);
	return skb;
}

int
isdn_v110_stat_callback(int idx, isdn_ctrl * c)
{
	isdn_v110_stream *v = NULL;
	int i;
	int ret;

	if (idx < 0)
		return 0;
	switch (c->command) {
		case ISDN_STAT_BSENT:
                        /* Keep the send-queue of the driver filled
			 * with frames:
			 * If number of outstanding frames < 3,
			 * send down an Idle-Frame (or an Sync-Frame, if
			 * v->SyncInit != 0). 
			 */
			if (!(v = dev->v110[idx]))
				return 0;
			atomic_inc(&dev->v110use[idx]);
			if (v->skbidle) {
				v->skbidle--;
				ret = 1;
			} else {
				if (v->skbuser)
					v->skbuser--;
				ret = 0;
			}
			for (i = v->skbuser + v->skbidle; i < 2; i++) {
				struct sk_buff *skb;
				if (v->SyncInit)
					skb = isdn_v110_sync(v);
				else
					skb = isdn_v110_idle(v);
				if (skb) {
					if (dev->drv[c->driver]->interface->writebuf_skb(c->driver, c->arg, 1, skb) <= 0) {
						dev_kfree_skb(skb);
						break;
					} else {
						if (v->SyncInit)
							v->SyncInit--;
						v->skbidle++;
					}
				} else
					break;
			}
			atomic_dec(&dev->v110use[idx]);
			return ret;
		case ISDN_STAT_DHUP:
		case ISDN_STAT_BHUP:
			while (1) {
				atomic_inc(&dev->v110use[idx]);
				if (atomic_dec_and_test(&dev->v110use[idx])) {
					isdn_v110_close(dev->v110[idx]);
					dev->v110[idx] = NULL;
					break;
				}
				sti();
			}
			break;
		case ISDN_STAT_BCONN:
			if (dev->v110emu[idx] && (dev->v110[idx] == NULL)) {
				int hdrlen = dev->drv[c->driver]->interface->hl_hdrlen;
				int maxsize = dev->drv[c->driver]->interface->maxbufsize;
				atomic_inc(&dev->v110use[idx]);
				switch (dev->v110emu[idx]) {
					case ISDN_PROTO_L2_V11096:
						dev->v110[idx] = isdn_v110_open(V110_9600, hdrlen, maxsize);
						break;
					case ISDN_PROTO_L2_V11019:
						dev->v110[idx] = isdn_v110_open(V110_19200, hdrlen, maxsize);
						break;
					case ISDN_PROTO_L2_V11038:
						dev->v110[idx] = isdn_v110_open(V110_38400, hdrlen, maxsize);
						break;
					default:
				}
				if ((v = dev->v110[idx])) {
					while (v->SyncInit) {
						struct sk_buff *skb = isdn_v110_sync(v);
						if (dev->drv[c->driver]->interface->writebuf_skb(c->driver, c->arg, 1, skb) <= 0) {
							dev_kfree_skb(skb);
							/* Unable to send, try later */
							break;
						}
						v->SyncInit--;
						v->skbidle++;
					}
					printk(KERN_DEBUG "v110_open\n");
				} else
					printk(KERN_WARNING "isdn_v110: Couldn't open stream for chan %d\n", idx);
				atomic_dec(&dev->v110use[idx]);
			}
			break;
		default:
			return 0;
	}
	return 0;
}
