/* $Id$

 * Main header for the Linux ISDN subsystem (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 */

#ifndef isdn_h
#define isdn_h

#include <linux/isdn_compat.h>
#include <linux/config.h>
#include <linux/ioctl.h>

#define ISDN_TTY_MAJOR    43
#define ISDN_TTYAUX_MAJOR 44
#define ISDN_MAJOR        45

/* The minor-devicenumbers for Channel 0 and 1 are used as arguments for
 * physical Channel-Mapping, so they MUST NOT be changed without changing
 * the correspondent code in isdn.c
 */

#ifdef CONFIG_COBALT_MICRO_SERVER
/* Save memory */
#define ISDN_MAX_DRIVERS    2
#define ISDN_MAX_CHANNELS   8
#else
#define ISDN_MAX_DRIVERS    32
#define ISDN_MAX_CHANNELS   64
#endif
#define ISDN_MINOR_B        0
#define ISDN_MINOR_BMAX     (ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_CTRL     64
#define ISDN_MINOR_CTRLMAX  (64 + (ISDN_MAX_CHANNELS-1))
#define ISDN_MINOR_PPP      128
#define ISDN_MINOR_PPPMAX   (128 + (ISDN_MAX_CHANNELS-1))
#define ISDN_MINOR_STATUS   255

#ifndef CONFIG_ISDN_WITH_ABC
#undef CONFIG_ISDN_WITH_ABC_CALLB
#undef CONFIG_ISDN_WITH_ABC_UDP_CHECK
#undef CONFIG_ISDN_WITH_ABC_UDP_CHECK_HANGUP
#undef CONFIG_ISDN_WITH_ABC_UDP_CHECK_DIAL
#undef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
#undef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
#undef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
#undef CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR
#undef CONFIG_ISDN_WITH_ABC_RCV_NO_HUPTIMER
#undef CONFIG_ISDN_WITH_ABC_ICALL_BIND
#undef CONFIG_ISDN_WITH_ABC_CH_EXTINUSE
#undef CONFIG_ISDN_WITH_ABC_CONN_ERROR
#undef CONFIG_ISDN_WITH_ABC_RAWIPCOMPRESS
#undef CONFIG_ISDN_WITH_ABC_IPV4_RW_SOCKADDR 
#undef CONFIG_ISDN_WITH_ABC_IPV4_RWUDP_SOCKADDR 
#else
#include <linux/isdn_dwabc.h>

volatile u_long dwsjiffies;
#define ISDN_DW_ABC_FLAG_NO_TCP_KEEPALIVE	0x00000001L
#define ISDN_DW_ABC_FLAG_NO_UDP_CHECK		0x00000002L
#define ISDN_DW_ABC_FLAG_NO_UDP_HANGUP		0x00000004L
#define ISDN_DW_ABC_FLAG_NO_UDP_DIAL		0x00000008L
#define ISDN_DW_ABC_FLAG_DYNADDR			0x00000010L
#define ISDN_DW_ABC_FLAG_RCV_NO_HUPTIMER	0x00000020L
#define ISDN_DW_ABC_FLAG_NO_CH_EXTINUSE		0x00000040L
#define ISDN_DW_ABC_FLAG_NO_CONN_ERROR		0x00000080L
#define ISDN_DW_ABC_FLAG_BSD_COMPRESS		0x00000100L
#define ISDN_DW_ABC_FLAG_NO_LCR				0x00000200L
#define ISDN_DW_ABC_FLAG_RW_SOCKADDR		0x00000400L
#define ISDN_DW_ABC_FLAG_RWUDP_SOCKADDR		0x00000800L
#define ISDN_DW_ABC_FLAG_LEASED_LINE		0x00001000L

#define ISDN_DW_ABC_IFFLAG_NODCHAN		0x00000001L
#define ISDN_DW_ABC_IFFLAG_BSDAKTIV		0x00000002L

#define ISDN_DW_ABC_BITLOCK_SEND		0
#define ISDN_DW_ABC_BITLOCK_RECEIVE		1

#define ISDN_DW_ABC_MAX_CH_P_RIVER		(32)
#endif


/* New ioctl-codes */
#define IIOCNETAIF  _IO('I',1)
#define IIOCNETDIF  _IO('I',2)
#define IIOCNETSCF  _IO('I',3)
#define IIOCNETGCF  _IO('I',4)
#define IIOCNETANM  _IO('I',5)
#define IIOCNETDNM  _IO('I',6)
#define IIOCNETGNM  _IO('I',7)
#define IIOCGETSET  _IO('I',8) /* no longer supported */
#define IIOCSETSET  _IO('I',9) /* no longer supported */
#define IIOCSETVER  _IO('I',10)
#define IIOCNETHUP  _IO('I',11)
#define IIOCSETGST  _IO('I',12)
#define IIOCSETBRJ  _IO('I',13)
#define IIOCSIGPRF  _IO('I',14)
#define IIOCGETPRF  _IO('I',15)
#define IIOCSETPRF  _IO('I',16)
#define IIOCGETMAP  _IO('I',17)
#define IIOCSETMAP  _IO('I',18)
#define IIOCNETASL  _IO('I',19)
#define IIOCNETDIL  _IO('I',20)
#define IIOCGETCPS  _IO('I',21)
#define IIOCGETDVR  _IO('I',22)
#define IIOCNETLCR  _IO('I',23) /* dwabc ioctl for LCR from isdnlog */
#define IIOCNETDWRSET  _IO('I',24) /* dwabc ioctl to reset abc-values to default on a net-interface */

