/* $Id$
 *
 * teles0.h   Header for Teles 16.0 8.0 & compatible
 *
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log$
 * Revision 1.1  1996/10/13 20:03:48  keil
 * Initial revision
 *
 *
*/

extern	void teles0_report(struct IsdnCardState *sp);
extern  void release_io_teles0(struct IsdnCard *card);
extern	int  setup_teles0(struct IsdnCard *card);
extern  int  initteles0(struct IsdnCardState *sp);
