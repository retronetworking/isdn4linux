# Makefile for the eicon diva isdn drivers.

O_TARGET        := vmlinux-obj.o

export-objs     := diva_didd.o 

list-multi      := diva2i4l.o divadidd.o divacapi.o divas.o diva_mnt.o diva_idi.o
divas-objs      := divasmain.o di.o io.o istream.o diva.o dlist.o divasproc.o

diva2i4l-objs   := i4l_idi.o i4lididrv.o
divacapi-objs   := capimain.o message.o 
divadidd-objs   := diva_didd.o dadapter.o
diva_mnt-objs   := divamnt.o debug.o
diva_idi-objs   := divasi.o um_idi.o dqueue.o dlist.o

divas-objs-$(CONFIG_ISDN_DIVAS_BRIPCI)  += os_bri.o  s_bri.o
divas-objs-$(CONFIG_ISDN_DIVAS_4BRIPCI) += os_4bri.o s_4bri.o
divas-objs-$(CONFIG_ISDN_DIVAS_PRIPCI)  += os_pri.o  s_pri.o

divas-objs += $(sort $(divas-objs-y))

obj-$(CONFIG_ISDN_DIVAS)               += divadidd.o divas.o diva_mnt.o diva_idi.o
obj-$(CONFIG_ISDN_DIVAS_DIVA2I4L)      += diva2i4l.o
obj-$(CONFIG_ISDN_DIVAS_DIVACAPI)      += divacapi.o

include $(TOPDIR)/Rules.make

divas.o: $(divas-objs)
		$(LD) -r -o $@ $(divas-objs)

diva2i4l.o: $(diva2i4l-objs)
		$(LD) -r -o $@ $(diva2i4l-objs)

divacapi.o: $(divacapi-objs)
		$(LD) -r -o $@ $(divacapi-objs)

divadidd.o: $(divadidd-objs)
		$(LD) -r -o $@ $(divadidd-objs)

diva_mnt.o: $(diva_mnt-objs)
		$(LD) -r -o $@ $(diva_mnt-objs)

diva_idi.o: $(diva_idi-objs)
		$(LD) -r -o $@ $(diva_idi-objs)

