# Makefile for the pcbit ISDN device driver

O_TARGET	:= vmlinux-obj.o

list-multi	:= pcbit.o
pcbit-objs	:= module.o edss1.o drv.o layer2.o capi.o callbacks.o

obj-$(CONFIG_ISDN_DRV_PCBIT)	+= pcbit.o

include $(TOPDIR)/Rules.make

pcbit.o: $(pcbit-objs)
	$(LD) -r -o $@ $(pcbit-objs)
