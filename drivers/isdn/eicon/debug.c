
/*
 *
  Copyright (c) Eicon Technology Corporation, 2000.
  Copyright (c) Cytronics & Melware, 2000.
 *
  This source file is supplied for the exclusive use with Eicon
  Technology Corporation's range of DIVA Server Adapters.
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*------------------------------------------------------------------*/
/* file: debug.c                                                    */
/*------------------------------------------------------------------*/
# include "dbgioctl.h"
#ifdef NOT_YET_USED
# include "typedefs.h"

# include "dimaint.h"	/* need the LOG structure from there */
#else
#include "platform.h"
extern int sprintf(char *, const char*, ...);
extern int vsprintf(char *buf, const char *, va_list);
#endif

# include "debuglib.h"

# include "debug_if.h"
# include "main_if.h"

#ifdef NOT_YET_USED
# include "print_if.h"
#endif
# ifdef LOGGING_ABOVE_DISPATCH
#   include "atom_if.h"
# endif

# define MAX_DBG_REQUESTS	10	/* max no of concurrent requestes	*/
# define MAX_DBG_CLIENTS	20	/* max no of registered clients		*/
# define DBG_MAGIC			(0x47114711L)

/*------------------------------------------------------------------*/
/* exported data (only to set the time outside of locked code)		*/
/*------------------------------------------------------------------*/

static unsigned long Now;

/*------------------------------------------------------------------*/
/* local functions													*/
/*------------------------------------------------------------------*/

#ifdef NOT_YET_NEEDED
static void		DI_unpendreq (DbgRequest *pDbgReq) ;
static void		DI_flush (void) ;
static void		DI_timeout (void) ;
#endif

/*------------------------------------------------------------------*/
/* local data														*/
/*------------------------------------------------------------------*/

static unsigned long	MaxDumpSize = 256 ;
static unsigned long	MaxXlogSize = 2 + 128 ;
#ifdef NOT_YET_NEEDED
static unsigned long	ScreenDriverMask = 0xFFFFFFFF ;	/* no restrictions */
static char				ScreenDebugStyle = 1 ;	/* nice */
#endif
static char				Initialized = 0 ;	/* not initialized */

#ifdef NOT_YET_NEEDED
static struct {
	char		 *Base ;	/* the queue buffer */
	unsigned long Size ;	/* size of buffer	*/
	char		 *High ;	/* end of buffer	*/
	char		 *Head ;	/* first message	*/
	char		 *Tail ;	/* where to append	*/
	char		 *Wrap ;	/* where it wraps	*/
	unsigned long Used ;	/* # of bytes currently used		*/
	unsigned long Seq ;		/* sequence # for next message		*/
	LIST_ENTRY    List ;	/* list of pending i/o requests		*/
	unsigned long Trigger ;	/* when to complete 1st pending req	*/
} Queue ;
#endif

static struct {
	pDbgHandle  client ;			/* ptr to handle of client driver	*/
#ifdef NOT_YET_NEEDED
	ULONG		regTimeLowPart;		/* not a struct to hold aligment	*/
	LONG		regTimeHighPart;	/*		  --- ditto ---				*/
#endif
	char        drvName[16] ;		/* ASCII name of registered driver  */
} dbgQue [MAX_DBG_CLIENTS] = { { NULL } };

#ifdef NOT_YET_NEEDED
static DbgRequest		DbgRequests[MAX_DBG_REQUESTS] ;
static LIST_ENTRY		DbgRequestList ;
#endif

#ifdef NOT_YET_NEEDED
static unsigned long	ScreenDebugMask ;
#endif

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
static void
queueReset (void)
{	/*LOCKED*/
/*
 * Reset the queue to initial state (empty)
 */
	Queue.Head = Queue.Tail = Queue.Base ;
	Queue.Wrap = 0 ;
	Queue.Used = 0 ;
}	/*LOCKED*/
#endif

#ifdef NOT_YET_NEEDED
static void
queueInit (char *Buffer, unsigned long sizeBuffer)
{	/*LOCKED*/
/*
 * Initialize the queue constants and set the queue empty
 */
	Queue.Base = Buffer;
	Queue.Size = sizeBuffer;
	Queue.High = Buffer + sizeBuffer;
	queueReset ();
}	/*LOCKED*/
#endif

#ifdef NOT_YET_NEEDED
static DbgMessage
*queueAllocMsg (unsigned long sizeMsg)
{	/*LOCKED*/
/*
 * Allocate 'sizeMsg' bytes at tail of queue
 */
	DbgMessage		*Msg ;
	unsigned long	need = MSG_ALIGN(sizeMsg) ;

	if ( Queue.Tail == Queue.Head )
	{
		if (Queue.Wrap || need > Queue.Size)
		{
			return((DbgMessage *)0) ; /* full or too big */
		}
		goto alloc ; /* empty */
	}

	if ( Queue.Tail > Queue.Head )
	{
		if ( Queue.Tail + need <= Queue.High )
		{
			goto alloc ; /* append */
		}
		if ( Queue.Base + need > Queue.Head )
		{
			return ((DbgMessage *)0) ; /* too much */
		}

		/* wraparound the queue (but not the message) */

		Queue.Wrap = Queue.Tail ;
		Queue.Tail = Queue.Base ;
		goto alloc ;
	}

	if ( Queue.Tail + need > Queue.Head )
	{
		return ((DbgMessage *)0) ; /* too much */
	}

alloc:

	Msg = (DbgMessage *)Queue.Tail ;

	Queue.Tail += need ;
	Queue.Used += need ;

	*((LARGE_INTEGER *)&Msg->NTtime) = Now ;
	Msg->size	= (unsigned short) sizeMsg ;
	Msg->ffff	= 0xFFFF ;
	Msg->seq 	= Queue.Seq++ ;

	return (Msg) ;
}	/*LOCKED*/
#endif

