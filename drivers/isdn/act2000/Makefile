# Makefile for the act2000 ISDN device driver

O_TARGET	:= vmlinux-obj.o

list-multi	:= act2000.o
act2000-objs	:= module.o capi.o act2000_isa.o

obj-$(CONFIG_ISDN_DRV_ACT2000)	+= act2000.o

include $(TOPDIR)/Rules.make

act2000.o: $(act2000-objs)
	$(LD) -r -o $@ $(act2000-objs)