#define IIOCNETALN  _IO('I',32)
#define IIOCNETDLN  _IO('I',33)

#define IIOCNETGPN  _IO('I',34)

#define IIOCDBGVAR  _IO('I',127)

#define IIOCDRVCTL  _IO('I',128)

/* Packet encapsulations for net-interfaces */
#define ISDN_NET_ENCAP_ETHER      0
#define ISDN_NET_ENCAP_RAWIP      1
#define ISDN_NET_ENCAP_IPTYP      2
#define ISDN_NET_ENCAP_CISCOHDLC  3 /* Without SLARP and keepalive */
#define ISDN_NET_ENCAP_SYNCPPP    4
#define ISDN_NET_ENCAP_UIHDLC     5
#define ISDN_NET_ENCAP_CISCOHDLCK 6 /* With SLARP and keepalive    */
#define ISDN_NET_ENCAP_X25IFACE   7 /* Documentation/networking/x25-iface.txt*/
#define ISDN_NET_ENCAP_MAX_ENCAP  ISDN_NET_ENCAP_X25IFACE
/* Facility which currently uses an ISDN-channel */
#define ISDN_USAGE_NONE       0
#define ISDN_USAGE_RAW        1
#define ISDN_USAGE_MODEM      2
#define ISDN_USAGE_NET        3
#define ISDN_USAGE_VOICE      4
#define ISDN_USAGE_FAX        5
#define ISDN_USAGE_MASK       7 /* Mask to get plain usage */
#define ISDN_USAGE_DISABLED  32 /* This bit is set, if channel is disabled */
#define ISDN_USAGE_EXCLUSIVE 64 /* This bit is set, if channel is exclusive */
#define ISDN_USAGE_OUTGOING 128 /* This bit is set, if channel is outgoing  */

#define ISDN_MODEM_NUMREG    24        /* Number of Modem-Registers        */
#define ISDN_LMSNLEN         255 /* Length of tty's Listen-MSN string */
#define ISDN_CMSGLEN	     50	 /* Length of CONNECT-Message to add for Modem */

#ifdef BIG_PHONE_NUMBERS
#define ISDN_MSNLEN          32
#define NET_DV 0x06  /* Data version for isdn_net_ioctl_cfg   */
#define TTY_DV 0x06  /* Data version for iprofd etc.          */
#else
#define ISDN_MSNLEN          20
#define NET_DV 0x05  /* Data version for isdn_net_ioctl_cfg   */
#define TTY_DV 0x05  /* Data version for iprofd etc.          */
#endif

#define INF_DV 0x01  /* Data version for /dev/isdninfo        */

typedef struct {
  char drvid[25];
  unsigned long arg;
} isdn_ioctl_struct;

typedef struct {
  unsigned long isdndev;
  unsigned long atmodem[ISDN_MAX_CHANNELS];
  unsigned long info[ISDN_MAX_CHANNELS];
} debugvar_addr;

typedef struct {
  char name[10];
  char phone[ISDN_MSNLEN];
  int  outgoing;
} isdn_net_ioctl_phone;

typedef struct {
  char name[10];     /* Name of interface                     */
  char master[10];   /* Name of Master for Bundling           */
  char slave[10];    /* Name of Slave for Bundling            */
  char eaz[256];     /* EAZ/MSN                               */
  char drvid[25];    /* DriverId for Bindings                 */
  int  onhtime;      /* Hangup-Timeout                        */
  int  charge;       /* Charge-Units                          */
  int  l2_proto;     /* Layer-2 protocol                      */
  int  l3_proto;     /* Layer-3 protocol                      */
  int  p_encap;      /* Encapsulation                         */
  int  exclusive;    /* Channel, if bound exclusive           */
  int  dialmax;      /* Dial Retry-Counter                    */
  int  slavedelay;   /* Delay until slave starts up           */
  int  cbdelay;      /* Delay before Callback                 */
  int  chargehup;    /* Flag: Charge-Hangup                   */
  int  ihup;         /* Flag: Hangup-Timeout on incoming line */
  int  secure;       /* Flag: Secure                          */
  int  callback;     /* Flag: Callback                        */
  int  cbhup;        /* Flag: Reject Call before Callback     */
  int  pppbind;      /* ippp device for bindings              */
  int  chargeint;    /* Use fixed charge interval length      */
  int  triggercps;   /* BogoCPS needed for triggering slave   */
  int  dialtimeout;  /* Dial-Timeout                          */
  int  dialwait;     /* Time to wait after failed dial        */
  int  dialmode;     /* Flag: off / on / auto                 */
} isdn_net_ioctl_cfg;

#define ISDN_NET_DIALMODE_MASK 0xC0  /* bits for status                   */
#define  ISDN_NET_DM_OFF	0x00    /* this interface is stopped      */
#define  ISDN_NET_DM_MANUAL	0x40    /* this interface is on (manual)  */
#define  ISDN_NET_DM_AUTO	0x80    /* this interface is autodial     */
#define ISDN_NET_DIALMODE(x) ((&(x))->flags & ISDN_NET_DIALMODE_MASK)

#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/fcntl.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#ifdef CONFIG_ISDN_PPP

#ifdef CONFIG_ISDN_PPP_VJ
#  include <net/slhc_vj.h>
#endif

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppvar.h>

