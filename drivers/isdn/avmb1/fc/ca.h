/*
 * Copyright (C) 2000 AVM GmbH. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, and WITHOUT 
 * ANY LIABILITY FOR ANY DAMAGES arising out of or in connection 
 * with the use or performance of this software. See the
 * GNU General Public License for further details.
 *
 */

#ifndef __have_ca_h__
#define __have_ca_h__

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void CA_INIT (unsigned Len, void (* Register) (void *adata, unsigned ApplId),
                            void (* Release) (void * adata),
                            void (* Down) (void));

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
char *CA_PARAMS (void);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
int CA_GET_MESSAGE (unsigned char *Msg);
void CA_PUT_MESSAGE (unsigned char *Msg);

unsigned char *CA_NEW_DATA_B3_IND (unsigned ApplId, unsigned long NCCI, 
                                                              unsigned Index);
void CA_FREE_DATA_B3_REQ (unsigned ApplId, unsigned char *data);

int CA_NEW_NCCI (unsigned ApplId, unsigned long NCCI, unsigned WindowSize, 
                                                          unsigned BlockSize);
void CA_FREE_NCCI (unsigned ApplId, unsigned long NCCI);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void *CA_MALLOC (unsigned);
void CA_FREE (void *p);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#define MSEC_PER_SEC 1000l

unsigned long CA_MSEC (void);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
unsigned CA_KARTE (void);
unsigned CA_BLOCKSIZE (unsigned ApplId);
unsigned CA_WINDOWSIZE (unsigned ApplId);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
typedef struct {

    unsigned  Nr;
    char     *Buffer;
} __ApplsFirstNext, *_ApplsFirstNext;

void *CA_APPLDATA (unsigned ApplId);
_ApplsFirstNext CA_APPLDATA_FIRST (_ApplsFirstNext s);
_ApplsFirstNext CA_APPLDATA_NEXT (_ApplsFirstNext s);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
typedef enum {
    CA_TIMER_END     = 0,
    CA_TIMER_RESTART = 1
} CA_RESTARTTIMER;

int CA_TIMER_NEW (unsigned nTimers);
void CA_TIMER_DELETE (void);

int CA_TIMER_START (unsigned          index,
                    unsigned long     TimeoutValue,
                    unsigned long     param,
                    CA_RESTARTTIMER (*f)(unsigned long param));
int CA_TIMER_STOP (unsigned index);

void CA_TIMER_POLL (void);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
void CA_PRINTF (char *s, ...);

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#if !defined (NDEBUG)

void CA_PUTS (char *);
void CA_PUTI (int);
void CA_PUTL (long);
void CA_PUTC (char);
void CA_PUTNL (void);

#endif

/*---------------------------------------------------------------------------*\
\*---------------------------------------------------------------------------*/
#endif
