#include "isdnl4.h"
#include "stack.h"

static int
init_st_1(struct PStack *st, int b1_mode, struct IsdnCardState *cs, int bchannel, 
	  unsigned int headroom)
{
	st->l1.mode = b1_mode;
	st->l1.hardware = cs;
	st->l1.bc = bchannel;
	switch (bchannel) {
	case CHANNEL_D:
		HiSax_addlist(cs, st);
		setstack_HiSax(st, cs);
		break;
	case CHANNEL_B1:
	case CHANNEL_B2:
		if (cs->bcs[bchannel].BC_SetStack(st, &cs->bcs[bchannel]))
			return -1;
		cs->bcs[bchannel].headroom = headroom;
		st->l1.bcs->conmsg = NULL;
		break;
	default:
		int_error();
		return -1;
	}
	return 0;
}

static void 
release_st_1(struct PStack *st)
{
	switch (st->l1.bc) {
	case CHANNEL_D:
		HiSax_rmlist(st->l1.hardware, st);
		break;
	case CHANNEL_B1:
	case CHANNEL_B2:
		st->l1.bcs->BC_Close(st->l1.bcs);
		break;
	default:
		int_error();
	}
}

static int
init_st_2(struct PStack *st, int b2_mode)
{
	st->l2.mode = b2_mode;
	switch (b2_mode) {
	case B2_MODE_LAPD:
		st->l2.sap = 0;
		st->l2.tei = -1;
		st->l2.flag = 0;
		test_and_set_bit(FLG_MOD128, &st->l2.flag);
		test_and_set_bit(FLG_LAPD, &st->l2.flag);
		test_and_set_bit(FLG_ORIG, &st->l2.flag);
		st->l2.maxlen = MAX_DFRAME_LEN; // FIXME!
		st->l2.window = 1;
		st->l2.T200 = 1000;	/* 1000 milliseconds  */
		st->l2.N200 = 3;	/* try 3 times        */
		st->l2.T203 = 10000;	/* 10000 milliseconds */
		setstack_isdnl2(st, "DCh Q.921 ");
		break;
	case B2_MODE_X75SLP:
		st->l2.flag = 0;
		test_and_set_bit(FLG_LAPB, &st->l2.flag);
		st->l2.AddressA = 0x03;
		st->l2.AddressB = 0x01;
		st->l2.maxlen = MAX_DATA_SIZE;
		st->l2.T200 = 1000;	/* 1000 milliseconds */
		st->l2.window = 7;
		st->l2.N200 = 4;	/* try 4 times       */
		st->l2.T203 = 5000;	/* 5000 milliseconds */
		setstack_isdnl2(st, "X.75 ");
		break;
	case B2_MODE_TRANS:
		setstack_transl2(st);
		break;
	default:
		int_error();
		return -EINVAL;
	}
	return 0;
}

static void
release_st_2(struct PStack *st)
{
	switch (st->l2.mode) {
	case B2_MODE_LAPD:
	case B2_MODE_X75SLP:
		releasestack_isdnl2(st);
		break;
	case B2_MODE_TRANS:
		releasestack_transl2(st);
		break;
	default:
		int_error();
	}
}

static int
init_st_3(struct PStack *st, int b3_mode)
{
	st->l3.mode = b3_mode;
	switch (b3_mode) {
	case B3_MODE_DSS1:
	case B3_MODE_1TR6:
	case B3_MODE_LEASED:
	case B3_MODE_NI1:
		setstack_l3cc(st, b3_mode);
		break;
	case B3_MODE_TRANS:
		setstack_l3trans(st);
		break;
	}
	return 0;
}

static void
release_st_3(struct PStack *st)
{
	switch (st->l3.mode) {
	case B3_MODE_DSS1:
	case B3_MODE_1TR6:
	case B3_MODE_LEASED:
	case B3_MODE_NI1:
		releasestack_l3cc(st);
		break;
	case B3_MODE_TRANS:
		releasestack_l3trans(st);
		break;
	default:
		int_error();
	}
}

int
init_st(struct Layer4 *l4, struct IsdnCardState *cs, struct StackParams *sp, 
	int bchannel)
{
	struct PStack *st;
	int ret;

	st = l4->st;
	st->l4 = l4;
	st->next = NULL;
	ret = init_st_1(st, sp->b1_mode, cs, bchannel, sp->headroom);
	if (ret) return ret;
	ret = init_st_2(st, sp->b2_mode);
	if (ret) return ret;
	ret = init_st_3(st, sp->b3_mode);
	if (ret) return ret;
	return 0;
}

void
release_st(struct PStack *st)
{
	release_st_1(st);
	release_st_2(st);
	release_st_3(st);
}
