#
# $Id$
#
# Makefile for the CAPI and AVM-B1 device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# $Log$
# Revision 2.2  1997/02/12 09:31:39  calle
#
# Revision 1.1  1997/01/31 10:32:20  calle
# Initial revision
#
#

L_OBJS :=
M_OBJS :=
O_OBJS := 

O_TARGET := 
ifeq ($(CONFIG_ISDN_DRV_AVMB1),y)
  O_TARGET += avmb1.o
  O_OBJS += capidrv.o capi.o kernelcapi.o capiutil.o
else
  ifeq ($(CONFIG_ISDN_DRV_AVMB1),m)
  O_TARGET += avmb1.o
  M_OBJS += capidrv.o capi.o kernelcapi.o capiutil.o
  endif
endif

include $(TOPDIR)/Rules.make
 
kernelcapi.o: b1lli.o b1capi.o
	ld -x -r -o $@ $^