#ifdef NOT_YET_NEEDED
static void
queueFreeMsg (void)
{	/*LOCKED*/
/*
 * Free the message at head of queue
 */
	DbgMessage		*Msg ;
	unsigned long	need ;

	if ( (Queue.Tail == Queue.Head) && !Queue.Wrap )
	{
		return ; /* empty */
	}

	Msg = (DbgMessage *)Queue.Head ;
	need = MSG_ALIGN(Msg->size) ;

	Queue.Used -= need ;
	Queue.Head += need ;

	if ( Queue.Wrap )
	{
		if ( Queue.Head >= Queue.Wrap )
		{
			Queue.Head = Queue.Base ;
			Queue.Wrap = 0 ;
		}
	}
	else
	{
		if ( Queue.Head >= Queue.Tail )
		{
			Queue.Head = Queue.Tail = Queue.Base ;
		}
	}
}	/*LOCKED*/
#endif

#ifdef NOT_YET_NEEDED
static DbgMessage
*queuePeekMsg (void)
{	/*LOCKED*/
/*
 * Show the first message in queue BUT DONT free the message.
 * After looking on the message contents it can be freed via
 * queueFreeMsg() or simply remain in queue.
 */
	if ( (Queue.Tail == Queue.Head) && !Queue.Wrap )
	{
		return ((DbgMessage *)0) ; /* empty */
	}
	return ((DbgMessage *)Queue.Head) ;
}	/*LOCKED*/
#endif

#ifdef NOT_YET_NEEDED
static void
queueCopyAll (char *Buffer)
{	/*LOCKED*/
/*
 * Copy out all messages from the queue to 'Buffer'.
 * It's assumed that the queue is not empty and that 'Buffer' has enough space.
 */
	if ( Queue.Tail > Queue.Head )
	{
		memcpy (Buffer, Queue.Head, Queue.Tail - Queue.Head) ;
	}
	else if ( Queue.Wrap )
	{
		memcpy (Buffer, Queue.Head, (Queue.Wrap - Queue.Head)) ;
		memcpy (Buffer + (Queue.Wrap - Queue.Head),
				Queue.Base, (Queue.Tail - Queue.Base)) ;
	}
}	/*LOCKED*/
#endif


/*****************************************************************************/

void
DI_init (void *Buffer, unsigned long sizeBuffer,
		 unsigned long maxDump, unsigned long maxXlog,
		 unsigned long ScreenMask, unsigned long ScreenStyle,
		 unsigned long DriverMask)
{
/*
 * Initialize preallocated log buffer memory and the request list.
 * Because this is called only once from DriverEntry() it is not
 * necessary here to acquire the lock.
 */
#ifdef NOT_YET_NEEDED
 	int i;

# ifdef LOGGING_ABOVE_DISPATCH
	DI_atomic_init ();
# endif

	queueInit (Buffer, sizeBuffer) ;

	Queue.Seq = 0 ;

	InitializeListHead (&Queue.List) ;
	Queue.Trigger = 0 ;

	InitializeListHead (&DbgRequestList) ;
	for ( i = 0; i < sizeof(DbgRequests) / sizeof(DbgRequests[0]); i++ )
	{
		InsertTailList (&DbgRequestList, &DbgRequests[i].List) ;
	}
#endif

#ifdef NOT_YET_NEEDED
/*
 * Adjust the optional configuration parameters
 */
	if ( maxXlog )
	{
		if ( maxXlog < 28 )
			 maxXlog = 28 ;						/* saw never more B-X data	*/
		maxXlog += 2 /*xlog type field*/ ;
		if ( maxXlog > MSG_FRAME_MAX_SIZE - 2 )
			 maxXlog = MSG_FRAME_MAX_SIZE - 2 ;	/* but get all B-R data		*/
		MaxXlogSize = maxXlog ;
	}
	if ( maxDump )
	{
		if ( maxDump < 32 )
			 maxDump = 32 ;
		if ( maxDump > MSG_FRAME_MAX_SIZE )
			 maxDump = MSG_FRAME_MAX_SIZE ;
		MaxDumpSize = maxDump ;
	}
	if ( DriverMask )
	{
		ScreenDriverMask = DriverMask ;
	}
	ScreenDebugMask  = ScreenMask ;
	ScreenDebugStyle = (char) ScreenStyle ;
#endif
/*
 * Now we are ready for work
 */
	Initialized = 1 ;
}

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
DbgRequest
*DI_getreq (void)
{
	DbgRequest *pDbgReq ;

	if ( !Initialized ) return ((DbgRequest *) 0) ;

	DI_lock () ;
	if ( IsListEmpty (&DbgRequestList) )
	{
		pDbgReq = (DbgRequest *) 0 ;
	}
	else
	{
		pDbgReq = (DbgRequest *) RemoveHeadList (&DbgRequestList) ;
		memset (pDbgReq, 0, sizeof(*pDbgReq)) ;
	}
	DI_unlock () ;

	return (pDbgReq) ;
}
#endif

#ifdef NOT_YET_NEEDED
void
DI_relreq (DbgRequest *pDbgReq)
{
	DI_lock () ;
	InsertTailList (&DbgRequestList, &pDbgReq->List) ;
	DI_unlock () ;
}
#endif

#ifdef NOT_YET_NEEDED
static void
DI_timeout (void)
{
	DI_lock () ;
	DI_flush () ;
	DI_unlock () ;
}
#endif

