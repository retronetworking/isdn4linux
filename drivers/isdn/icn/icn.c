/* $Id$
 *
 * ISDN low-level module for the ICN active ISDN-Card.
 *
 * Copyright 1994 by Fritz Elfert (fritz@wuemaus.franken.de)
 *
 * $Log$
 */

static char *revision = "$Revision$";

#include "icn.h"

/* release.h (Kernel-Release) is generated using uname -r in the Makefile */
#include "release.h"

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
  OUTB_P(1,ICN_MAPRAM);                           /* Enable RAM           */
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

static void
maplock_channel(int channel, int may_sched) {
  ulong flags;

  while (1) {
    save_flags(flags);
    cli();
    if ((!dev->chanlock) || (dev->channel == channel))
      break;
    restore_flags(flags);
    if (may_sched) {
      current->state = TASK_INTERRUPTIBLE;
      current->timeout = jiffies + ICN_CHANLOCK_DELAY;
      schedule();
    }
  }
  dev->chanlock++;
  restore_flags(flags);
  map_channel(channel);
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
 * This routine is called via timer-callback initiated from pollcard().
 * It schedules itself while any B-Channel is open.
 */

static void
pollbchan_work(int channel) {
  int eflag;
  int cnt;

  if (trymaplock_channel(channel)) {
    while (rbavl) {
      cnt = rbuf_l;
      if ((dev->rcvidx[channel]+cnt)>4001) {
        printk("icn: packet too long on ch%d, dropping.\n",channel+1);
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
	dev->interface.rcvcallb(dev->myid,
	  dev->channel,dev->rcvbuf[channel],dev->rcvidx[channel]);
	dev->rcvidx[channel] = 0;
      }
      if (!trymaplock_channel(channel)) break;
    }
    maprelease_channel(0);
  }
}

static void
pollbchan(unsigned long dummy) {
  if (dev->flags & ICN_FLAGS_B1ACTIVE)
    pollbchan_work(0);
  if (dev->flags & ICN_FLAGS_B1ACTIVE)
    pollbchan_work(1);
  if (dev->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE)) {
    /* schedule b-channel polling again */
    dev->flags |= ICN_FLAGS_RBTIMER;
    cli();
    del_timer(&dev->rb_timer);
    dev->rb_timer.function = pollbchan;
    dev->rb_timer.expires  = ICN_TIMER_BCREAD;
    add_timer(&dev->rb_timer);
    sti();
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
  u_char *p;
  u_char c;
  isdn_ctrl cmd;

  if (lock_channel(0)) {
    avail = msg_avail;
    for (left=avail,i=msg_o;left>0;i++,left--) {
      c = dev->shmem->comm_buffers.iopc_buf[i & 0xff];
      cli();
      *dev->msg_buf_write++ = (c==0xff)?'\n':c;
      /* No check for buffer overflow yet */
      if (dev->msg_buf_write>dev->msg_buf_end)
	dev->msg_buf_write = dev->msg_buf;
      sti();
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
	    cmd.arg     = ch;
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
	    cmd.arg     = ch;
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DCON_",5)) {
	    cmd.command = ISDN_STAT_DCONN;
	    cmd.arg     = ch;
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DDIS_",5)) {
	    cmd.command = ISDN_STAT_DHUP;
	    cmd.arg     = ch;
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"CIF",3)) {
	    cmd.command = ISDN_STAT_CINF;
	    cmd.arg     = ch;
	    strcpy(cmd.num,p+3);
	    cmd.driver  = dev->myid;
	    dev->interface.statcallb(&cmd);
	    continue;
	  }
	  if (!strncmp(p,"DCAL_I",6)) {
	    cmd.command = ISDN_STAT_ICALL;
	    cmd.driver  = dev->myid;
	    cmd.arg     = ch;
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
      cli();
      del_timer(&dev->rb_timer);
      dev->rb_timer.function = pollbchan;
      dev->rb_timer.expires  = ICN_TIMER_BCREAD;
      add_timer(&dev->rb_timer);
      sti();
    }
  /* schedule again */
  cli();
  del_timer(&dev->st_timer);
  dev->st_timer.function = pollcard;
  dev->st_timer.expires  = ICN_TIMER_DCREAD;
  add_timer(&dev->st_timer);
  sti();
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
sendbuf(int channel, u_char *buffer, int count, int user) {
  register u_char *p    = buffer;
  register u_char cnt   = 0;
  uint            total = 0;
  int             len   = count;

  /* Return error, if packet-size too large */
  if (len>4000) return -EINVAL;
  while (len) {
    maplock_channel(channel,user);
    if (sbfree) {                       /* If there is a free buffer... */
      /* set EOB-flag if fragment is the last one, set bytecount  */
      cnt = 
	(sbuf_l = 
	 (len>ICN_FRAGSIZE)?((sbuf_f=0xff),ICN_FRAGSIZE):((sbuf_f=0),len));
      if (user)
	memcpy_fromfs(sbuf_d,p,cnt);      /* copy data                    */
      else
	memcpy(sbuf_d,p,cnt);             /* copy data                    */
      sbnext;                             /* switch to next buffer        */
      maprelease_channel(0);
      p += cnt;
      len -= cnt;
      total += cnt;
    } else {
      maprelease_channel(0);
      if (user) {
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + ICN_SEND_TIMEOUT;
	schedule();
      }
    }
  }
  return (int)total;
}

/* Load the protocol-code into the transmit-buffers.
 * Always called from user-process.
 * 
 * Parameters:
 *            buffer = pointer to packet
 *            len    = size of packet
 * Return:
 *        Number of bytes transferred
 */
static int
loadproto(u_char *buffer, int len) {
  u_char *p    = buffer;
  uint  total = 0;
  uint  left  = len;
  uint  cnt;
  isdn_ctrl cmd;

  while (left) {
    switch (dev->bootstate) {
      case 0:
	dev->codeptr = (char *)dev->shmem;
	dev->codelen = 0;
	if (check_region(dev->port,ICN_PORTLEN)) {
	  printk("icn: ports 0x%03x-0x%03x in use.\n",dev->port,
		 dev->port+ICN_PORTLEN);
	  dev->bootstate = 0;
	  return -EIO;
	}
	OUTB_P(0,ICN_RUN);                             /* Reset Controler */
	OUTB_P(0,ICN_MAPRAM);                          /* Disable RAM     */
	shiftout(ICN_CFG,0x0f,3,4);                    /* Windowsize= 16k */
	shiftout(ICN_CFG,(unsigned long)dev->shmem,23,10);
				/* Set RAM-Addr.   */
	shiftout(ICN_BANK,0,3,4);                      /* Select Bank 0   */
	OUTB_P(0xff,ICN_MAPRAM);                       /* Enable  RAM     */
	dev->bootstate = 1;
	break;
      case 1:
	cnt = ((dev->codelen+len)>ICN_CODE_STAGE1)?
	  ICN_CODE_STAGE1-dev->codelen:len;
	memcpy_fromfs(dev->codeptr,p,cnt);
	dev->codeptr += cnt;
	dev->codelen += cnt;
	left -= cnt;
	total += cnt;
	p += cnt;
	if (dev->codelen >= ICN_CODE_STAGE1) {
	  dev->codelen = 0;
	  OUTB_P(0xff,ICN_RUN);                  /* Start Boot-Code      */
	  dev->timer1 = 0;
	  while (1)
	    if (dev->shmem->data_control.scns || 
		dev->shmem->data_control.scnr) {
	      if (dev->timer1 > 3) {
		dev->bootstate = 0;
		return -EIO;
	      }
	      dev->timer1++;
	      current->state = TASK_INTERRUPTIBLE;
	      current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
	      schedule();
	    } else {
	      dev->bootstate = 2;
	      break;
	    }
	}
	break;
      case 2:
	dev->timer1 = 0;
	if (sbfree) {                  /* If there is a free buffer... */
	  cnt = (left>256)?256:left;
	  memcpy_fromfs(&sbuf_l,p,cnt); /* copy data                     */
	  sbnext;                       /* switch to next buffer         */
	  p += cnt;
	  total += cnt;
	  left  -= cnt;
	  dev->codelen += cnt;
	  if (dev->codelen >= ICN_CODE_STAGE2) {
	    total += left;
	    left = 0;
	    while (1) {
	      if (cmd_o || cmd_i) {
		if (dev->timer1 > 4) {
		  dev->bootstate = 0;
		  return -EIO;
		}
		dev->timer1++;
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
		schedule();
	      } else {
		dev->bootstate = 3;
		printk("icn: Protocol loaded and running\n");
		cmd.command = ISDN_STAT_RUN;
		cmd.driver  = dev->myid;
		dev->interface.statcallb(&cmd);
		cli();
		init_timer(&dev->st_timer);
		dev->st_timer.expires  = ICN_TIMER_DCREAD;
		dev->st_timer.function = pollcard;
		add_timer(&dev->st_timer);
		sti();
		break;
	      }
	    }
	  }
	  dev->timer1 = 0;
	} else {
	  dev->timer1++;
	  if (dev->timer1 > 3) {
	    dev->bootstate = 0;
	    return -EIO;
	  }
	  current->state = TASK_INTERRUPTIBLE;
	  current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
	  schedule();
	}
	break;
      default:
	cmd.command = ISDN_STAT_RUN;
	cmd.driver  = dev->myid;
	dev->interface.statcallb(&cmd);
	cli();
	init_timer(&dev->st_timer);
	dev->st_timer.expires  = ICN_TIMER_DCREAD;
	dev->st_timer.function = pollcard;
	add_timer(&dev->st_timer);
	sti();
	total = len;
	left  = 0;
	break;
    }
  }
  return (int)total;
}

/* Read the Status-replies from the Interface */
static int
readstatus(u_char *buf, int len, int user) {
  int avail;
  int left;
  int count;
  u_char *p;

  avail = (dev->msg_buf_read>dev->msg_buf_write)?
    (uint)dev->msg_buf_end-(uint)dev->msg_buf_read+
    (uint)dev->msg_buf_write-(uint)dev->msg_buf:
    (uint)dev->msg_buf_write-(uint)dev->msg_buf_read;
  left = (avail>len)?len:avail;
  for (p=buf,count=0;left>0;p++,left--,count++) {
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
  u_char *p;
  u_char msg[0x100];
  
  if (lock_channel(0)) {
    avail = cmd_free;
    count = (len>avail)?avail:len;
    if (user)
      memcpy_fromfs(msg,buf,count);
    else
      memcpy(msg,buf,count);
    for (p=msg,pp=cmd_i,i=count;i>0;i--,p++,pp++) {
      dev->shmem->comm_buffers.pcio_buf[pp & 0xff] = (*p=='\n')?0xff:*p;
    }
    cmd_i = (cmd_i+count) & 0xff;
    release_channel();
  } else count = 0;
  return count;
}

static int
command (isdn_ctrl *c) {
  ulong a;

  switch (c->command) {
    case ISDN_CMD_IOCTL:
      switch (a = c->arg) {
	case ICN_IOCTL_SETMMIO:
	  if ((unsigned long)dev->shmem != (a & 0x0ffc000)) {
	    dev->shmem = (icn_shmem *)(a & 0x0ffc000);
	    dev->bootstate = 0;
	    printk("icn: mmio set to 0x%08lx\n",(unsigned long)dev->shmem);
	  }
	  break;
	case ICN_IOCTL_GETMMIO:
	  return (int)dev->shmem;
	case ICN_IOCTL_SETPORT:
	  if (a == 0x300 || a == 0x310 || a == 0x320 || a == 0x330
	      || a == 0x340 || a == 0x350 || a == 0x360) {
	    if (dev->port != (unsigned short)a) {
	      dev->port = (unsigned short)a;
	      dev->bootstate = 0;
	      if (check_region(dev->port,ICN_PORTLEN)) {
		printk("icn: ports 0x%03x-0x%03x in use.\n",dev->port,
		       dev->port+ICN_PORTLEN);
		dev->bootstate = 0;
		return -EINVAL;
	      }
	      printk("icn: port set to 0x%03x\n",dev->port);
	    }
	  } else
	    return -EINVAL;
	  break;
	case ICN_IOCTL_GETPORT:
	  return (int)dev->port;
	default:
	  return -EINVAL;
      }
      break;
    case ISDN_CMD_DIAL:
      break;
    case ISDN_CMD_ACCEPT:
      break;
    case ISDN_CMD_HANGUP:
      break;
    case ISDN_CMD_SETEAZ:
      break;
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

  if (!(dev = (icn_devptr)kmalloc(sizeof(icn_dev),GFP_KERNEL))) {
    printk("icn: Insufficient memory while allocating device-struct.\n");
    return -EIO;
  }
  memset((char *)dev,0,sizeof(icn_dev));
  dev->bootstate           = 4; /* Set to 0 when loadprot() is debugged */
  dev->port                = ICN_BASEADDR;
  dev->shmem               = (icn_shmem *)(ICN_MEMADDR & 0x0ffc000);
  dev->interface.channels  = 2;
  dev->interface.loadproto = loadproto;
  dev->interface.command   = command;
  dev->interface.writebuf  = sendbuf;
  dev->interface.writecmd  = writecmd;
  dev->interface.readstat  = readstatus;
  dev->msg_buf_write       = dev->msg_buf;
  dev->msg_buf_read        = dev->msg_buf;
  dev->msg_buf_end         = &dev->msg_buf[sizeof(dev->msg_buf)];
  if (!register_isdn(&dev->interface)) {
    printk("icn: Unable to register\n");
    kfree(dev);
    return -EIO;
  }
  dev->myid = dev->interface.channels;
  printk("ICN-ISDN-driver loaded, port=0x%03x mmio=0x%08x\n",dev->port,
	 (uint)dev->shmem);
  return 0;
}

void
cleanup_module( void) {
isdn_ctrl cmd;

  if (MOD_IN_USE) {
    printk("icn: device busy, remove cancelled\n");
    return;
  }
  del_timer(&dev->st_timer);
  del_timer(&dev->rb_timer);
  cmd.command = ISDN_STAT_STOP;
  cmd.driver = dev->myid;
  dev->interface.statcallb(&cmd);
  cmd.command = ISDN_STAT_UNLOAD;
  cmd.driver = dev->myid;
  dev->interface.statcallb(&cmd);
  OUTB_P(0,ICN_RUN);                              /* Reset Controler      */
  OUTB_P(0,ICN_MAPRAM);                           /* Disable RAM          */
  kfree(dev);
  printk("ICN-ISDN-driver unloaded\n");
}



