#include <linux/isdn_ppp.h>
#endif

#ifdef CONFIG_ISDN_X25
#  include <linux/concap.h>
#endif

#ifdef HAVE_DEVFS_FS
#ifdef CONFIG_DEVFS_FS
#  include <linux/devfs_fs_kernel.h>
#endif
#endif /* HAVE_DEVFS_FS */

#include <linux/isdnif.h>

#define ISDN_DRVIOCTL_MASK       0x7f  /* Mask for Device-ioctl */

/* Until now unused */
#define ISDN_SERVICE_VOICE 1
#define ISDN_SERVICE_AB    1<<1 
#define ISDN_SERVICE_X21   1<<2
#define ISDN_SERVICE_G4    1<<3
#define ISDN_SERVICE_BTX   1<<4
#define ISDN_SERVICE_DFUE  1<<5
#define ISDN_SERVICE_X25   1<<6
#define ISDN_SERVICE_TTX   1<<7
#define ISDN_SERVICE_MIXED 1<<8
#define ISDN_SERVICE_FW    1<<9
#define ISDN_SERVICE_GTEL  1<<10
#define ISDN_SERVICE_BTXN  1<<11
#define ISDN_SERVICE_BTEL  1<<12

/* Macros checking plain usage */
#define USG_NONE(x)         ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NONE)
#define USG_RAW(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_RAW)
#define USG_MODEM(x)        ((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM)
#define USG_VOICE(x)        ((x & ISDN_USAGE_MASK)==ISDN_USAGE_VOICE)
#define USG_NET(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NET)
#define USG_FAX(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_FAX)
#define USG_OUTGOING(x)     ((x & ISDN_USAGE_OUTGOING)==ISDN_USAGE_OUTGOING)
#define USG_MODEMORVOICE(x) (((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM) || \
                             ((x & ISDN_USAGE_MASK)==ISDN_USAGE_VOICE)     )

/* Timer-delays and scheduling-flags */
#define ISDN_TIMER_RES         3                         /* Main Timer-Resolution   */
#define ISDN_TIMER_02SEC       (HZ/(ISDN_TIMER_RES+1)/5) /* Slow-Timer1 .2 sec      */
#define ISDN_TIMER_1SEC        (HZ/(ISDN_TIMER_RES+1))   /* Slow-Timer2 1 sec       */
#define ISDN_TIMER_RINGING     5 /* tty RINGs = ISDN_TIMER_1SEC * this factor       */
#define ISDN_TIMER_KEEPINT    10 /* Cisco-Keepalive = ISDN_TIMER_1SEC * this factor */
#define ISDN_TIMER_MODEMREAD   1
#define ISDN_TIMER_MODEMPLUS   2
#define ISDN_TIMER_MODEMRING   4
#define ISDN_TIMER_MODEMXMIT   8
#define ISDN_TIMER_NETDIAL    16 
#define ISDN_TIMER_NETHANGUP  32
#define ISDN_TIMER_IPPP       64 
#define ISDN_TIMER_KEEPALIVE 128 /* Cisco-Keepalive */
#define ISDN_TIMER_CARRIER   256 /* Wait for Carrier */
#define ISDN_TIMER_FAST      (ISDN_TIMER_MODEMREAD | ISDN_TIMER_MODEMPLUS | \
                              ISDN_TIMER_MODEMXMIT)
#define ISDN_TIMER_SLOW      (ISDN_TIMER_MODEMRING | ISDN_TIMER_NETHANGUP | \
                              ISDN_TIMER_NETDIAL | ISDN_TIMER_KEEPALIVE | \
                              ISDN_TIMER_CARRIER)

/* Timeout-Values for isdn_net_dial() */
#define ISDN_TIMER_DTIMEOUT10 (10*HZ/(ISDN_TIMER_02SEC*(ISDN_TIMER_RES+1)))
#define ISDN_TIMER_DTIMEOUT15 (15*HZ/(ISDN_TIMER_02SEC*(ISDN_TIMER_RES+1)))
#define ISDN_TIMER_DTIMEOUT60 (60*HZ/(ISDN_TIMER_02SEC*(ISDN_TIMER_RES+1)))

/* GLOBAL_FLAGS */
#define ISDN_GLOBAL_STOPPED 1

/*=================== Start of ip-over-ISDN stuff =========================*/

/* Feature- and status-flags for a net-interface */
#define ISDN_NET_CONNECTED  0x01       /* Bound to ISDN-Channel             */
#define ISDN_NET_SECURE     0x02       /* Accept calls from phonelist only  */
#define ISDN_NET_CALLBACK   0x04       /* activate callback                 */
#define ISDN_NET_CBHUP      0x08       /* hangup before callback            */
#define ISDN_NET_CBOUT      0x10       /* remote machine does callback      */
#if 0
/* Unused??? */
#define ISDN_NET_CLONE      0x08       /* clone a tmp interface when called */
#define ISDN_NET_TMP        0x10       /* tmp interface until getting an IP */
#define ISDN_NET_DYNAMIC    0x20       /* this link is dynamically allocated */
#endif

#define ISDN_NET_MAGIC      0x49344C02 /* for paranoia-checking             */

/* Phone-list-element */
typedef struct {
  void *next;
  char num[ISDN_MSNLEN];
} isdn_net_phone;

