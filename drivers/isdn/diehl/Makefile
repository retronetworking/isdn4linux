#diehl_load.h:	dosdrv/dnload.bin dosdrv/prload.bin
#	echo '/*-----------------------------------------------------*/' > $@
#	echo '/*                                                     */' >> $@
#	echo '/* This code is propietary material of Diehl ISDN GmbH */' >> $@
#	echo '/* Copyright (c) Diehl ISDN GmbH                       */' >> $@
#	echo '/*                                                     */' >> $@
#	echo '/*-----------------------------------------------------*/' >> $@
#	echo '' >> $@
#	echo 'static u_char diehl_dnload[] = {' >> $@
#	hexdump -v -e '16/1 "0x%02x," "\n"' dosdrv/dnload.bin >> $@
#	echo '};' >> $@
#	echo 'static u_char diehl_prload[] = {' >> $@
#	hexdump -v -e '16/1 "0x%02x," "\n"' dosdrv/prload.bin >> $@
#	echo '};' >> $@

L_OBJS :=
M_OBJS :=
O_OBJS := module.o diehl_isa.o diehl_pci.o idi.o

O_TARGET :=
ifeq ($(CONFIG_ISDN_DRV_DIEHL),y)
  O_TARGET += diehl.o
else
  O_TARGET += diehl.o
  M_OBJS   = diehl.o
endif

include $(TOPDIR)/Rules.make
