/* $Id$
 *
 * $Log$
 * Revision 1.1  1996/10/13 20:03:47  keil
 * Initial revision
 *
 *
 *
 */
 
/* DEBUG Level */

#define	L1_DEB_WARN		0x01
#define	L1_DEB_INTSTAT		0x02
#define	L1_DEB_ISAC		0x04
#define	L1_DEB_ISAC_FIFO	0x08
#define	L1_DEB_HSCX		0x10
#define	L1_DEB_HSCX_FIFO	0x20


#define ISAC_RCVBUFREADY 0
#define ISAC_XMTBUFREADY 1
#define ISAC_PHCHANGE    2

#define HSCX_RCVBUFREADY 0
#define HSCX_XMTBUFREADY 1

extern void debugl1(struct IsdnCardState *sp, char *msg);
extern char *HscxVersion(byte v);
extern char *ISACVersion(byte v);
extern void hscx_sched_event(struct HscxState *hsp, int event);
extern void isac_sched_event(struct IsdnCardState *sp, int event);
extern void isac_new_ph(struct IsdnCardState *sp);
extern get_irq(int cardnr, void *routine);
