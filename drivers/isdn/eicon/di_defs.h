
/*
 *
  Copyright (c) Eicon Networks, 2000.
 *
  This source file is supplied for the exclusive use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    1.9
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
#ifndef _DI_DEFS_  
/*------------------------------------------------------------------*/
/* File: di_defs.h                                                  */
/* Copyright (c) Eicon Networks 1993-2000                           */
/*                                                                  */
/* definitions for isdn driver interface                            */
/*------------------------------------------------------------------*/
#define _DI_DEFS_
        /* typedefs for our data structures                         */
typedef struct get_name_s GET_NAME;
/*  The entity_s structure is used to pass all 
    parameters between application and IDI   */
typedef struct entity_s ENTITY;
typedef struct buffers_s BUFFERS;
typedef struct postcall_s POSTCALL;
#define BOARD_NAME_LENGTH 9
#define IDI_CALL_LINK_T
#define IDI_CALL_ENTITY_T
/* typedef void ( * IDI_CALL)(ENTITY *); */
/* --------------------------------------------------------
    IDI_CALL
   -------------------------------------------------------- */
typedef void (IDI_CALL_LINK_T * IDI_CALL)(ENTITY IDI_CALL_ENTITY_T *);
typedef struct {
  word length;          /* length of data/parameter field           */
  byte P[270];          /* data/parameter field                     */
} DBUFFER;
struct get_name_s {
  word command;         /* command = 0x0100 */
  byte name[BOARD_NAME_LENGTH];
};
struct postcall_s {
  word      command;                           /* command = 0x0300 */
  word      dummy;                             /* not used */
  void      (  * callback)(void   *);      /* call back */
  void    *context;                          /* context pointer */
};
struct buffers_s {
  word PLength;
  byte   * P;
};
struct entity_s {
  byte                  Req;            /* pending request          */
  byte                  Rc;             /* return code received     */
  byte                  Ind;            /* indication received      */
  byte                  ReqCh;          /* channel of current Req   */
  byte                  RcCh;           /* channel of current Rc    */
  byte                  IndCh;          /* channel of current Ind   */
  byte                  Id;             /* ID used by this entity   */
  byte                  GlobalId;       /* reserved field           */
  byte                  XNum;           /* number of X-buffers      */
  byte                  RNum;           /* number of R-buffers      */
  BUFFERS                 * X;        /* pointer to X-buffer list */
  BUFFERS                 * R;        /* pointer to R-buffer list */
  word                  RLength;        /* length of current R-data */
  DBUFFER   *         RBuffer;        /* buffer of current R-data */
  byte                  RNR;            /* receive not ready flag   */
  byte                  complete;       /* receive complete status  */
  IDI_CALL              callback;
  word                  user[2];
        /* fields used by the driver internally                     */
  byte                  No;             /* entity number            */
  byte                  reserved2;      /* reserved field           */
  byte                  More;           /* R/X More flags           */
  byte                  MInd;           /* MDATA coding for this ID */
  byte                  XCurrent;       /* current transmit buffer  */
  byte                  RCurrent;       /* current receive buffer   */
  word                  XOffset;        /* offset in x-buffer       */
  word                  ROffset;        /* offset in r-buffer       */
};
typedef struct {
  byte                  type;
  byte                  channels;
  word                  features;
  IDI_CALL              request;
} DESCRIPTOR;
        /* descriptor type field coding */
#define IDI_ADAPTER_S           1
#define IDI_ADAPTER_PR          2
#define IDI_ADAPTER_DIVA        3
#define IDI_ADAPTER_MAESTRA     4
#define IDI_VADAPTER            0x40
#define IDI_DRIVER              0x80
#define IDI_DADAPTER            0xfd
#define IDI_DIDDPNP             0xfe
#define IDI_DIMAINT             0xff
        /* Hardware IDs ISA PNP */
#define HW_ID_DIVA_PRO     3    /* same as IDI_ADAPTER_DIVA    */
#define HW_ID_MAESTRA      4    /* same as IDI_ADAPTER_MAESTRA */
#define HW_ID_PICCOLA      5
#define HW_ID_DIVA_PRO20   6
#define HW_ID_DIVA20       7
#define HW_ID_DIVA_PRO20_U 8
#define HW_ID_DIVA20_U     9
#define HW_ID_DIVA30       10
#define HW_ID_DIVA30_U     11
        /* Hardware IDs PCI */
#define HW_ID_EICON_PCI              0x1133
#define HW_ID_SIEMENS_PCI            0x8001 /* unused SubVendor ID for Siemens Cornet-N cards */
#define HW_ID_PROTTYPE_CORNETN       0x0014 /* SubDevice ID for Siemens Cornet-N cards */
#define HW_ID_DIVA_PRO20_PCI         0xe001
#define HW_ID_DIVA20_PCI             0xe002
#define HW_ID_DIVA_PRO20_PCI_U       0xe003
#define HW_ID_DIVA20_PCI_U           0xe004
#define HW_ID_DIVA201_PCI            0xe005
#define HW_ID_DIVA_CT_ST             0xe006
#define HW_ID_DIVA_CT_U              0xe007
#define HW_ID_DIVA_CTL_ST            0xe008
#define HW_ID_DIVA_CTL_U             0xe009
#define HW_ID_DIVA202_PCI_ST         0xe00b
#define HW_ID_DIVA202_PCI_U          0xe00c
#define HW_ID_MAESTRA_PCI            0xe010
#define HW_ID_MAESTRAQ_PCI           0xe012
#define HW_ID_MAESTRAP_PCI           0xe014
#define HW_ID_DSRV_VOICE_Q8M_PCI     0xe016
#define HW_ID_DSRV_VOICE_P30M_V2_PCI 0xe018
/* --------------------------------------------------------------------------
  Adapter array change notification framework
  -------------------------------------------------------------------------- */
typedef void (IDI_CALL_LINK_T* didd_adapter_change_callback_t)(     void IDI_CALL_ENTITY_T * context, DESCRIPTOR* adapter, int removal);
/* -------------------------------------------------------------------------- */
#define DI_VOICE          0x0 /* obsolete define */
#define DI_FAX3           0x1
#define DI_MODEM          0x2
#define DI_POST           0x4
#define DI_V110           0x8
#define DI_V120           0x10
#define DI_POTS           0x20
#define DI_CODEC          0x40
#define DI_MANAGE         0x80
#define DI_V_42           0x0100
#define DI_EXTD_FAX       0x0200 /* Extended FAX (ECM, 2D, T.6, Polling) */
#define DI_AT_PARSER      0x0400 /* Build-in AT Parser in the L2 */
#define DI_VOICE_OVER_IP  0x0800 /* Voice over IP support */
typedef void (IDI_CALL_LINK_T* _IDI_CALL)(void*, ENTITY*);  
#endif  