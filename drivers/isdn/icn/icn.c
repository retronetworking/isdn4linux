/* $Id$
 *
 * ISDN low-level module for the ICN active ISDN-Card.
 *
 * Copyright 1994 by Fritz Elfert (fritz@wuemaus.franken.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log$
 * Revision 1.8  1995/03/15  12:49:44  fritz
 * Added support for SPV's
 * Splitted pollbchan_work ifor calling send-routine directly
 *
 * Revision 1.7  1995/02/20  03:48:03  fritz
 * Added support of new request_region-function.
 * Minor bugfixes.
 *
 * Revision 1.6  1995/01/31  15:48:45  fritz
 * Added Cause-Messages to be signaled to upper layers.
 * Added Revision-Info on load.
 *
 * Revision 1.5  1995/01/29  23:34:59  fritz
 * Added stopdriver() and appropriate calls.
 * Changed printk-statements to support loglevels.
 *
 * Revision 1.4  1995/01/09  07:40:46  fritz
 * Added GPL-Notice
 *
 * Revision 1.3  1995/01/04  05:15:18  fritz
 * Added undocumented "bootload-finished"-command in download-code
 * to satisfy some brain-damaged icn card-versions.
 *
 * Revision 1.2  1995/01/02  02:14:45  fritz
 * Misc Bugfixes
 *
 * Revision 1.1  1994/12/14  17:56:06  fritz
 * Initial revision
 *
 */

#include "icn.h"

/*
 * Verbose bootcode- and protocol-downloading.
 */
#undef BOOT_DEBUG

/* If defined, no bootcode- and protocol-downloading is supported and
 * you must use an external loader
 */
#undef LOADEXTERN

static char
*revision = "$Revision$";

/* Try to allocate a new buffer, link it into queue. */
static  u_char*
new_buf(pqueue **queue, int length) {
  pqueue *p;
  pqueue *q;

  if ((p = *queue)) {
    while (p) {
      q = p;
      p = (pqueue*)p->next;
    }
    p = (pqueue*)kmalloc(sizeof(pqueue)+length,GFP_ATOMIC);
    q->next = (u_char*)p;
  } else
    p = *queue = (pqueue*)kmalloc(sizeof(pqueue)+length,GFP_ATOMIC);
  if (p) {
    p->size = sizeof(pqueue)+length;
    p->length = length;
    p->next = NULL;
    p->rptr = p->buffer;
    return p->buffer;
  } else {
    return (u_char *)NULL;
  }
}

static void
free_queue(pqueue **queue) {
  pqueue *p;
  pqueue *q;

  p = *queue;
  while (p) {
    q = p;
    p = (pqueue*) p->next;
    kfree_s(q,q->size);
  }
  *queue = (pqueue *)0;
}

/* Put a value into a shift-register, highest bit first.
 * Parameters:
 *            port     = port for output (bit 0 is significant)
 *            val      = value to be output
 *            firstbit = Bit-Number of highest bit
 *            bitcount = Number of bits to output
 */
static void
shiftout (unsigned short port,
	       unsigned long val,
	       int firstbit,
	       int bitcount        ) {

  register u_char s;
  register u_char c;

  for (s=firstbit,c=bitcount;c>0;s--,c--)
    OUTB_P((u_char)((val >> s)&1)?0xff:0,port);
}

/*
 * Map Cannel0 (Bank0) or Channel1 (Bank4)
 */
static void
map_channel(int channel) {
  static u_char chan2bank[] = {0,4,8,12};

  if (channel == dev->channel) return;
  OUTB_P(0,ICN_MAPRAM);                           /* Disable RAM          */
  shiftout(ICN_BANK,chan2bank[channel],3,4);      /* Select Bank          */
  OUTB_P(0xff,ICN_MAPRAM);                        /* Enable RAM           */
  dev->channel = channel;
}

static int
lock_channel(int channel) {
  register int retval;
  ulong flags;

  save_flags(flags);
  cli();
  if (dev->channel==channel) {
    dev->chanlock++;
    retval = 1;
  } else retval = 0;
  restore_flags(flags);
  return retval;
}