/*
   Principles when extending structures for generic encapsulation protocol
   ("concap") support:
   - Stuff which is hardware specific (here i4l-specific) goes in 
     the netdev -> local structure (here: isdn_net_local)
   - Stuff which is encapsulation protocol specific goes in the structure
     which holds the linux device structure (here: isdn_net_device)
*/

/* Local interface-data */
typedef struct isdn_net_local_s {
  ulong                  magic;
  char                   name[10];     /* Name of device                   */
  struct enet_statistics stats;        /* Ethernet Statistics              */
  int                    isdn_device;  /* Index to isdn-device             */
  int                    isdn_channel; /* Index to isdn-channel            */
  int			 ppp_slot;     /* PPPD device slot number          */
  int                    pre_device;   /* Preselected isdn-device          */
  int                    pre_channel;  /* Preselected isdn-channel         */
  int                    exclusive;    /* If non-zero idx to reserved chan.*/
  int                    flags;        /* Connection-flags                 */
  int                    dialretry;    /* Counter for Dialout-retries      */
  int                    dialmax;      /* Max. Number of Dial-retries      */
  int                    cbdelay;      /* Delay before Callback starts     */
  int                    dtimer;       /* Timeout-counter for dialing      */
  char                   msn[ISDN_MSNLEN]; /* MSNs/EAZs for this interface */
  u_char                 cbhup;        /* Flag: Reject Call before Callback*/
  u_char                 dialstate;    /* State for dialing                */
  u_char                 p_encap;      /* Packet encapsulation             */
                                       /*   0 = Ethernet over ISDN         */
				       /*   1 = RAW-IP                     */
                                       /*   2 = IP with type field         */
  u_char                 l2_proto;     /* Layer-2-protocol                 */
				       /* See ISDN_PROTO_L2..-constants in */
                                       /* isdnif.h                         */
                                       /*   0 = X75/LAPB with I-Frames     */
				       /*   1 = X75/LAPB with UI-Frames    */
				       /*   2 = X75/LAPB with BUI-Frames   */
				       /*   3 = HDLC                       */
  u_char                 l3_proto;     /* Layer-3-protocol                 */
				       /* See ISDN_PROTO_L3..-constants in */
                                       /* isdnif.h                         */
                                       /*   0 = Transparent                */
  int                    huptimer;     /* Timeout-counter for auto-hangup  */
  int                    charge;       /* Counter for charging units       */
  ulong                  chargetime;   /* Timer for Charging info          */
  int                    hupflags;     /* Flags for charge-unit-hangup:    */
				       /* bit0: chargeint is invalid       */
				       /* bit1: Getting charge-interval    */
                                       /* bit2: Do charge-unit-hangup      */
                                       /* bit3: Do hangup even on incoming */
  int                    outgoing;     /* Flag: outgoing call              */
  int                    onhtime;      /* Time to keep link up             */
  int                    chargeint;    /* Interval between charge-infos    */
  int                    onum;         /* Flag: at least 1 outgoing number */
  int                    cps;          /* current speed of this interface  */
  int                    transcount;   /* byte-counter for cps-calculation */
  int                    sqfull;       /* Flag: netdev-queue overloaded    */
  ulong                  sqfull_stamp; /* Start-Time of overload           */
  ulong                  slavedelay;   /* Dynamic bundling delaytime       */
  int                    triggercps;   /* BogoCPS needed for trigger slave */
  isdn_net_phone         *phone[2];    /* List of remote-phonenumbers      */
				       /* phone[0] = Incoming Numbers      */
				       /* phone[1] = Outgoing Numbers      */
  isdn_net_phone         *dial;        /* Pointer to dialed number         */
  struct net_device      *master;      /* Ptr to Master device for slaves  */
  struct net_device      *slave;       /* Ptr to Slave device for masters  */
  struct isdn_net_local_s *next;       /* Ptr to next link in bundle       */
  struct isdn_net_local_s *last;       /* Ptr to last link in bundle       */
  struct isdn_net_dev_s  *netdev;      /* Ptr to netdev                    */
  struct sk_buff_head    super_tx_queue; /* List of supervisory frames to  */
	                               /* be transmitted asap              */
  atomic_t frame_cnt;                  /* number of frames currently       */
                        	       /* queued in HL driver              */    
                                       /* Ptr to orig. hard_header_cache   */
  spinlock_t             xmit_lock;    /* used to protect the xmit path of */
                                       /* a particular channel (including  */
                                       /* the frame_cnt                    */

  int                    (*org_hhc)(
				    struct neighbour *neigh,
				    struct hh_cache *hh);
                                       /* Ptr to orig. header_cache_update */
  void                   (*org_hcu)(struct hh_cache *,
				    struct net_device *,
                                    unsigned char *);
  int  pppbind;                        /* ippp device for bindings         */
  int					dialtimeout;	/* How long shall we try on dialing? (jiffies) */
  int					dialwait;		/* How long shall we wait after failed attempt? (jiffies) */
  ulong					dialstarted;	/* jiffies of first dialing-attempt */
  ulong					dialwait_timer;	/* jiffies of earliest next dialing-attempt */
  int					huptimeout;		/* How long will the connection be up? (seconds) */
#ifdef CONFIG_ISDN_X25
  struct concap_device_ops *dops;      /* callbacks used by encapsulator   */
#endif
  int  cisco_loop;                     /* Loop counter for Cisco-SLARP     */
  ulong cisco_myseq;                   /* Local keepalive seq. for Cisco   */
  ulong cisco_yourseq;                 /* Remote keepalive seq. for Cisco  */
  struct tq_struct tqueue;
#ifdef CONFIG_ISDN_WITH_ABC
  ulong 	dw_abc_flags;
  ulong 	dw_abc_if_flags;
  int   	dw_abc_inuse_secure;
  ulong 	dw_abc_comhd_last_send;
  ulong 	dw_abc_dialstart;
  int   	dw_abc_old_onhtime;
  int 		dw_abc_remote_version;
  int		dw_abc_bitlocks;
#ifdef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
  char	dw_out_msn[ISDN_MSNLEN]; /* eaz for outgoing call if *out_msn != 0 */
#endif
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
  ulong 				dw_abc_lcr_callid;
  ulong 				dw_abc_lcr_start_request;
  ulong 				dw_abc_lcr_end_request;
  isdn_ctrl 			*dw_abc_lcr_cmd;
  struct ISDN_DWABC_LCR_IOCTL	*dw_abc_lcr_io;
#endif
  ulong dw_abc_bchan_last_connect;
#ifdef CONFIG_ISDN_WITH_ABC_CONN_ERROR
  short dw_abc_bchan_errcnt;
#endif
#ifdef CONFIG_ISDN_WITH_ABC_RAWIPCOMPRESS
  void *dw_abc_bsd_compressor;
  void *dw_abc_bsd_stat_rx;
  void *dw_abc_bsd_stat_tx;
#endif
  ulong	dw_abc_bsd_snd;
  ulong	dw_abc_bsd_bsd_snd;
  ulong	dw_abc_bsd_rcv;
  ulong	dw_abc_bsd_bsd_rcv;
#endif
} isdn_net_local;

