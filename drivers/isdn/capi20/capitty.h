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

/*
 * ctty_dev is constructed on open of /dev/capitty
 * and destructed on release of /dev/capitty
 *
 * With the ioctl CAPITTY_ATTACH it can be associated with a
 * particular tty slot (i.e. set ->line and enter it into
 * ctty_dev_table)
 *
 * Only then, it'll be possible to open the corresponding
 * tty device, currently /dev/ttyIx where x = line + CAPITTY_START 
 *
 * When the control connection to /dev/capitty is closed,
 * the corresponding tty will be hungup.
 *
 * Data flow works like the following:
 *
 * Normal mode:
 *
 * tty receive path: Frames are written to the corresponding /dev/capitty
 * and queued. If the queue length exceeds a certain size (SYNCDEV_QUEUE_LEN)
 * the writing process is blocked / -EAGAIN is returned
 * After queueing, capitty_run_queue is called, which tries to push
 * pending frames to the tty layer. If this is not possible because
 * the tty receive path is throttled, the frames stay on the queue.
 * if the tty is untrottled, capitty_run_queue takes care of frames
 * still queue for receive and pushes them to the tty layer.
 *
 * tty write path: Written frames are queued on a queue for
 * reading from /dev/capitty. When the queue exceeds a certain length
 * (SYNCDEV_QUEUE_LEN), flow control is triggered (virtual CTS is cleared)
 * and no more frames are taken.
 * Frames are dequeued when reading from /dev/capitty, virtual CTS is set when
 * the queue has room for additional frames.
 * 
 * Direct data connection mode:
 *
 * In this case, /dev/capitty is not involved, but data are exchanged
 * directly between the tty and CAPI.
 *
 * tty receive path: Frames from CAPI are queued on the receive queue,
 * and capitty_run_write_queue is called, so that these frames are
 * pushed to the tty layer if not throttled. We don't need to throttle
 * the CAPI side of things, because packets only get acknowledged to CAPI
 * when they are accepted by the tty layer, and CAPI won't send more than 8
 * unacknowledged frames.
 * 
 * tty write path: Written frames are directly pushed to CAPI.
 * When the CAPI send queue is full, it will call back into 
 * capitty_recv_stop_queue, which will clear virtual CTS, When
 * the queue becomes available again, capitty_recv_wake_queue will
 * be called back and the tty will start accepting further data.
 *
 *
 * The basic states for a struct ctty_dev are:
 * allocated: /dev/capitty is open, but not connected to a particular tty
 *            yet.
 *            (d->line == -1, not entered in ctty_dev_table)_
 * bound to tty: now it's possible to open the corresponding tty
 *            (d->line >= 0, ctty_dev_table[line] = ctty)
 * connected: data exchange between tty and /dev/capitty work as
 *            described in Normal mode above
 *            (d->tty != NULL, tty->driver_data = d) 
 * direct mode: data exchange happens directly between tty and CAPI
 *            (d->ncci.ncci != 0)
 */

extern struct ctty_dev *ctty_dev_table[CAPITTY_COUNT];

extern int capitty_tty_init(void);
extern void capitty_tty_exit(void);

extern void capitty_run_write_queue(struct ctty_dev *d);
