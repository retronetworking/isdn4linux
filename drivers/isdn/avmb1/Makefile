#
# Makefile for the kernel ISDN subsystem and device drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
MOD_IN_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS)

# The target object and module list name.

O_TARGET	:= avmb1.o
M_OBJS		:=
O_OBJS		:=

# Objects that export symbols.

export-objs	:= kcapi.o capiutil.o b1dma.o b1pcmcia.o b1.o capifs.o

# Multipart objects.

list-multi	:= kernelcapi.o
kernelcapi-objs	:= kcapi.o

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_CAPI)			+= kernelcapi.o capiutil.o
obj-$(CONFIG_ISDN_CAPI_CAPI20)		+= capi.o 
obj-$(CONFIG_ISDN_CAPI_CAPIDRV)		+= capidrv.o
obj-$(CONFIG_ISDN_CAPI_CAPIFS)		+= capifs.o
obj-$(CONFIG_ISDN_DRV_AVMB1_B1ISA)	+= b1isa.o b1.o
obj-$(CONFIG_ISDN_DRV_AVMB1_B1PCI)	+= b1pci.o b1.o b1dma.o
obj-$(CONFIG_ISDN_DRV_AVMB1_B1PCMCIA)	+= b1pcmcia.o b1.o
obj-$(CONFIG_ISDN_DRV_AVMB1_AVM_CS)	+= avm_cs.o
obj-$(CONFIG_ISDN_DRV_AVMB1_T1ISA)	+= t1isa.o b1.o
obj-$(CONFIG_ISDN_DRV_AVMB1_T1PCI)	+= t1pci.o b1.o b1dma.o
obj-$(CONFIG_ISDN_DRV_AVMB1_C4)		+= c4.o b1.o

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

kernelcapi.o: $(kernelcapi-objs)
	$(LD) -r -o $@ $(kernelcapi-objs)