/* the interface itself */
typedef struct isdn_net_dev_s {
  isdn_net_local *local;
  isdn_net_local *queue;               /* circular list of all bundled
					  channels, which are currently
					  online                           */
  spinlock_t queue_lock;               /* lock to protect queue            */
  void *next;                          /* Pointer to next isdn-interface   */
  struct net_device dev;               /* interface to upper levels        */
#ifdef CONFIG_ISDN_PPP
  struct mpqueue *mp_last; 
  struct ippp_bundle ib;
#endif
#ifdef CONFIG_ISDN_X25
  struct concap_proto  *cprot; /* connection oriented encapsulation protocol */
#endif

} isdn_net_dev;

/*===================== End of ip-over-ISDN stuff ===========================*/

/*======================= Start of ISDN-tty stuff ===========================*/

#define ISDN_ASYNC_MAGIC          0x49344C01 /* for paranoia-checking        */
#define ISDN_ASYNC_INITIALIZED	  0x80000000 /* port was initialized         */
#define ISDN_ASYNC_CALLOUT_ACTIVE 0x40000000 /* Call out device active       */
#define ISDN_ASYNC_NORMAL_ACTIVE  0x20000000 /* Normal device active         */
#define ISDN_ASYNC_CLOSING	  0x08000000 /* Serial port is closing       */
#define ISDN_ASYNC_CTS_FLOW	  0x04000000 /* Do CTS flow control          */
#define ISDN_ASYNC_CHECK_CD	  0x02000000 /* i.e., CLOCAL                 */
#define ISDN_ASYNC_HUP_NOTIFY         0x0001 /* Notify tty on hangups/closes */
#define ISDN_ASYNC_SESSION_LOCKOUT    0x0100 /* Lock cua opens on session    */
#define ISDN_ASYNC_PGRP_LOCKOUT       0x0200 /* Lock cua opens on pgrp       */
#define ISDN_ASYNC_CALLOUT_NOHUP      0x0400 /* No hangup for cui            */
#define ISDN_ASYNC_SPLIT_TERMIOS      0x0008 /* Sep. termios for dialin/out  */
#define ISDN_SERIAL_XMIT_SIZE           1024 /* Default bufsize for write    */
#define ISDN_SERIAL_XMIT_MAX            4000 /* Maximum bufsize for write    */
#define ISDN_SERIAL_TYPE_NORMAL            1
#define ISDN_SERIAL_TYPE_CALLOUT           2

#ifdef CONFIG_ISDN_AUDIO
/* For using sk_buffs with audio we need some private variables
 * within each sk_buff. For this purpose, we declare a struct here,
 * and put it always at skb->head. A few macros help accessing the
 * variables. Of course, we need to check skb_headroom prior to
 * any access.
 */
typedef struct isdn_audio_skb {
  unsigned short dle_count;
  unsigned char  lock;
} isdn_audio_skb;

#define ISDN_AUDIO_SKB_DLECOUNT(skb) (((isdn_audio_skb*)skb->head)->dle_count)
#define ISDN_AUDIO_SKB_LOCK(skb) (((isdn_audio_skb*)skb->head)->lock)
#endif

