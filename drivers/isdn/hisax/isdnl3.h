/* $Id$
 *
 * $Log$
 *
 */
    
#define SBIT(state) (1<<state)
#define ALL_STATES  0x00ffffff

#define	PROTO_DIS_EURO	0x08

#define L3_DEB_WARN	0x01
#define	L3_DEB_PROTERR	0x02
#define	L3_DEB_STATE	0x04
#define	L3_DEB_CHARGE	0x08

struct stateentry {
	int             state;
	byte            primitive;
	void            (*rout) (struct PStack *, byte, void *);
};

extern void l3_debug(struct PStack *st, char *s);
extern void newl3state(struct PStack *st, int state);
