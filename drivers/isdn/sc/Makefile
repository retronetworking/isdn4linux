# Makefile for the sc ISDN device driver

O_TARGET	:= vmlinux-obj.o

list-multi	:= sc.o
sc-objs		:= shmem.o init.o debug.o packet.o command.o event.o \
		   ioctl.o interrupt.o message.o timer.o	

obj-$(CONFIG_ISDN_DRV_SC)	+= sc.o

include $(TOPDIR)/Rules.make

sc.o: $(sc-objs)
	$(LD) -r -o $@ $(sc-objs)