static void
release_channel() {
  ulong flags;

  save_flags(flags);
  cli();
  if (dev->chanlock) dev->chanlock--;
  restore_flags(flags);
}

static int
trymaplock_channel(int channel) {
  ulong flags;

  save_flags(flags);
  cli();
  if ((!dev->chanlock) || (dev->channel == channel)) {
    dev->chanlock++;
    restore_flags(flags);
    map_channel(channel);
    return 1;
  }
  restore_flags(flags);
  return 0;
}

static void
maprelease_channel(int channel) {
  ulong flags;

  save_flags(flags);
  cli();
  if (dev->chanlock) dev->chanlock--;
  if (!dev->chanlock) map_channel(channel);
  restore_flags(flags);
}

/* Get Data from the B-Channel, assemble fragmented packets and put them
 * into receive-queue. Wake up any B-Channel-reading processes.
 * This routine is called via timer-callback from pollbchan().
 * It schedules itself while any B-Channel is open.
 */

#ifdef DEBUG_RCVCALLBACK
static int max_pending[2] = {0,0};
#endif

static void
pollbchan_receive(int channel) {
  int eflag;
  int cnt;
  int flags;
#ifdef DEBUG_RCVCALLBACK
  int rcv_pending1;
  int rcv_pending2;
  int akt_pending;
#endif

  if (trymaplock_channel(channel)) {
    while (rbavl) {
      cnt = rbuf_l;
      if ((dev->rcvidx[channel]+cnt)>4000) {
        printk(KERN_WARNING "icn: bogus packet on ch%d, dropping.\n",
	       channel+1);
        dev->rcvidx[channel] = 0;
        eflag = 0;
      } else {
        memcpy(&dev->rcvbuf[channel][dev->rcvidx[channel]],rbuf_d,cnt);
        dev->rcvidx[channel] += cnt;
        eflag = rbuf_f;
      }
      rbnext;
      maprelease_channel(0);
      if (!eflag) {
	save_flags(flags);
	cli();
#ifdef DEBUG_RCVCALLBACK
	rcv_pending1 =
	  (dev->shmem->data_control.ecnr>dev->shmem->data_control.ecns)?
	  0xf-dev->shmem->data_control.ecnr+dev->shmem->data_control.ecns:
	  dev->shmem->data_control.ecns-dev->shmem->data_control.ecnr;
#endif
	dev->interface.rcvcallb(dev->myid,channel,dev->rcvbuf[channel],
				dev->rcvidx[channel]);
	dev->rcvidx[channel] = 0;
#ifdef DEBUG_RCVCALLBACK
	rcv_pending2 =
	  (dev->shmem->data_control.ecnr>dev->shmem->data_control.ecns)?
	  0xf-dev->shmem->data_control.ecnr+dev->shmem->data_control.ecns:
	  dev->shmem->data_control.ecns-dev->shmem->data_control.ecnr;
	akt_pending = rcv_pending2 - rcv_pending1;
	if (akt_pending>max_pending[channel]) {
	  max_pending[channel]=akt_pending;
	  printk("ICN_DEBUG: pend: %d %d\n",max_pending[0],max_pending[1]);
	}
#endif
	restore_flags(flags);
      }
      if (!trymaplock_channel(channel)) break;
    }
    maprelease_channel(0);
  }
}

/* Send data-packet to B-Channel, split it up into fragments of
 * ICN_FRAGSIZE length. If last fragment is sent out, signal
 * success to upper layers via statcallb with ISDN_STAT_BSENT argument.
 * This routine is called via timer-callback from pollbchan() or
 * directly from sendbuf().
 */