/* Private data of AT-command-interpreter */
typedef struct atemu {
	u_char       profile[ISDN_MODEM_NUMREG]; /* Modem-Regs. Profile 0              */
	u_char       mdmreg[ISDN_MODEM_NUMREG];  /* Modem-Registers                    */
	char         pmsn[ISDN_MSNLEN];          /* EAZ/MSNs Profile 0                 */
	char         msn[ISDN_MSNLEN];           /* EAZ/MSN                            */
	char         plmsn[ISDN_LMSNLEN];        /* Listening MSNs Profile 0           */
	char         lmsn[ISDN_LMSNLEN];         /* Listening MSNs                     */
	char         cpn[ISDN_MSNLEN];           /* CalledPartyNumber on incoming call */
	char         connmsg[ISDN_CMSGLEN];	 /* CONNECT-Msg from HL-Driver	       */
#ifdef CONFIG_ISDN_AUDIO
	u_char       vpar[10];                   /* Voice-parameters                   */
	int          lastDLE;                    /* Flag for voice-coding: DLE seen    */
#endif
	int          mdmcmdl;                    /* Length of Modem-Commandbuffer      */
	int          pluscount;                  /* Counter for +++ sequence           */
	int          lastplus;                   /* Timestamp of last +                */
	int	     carrierwait;                /* Seconds of carrier waiting         */
	char         mdmcmd[255];                /* Modem-Commandbuffer                */
	unsigned int charge;                     /* Charge units of current connection */
} atemu;

/* Private data (similar to async_struct in <linux/serial.h>) */
typedef struct modem_info {
  int			magic;
  int			flags;		 /* defined in tty.h               */
  int			x_char;		 /* xon/xoff character             */
  int			mcr;		 /* Modem control register         */
  int                   msr;             /* Modem status register          */
  int                   lsr;             /* Line status register           */
  int			line;
  int			count;		 /* # of fd on device              */
  int			blocked_open;	 /* # of blocked opens             */
  long			session;	 /* Session of opening process     */
  long			pgrp;		 /* pgrp of opening process        */
  int                   online;          /* 1 = B-Channel is up, drop data */
					 /* 2 = B-Channel is up, deliver d.*/
  int                   dialing;         /* Dial in progress or ATA        */
  int                   rcvsched;        /* Receive needs schedule         */
  int                   isdn_driver;	 /* Index to isdn-driver           */
  int                   isdn_channel;    /* Index to isdn-channel          */
  int                   drv_index;       /* Index to dev->usage            */
  int                   ncarrier;        /* Flag: schedule NO CARRIER      */
  unsigned char         last_cause[8];   /* Last cause message             */
  unsigned char         last_num[ISDN_MSNLEN];
	                                 /* Last phone-number              */
  unsigned char         last_l2;         /* Last layer-2 protocol          */
  unsigned char         last_si;         /* Last service                   */
  unsigned char         last_lhup;       /* Last hangup local?             */
  unsigned char         last_dir;        /* Last direction (in or out)     */
  struct timer_list     nc_timer;        /* Timer for delayed NO CARRIER   */
  int                   send_outstanding;/* # of outstanding send-requests */
  int                   xmit_size;       /* max. # of chars in xmit_buf    */
  int                   xmit_count;      /* # of chars in xmit_buf         */
  unsigned char         *xmit_buf;       /* transmit buffer                */
  struct sk_buff_head   xmit_queue;      /* transmit queue                 */
  atomic_t              xmit_lock;       /* Semaphore for isdn_tty_write   */
#ifdef CONFIG_ISDN_AUDIO
  int                   vonline;         /* Voice-channel status           */
					 /* Bit 0 = recording              */
					 /* Bit 1 = playback               */
					 /* Bit 2 = playback, DLE-ETX seen */
  struct sk_buff_head   dtmf_queue;      /* queue for dtmf results         */
  void                  *adpcms;         /* state for adpcm decompression  */
  void                  *adpcmr;         /* state for adpcm compression    */
  void                  *dtmf_state;     /* state for dtmf decoder         */
  void                  *silence_state;  /* state for silence detection    */
#endif
#ifdef CONFIG_ISDN_TTY_FAX
  struct T30_s		*fax;		 /* T30 Fax Group 3 data/interface */
  int			faxonline;	 /* Fax-channel status             */
#endif
  struct tty_struct 	*tty;            /* Pointer to corresponding tty   */
  atemu                 emu;             /* AT-emulator data               */
  struct termios	normal_termios;  /* For saving termios structs     */
  struct termios	callout_termios;
#ifdef COMPAT_HAS_NEW_WAITQ
  wait_queue_head_t	open_wait, close_wait;
#else
  struct wait_queue	*open_wait;
  struct wait_queue	*close_wait;
#endif
  struct semaphore      write_sem;
} modem_info;

#define ISDN_MODEM_WINSIZE 8

/* Description of one ISDN-tty */
typedef struct {
  int                refcount;			   /* Number of opens        */
  struct tty_driver  tty_modem;			   /* tty-device             */
  struct tty_driver  cua_modem;			   /* cua-device             */
  struct tty_struct  *modem_table[ISDN_MAX_CHANNELS]; /* ?? copied from Orig */
  struct termios     *modem_termios[ISDN_MAX_CHANNELS];
  struct termios     *modem_termios_locked[ISDN_MAX_CHANNELS];
  modem_info         info[ISDN_MAX_CHANNELS];	   /* Private data           */
} modem;

/*======================= End of ISDN-tty stuff ============================*/

/*======================== Start of V.110 stuff ============================*/
#define V110_BUFSIZE 1024

