/* $Id$
 *
 * teles3.h   Header for Teles 16.3 PNP & compatible
 *
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * 
 * $Log$
 *
*/

extern	void teles3_report(struct IsdnCardState *sp);
extern  void release_io_teles3(struct IsdnCard *card);
extern	int  setup_teles3(struct IsdnCard *card);
extern  int  initteles3(struct IsdnCardState *sp);