#ifdef NOT_YET_NEEDED
void
DI_pendreq (DbgRequest *pDbgReq)
{
	DI_lock () ;
	if ( IsListEmpty (&Queue.List) )
	{
		Queue.Trigger = pDbgReq->waitSize ;
#if USE_WAIT_ARGS
		if ( pDbgReq->waitTime )
			StartIoctlTimer (DI_timeout, pDbgReq->waitTime); /* OS_DEP */
#endif /* USE_WAIT_ARGS */
	}
	InsertTailList (&Queue.List, &pDbgReq->List) ;
	DI_unlock () ;
}
#endif

#ifdef NOT_YET_NEEDED
void
DI_unpendreq (DbgRequest *pDbgReq)
{	/* LOCKED */
#if USE_WAIT_ARGS
	StopIoctlTimer ();		/* OS_DEP */
#endif /* USE_WAIT_ARGS */
	UnpendIoctl (pDbgReq) ;	/* OS_DEP */
	InsertTailList (&DbgRequestList, &pDbgReq->List) ;
	Queue.Trigger = 0 ;
	if ( !IsListEmpty (&Queue.List) )
	{
		DbgRequest *pDbgReq = (DbgRequest *)Queue.List.Flink;
		Queue.Trigger = pDbgReq->waitSize ;
#if USE_WAIT_ARGS
		if ( pDbgReq->waitTime )
			StartIoctlTimer (DI_timeout, pDbgReq->waitTime); /* OS_DEP */
#endif /* USE_WAIT_ARGS */
	}
}	/* LOCKED */
#endif

#ifdef NOT_YET_NEEDED
DbgRequest
*DI_cancelreq (void *Handle)
{
	DbgRequest *This, *pDbgReq;

	if ( !Initialized || !Handle ) return ((DbgRequest *) 0) ;

	pDbgReq = (DbgRequest *) 0 ;

	DI_lock () ;
	for ( This = (DbgRequest *)Queue.List.Flink ;
		  This != (DbgRequest *)&Queue.List ;
		  This = (DbgRequest *)This->List.Flink )
	{
		if ( This->Handle != Handle )
			continue ;

		pDbgReq = This ;

		if ( This != (DbgRequest *)Queue.List.Flink )
		{
			RemoveEntryList(&This->List) ;
			break ;
		}
/*
 * cancelling first entry needs nearly the same handling as unpending it
 */
		RemoveEntryList(&This->List) ;
		Queue.Trigger = 0 ;
#if USE_WAIT_ARGS
		StopIoctlTimer ();		/* OS_DEP */
#endif /* USE_WAIT_ARGS */
		if ( !IsListEmpty (&Queue.List) )
		{
			This = (DbgRequest *)Queue.List.Flink;
			Queue.Trigger = This->waitSize ;
#if USE_WAIT_ARGS
			if ( This->waitTime )
				StartIoctlTimer (DI_timeout, This->waitTime); /*OS_DEP*/
#endif /* USE_WAIT_ARGS */
		}
		break ;
	}
	DI_unlock () ;

	return (pDbgReq) ;
}
#endif

void
DI_finit (void)
{
#ifdef NOT_YET_NEEDED
	DbgRequest *pDbgReq;

	DI_lock () ;
#if USE_WAIT_ARGS
	StopIoctlTimer ();		/* OS_DEP */
#endif /* USE_WAIT_ARGS */
	while ( !IsListEmpty (&Queue.List) )
	{
		pDbgReq = (DbgRequest *) RemoveHeadList (&Queue.List) ;
		pDbgReq->Status =  STATUS_CANCELLED ;
		DI_unpendreq (pDbgReq) ;
	}
	DI_unlock () ;
#endif
}

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
unsigned long
DI_dilog (char *ubuf, unsigned long usize)
{
	DbgMessage		*msg ;
	LOG				*log ;
	unsigned long	size, time ;
	int				mtype ;

	if ( !Initialized || !(log = (LOG *)ubuf) || (usize < sizeof(*log)) )
	{
		return (0) ;
	}
/*
 *	copy next dilog compatible message to user buffer, discard all others
 */
	DI_lock () ;
	for ( size = 0 ; (size == 0) && (msg = queuePeekMsg ()) ; )
	{
		mtype = DT_INDEX(msg->type);
		if ( ( (mtype == MSG_TYPE_STRING) || (DLI_NAME(msg->type) == DLI_XLOG) )
		  && (size = msg->size) )
		{
			if ( size > sizeof (log->buffer) )
				 size = sizeof (log->buffer) ;
			memcpy (&log->buffer[0], &msg->data[0], size) ;
			if ( mtype == MSG_TYPE_STRING ) {
				/* Our strings are stored whith a terminating 0 but dilog.c	*/
				/* doesn't expect such kindness and adds a 0 itself.		*/
				/* We always decrement the size here even if the whole text	*/
				/* buffer contains pure text because dilog crashes when the	*/
				/* size includes the last byte of the text buffer.			*/
				/* The terminating 0 is set for programmers convenience.	*/
				log->buffer[--size] = 0 ;
				log->code = 1 ;
			} else {
				log->code = 2 ;
			}
			time = DI_wintime(&msg->NTtime) ;
			((UNALIGNED unsigned short *)&log->timeh)[0] =
										(unsigned short)(time >> 16) ;
			((UNALIGNED unsigned short *)&log->timel)[0] =
										(unsigned short)(time) ;
			size += (unsigned long)&(((LOG *)0)->buffer) ;
		}
		queueFreeMsg () ;
	}
	DI_unlock () ;

	((UNALIGNED unsigned short *)&log->length)[0] = (unsigned short) size ;

	return (size) ;
}
#endif

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
static void
DI_copyout (DbgRequest *pDbgReq)
{	/* LOCKED */
	DbgMessage   *msg ;
	char         *ubuf ;
	unsigned long usize, used ;
/*
 * copy as much messages as fit to user buffer
 */
	ubuf  = pDbgReq->Buffer ;
	usize = pDbgReq->sizeBuffer ;

 	if ( used = Queue.Used ) {
		if ( usize >= used )
		{
			queueCopyAll (ubuf) ;
			queueReset () ;
		}
		else
		{
			for (used = 0 ; msg = queuePeekMsg () ; )
			{
				unsigned long msiz = MSG_ALIGN(msg->size) ;
				if ( (used + msiz) > usize )
					break ;
				memcpy (&ubuf[used], msg, msiz) ;
				used += msiz ;
				queueFreeMsg () ;
			}
		}
	}
/*
 *	set # of copied bytes and status
 */
	pDbgReq->sizeUsed = used ;
	pDbgReq->Status = STATUS_SUCCESS ;
}	/* LOCKED */
#endif

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
static void
DI_flush (void)
{	/* LOCKED */
	DbgRequest	  *pDbgReq ;
/*
 *	no work if there is no pending request
 */
	if ( IsListEmpty (&Queue.List) )
	{
		return ;
	}
/*
 *	get first pending request
 */
	pDbgReq = (DbgRequest *)RemoveHeadList (&Queue.List) ;
/*
 *	copy messages (if any) to user buffer
 */
	DI_copyout (pDbgReq) ;
/*
 *	finish I/O request
 */
	DI_unpendreq (pDbgReq) ;

}	/* LOCKED */
#endif

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
static void
DiOutputTimeStamp (DbgMessage *msg)
{
	TIME_FIELDS   sysTime ;
	LARGE_INTEGER lclTime ;
	char          curTime[32] ;
	int           procNo ;

	if ( ScreenDebugStyle )
	{
		DI_ntlcltime (&msg->NTtime, &lclTime) ;
		DI_nttimefields (&lclTime, &sysTime) ;
		procNo = MSG_PROC_NO_GET(msg->type) ;

		if ( procNo >= 0 )
		{
			sprintf (&curTime[0], "%2d:%02d:%02d.%03d %d %d ",
					 sysTime.Hour, sysTime.Minute, sysTime.Second,
					 sysTime.Milliseconds, msg->id, procNo) ;
		}
		else
		{
			sprintf (&curTime[0], "%2d:%02d:%02d.%03d %d ",
					 sysTime.Hour, sysTime.Minute, sysTime.Second,
					 sysTime.Milliseconds, msg->id) ;
		}
		putScreen (&curTime[0]) ;
	}
}
#endif