static void
pollbchan_send(int channel) {
  int eflag = 0;
  int cnt;
  int left;
  int flags;
  pqueue *p;
  isdn_ctrl cmd;

  if (!dev->sndcount[channel])
    return;
  if (trymaplock_channel(channel)) {
    while (sbfree && dev->sndcount[channel]) {
      left = dev->spqueue[channel]->length;
      cnt = 
	(sbuf_l = 
	 (left>ICN_FRAGSIZE)?((sbuf_f=0xff),ICN_FRAGSIZE):((sbuf_f=0),left));
      memcpy(sbuf_d,dev->spqueue[channel]->rptr,cnt);
      sbnext;                             /* switch to next buffer        */
      maprelease_channel(0);
      dev->spqueue[channel]->rptr += cnt;
      eflag = ((dev->spqueue[channel]->length -= cnt) == 0);
      save_flags(flags);
      cli();
      p = dev->spqueue[channel];
      dev->sndcount[channel] -= cnt;
      if (eflag)
	dev->spqueue[channel] = (pqueue*)dev->spqueue[channel]->next;
      restore_flags(flags);
      if (eflag)
	kfree_s(p,p->size);
      if (!trymaplock_channel(channel)) break;
    }
    maprelease_channel(0);
  }
  if (eflag) {
    cmd.command = ISDN_STAT_BSENT;
    cmd.driver  = dev->myid;
    cmd.arg     = channel;
    dev->interface.statcallb(&cmd);
  }
}

/* Send/Receive Data to/from the B-Channel.
 * This routine is called via timer-callback.
 * It schedules itself while any B-Channel is open.
 */

static void
pollbchan(unsigned long dummy) {
  unsigned long flags;

  dev->flags |= ICN_FLAGS_RBTIMER;
  if (dev->flags & ICN_FLAGS_B1ACTIVE)
    pollbchan_receive(0);
    pollbchan_send(0);
  if (dev->flags & ICN_FLAGS_B2ACTIVE)
    pollbchan_receive(1);
    pollbchan_send(1);
  if (dev->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE)) {
    /* schedule b-channel polling again */
    save_flags(flags);
    cli();
    del_timer(&dev->rb_timer);
    dev->rb_timer.function = pollbchan;
    dev->rb_timer.expires  = ICN_TIMER_BCREAD;
    add_timer(&dev->rb_timer);
    restore_flags(flags);
  } else dev->flags &= ~ICN_FLAGS_RBTIMER;
}

/*
 * Check Statusqueue-Pointer from isdn-card.
 * If there are new status-replies from the interface, check
 * them against B-Channel-connects/disconnects and set flags arrcordingly.
 * Wake-Up any processes, who are reading the status-device.
 * If there are B-Channels open, initiate a timer-callback to
 * pollbchan().
 * This routine is called periodically via timer.
 */

