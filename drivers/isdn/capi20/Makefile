# Makefile for capi20.

O_TARGET		:= vmlinux-obj.o

export-objs		:= capi.o capifs.o

list-multi		:= capitty.o capi_legacy.o

capi_legacy-objs	:= capiminor.o capiraw.o capinc_tty.o
capi_legacy-objs-$(CONFIG_ISDN_KCAPI_CAPIFS) += capifs.o
capi_legacy-objs	+= $(capi_legacy-objs-y)

capitty-objs		:= capitty_dev.o capitty_tty.o

obj-$(CONFIG_ISDN_KCAPI_CAPI20)		+= capi.o
obj-$(CONFIG_ISDN_KCAPI_LEGACY) 	+= capi_legacy.o
obj-$(CONFIG_ISDN_KCAPI_PPPTTY) 	+= capi_ppptty.o
obj-$(CONFIG_ISDN_KCAPI_CAPITTY)	+= capitty.o

include $(TOPDIR)/drivers/isdn/Rules.make

capi_legacy.o: $(capi_legacy-objs)
	$(LD) -r -o $@ $(capi_legacy-objs)

capitty.o: $(capitty-objs)
	$(LD) -r -o $@ $(capitty-objs)
