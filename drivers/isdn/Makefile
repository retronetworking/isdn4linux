#
# Makefile for the kernel ISDN subsystem and device drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
MOD_IN_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS) icn pcbit hisax avmb1 act2000 eicon divert hysdn

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

ifeq ($(CONFIG_ISDN_DIVERSION),y)
	ifeq ($(CONFIG_MODULES),y)
		MOD_SUB_DIRS += divert
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_HISAX),y)
	SUB_DIRS += hisax
	obj-y += hisax/hisax_drv.o
else
	ifeq ($(CONFIG_ISDN_DRV_HISAX),m)
		MOD_SUB_DIRS += hisax
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_ICN),y)
	SUB_DIRS += icn
	obj-y += icn/icn_obj.o
else
	ifeq ($(CONFIG_ISDN_DRV_ICN),m)
		MOD_SUB_DIRS += icn
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_PCBIT),y)
	SUB_DIRS += pcbit
	obj-y += pcbit/pcbit.o
else
	ifeq ($(CONFIG_ISDN_DRV_PCBIT),m)
		MOD_SUB_DIRS += pcbit
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_SC),y)
	SUB_DIRS += sc
	obj-y += sc/sc.o
else
	ifeq ($(CONFIG_ISDN_DRV_SC),m)
		MOD_SUB_DIRS += sc
	endif
endif

ifeq ($(CONFIG_ISDN_CAPI),y)
	SUB_DIRS += avmb1
	obj-y += avmb1/avmb1.o
else
	ifeq ($(CONFIG_ISDN_CAPI),m)
		MOD_SUB_DIRS += avmb1
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_LOOP),y)
	SUB_DIRS += isdnloop
	obj-y += isdnloop/isdnloop.o
else
	ifeq ($(CONFIG_ISDN_DRV_LOOP),m)
		MOD_SUB_DIRS += isdnloop
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_ACT2000),y)
	SUB_DIRS += act2000
	obj-y += act2000/act2000.o
else
	ifeq ($(CONFIG_ISDN_DRV_ACT2000),m)
		MOD_SUB_DIRS += act2000
	endif
endif

ifeq ($(CONFIG_ISDN_DRV_EICON),y)
	SUB_DIRS += eicon
	obj-y += eicon/eicon.o
else
	ifeq ($(CONFIG_ISDN_DRV_EICON),m)
		MOD_SUB_DIRS += eicon
	endif
endif

ifeq ($(CONFIG_HYSDN),y)
	SUB_DIRS += hysdn
	obj-y += hysdn/hysdn.o
else
	ifeq ($(CONFIG_HYSDN),m)
		MOD_SUB_DIRS += hysdn
	endif
endif

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN)	+= isdn.o

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

