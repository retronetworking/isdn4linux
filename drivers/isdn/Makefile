#
# Makefile for the kernel ISDN subsystem and device drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= 
ALL_SUB_DIRS	:= icn pcbit hisax avmb1 act2000 eicon divert hysdn

# The target object and module list name.

O_TARGET	:= isdn.a
M_OBJS		:=
O_OBJS		:=

# Objects that export symbols.

export-objs	:= isdn_common.o

# Multipart objects.

list-multi	:= isdn.o
isdn-objs	:= isdn_net.o isdn_tty.o isdn_cards.o isdn_v110.o \
		   isdn_common.o

# Optional parts of multipart objects.

ifeq ($(CONFIG_ISDN_PPP),y)
	isdn-objs	+= isdn_ppp.o
endif
ifeq ($(CONFIG_ISDN_X25),y)
	isdn-objs	+= isdn_concap.o isdn_x25iface.o
endif
ifeq ($(CONFIG_ISDN_AUDIO),y)
	isdn-objs	+= isdn_audio.o
endif
ifeq ($(CONFIG_ISDN_TTY_FAX),y)
	isdn-objs	+= isdn_ttyfax.o
endif
ifeq ($(CONFIG_ISDN_WITH_ABC),y)
	isdn-objs	+= isdn_dwabc.o
endif

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Object files in subdirectories

subdir-$(CONFIG_ISDN_DIVERSION)		+= divert
subdir-$(CONFIG_ISDN_DRV_HISAX)		+= hisax
subobj-$(CONFIG_ISDN_DRV_HISAX)		+= hisax/hisax_drv.o
subdir-$(CONFIG_ISDN_DRV_ICN)		+= icn
subobj-$(CONFIG_ISDN_DRV_ICN)		+= icn/icn_obj.o
subdir-$(CONFIG_ISDN_DRV_PCBIT)		+= pcbit
subobj-$(CONFIG_ISDN_DRV_PCBIT)		+= pcbit/pcbit.o
subdir-$(CONFIG_ISDN_DRV_SC)		+= sc
subobj-$(CONFIG_ISDN_DRV_SC)		+= sc/sc.o
subdir-$(CONFIG_ISDN_CAPI)		+= avmb1
subobj-$(CONFIG_ISDN_CAPI)		+= avmb1/avmb1.o
subdir-$(CONFIG_ISDN_DRV_LOOP)		+= isdnloop
subobj-$(CONFIG_ISDN_DRV_LOOP)		+= isdnloop/isdnloop.o
subdir-$(CONFIG_ISDN_DRV_ACT2000)	+= act2000
subobj-$(CONFIG_ISDN_DRV_ACT2000)	+= act2000/act2000.o
subdir-$(CONFIG_ISDN_DRV_EICON)		+= eicon
subobj-$(CONFIG_ISDN_DRV_EICON)		+= eicon/eicon.o
subdir-$(CONFIG_HYSDN)			+= hysdn
subobj-$(CONFIG_HYSDN)			+= hysdn/hysdn.o

SUB_DIRS	+= $(subdir-y)
MOD_SUB_DIRS	+= $(subdir-m)

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN)	+= isdn.o
obj-y			+= $(subobj-y)

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
OX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

# The global Rules.make.

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

isdn.o: $(isdn-objs)
	$(LD) -r -o $@ $(isdn-objs)

