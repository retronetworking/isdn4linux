#ifndef __L4L3IF_H__
#define __L4L3IF_H__

#include "callc.h" // FIXME

// =================================================================
// process

static inline void
p_L4L3(struct l4_process *l4pc, int pr, void *arg)
{
	if (!l4pc->l3pc) {
		int_error();
		return;
	}
	if (!l4pc->l3pc->l4l3) {
		int_error();
		return;
	}
	l4pc->l3pc->l4l3(l4pc->l3pc, pr, arg);
}

static inline void 
p_L3L4(struct l3_process *l3pc, int pr, void *arg)
{
	if (!l3pc->l4pc) {
		printk("pr %#x\n", pr);
		int_error();
		return;
	}
	if (!l3pc->l4pc->l3l4) {
		int_error();
		return;
	}

	l3pc->l4pc->l3l4(l3pc->l4pc, pr, arg);
};

#endif
