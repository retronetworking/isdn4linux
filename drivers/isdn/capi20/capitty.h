/* -*- linux-c -*-  */

#include <linux/skbuff.h>
#include "capi.h"

#define	CAPI_TTY_ATTACH	  _IO  ('C',0x20)
#define	CAPI_TTY_SET_MSR  _IOW ('C',0x21, unsigned long)
#define	CAPI_TTY_SBIT_MSR _IOW ('C',0x22, unsigned long)
#define	CAPI_TTY_CBIT_MSR _IOW ('C',0x23, unsigned long)
#define	CAPI_TTY_GET_MCR  _IOR ('C',0x24, unsigned long)

#define CTTY_CHECK_CAR  0
#define CTTY_CTS_FLOW   1
#define CTTY_HANGING_UP 2

#define CAPITTY_MAJOR 43
#define CAPITTY_START 128
#define CAPITTY_COUNT 4 

#define MAX_FRAME_SIZE 2048

//#define CAPITTY_WRITE        0x01

struct ctty_dev {
	int               line;
	struct syncdev_r  rdev;
	struct syncdev_w  wdev;

	struct tty_struct *tty;
	wait_queue_head_t open_wait;
	int               tty_count;
	unsigned long     flags;
	unsigned int      MCR, MSR;
	struct capincci   ncci;
};

// ctty_dev will be constructed on open of /dev/capitty
// and destructed on close of /dev/capitty

// with ioctl CAPITTY_ATTACH it can be associated with a
// particular tty slot (i.e. set ->line and enter it into
// ctty_dev_table)

// 

extern struct ctty_dev *ctty_dev_table[CAPITTY_COUNT];

extern int capitty_recv(struct capincci *np, struct sk_buff *skb);
extern int capitty_tty_init(void);
extern void capitty_tty_exit(void);

void capitty_run_write_queue(struct ctty_dev *d);