/*****************************************************************************/

void
DI_append (unsigned short id, int type, char *log, unsigned long length)
{	/* LOCKED */

  add_to_q(type, log, length);
#ifdef NOT_YET_NEEDED
	DbgMessage *msg ;
/*
 *	append the message to the debug message queue,
 *	it's assumed here that the caller asserts "Initialized"
 */
	while ( ! (msg = queueAllocMsg (length)) )
	{
		queueFreeMsg () ;	/* free message from head of queue */
	}
	msg->id   = id ;
	msg->type = DiInsertProcessorNumber (type) ;
	memcpy ((void *)&msg->data[0], log, length) ;

/*
 *	copy important messages to screen if possible
 */
	if ( ScreenBase && (ScreenDriverMask & (1 << id)) )
	{
		if ( ScreenDebugMask & (1 << DL_INDEX(type)) )
		{
			switch (DT_INDEX(type))
			{
			case MSG_TYPE_STRING:
				DiOutputTimeStamp (msg) ;
				putScreen ((char *)log) ;

				if ( ScreenDebugStyle )
					putScreen ("\r\n") ;
				break ;

			case MSG_TYPE_BINARY:
				if ( (ScreenDebugMask & 0x80000000)
				  && ((DLI_XLOG == DLI_NAME(type)) || (id == DRV_ID_UNKNOWN)) )
				{
					DiOutputTimeStamp (msg) ;

					if ( display_xlog (log) )
					{
						if ( ScreenDebugStyle )
							putScreen ("\r\n") ;
					}
					else
					{
						if ( ScreenDebugStyle )
							putScreen ("\r") ;
					}
				}
				break ;
			}
		}
	}
/*
 *	flush the queue to user buffer if we overshot the trigger
 */
	if ( Queue.Trigger && ((Queue.Used + MSG_MAX_SIZE) >= Queue.Trigger) )
	{
		DI_flush () ;
	}
#endif

}	/* LOCKED */

/*****************************************************************************/

