# Makefile for kernelcapi.

O_TARGET	:= vmlinux-obj.o

export-objs	:= kcapi.o capiutil.o

list-multi	:= kernelcapi.o
kernelcapi-objs	:= kcapi.o capiutil.o

obj-$(CONFIG_ISDN_KCAPI)		+= kernelcapi.o

include $(TOPDIR)/Rules.make

kernelcapi.o: $(kernelcapi-objs)
	$(LD) -r -o $@ $(kernelcapi-objs)

