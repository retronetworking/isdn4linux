/*
 *
  Copyright (c) Eicon Technology Corporation, 2000.
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
/* file: debug_if.h                                                 */
/*------------------------------------------------------------------*/
# ifndef DEBUG_IF___H
# define DEBUG_IF___H

/*------------------------------------------------------------------*/
/* debug request parameter block structure  						*/
/*------------------------------------------------------------------*/

#ifdef NOT_YET_NEEDED
typedef struct DbgRequest
{
  LIST_ENTRY		List ;		/* list of pending i/o requests	*/
  void	       *Handle ;	/* the original request (IRP)	*/
  unsigned long	Tag1 ;		/* additional request info		*/
  unsigned long	Tag2 ;		/* additional request info		*/
  unsigned long	Tag3 ;		/* additional request info		*/
  char	       *Args ;		/* argument buffer				*/
  unsigned long	sizeArgs ;	/* size of argument buffer		*/
  char	       *Buffer ;	/* buffer to copy out logs		*/
  unsigned long	sizeBuffer ;/* size of copy out buffer		*/
  unsigned long	waitTime ;	/* time limit (milliseconds)	*/
  unsigned long	waitSize ;	/* size threshold 				*/
  unsigned long	sizeUsed ;	/* size of copied data			*/
  NTSTATUS	Status ;	/* final operation status		*/
} DbgRequest ;
#endif

/*------------------------------------------------------------------*/
/* exported debug functions for use in main.c						*/
/*------------------------------------------------------------------*/

void  DI_init (void *Buffer, unsigned long sizeBuffer,
               unsigned long maxDump, unsigned long maxXlog,
               unsigned long ScreenMask, unsigned long ScreenStyle,
               unsigned long DriverMask) ;
void  DI_finit (void) ;
#ifdef NOT_YET_NEEDED
DbgRequest  *DI_getreq (void) ;
void  DI_relreq (DbgRequest *pDbgReq) ;
void  DI_pendreq (DbgRequest *pDbgReq) ;
DbgRequest  *DI_cancelreq (void *Handle) ;
unsigned long  DI_ioctl (DbgRequest *pDbgReq) ;
unsigned long  DI_dilog (char *ubuf, unsigned long usize) ;
void  DI_append (unsigned short id, int type, char *log, unsigned long length) ;
#endif
void  DI_unload (void) ;
void prtComp (char *format, ...) ;


# endif /* DEBUG_IF___H */