static void
DI_format (unsigned short id, int type, char *format, va_list argument_list)
{
#ifdef NOT_YET_NEEDED
	static char    fatal_buffer_overflow_msg[] = "<DIMAINT> FATAL - debug buffer overflow !\r\n";
#endif
	static int	InFormat = 0 ;
	static char fmtBuf[MSG_FRAME_MAX_SIZE] ;
	unsigned long  length ;
	char          *data ;
	unsigned short code ;
#ifdef NOT_YET_NEEDED
	char           msg[64] ;
#endif
	va_list        ap;
# ifdef LOGGING_ABOVE_DISPATCH
	char          *ad_buffer;

	if (DI_above_dispatch_level ())
	{
		if (!format)
			return;

		ap = argument_list;
		switch (type)
		{
		case DLI_MXLOG :
		case DLI_BLK :
		case DLI_SEND:
		case DLI_RECV:
			length = va_arg(ap, unsigned long) ;
			if ( length == 0 )
				return;
			if ( length > MaxDumpSize )
				length = MaxDumpSize ;
			ad_buffer = atomic_heap_alloc (length);
			if (ad_buffer == NULL)
				return;
			memcpy (ad_buffer, format, length);
			type  |= MSG_TYPE_BINARY ;
			break ;

		case DLI_XLOG:
			data	= va_arg(ap, char *) ;
			code	= va_arg(ap, unsigned short) ;
			length	= (unsigned long) va_arg(ap, unsigned short) ;
			if ( length > MaxXlogSize )
				length = MaxXlogSize ;
			length += 2 ;
			ad_buffer = atomic_heap_alloc (length);
			if (ad_buffer == NULL)
				return;
			ad_buffer[0] = (char)(code) ;
			ad_buffer[1] = (char)(code >> 8) ;
			if ((data != NULL) && (length > 2))
				memcpy (ad_buffer + 2, data, length - 2) ;
			type  |= MSG_TYPE_BINARY ;
			break ;

		case DLI_LOG :
		case DLI_FTL :
		case DLI_ERR :
		case DLI_TRC :
		case DLI_REG :
		case DLI_MEM :
		case DLI_SPL :
		case DLI_IRP :
		case DLI_TIM :
		case DLI_TAPI:
		case DLI_NDIS:
		case DLI_CONN:
		case DLI_STAT:
		case DLI_PRV0:
		case DLI_PRV1:
		case DLI_PRV2:
		case DLI_PRV3:
			ad_buffer = atomic_heap_alloc (sizeof(fmtBuf)) ;
			if (ad_buffer == NULL)
				return;
			length = (unsigned long)(vsprintf (ad_buffer, format, ap)) ;
			if ( (int)length <= 0 )
			{
				atomic_heap_free (ad_buffer, sizeof(fmtBuf)) ;
				return;
			}
/*
 *	if buffersize was not sufficient, print 'old style' warning and crash (?!)
 */
			++length ;

			if ( length > sizeof(fmtBuf) )
			{
				memcpy (ad_buffer, fatal_buffer_overflow_msg, sizeof(fatal_buffer_overflow_msg)) ;
				length = sizeof(fatal_buffer_overflow_msg) ;
			}
			ad_buffer = atomic_heap_resize (ad_buffer, sizeof(fmtBuf), length) ;
			type  |= MSG_TYPE_STRING ;
			break ;

		default:
			return;
		}
		if (!atomic_logqueue_put (id, type, ad_buffer, length))
		{
			atomic_heap_free (ad_buffer, length);
			return;
		}
		DI_schedule_logqueue_processing ();
		return;
	}
# endif
	ap = argument_list;
/*
 *  take the time outside of locked code, seems Win95 enables interrupts
 */
#ifdef NOT_YET_NEEDED
	DI_nttime (&Now) ;
# endif
/*
 *	lock the whole operation to prevent recursive use of 'fmtBuf' !
 */
	DI_lock () ;
	InFormat++ ;
#ifdef NOT_YET_NEEDED
	if ( InFormat > 1 )
	{
		sprintf (msg, "PANIC -> RECURSIVE CALL-%d Seq=%d\r\n",
					   InFormat, Queue.Seq) ;
		putScreen (msg) ;
		Queue.Seq++ ;
		goto unlock ;
	}
#endif
# ifdef LOGGING_ABOVE_DISPATCH
	DI_process_atomic_logqueue ();
# endif
/*
 *	check for a regilar format string
 */
#ifdef NOT_YET_NEEDED
	if ( !format )
	{
		sprintf (msg, "PANIC -> drv %d - NULL data / format ptr\r\n", id) ;
		putScreen (msg) ;
		Queue.Seq++ ;
		goto unlock ;
	}
#endif
/*
 *	if no extended debugging available, print FATAL messages in 'old style'
 */
#ifdef NOT_YET_NEEDED
	if ( !Initialized )
	{
		if ( (type == DLI_FTL) || (type == DLI_LOG) )
		{
			(void)vsprintf (&fmtBuf[0], format, ap) ;
			putScreen (&fmtBuf[0]) ;
		}
		goto unlock;
	}
#endif
/*
 *	log raw data or format output
 */
	switch (type)
	{
	case DLI_MXLOG :
	case DLI_BLK :
	case DLI_SEND:
	case DLI_RECV:
		length = va_arg(ap, unsigned long) ;
		if ( length == 0 )
		{
			goto unlock;
		}
		if ( length > MaxDumpSize )
			 length = MaxDumpSize ;

		type  |= MSG_TYPE_BINARY ;
		break ;

	case DLI_XLOG:
		data	= va_arg(ap, char *) ;
		code	= va_arg(ap, unsigned short) ;
		length	= (unsigned long) va_arg(ap, unsigned short) ;

		if ( length > MaxXlogSize )
			 length = MaxXlogSize ;

		fmtBuf[0] = (char)(code) ;
		fmtBuf[1] = (char)(code >> 8) ;
		if ( data && length )
			memcpy (&fmtBuf[2], &data[0], length) ;
		length += 2 ;

		format = &fmtBuf[0] ;
		type  |= MSG_TYPE_BINARY ;
		break ;

	case DLI_LOG :
	case DLI_FTL :
	case DLI_ERR :
	case DLI_TRC :
	case DLI_REG :
	case DLI_MEM :
	case DLI_SPL :
	case DLI_IRP :
	case DLI_TIM :
	case DLI_TAPI:
	case DLI_NDIS:
	case DLI_CONN:
	case DLI_STAT:
	case DLI_PRV0:
	case DLI_PRV1:
	case DLI_PRV2:
	case DLI_PRV3:
		length = (unsigned long)vsprintf (&fmtBuf[0], format, ap) ;
		if ( (int)length <= 0 )
		{
			goto unlock;
		}
/*
 *	if buffersize was not sufficient, print 'old style' warning and crash (?!)
 */
		++length ;

		if ( length > sizeof(fmtBuf) )
		{
#ifdef NOT_YET_NEEDED
			putScreen (fatal_buffer_overflow_msg) ;
#endif
			goto unlock;
		}
		format = &fmtBuf[0] ;
		type  |= MSG_TYPE_STRING ;
		break ;

	default:
		goto unlock ;
	}
	DI_append (id, type, format, length) ;
unlock:
	InFormat-- ;
	DI_unlock () ;
}

