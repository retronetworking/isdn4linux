#
# $Id$
#
# Makefile for the CAPI2.0 driver of the FRITZ!Card PNP
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# $Log$
#

VPATH=../fc

CARD=fcpnp

EXTRA_CFLAGS += -I.. -D__$(CARD)__ -DTARGET=\"$(CARD)\"

#
# Objects that don't export a symtab
#
L_OBJS := # used as component of an L_TARGET
O_OBJS := # used as component of an O_TARGET
M_OBJS := # used as module
#
# Objects that do export a symtab
#
LX_OBJS := # used as component of an L_TARGET
OX_OBJS := # used as component of an O_TARGET
MX_OBJS := # used as module
#
# Targets, created by linking others
#
O_TARGET := # used for .o targets (from O and OX objects)
L_TARGET := # used for .a targets (from L and LX objects)

ifeq ($(CONFIG_ISDN_CAPI),y)
  	O_TARGET += $(CARD).o
  	O_OBJS   += driver.o lib.o main.o queue.o tables.o tools.o
  	O_OBJS   += $(CARD)-lib.o
	O_OBJS   += main.o
else
  ifeq ($(CONFIG_ISDN_CAPI),m)
  	O_TARGET += $(CARD).o
  	O_OBJS   += driver.o lib.o queue.o tables.o tools.o
  	O_OBJS   += $(CARD)-lib.o
	OX_OBJS  += main.o
  	M_OBJS   += $(CARD).o
  endif
endif

include $(TOPDIR)/Rules.make
