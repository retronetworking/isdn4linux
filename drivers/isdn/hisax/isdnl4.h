#ifndef __ISDNL4_H__
#define __ISDNL4_H__

// =================================================================
// struct l4_process

struct l4_process {
	struct l3_process *l3pc;
	void (*l3l4)(struct l4_process *l4pc, int pr, void *arg);
	void *priv;
};

#endif