/*****************************************************************************/

static void
DI_format_old (unsigned short id, char *format, va_list ap)
{
	if ( format && (format[0] != 0 || format[1] == 2) )
	{
		DI_format (id, (format[0] != 0) ? DLI_TRC : DLI_XLOG, format, ap);
	}
}

/*****************************************************************************/

static void
DI_deregister (pDbgHandle hDbg)
{
	short id ;

#ifdef NOT_YET_NEEDED
	DI_nttime (&Now) ;
#endif

	if ( hDbg == NULL ) return ;

	DI_lock () ;
	for ( id = 0 ; id < sizeof(dbgQue)/sizeof(dbgQue[0]) ; id++ )
	{
		if ( hDbg == dbgQue[id].client )
		{
			dbgQue[id].client = NULL ;

			hDbg->id				= -1 ;
			hDbg->dbgMask			= 0 ;
			hDbg->dbg_end			= NULL ;
			hDbg->dbg_prt			= NULL ;
# ifdef LOGGING_ABOVE_DISPATCH
			hDbg->dbg_irq			= NULL ;
# endif
			if ( hDbg->Version > 0 )
				hDbg->dbg_old		= NULL ;
			hDbg->Registered		= 0 ;
			hDbg->next		 		= NULL ;	/* not MAGIC anymore */
			hDbg->regTime.LowPart	= 0L ;
			hDbg->regTime.HighPart	= 0L ;

			if ( myDriverDebugHandle.dbgMask & (unsigned long)DL_REG )
			{
				char buffer[128] ; int length ;
				length =
				sprintf (buffer, "DIMAINT - drv # %d = '%s' deregistered",
			         			  id, &hDbg->drvName[0]) ;
				DI_append (myDriverDebugHandle.id, MSG_TYPE_STRING | DLI_REG,
						   &buffer[0], (unsigned long)(length + 1)) ;
			}
			break ;
		}
	}
	DI_unlock () ;
}

/*****************************************************************************/

static void
DI_register (void *arg)
{
	pDbgHandle  	hDbg ;
	short			id, old_id, new_id, any_id ;

#ifdef NOT_YET_NEEDED
	DI_nttime (&Now) ;
#endif

	hDbg = (pDbgHandle)arg ;
/*
 *	Check for bad args, specially for the old obsolete debug handle
 */
	if ( !Initialized
	  || (hDbg == NULL)
	  || ((hDbg->id == 0) && (((_OldDbgHandle_ *)hDbg)->id == -1))
	  || (hDbg->Registered != 0) )
		return ;

	DI_lock () ;

	for ( id = 0, any_id = old_id = new_id = -1 ;
		  id < sizeof(dbgQue)/sizeof(dbgQue[0]) ; id++ )
	{
		if ( id == DRV_ID_UNKNOWN )
		{	/* this id is reserved for compatibility traces */
			continue ;
		}
 		if ( dbgQue[id].client != NULL )
		{
 			if (  dbgQue[id].client == hDbg )
			{	/* already registered */
				any_id = -1 ;
				break ;
			}
			continue ;
		}

		if ( any_id < 0 )
			any_id = id ;

		if ( dbgQue[id].drvName[0] == '\0' )
		{	/* slot never used before */
			if ( new_id < 0 )
				new_id = id ;
		}
		else if ( !strcmp (dbgQue[id].drvName, hDbg->drvName) )
		{	/* reuse this slot */
			old_id = id ;
			break ;
		}
	}

    if ( any_id < 0 )
	{
		;	/* forget it, already registered or queue full */
	}
	else
	{
		if ( old_id >= 0 )
			id = old_id ;
		else if	( new_id >= 0 )
			id = new_id ;
		else
			id = any_id ;

		hDbg->Registered	= DBG_HANDLE_REG_NEW ;
		hDbg->id			= id ;
		hDbg->dbg_end   	= DI_deregister ;
		hDbg->dbg_prt   	= DI_format ;
#ifdef NOT_YET_NEEDED
		hDbg->dbg_ev    	= DiProcessEventLog ;
#else
          hDbg->dbgMask = 0xffff; 
#endif
# ifdef LOGGING_ABOVE_DISPATCH
		hDbg->dbg_irq		= DI_format ;
# endif
		if ( hDbg->Version > 0 )
			hDbg->dbg_old = DI_format_old ;

		hDbg->next = (pDbgHandle)DBG_MAGIC ;
		*((ulong *)&hDbg->regTime) = Now ;

		dbgQue[id].client		   = hDbg ;
#ifdef NOT_YET_NEEDED
		dbgQue[id].regTimeLowPart  = hDbg->regTime.LowPart ;
		dbgQue[id].regTimeHighPart = hDbg->regTime.HighPart ;
#endif
		memcpy (&dbgQue[id].drvName[0], &hDbg->drvName[0],
				sizeof(dbgQue[id].drvName)) ;

#ifdef NOT_YET_NEEDED
		DBG_DBG_L(("<register> drv # %d = %s !\r\n", id, &hDbg->drvName[0])) ;
#endif

		if ( myDriverDebugHandle.dbgMask & (unsigned long)DL_REG )
		{
			char buffer[128] ; int length ;
			length =
			sprintf (buffer, "DIMAINT - drv # %d = '%s' registered",
							  id, &hDbg->drvName[0]) ;
			DI_append (myDriverDebugHandle.id, MSG_TYPE_STRING | DLI_REG,
					   &buffer[0], (unsigned long)(length + 1)) ;
		}
	}

	DI_unlock () ;
}

