/* $Id$
 * isar.h   ISAR (Siemens PSB 7110) specific defines
 *
 * Author Karsten Keil (keil@isdn4linux.de)
 *
 *
 * $Log$
 *
 */
 
#define ISAR_IRQMSK	0x04
#define ISAR_IRQSTA	0x04
#define ISAR_IRQBIT	0x75
#define ISAR_CTRL_H	0x61
#define ISAR_CTRL_L	0x60
#define ISAR_IIS	0x58
#define ISAR_IIA	0x58
#define ISAR_HIS	0x50
#define ISAR_HIA	0x50
#define ISAR_MBOX	0x4c
#define ISAR_WADR	0x4a
#define ISAR_RADR	0x48 

#define ISAR_HIS_VNR	0x14
#define ISAR_HIS_DKEY	0x02
#define ISAR_HIS_FIRM	0x1e
#define ISAR_HIS_STDSP  0x08
#define ISAR_HIS_DIAG	0x05

#define ISAR_IIS_VNR	0x15
#define ISAR_IIS_DKEY	0x03
#define ISAR_IIS_FIRM	0x1f
#define ISAR_IIS_STDSP  0x09
#define ISAR_IIS_DIAG	0x25

#define ISAR_CTRL_SWVER	0x10
#define ISAR_CTRL_STST	0x40

#define ISAR_MSG_HWVER	{0x20, 0, 1}

extern int ISARVersion(struct IsdnCardState *cs, char *s);
extern int sendmsg(struct IsdnCardState *cs, u_char his, u_char creg,
			u_char len, u_char *msg);
extern int receivemsg(struct IsdnCardState *cs, u_char *his, u_char *creg,
			u_char *len, u_char *msg);
extern int isar_load_firmware(struct IsdnCardState *cs, u_char *buf);
extern void isar_int_main(struct IsdnCardState *cs);
