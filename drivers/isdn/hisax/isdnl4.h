#ifndef __ISDNL4_H__
#define __ISDNL4_H__

#include "stack.h"

// =================================================================
// Layer4

struct Layer4 {
        void (*l3l4)(struct PStack *st, int pr, void *arg);
	struct PStack *st;
};

// =================================================================
// struct l4_process

struct l4_process {
	struct l3_process *l3pc;
	void (*l3l4)(struct l4_process *l4pc, int pr, void *arg);
	void *priv;
};

#endif