/*****************************************************************************/

#ifdef NOT_YET_NEEDED
unsigned long
DI_ioctl (DbgRequest *pDbgReq)
{
	DbgIoctlArgs *Args;
	pDbgHandle    hDbg ;
	DbgMessage   *msg = NULL ;
	unsigned long async, i, mask, used, need ;
/*
 *	check passed parameters
 */
	if ( !pDbgReq->Args || (pDbgReq->sizeArgs != sizeof(*Args)) )
	{
		return (pDbgReq->Status = STATUS_INVALID_PARAMETER) ;
	}
	Args = (DbgIoctlArgs *) pDbgReq->Args;

	DBG_DBG_U(("<ioctl_%ld> buf 0x%lx, size %ld\r\n",
		    	Args->arg0, pDbgReq->Buffer, pDbgReq->sizeBuffer)) ;

/*
 *	evaluate subfunction code
 */
	switch (Args->arg0)
	{
/*
 *	queue ioctl until highwater mark reached or a flush is requested
 */
	case DBG_COPY_LOGS:
		if ( !pDbgReq->Buffer || (pDbgReq->sizeBuffer < MSG_MAX_SIZE) )
		{
			return (pDbgReq->Status = STATUS_INVALID_PARAMETER) ;
		}
/*
 * if this is a sync request or the queue is already full and there is
 * no pending request copy out as much as possible to callers buffer.
 */
		if ( !(async = Args->arg1 | Args->arg2 | Args->arg3)
		  || ((Queue.Used + MSG_MAX_SIZE) >= pDbgReq->sizeBuffer) )
		{
			int done = 1 ;	/* just to hold the DI_*lock() calls paired */
			DI_lock () ;
			if ( IsListEmpty (&Queue.List) )
				DI_copyout (pDbgReq) ;
			else if ( !async )
				pDbgReq->Status = STATUS_TOO_MANY_COMMANDS ;
			else
				done = 0 ;
			DI_unlock () ;
			if ( done )
				return (pDbgReq->Status) ;
		}
/*
 * evaluate the special wait args
 */
		if ( Args->arg1 >= 256 && Args->arg1 <= pDbgReq->sizeBuffer )
			pDbgReq->waitSize = Args->arg1 ;
		else
			pDbgReq->waitSize = pDbgReq->sizeBuffer ;
#if USE_WAIT_ARGS
		pDbgReq->waitTime = (Args->arg2 >= 100) ? Args->arg2 - 20 : 0 ;
#else /* !USE_WAIT_ARGS */
		pDbgReq->waitTime = 0 ;
#endif /* USE_WAIT_ARGS */
/*
 *	First the caller must do what is necessary to pend this request.
 *  If this worked the caller must call DI_pendreq() to append this
 *  request to our pending queue.
 */
		return (pDbgReq->Status = STATUS_PENDING) ;
/*
 *	copy all debugs to pending user buffer (if any)
 */
	case DBG_FLUSH_LOGS:
		DI_lock () ;
		DI_flush () ;
		DI_unlock () ;
		return (pDbgReq->Status = STATUS_SUCCESS) ;
/*
 *	list all registered drivers, copy info to user buffer
 */
	case DBG_LIST_DRVS:
		DI_lock () ;
		for ( used = i = 0 ; i < sizeof(dbgQue)/sizeof(dbgQue[0]) ; i++ )
		{
			if ( (hDbg = dbgQue[i].client) == NULL )
				continue ;

			need = strlen (hDbg->drvName) + strlen (hDbg->drvTag) + 3 ;
			if ( (used + MSG_ALIGN(need)) > pDbgReq->sizeBuffer )
			{
				used = (unsigned long) -1 ;
				break ;
			}

			msg = (DbgMessage *)&pDbgReq->Buffer[used] ;
			msg->NTtime.LowPart	 = hDbg->regTime.LowPart ;
			msg->NTtime.HighPart = hDbg->regTime.HighPart ;
			msg->size	= (unsigned short) need ;
			msg->ffff	= 0xFFFF ;
			msg->id     = hDbg->id  ;
			msg->type   = MSG_TYPE_DRV_ID | DLI_REG ;
			msg->seq    = 0 ;
			sprintf (&msg->data[0], "%s: %s", hDbg->drvName, hDbg->drvTag) ;
			used += MSG_ALIGN(need) ;
		}
/*
 *	check for sufficient user buffersize
 */
		if ( used <= pDbgReq->sizeBuffer )
		{
			pDbgReq->sizeUsed = used ;
			pDbgReq->Status = STATUS_SUCCESS ;
		}
		else
		{
			pDbgReq->sizeUsed = 0 ;
			pDbgReq->Status = STATUS_BUFFER_TOO_SMALL ;
		}
		DI_unlock () ;
		return (pDbgReq->Status) ;

	case DBG_GET_MASK:
		pDbgReq->Status = STATUS_INVALID_PARAMETER_2 ;
		DI_lock () ;
		for ( i = 0 ; i < sizeof(dbgQue)/sizeof(dbgQue[0]) ; i++ )
		{
			if ( (hDbg = dbgQue[i].client) == NULL )
				continue ;

			if ( hDbg->id != (short)Args->arg1 )
				continue ;

			DBG_DBG_L(("<get_mask> drv %d, mask 0x%lx\r\n",
						hDbg->id, hDbg->dbgMask)) ;
			need = sizeof(unsigned long) ;
			used = MSG_ALIGN(need) ;
			if ( used > pDbgReq->sizeBuffer )
			{
				pDbgReq->sizeUsed = 0L ;
				pDbgReq->Status = STATUS_BUFFER_TOO_SMALL ;
				break ;
			}

			msg = (DbgMessage *)&pDbgReq->Buffer[0] ;
			*((LARGE_INTEGER *)&msg->NTtime) = Now ;
			msg->size = (unsigned short) need ;
			msg->ffff = 0xFFFF ;
			msg->id   = hDbg->id ;
			msg->type = MSG_TYPE_FLAGS | DLI_REG ;
			msg->seq  = 0 ;
			memcpy (&msg->data[0], &hDbg->dbgMask, need) ;
			pDbgReq->sizeUsed = used ;
			pDbgReq->Status = STATUS_SUCCESS ;
			break ;
		}
		DI_unlock () ;
		return (pDbgReq->Status) ;

	case DBG_SET_MASK:
		pDbgReq->Status = STATUS_INVALID_PARAMETER_2 ;
		if ( mask = Args->arg2 ) {
			mask |= ( DL_FTL | DL_LOG ) ;
		}
		DI_lock () ;
		for ( i = 0 ; i < sizeof(dbgQue)/sizeof(dbgQue[0]) ; i++ )
		{
			short id ; unsigned long *pdbgMask ;

			if ( ((hDbg = dbgQue[i].client) == NULL)
			  || (hDbg->next != (pDbgHandle)DBG_MAGIC)
			  || (dbgQue[i].regTimeLowPart  != hDbg->regTime.LowPart)
			  || (dbgQue[i].regTimeHighPart != hDbg->regTime.HighPart) )
				continue ;

			if ( hDbg->id == (short)Args->arg1 )
			{
				DBG_DBG_L(("<set_mask> drv %d, mask 0x%lx\r\n", hDbg->id, mask)) ;
				hDbg->dbgMask = mask ;
				pDbgReq->Status = STATUS_SUCCESS ;
				break ;
			}
		}
		DI_unlock () ;
		return (pDbgReq->Status) ;

	case DBG_GET_BUFSIZE:
		DBG_DBG_U(("<get_bufsize> %ld", Queue.Size)) ;
		if ( pDbgReq->sizeBuffer >= sizeof(unsigned long) )
		{
			*((unsigned long *)pDbgReq->Buffer) = Queue.Size ;
			pDbgReq->sizeUsed = sizeof(unsigned long) ;
			pDbgReq->Status = STATUS_SUCCESS ;
		}
		else
		{
			pDbgReq->sizeUsed = (unsigned long)(0 - sizeof(unsigned long)) ;
			pDbgReq->Status = STATUS_BUFFER_TOO_SMALL ;
		}
		return (pDbgReq->Status) ;

	case DBG_SET_BUFSIZE:
	default:
		break ;
	}
	return (pDbgReq->Status = STATUS_ILLEGAL_FUNCTION) ;
}
#endif