static void
pollcard(unsigned long dummy) {
  int avail = 0;
  int dflag = 0;
  int i;
  int left;
  int ch;
  int flags;
  int avsub;
  u_char *p;
  u_char c;
  isdn_ctrl cmd;

  if (lock_channel(0)) {
    avail = msg_avail;
    avsub = 0;
    for (left=avail,i=msg_o;left>0;i++,left--) {
      c = dev->shmem->comm_buffers.iopc_buf[i & 0xff];
      save_flags(flags);
      cli();
      *dev->msg_buf_write++ = (c==0xff)?'\n':c;
      /* No checks for buffer overflow for raw-status-device*/
      if (dev->msg_buf_write>dev->msg_buf_end)
	dev->msg_buf_write = dev->msg_buf;
      restore_flags(flags);
      if (c==0xff) {
	dev->imsg[dev->iptr] = 0;
	dev->iptr=0;
	if (dev->imsg[0]=='0' && dev->imsg[1]>='0' &&
	    dev->imsg[1]<='2' && dev->imsg[2]==';') {
	  ch = dev->imsg[1]-'0';
	  p = &dev->imsg[3];
	  if (!strncmp(p,"BCON_",5)) {
	    switch (ch) {
	      case 1:
		dev->flags |= ICN_FLAGS_B1ACTIVE;
		break;
	      case 2:
		dev->flags |= ICN_FLAGS_B2ACTIVE;
		break;
	    }
	    cmd.command = ISDN_STAT_BCONN;
	    cmd.driver  = dev->myid;
	    cmd.arg     = ch-1;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"TEI OK",6)) {
	    cmd.command = ISDN_STAT_RUN;
	    cmd.driver  = dev->myid;
	    cmd.arg     = ch-1;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"BDIS_",5)) {
	    switch (ch) {
	      case 1:
		dev->flags &= ~ICN_FLAGS_B1ACTIVE;
		dflag |= 1;
		break;
	      case 2:
		dev->flags &= ~ICN_FLAGS_B2ACTIVE;
		dflag |= 2;
		break;
	    }
	    cmd.command = ISDN_STAT_BHUP;
	    cmd.arg     = ch-1;
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DCON_",5)) {
	    cmd.command = ISDN_STAT_DCONN;
	    cmd.arg     = ch-1;
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DDIS_",5)) {
	    cmd.command = ISDN_STAT_DHUP;
	    cmd.arg     = ch-1;
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"CIF",3)) {
	    cmd.command = ISDN_STAT_CINF;
	    cmd.arg     = ch-1;
	    strcpy(cmd.num,p+3);
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"CAU",3)) {
	    cmd.command = ISDN_STAT_CAUSE;
	    cmd.arg     = ch-1;
	    strcpy(cmd.num,p+3);
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DCAL_I",6)) {
	    cmd.command = ISDN_STAT_ICALL;
	    cmd.driver  = dev->myid;
	    cmd.arg     = ch-1;
	    strcpy(cmd.num,p+6);
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DSCA_I",6)) {
	    cmd.command = ISDN_STAT_ICALL;
	    cmd.driver  = dev->myid;
	    cmd.arg     = ch-1;
	    strcpy(cmd.num,p+6);
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"NO D-CHAN",9)) {
	    cmd.command = ISDN_STAT_NODCH;
	    cmd.driver  = dev->myid;
	    cmd.arg     = ch-1;
	    strcpy(cmd.num,p+6);
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	}
      } else {
	dev->imsg[dev->iptr] = c;
	if (dev->iptr<39) dev->iptr++;
      }
    }
    msg_o = (msg_o+avail) & 0xff;
    release_channel();
  }
  if (avail) {
    cmd.command = ISDN_STAT_STAVAIL;
    cmd.driver  = dev->myid;
    cmd.arg     = avail;
    dev->interface.statcallb(&cmd);
  }
  if (dflag & 1)
    dev->interface.rcvcallb(dev->myid,0,dev->rcvbuf[0],0);
  if (dflag & 2)
    dev->interface.rcvcallb(dev->myid,1,dev->rcvbuf[1],0);
  if (dev->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE))
    if (!(dev->flags & ICN_FLAGS_RBTIMER)) {
      /* schedule b-channel polling */
      dev->flags |= ICN_FLAGS_RBTIMER;
      save_flags(flags);
      cli();
      del_timer(&dev->rb_timer);
      dev->rb_timer.function = pollbchan;
      dev->rb_timer.expires  = ICN_TIMER_BCREAD;
      add_timer(&dev->rb_timer);
      restore_flags(flags);
    }
  /* schedule again */
  save_flags(flags);
  cli();
  del_timer(&dev->st_timer);
  dev->st_timer.function = pollcard;
  dev->st_timer.expires  = ICN_TIMER_DCREAD;
  add_timer(&dev->st_timer);
  restore_flags(flags);
}

/* Send a packet to the transmit-buffers, handle fragmentation if necessary.
 * Parameters:
 *            channel = Number of B-channel
 *            buffer  = pointer to packet
 *            len     = size of packet (max 4000)
 *            dev     = pointer to device-struct
 *            user    = 1 = call from userproc, 0 = call from kernel
 * Return:
 *        Number of bytes transferred, -E??? on error
 */
