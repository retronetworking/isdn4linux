# Makefile for the dss1_divert ISDN module

O_TARGET		:= vmlinux-obj.o

list-multi		:= dss1_divert.o
dss1_divert-objs	:= isdn_divert.o divert_procfs.o divert_init.o

obj-$(CONFIG_ISDN_DIVERSION)	+= dss1_divert.o

include $(TOPDIR)/Rules.make

dss1_divert.o: $(dss1_divert-objs)
	$(LD) -r -o $@ $(dss1_divert-objs)






