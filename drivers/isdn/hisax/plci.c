#include "hisax_capi.h"
#include "l4l3if.h"
#include "callc.h"

#include "isdnl3.h"
#include "l3dss1.h"

#define plciDebug(plci, lev, fmt, args...) \
        debug(lev, plci->contr->cs, "", fmt, ## args)


static void l4pc_l3l4(struct l4_process *l4_pc, int pr, void *arg)
{
	struct Plci *plci = l4_pc->priv;

	plci_l3l4(plci, pr, arg);
}

void plciConstr(struct Plci *plci, struct Contr *contr, __u32 adrPLCI)
{
	HiSax_mod_inc_use_count(contr->cs);

	memset(plci, 0, sizeof(struct Plci));
	plci->adrPLCI = adrPLCI;
	plci->contr = contr;
	plci->l4_pc.l3l4 = l4pc_l3l4;
	plci->l4_pc.l3pc = 0;
	plci->l4_pc.priv = plci;
}

void plciDestr(struct Plci *plci)
{
	if (plci->l4_pc.l3pc) {
		// FIXME: we need to kill l3_process, actually
		plci->l4_pc.l3pc->l4pc = 0;
	}
	HiSax_mod_dec_use_count(plci->contr->cs);
}

void plciHandleSetupInd(struct Plci *plci, int pr, struct sk_buff *skb)
{
	int ApplId;
	__u16 CIPValue;
	struct Appl *appl;
	struct Cplci *cplci;

	for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
		appl = contrId2appl(plci->contr, ApplId);
		if (appl) {
			CIPValue = q931CIPValue(skb);
			if (listenHandle(&appl->listen, CIPValue)) {
				cplci = applNewCplci(appl, plci);
				if (!cplci) {
					int_error();
					break;
				}
				cplci_l3l4(cplci, pr, skb);
			}
		}
	}
	if (plci->nAppl == 0) {
		p_L4L3(&plci->l4_pc, CC_IGNORE | REQUEST, 0);
	}
}

void plci_l3l4(struct Plci *plci, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	__u16 applId;
	struct Cplci *cplci;

	switch (pr) {
	case CC_SETUP | INDICATION:
		plciHandleSetupInd(plci, pr, skb);
		break;
	case CC_RELEASE_CR | INDICATION:
		plci->l4_pc.l3pc = 0;
		if (plci->nAppl == 0) {
			contrDelPlci(plci->contr, plci);
		}
		break;
	default:
		for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
			cplci = plci->cplcis[applId - 1];
			if (cplci) 
				cplci_l3l4(cplci, pr, arg);
		}
	}
}

void plciAttachCplci(struct Plci *plci, struct Cplci *cplci)
{
	__u16 applId = cplci->appl->ApplId;

	if (plci->cplcis[applId - 1]) {
		int_error();
		return;
	}
	plci->cplcis[applId - 1] = cplci;
	plci->nAppl++;
}

void plciDetachCplci(struct Plci *plci, struct Cplci *cplci)
{
	__u16 applId = cplci->appl->ApplId;

	if (plci->cplcis[applId - 1] != cplci) {
		int_error();
		return;
	}
	cplci->plci = 0;
	plci->cplcis[applId - 1] = 0;
	plci->nAppl--;
	if ((plci->nAppl == 0) && !plci->l4_pc.l3pc) {
		contrDelPlci(plci->contr, plci);
	}
}

void plciNewCrInd(struct Plci *plci, struct l3_process *l3_pc)
{
	l3_pc->l4pc = &plci->l4_pc; 
	l3_pc->l4pc->l3pc = l3_pc;
}

void plciNewCrReq(struct Plci *plci)
{
	L4L3(&plci->contr->l4, CC_NEW_CR | REQUEST, &plci->l4_pc);
}