static int
sendbuf(int channel, u_char *buffer, int len, int user) {
  register u_char *p;
  int flags;

  if (len>4000) return -EINVAL;
  if (len) {
    if (dev->sndcount[channel] > ICN_MAX_SQUEUE)
      return 0;
    save_flags(flags);
    cli();
    p = new_buf(&dev->spqueue[channel],len);
    if (!p) {
      restore_flags(flags);
      return 0;
    }
    if (user) {
      memcpy_fromfs(p,buffer,len);
    } else {
      memcpy(p,buffer,len);
    }
    dev->sndcount[channel] += len;
    pollbchan_send(channel);
    restore_flags(flags);
  }
  return len;
}

#ifndef LOADEXTERN
/* Load the boot-code into the interface-card's memory and start it.
 * Always called from user-process.
 * 
 * Parameters:
 *            buffer = pointer to packet
 * Return:
 *        0 if successfully loaded
 */

static int
loadboot(u_char *buffer) {
  int timer;
  ulong flags;

  save_flags(flags);
  cli();
  if (check_region(dev->port,ICN_PORTLEN)) {
    printk(KERN_WARNING "icn: ports 0x%03x-0x%03x in use.\n",dev->port,
	   dev->port+ICN_PORTLEN);
    return -EBUSY;
  }
  request_region(dev->port,ICN_PORTLEN,"icn-isdn");
  dev->rvalid = 1;
  restore_flags(flags);
  OUTB_P(0,ICN_RUN);                                 /* Reset Controler */
  OUTB_P(0,ICN_MAPRAM);                              /* Disable RAM     */
  shiftout(ICN_CFG,0x0f,3,4);                        /* Windowsize= 16k */
  shiftout(ICN_CFG,(unsigned long)dev->shmem,23,10); /* Set RAM-Addr.   */
  shiftout(ICN_BANK,0,3,4);                          /* Select Bank 0   */
  OUTB_P(0xff,ICN_MAPRAM);                           /* Enable  RAM     */
  memcpy_fromfs(dev->shmem,buffer,ICN_CODE_STAGE1);  /* Copy code       */
  OUTB_P(0xff,ICN_RUN);                              /* Start Boot-Code */
  timer = 0;
  while (1) {
#ifdef BOOT_DEBUG
    printk(KERN_DEBUG "Loader?\n");
#endif
    if (dev->shmem->data_control.scns || 
	dev->shmem->data_control.scnr) {
      if (timer++ > 5) {
	printk(KERN_WARNING "icn: Boot-Loader timed out.\n");
	return -EIO;
      }
#ifdef BOOT_DEBUG
      printk(KERN_DEBUG "Loader TO?\n");
#endif
      current->state = TASK_INTERRUPTIBLE;
      current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
      schedule();
    } else {
#ifdef BOOT_DEBUG
      printk(KERN_DEBUG "Loader OK\n");
#endif
      return 0;
    }
  }
}

static int
loadproto(u_char *buffer) {
  register u_char *p = buffer;
  uint  left  = ICN_CODE_STAGE2;
  uint  cnt;
  int   timer;
  unsigned long flags;

  timer = 0;
  while (left) {
    if (sbfree) {                   /* If there is a free buffer...  */
      cnt = MIN(256,left);
      memcpy_fromfs(&sbuf_l,p,cnt); /* copy data                     */
      sbnext;                       /* switch to next buffer         */
      p += cnt;
      left  -= cnt;
      timer = 0;
    } else {
#ifdef BOOT_DEBUG
      printk(KERN_DEBUG "boot 2 !sbfree\n");
#endif
      if (timer++ > 5)
	return -EIO;
      current->state = TASK_INTERRUPTIBLE;
      current->timeout = jiffies + 10;
      schedule();
    }
  }
  sbuf_n = 0x20;
  timer = 0;
  while (1) {
    if (cmd_o || cmd_i) {
#ifdef BOOT_DEBUG
      printk(KERN_DEBUG "Proto?\n");
#endif
      if (timer++ > 5) {
	printk(KERN_WARNING "icn: Protocol timed out.\n");
#ifdef BOOT_DEBUG
	printk(KERN_DEBUG "Proto TO!\n");
#endif
	return -EIO;
      }
#ifdef BOOT_DEBUG
      printk(KERN_DEBUG "Proto TO?\n");
#endif
      current->state = TASK_INTERRUPTIBLE;
      current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
      schedule();
    } else {
      printk(KERN_INFO "icn: Protocol loaded and running\n");
      save_flags(flags);
      cli();
      init_timer(&dev->st_timer);
      dev->st_timer.expires  = ICN_TIMER_DCREAD;
      dev->st_timer.function = pollcard;
      add_timer(&dev->st_timer);
      restore_flags(flags);
      return 0;
    }
  }
}
#endif /* !LOADEXTERN */