void
prtComp (char *format, ...)
{
	va_list ap ;
 	void   *hDbg ;

	if ( !format )
		return ;

	va_start(ap, format) ;

	if ( (format[0] != 0) || (format[1] == 2) )
	{
		/* log in compatibility mode according to format string */
		if ( myDriverDebugHandle.dbgMask & (unsigned long)DL_XLOG )
		{
			DI_format (DRV_ID_UNKNOWN, (format[0] != 0) ? DLI_TRC : DLI_XLOG,
					   format, ap);
		}
	}
	else
	{
		/* register to new log driver functions */
		if ( (format[0] == 0) && ((unsigned char)format[1] == 255) )
		{
			hDbg = va_arg(ap, void *) ;   /* ptr to DbgHandle */
			DI_register (hDbg) ;
		}
	}

	va_end (ap) ;
}

/*****************************************************************************/
void
DI_unload (void)
{
	short id ;
	pDbgHandle hDbg ;

#ifdef NOT_YET_NEEDED
	DI_nttime (&Now) ;
#endif

	DI_lock () ;
	for ( id = 0 ; id < sizeof(dbgQue)/sizeof(dbgQue[0]) ; id++ )
	{
		if ( !(hDbg = dbgQue[id].client) ) 
			continue ;
		dbgQue[id].client = NULL ;

		hDbg->id				= -1 ;
		hDbg->dbgMask			= 0 ;
		hDbg->dbg_end			= NULL ;
		hDbg->dbg_prt			= NULL ;
# ifdef LOGGING_ABOVE_DISPATCH
		hDbg->dbg_irq			= NULL ;
# endif
		hDbg->dbg_ev			= NULL ;
		if ( hDbg->Version > 0 )
			hDbg->dbg_old		= NULL ;
		hDbg->Registered		= 0 ;
		hDbg->next		 		= NULL ;	/* not MAGIC anymore */
		hDbg->regTime.LowPart	= 0L ;
		hDbg->regTime.HighPart	= 0L ;

		if ( myDriverDebugHandle.dbgMask & (unsigned long)DL_REG )
		{
			char buffer[128] ; int length ;
			length =
				sprintf (buffer, "DIMAINT - drv # %d = '%s' deregistered",
			         			  id, &hDbg->drvName[0]) ;
			DI_append (myDriverDebugHandle.id, MSG_TYPE_STRING | DLI_REG,
					   &buffer[0], (unsigned long)(length + 1)) ;
		}
	}
	DI_unlock () ;
}

/*****************************************************************************/
/*
 * To register ourself main.c calls DbgRegister() in debuglib.c,
 * DbgRegister() calls dprintf() for registration.
 * Thus a dprintf() must be available here !
 */
#ifdef NOT_YET_NEEDED
#define	  dprintf	prtComp
#include "debuglib.c"
#endif

/*****************************************************************************/