typedef struct {
	int nbytes;                    /* 1 Matrixbyte -> nbytes in stream     */
	int nbits;                     /* Number of used bits in streambyte    */
	unsigned char key;             /* Bitmask in stream eg. 11 (nbits=2)   */
	int decodelen;                 /* Amount of data in decodebuf          */
	int SyncInit;                  /* Number of sync frames to send        */
	unsigned char *OnlineFrame;    /* Precalculated V110 idle frame        */
	unsigned char *OfflineFrame;   /* Precalculated V110 sync Frame        */
	int framelen;                  /* Length of frames                     */
	int skbuser;                   /* Number of unacked userdata skbs      */
	int skbidle;                   /* Number of unacked idle/sync skbs     */
	int introducer;                /* Local vars for decoder               */
	int dbit;
	unsigned char b;
	int skbres;                    /* space to reserve in outgoing skb     */
	int maxsize;                   /* maxbufsize of lowlevel driver        */
	unsigned char *encodebuf;      /* temporary buffer for encoding        */
	unsigned char decodebuf[V110_BUFSIZE]; /* incomplete V110 matrices     */
} isdn_v110_stream;

/*========================= End of V.110 stuff =============================*/

/*======================= Start of general stuff ===========================*/

typedef struct {
	char *next;
	char *private;
} infostruct;

typedef struct isdn_module {
	struct isdn_module *prev;
	struct isdn_module *next;
	char *name;
	int (*get_free_channel)(int, int, int, int, int);
	int (*free_channel)(int, int, int);
	int (*status_callback)(isdn_ctrl *);
	int (*command)(isdn_ctrl *);
	int (*receive_callback)(int, int, struct sk_buff *);
	int (*writebuf_skb)(int, int, int, struct sk_buff *);
	int (*net_start_xmit)(struct sk_buff *, struct net_device *);
	int (*net_receive)(struct net_device *, struct sk_buff *);
	int (*net_open)(struct net_device *);
	int (*net_close)(struct net_device *);
	int priority;
} isdn_module;

#define DRV_FLAG_RUNNING 1
#define DRV_FLAG_REJBUS  2
#define DRV_FLAG_LOADED  4

/* Description of hardware-level-driver */
typedef struct {
	ulong               online;           /* Channel-Online flags             */
	ulong               flags;            /* Misc driver Flags                */
	int                 locks;            /* Number of locks for this driver  */
	int                 channels;         /* Number of channels               */
#ifdef COMPAT_HAS_NEW_WAITQ
	wait_queue_head_t   st_waitq;         /* Wait-Queue for status-read's     */
#else
	struct wait_queue  *st_waitq;         /* Wait-Queue for status-read's     */
#endif
	int                 maxbufsize;       /* Maximum Buffersize supported     */
	unsigned long       pktcount;         /* Until now: unused                */
	int                 stavail;          /* Chars avail on Status-device     */
	isdn_if            *interface;        /* Interface to driver              */
	int                *rcverr;           /* Error-counters for B-Ch.-receive */
	int                *rcvcount;         /* Byte-counters for B-Ch.-receive  */
#ifdef CONFIG_ISDN_AUDIO
	unsigned long      DLEflag;           /* Flags: Insert DLE at next read   */
#endif
	struct sk_buff_head *rpqueue;         /* Pointers to start of Rcv-Queue   */
#ifdef COMPAT_HAS_NEW_WAITQ
	wait_queue_head_t  *rcv_waitq;       /* Wait-Queues for B-Channel-Reads  */
	wait_queue_head_t  *snd_waitq;       /* Wait-Queue for B-Channel-Send's  */
#else
	struct wait_queue  **rcv_waitq;       /* Wait-Queues for B-Channel-Reads  */
	struct wait_queue  **snd_waitq;       /* Wait-Queue for B-Channel-Send's  */
#endif
	char               msn2eaz[10][ISDN_MSNLEN];  /* Mapping-Table MSN->EAZ   */
#ifdef CONFIG_ISDN_WITH_ABC_ICALL_BIND
	u_char   dwabc_lchmap[ISDN_DW_ABC_MAX_CH_P_RIVER]; /* locically channelmap */
	u_long 	dwabc_lch_use;				/* lasttime a locical chanelmap was set */
#endif
} driver;