/* Read the Status-replies from the Interface */
static int
readstatus(u_char *buf, int len, int user) {
  int count;
  u_char *p;

  for (p=buf,count=0;count<len;p++,count++) {
    if (user)
      put_fs_byte(*dev->msg_buf_read++,p);
    else
      *p = *dev->msg_buf_read++;
    if (dev->msg_buf_read>dev->msg_buf_end)
      dev->msg_buf_read = dev->msg_buf;
  }
  return count;
}

/* Put command-strings into the command-queue of the Interface */
static int
writecmd (u_char *buf, int len, int user) {
  int avail;
  int pp;
  int i;
  int count;
  int ocount;
  unsigned long flags;
  u_char *p;
  isdn_ctrl cmd;
  u_char msg[0x100];
  
  if (lock_channel(0)) {
    avail = cmd_free;
    count = MIN(avail,len);
    if (user)
      memcpy_fromfs(msg,buf,count);
    else
      memcpy(msg,buf,count);
    save_flags(flags);
    cli();
    ocount = 1;
    *dev->msg_buf_write++ = '>';
    if (dev->msg_buf_write>dev->msg_buf_end)
      dev->msg_buf_write = dev->msg_buf;
    for (p=msg,pp=cmd_i,i=count;i>0;i--,p++,pp++) {
      dev->shmem->comm_buffers.pcio_buf[pp & 0xff] = (*p=='\n')?0xff:*p;
      *dev->msg_buf_write++ = *p;
      if ((*p == '\n') && (i>1)) {
	*dev->msg_buf_write++ = '>';
	if (dev->msg_buf_write>dev->msg_buf_end)
	  dev->msg_buf_write = dev->msg_buf;
	ocount++;
      }
      /* No checks for buffer overflow of raw-status-device*/
      if (dev->msg_buf_write>dev->msg_buf_end)
	dev->msg_buf_write = dev->msg_buf;
      ocount++;
    }
    restore_flags(flags);
    cmd.command = ISDN_STAT_STAVAIL;
    cmd.driver  = dev->myid;
    cmd.arg     = ocount;
    dev->interface.statcallb(&cmd);
    cmd_i = (cmd_i+count) & 0xff;
    release_channel();
  } else count = 0;
  return count;
}

static void
stopdriver(void) {
  unsigned long flags;
  isdn_ctrl cmd;

  save_flags(flags);
  cli();
  del_timer(&dev->st_timer);
  del_timer(&dev->rb_timer);
  cmd.command = ISDN_STAT_STOP;
  cmd.driver = dev->myid;
  dev->interface.statcallb(&cmd);
  restore_flags(flags);
}

