/* $Id$
 *
 * diva.h   Header for Eicon.Diehl Diva Family ISDN cards
 *
 * Author	Karsten Keil (keil@temic-ech.spacenet.de)
 *
 * Thanks to Eicon Technology Diehl GmbH & Co. oHG for documents and informations
 *
 *
 * $Log$
 * Revision 1.1  1997/09/18 17:11:21  keil
 * first version
 *
 *
*/

#define DIVA_HSCX_DATA		0
#define DIVA_HSCX_ADR		4
#define DIVA_ISA_ISAC_DATA	2
#define DIVA_ISA_ISAC_ADR	6
#define DIVA_ISA_CTRL		7

#define DIVA_PCI_ISAC_DATA	8
#define DIVA_PCI_ISAC_ADR	0xc
#define DIVA_PCI_CTRL		0x10

/* SUB Types */
#define DIVA_ISA	1
#define DIVA_PCI	2

/* PCI stuff */
#define PCI_VENDOR_EICON_DIEHL	0x1133
#define PCI_DIVA20PRO_ID	0xe001
#define PCI_DIVA20_ID		0xe002
#define PCI_DIVA20PRO_U_ID	0xe003
#define PCI_DIVA20_U_ID		0xe004

/* CTRL (Read) */
#define DIVA_IRQ_STAT	0x01
#define DIVA_EEPROM_SDA	0x02
/* CTRL (Write) */
#define DIVA_IRQ_REQ	0x01
#define DIVA_RESET	0x08
#define DIVA_EEPROM_CLK	0x40
#define DIVA_PCI_LED_A	0x10
#define DIVA_PCI_LED_B	0x20
#define DIVA_ISA_LED_A	0x20
#define DIVA_ISA_LED_B	0x40
#define DIVA_IRQ_CLR	0x80

extern  void release_io_diva(struct IsdnCard *card);
extern	int  setup_diva(struct IsdnCard *card);
extern  int  initdiva(struct IsdnCardState *cs);