/* Main driver-data */
typedef struct isdn_devt {
	unsigned short    flags;		       /* Bitmapped Flags:           */
	/*                            */
	int               drivers;		       /* Current number of drivers  */
	int               channels;		       /* Current number of channels */
	int               net_verbose;               /* Verbose-Flag               */
	int               modempoll;		       /* Flag: tty-read active      */
	int               tflags;                    /* Timer-Flags:               */
	/*  see ISDN_TIMER_..defines  */
	int               global_flags;
	infostruct        *infochain;                /* List of open info-devs.    */
#ifdef COMPAT_HAS_NEW_WAITQ
	wait_queue_head_t info_waitq;               /* Wait-Queue for isdninfo    */
#else
	struct wait_queue *info_waitq;               /* Wait-Queue for isdninfo    */
#endif
	struct timer_list timer;		       /* Misc.-function Timer       */
	int               chanmap[ISDN_MAX_CHANNELS];/* Map minor->device-channel  */
	int               drvmap[ISDN_MAX_CHANNELS]; /* Map minor->driver-index    */
	int               usage[ISDN_MAX_CHANNELS];  /* Used by tty/ip/voice       */
#ifdef CONFIG_ISDN_WITH_ABC_CH_EXTINUSE
	ulong			  dwabc_chan_external_inuse[ISDN_MAX_CHANNELS];
#endif
	char              num[ISDN_MAX_CHANNELS][ISDN_MSNLEN];
	/* Remote number of active ch.*/
	int               m_idx[ISDN_MAX_CHANNELS];  /* Index for mdm....          */
	driver            *drv[ISDN_MAX_DRIVERS];    /* Array of drivers           */
	isdn_net_dev      *netdev;		       /* Linked list of net-if's    */
	char              drvid[ISDN_MAX_DRIVERS][20];/* Driver-ID                 */
	struct task_struct *profd;                   /* For iprofd                 */
	modem             mdm;		       /* tty-driver-data            */
	isdn_net_dev      *rx_netdev[ISDN_MAX_CHANNELS]; /* rx netdev-pointers     */
	isdn_net_dev      *st_netdev[ISDN_MAX_CHANNELS]; /* stat netdev-pointers   */
	ulong             ibytes[ISDN_MAX_CHANNELS]; /* Statistics incoming bytes  */
	ulong             obytes[ISDN_MAX_CHANNELS]; /* Statistics outgoing bytes  */
	int               v110emu[ISDN_MAX_CHANNELS];/* V.110 emulator-mode 0=none */
	atomic_t          v110use[ISDN_MAX_CHANNELS];/* Usage-Semaphore for stream */
	isdn_v110_stream  *v110[ISDN_MAX_CHANNELS];  /* V.110 private data         */
	struct semaphore  sem;                       /* serialize list access*/
	isdn_module       *modules;
	unsigned long     global_features;
#ifdef CONFIG_ISDN_WITH_ABC_ICALL_BIND
	u_long	          dwabc_lch_check;           /* lasttime a locical chanelmap checked */
#endif 
#ifdef HAVE_DEVFS_FS
#ifdef CONFIG_DEVFS_FS
	devfs_handle_t devfs_handle_isdninfo;
	devfs_handle_t devfs_handle_isdnctrl;
	devfs_handle_t devfs_handle_isdnX[ISDN_MAX_CHANNELS];
	devfs_handle_t devfs_handle_isdnctrlX[ISDN_MAX_CHANNELS];
#ifdef CONFIG_ISDN_PPP
	devfs_handle_t devfs_handle_ipppX[ISDN_MAX_CHANNELS];
#endif
#endif /* CONFIG_DEVFS_FS */
#endif /* HAVE_DEVFS_FS */
} isdn_dev;

extern isdn_dev *dev;

#ifdef CONFIG_ISDN_WITH_ABC
extern void isdn_net_unreachable(struct net_device *,struct sk_buff *,char *);
extern void isdn_net_log_skb_dwabc(struct sk_buff *,isdn_net_local *,char *);
extern void isdn_net_hangup(struct net_device *d);
extern void isdn_dw_clear_if(ulong pm,isdn_net_local *);
extern void isdn_dwabc_test_phone(isdn_net_local *);
extern void isdn_dw_abc_init_func(void);
extern void isdn_dw_abc_release_func(void);
extern int isdn_dw_abc_reset_interface(isdn_net_local *,int);
extern int dwabc_bsd_init(isdn_net_local *lp);
extern void dwabc_bsd_free(isdn_net_local *lp);
extern struct sk_buff *dwabc_bsd_compress(isdn_net_local *,struct sk_buff *,struct net_device *);
extern void dwabc_bsd_first_gen(isdn_net_local *);
extern struct sk_buff *dwabc_bsd_rx_pkt(isdn_net_local *,struct sk_buff *,struct net_device *);
#ifdef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
extern size_t isdn_dw_abc_lcr_readstat(char *,size_t);
extern ulong isdn_dw_abc_lcr_call_number(isdn_net_local *,isdn_ctrl *);
extern void isdn_dw_abc_lcr_open(void);
extern void isdn_dw_abc_lcr_close(void);
extern void isdn_dw_abc_lcr_ioctl(ulong);
extern void isdn_dw_abc_lcr_clear(isdn_net_local *);
extern void isdn_dw_abc_free_lch_with_pch(int,int);
#endif
#ifdef CONFIG_ISDN_WITH_ABC_UDP_CHECK
extern int dw_abc_udp_test(struct sk_buff *skb,struct net_device *ndev); 
#endif
#if CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE || CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR
struct sk_buff *isdn_dw_abc_ip4_keepalive_test(struct net_device *ndev,struct sk_buff *skb);
#endif
#ifdef CONFIG_ISDN_WITH_ABC_ICALL_BIND 
extern int isdn_dwabc_check_icall_bind(isdn_net_local *,int,int);
extern int dwabc_isdn_get_net_free_channel(isdn_net_local *);
#endif
#endif


/* Utility-Macros */
#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)
#ifdef COMPAT_NO_SOFTNET
/*
 * Tell upper layers that the network device is ready to xmit more frames.
 */
static void __inline__ netif_wake_queue(struct net_device * dev)
{
	dev->tbusy = 0;
	mark_bh(NET_BH);
}

/*
 * called during net_device open()
 */
static void __inline__ netif_start_queue(struct net_device * dev)
{
	dev->tbusy = 0;
	/* actually, we never use the interrupt flag at all */
	dev->interrupt = 0;
	dev->start = 1;
}

/*
 * Ask upper layers to temporarily cease passing us more xmit frames.
 */
static void __inline__ netif_stop_queue(struct net_device * dev)
{
	dev->tbusy = 1;
}
#endif /* COMPAT_NO_SOFTNET */

#ifdef COMPAT_HAS_2_2_PCI 
struct pci_dev;

static int __inline__ pci_enable_device(struct pci_dev * pdev)
{
	return 0;
}
#endif

#endif /* __KERNEL__ */
#endif /* isdn_h */