static int
command (isdn_ctrl *c) {
  ulong a;
  ulong flags;
  int   i;
  static char cbuf[60];

  switch (c->command) {
    case ISDN_CMD_IOCTL:
      memcpy(&a,c->num,sizeof(unsigned long));
      switch (c->arg) {
	case ICN_IOCTL_SETMMIO:
	  if ((unsigned long)dev->shmem != (a & 0x0ffc000)) {
	    stopdriver();
	    dev->shmem = (icn_shmem *)(a & 0x0ffc000);
	    printk(KERN_INFO "icn: mmio set to 0x%08lx\n",
		   (unsigned long)dev->shmem);
	  }
	  break;
	case ICN_IOCTL_GETMMIO:
	  return (int)dev->shmem;
	case ICN_IOCTL_SETPORT:
	  if (a == 0x300 || a == 0x310 || a == 0x320 || a == 0x330
	      || a == 0x340 || a == 0x350 || a == 0x360) {
	    if (dev->port != (unsigned short)a) {
	      if (check_region((unsigned short)a,ICN_PORTLEN)) {
		printk(KERN_WARNING "icn: ports 0x%03x-0x%03x in use.\n",
		       (int)a,(int)a+ICN_PORTLEN);
		return -EINVAL;
	      }
	      stopdriver();
	      save_flags(flags);
	      cli();
	      if (dev->rvalid)
		release_region(dev->port,ICN_PORTLEN);
	      dev->port = (unsigned short)a;
	      dev->rvalid = 0;
	      restore_flags(flags);
	      printk(KERN_INFO "icn: port set to 0x%03x\n",dev->port);
	    }
	  } else
	    return -EINVAL;
	  break;
	case ICN_IOCTL_GETPORT:
	  return (int)dev->port;
#ifndef LOADEXTERN
	case ICN_IOCTL_LOADBOOT:
	  stopdriver();
	  return(loadboot((u_char*)a));
	case ICN_IOCTL_LOADPROTO:
	  stopdriver();
	  return(loadproto((u_char*)a));
#endif
	default:
	  return -EINVAL;
      }
      break;
    case ISDN_CMD_DIAL:
      if ((c->arg & 255)<ICN_BCH) {
	char *p = c->num;
	a = c->arg;
	if (*p == 's' || *p == 'S') {
	  /* Dial for SPV */
	  p++;
	  sprintf(cbuf,"%02d;DSCA_R%s,07,00,%d\n",(int)(a&255)+1,p,
	  (int)(a>>8));
	} else
	  /* Normal Dial */
	  sprintf(cbuf,"%02d;DCAL_R%s,07,00,%d\n",(int)(a&255)+1,p,
	  (int)(a>>8));
	i = writecmd(cbuf,strlen(cbuf),0);
      }
      break;
    case ISDN_CMD_ACCEPTD:
      if (c->arg<ICN_BCH) {
	a = c->arg+1;
	sprintf(cbuf,"%02d;DCON_R\n",(int)a);
	i = writecmd(cbuf,strlen(cbuf),0);
      }
      break;
    case ISDN_CMD_ACCEPTB:
      if (c->arg<ICN_BCH) {
	a = c->arg+1;
	sprintf(cbuf,"%02d;BCON_R\n",(int)a);
	i = writecmd(cbuf,strlen(cbuf),0);
      }
      break;
    case ISDN_CMD_HANGUP:
      if (c->arg<ICN_BCH) {
	a = c->arg+1;
	sprintf(cbuf,"%02d;BDIS_R\n%02d;DDIS_R\n",(int)a,(int)a);
	i = writecmd(cbuf,strlen(cbuf),0);
      }
      break;
    case ISDN_CMD_SETEAZ:
      if (c->arg<ICN_BCH) {
	a = c->arg+1;
	sprintf(cbuf,"%02d;EAZ%s\n",(int)a,c->num);
	i = writecmd(cbuf,strlen(cbuf),0);
      }
      break;
    case ISDN_CMD_CLREAZ:
      if (c->arg<ICN_BCH) {
	a = c->arg+1;
	sprintf(cbuf,"%02d;EAZC\n",(int)a);
	i = writecmd(cbuf,strlen(cbuf),0);
      }
      break;
    case ISDN_CMD_SETL2:
      if ((c->arg & 255)<ICN_BCH) {
	a = c->arg;
	switch (a >> 8) {
	  case ISDN_PROTO_L2_X75I:
	    sprintf(cbuf,"%02d;BX75\n",(int)(a&255)+1);
	    break;
#ifdef HASHDLC
	  case ISDN_PROTO_L2_HDLC:
	    sprintf(cbuf,"%02d;BTRA\n",(int)(a&255)+1);
	    break;
#endif
	  default:
	    return -EINVAL;
	}
	i = writecmd(cbuf,strlen(cbuf),0);
	dev->l2_proto[a&255] = (a >> 8);
      }
      break;
    case ISDN_CMD_GETL2:
      if ((c->arg & 255)<ICN_BCH)
	return dev->l2_proto[c->arg&255];
      else
	return -ENODEV;
    case ISDN_CMD_SETL3:
      return 0;
    case ISDN_CMD_GETL3:
      if ((c->arg & 255)<ICN_BCH)
	return ISDN_PROTO_L3_TRANS;
      else
	return -ENODEV;
    case ISDN_CMD_GETEAZ:
      break;
    case ISDN_CMD_SETSIL:
      break;
    case ISDN_CMD_GETSIL:
      break;
    case ISDN_CMD_LOCK:
      MOD_INC_USE_COUNT;
      break;
    case ISDN_CMD_UNLOCK:
      MOD_DEC_USE_COUNT;
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

int
init_module( void) {
#ifdef LOADEXTERN
  unsigned long flags;
#endif
  char *p;
  char rev[10];

  if (!(dev = (icn_devptr)kmalloc(sizeof(icn_dev),GFP_KERNEL))) {
    printk(KERN_WARNING "icn: Could not allocate device-struct.\n");
    return -EIO;
  }
  memset((char *)dev,0,sizeof(icn_dev));
  dev->port                = portbase;
  dev->shmem               = (icn_shmem *)(membase & 0x0ffc000);
  dev->interface.channels  = ICN_BCH;
  dev->interface.command   = command;
  dev->interface.writebuf  = sendbuf;
  dev->interface.writecmd  = writecmd;
  dev->interface.readstat  = readstatus;
  dev->interface.features  = ISDN_FEATURE_L2_X75I |
#ifdef HASHDLC
                             ISDN_FEATURE_L2_HDLC |
#endif
                             ISDN_FEATURE_L3_TRANS ;
  dev->msg_buf_write       = dev->msg_buf;
  dev->msg_buf_read        = dev->msg_buf;
  dev->msg_buf_end         = &dev->msg_buf[sizeof(dev->msg_buf)-1];
  memset((char *)dev->l2_proto,ISDN_PROTO_L2_X75I,sizeof(dev->l2_proto));
  if (!register_isdn(&dev->interface)) {
    printk(KERN_WARNING "icn: Unable to register\n");
    kfree(dev);
    return -EIO;
  }
  dev->myid = dev->interface.channels;
  if ((p = strchr(revision,':'))) {
    strcpy(rev,p+1);
    p = strchr(rev,'$');
    *p = 0;
  } else
    strcpy(rev," ??? ");
  printk(KERN_INFO "ICN-ISDN-driver Rev%sport=0x%03x mmio=0x%08x\n",rev,
	 dev->port,(uint)dev->shmem);
#ifdef LOADEXTERN
  save_flags(flags);
  cli();
  init_timer(&dev->st_timer);
  dev->st_timer.expires  = ICN_TIMER_DCREAD;
  dev->st_timer.function = pollcard;
  add_timer(&dev->st_timer);
  restore_flags(flags);
#endif
  return 0;
}

void
cleanup_module( void) {
  isdn_ctrl cmd;
  int i;

  if (MOD_IN_USE) {
    printk(KERN_WARNING "icn: device busy, remove cancelled\n");
    return;
  }
  stopdriver();
  cmd.command = ISDN_STAT_UNLOAD;
  cmd.driver = dev->myid;
  dev->interface.statcallb(&cmd);
  if (dev->rvalid) {
    OUTB_P(0,ICN_RUN);                              /* Reset Controler      */
    OUTB_P(0,ICN_MAPRAM);                           /* Disable RAM          */
    release_region(dev->port,ICN_PORTLEN);
  }
  for (i=0;i<ICN_BCH;i++)
    free_queue(&dev->spqueue[1]);
  kfree(dev);
  printk(KERN_INFO "ICN-ISDN-driver unloaded\n");
}










