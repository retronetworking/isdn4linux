I4LVERSION=2.0.28

.EXPORT_ALL_VARIABLES:

#
# SMP = 1
# SMP_PROF = 1
#
ARCH      = i386
KERNELDIR = /usr/src/linux
MODDEST   = /lib/modules/`uname -r`/misc

######### NOTHING TO CHANGE BELOW ################

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	else if [ -x /bin/bash ]; then echo /bin/bash; \
	else echo sh; fi ; fi)
KCONFIG         = $(KERNELDIR)/.config
ARCHMAKE        = $(KERNELDIR)/arch/$(ARCH)/Makefile

TOPDIR  := $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
ISDNINC  := $(ISDNTOP)/include

HPATH			= $(KERNELDIR)/include
HOSTCC			=gcc -I$(HPATH) -I$(ISDNINC)
HOSTCFLAGS		=-O2 -fomit-frame-pointer

CROSS_COMPILE	=

AS      =$(CROSS_COMPILE)as
LD      =$(CROSS_COMPILE)ld
CC      =$(CROSS_COMPILE)gcc -g -D__KERNEL__ -I$(HPATH)
CPP     =$(CC) -E
AR      =$(CROSS_COMPILE)ar
NM      =$(CROSS_COMPILE)nm
STRIP   =$(CROSS_COMPILE)strip
MAKE    =make

ifeq ($(KCONFIG),$(wildcard $(KCONFIG)))
include $(KCONFIG)
ifeq ($(CONFIG_MODULES),y)
CONFIG_ISDN=m
CONFIG_ISDN_PPP=y
CONFIG_ISDN_PPP_VJ=y
CONFIG_ISDN_MPP=y
CONFIG_ISDN_AUDIO=y
CONFIG_ISDN_DRV_ICN=m
CONFIG_ISDN_DRV_PCBIT=m
CONFIG_ISDN_DRV_TELES=m
CONFIG_ISDN_DRV_HISAX=m
CONFIG_HISAX_16_0=y
CONFIG_HISAX_16_3=y
CONFIG_HISAX_AVM_A1=y
CONFIG_HISAX_ELSA_PCC=y
CONFIG_HISAX_IX1MICROR2=y
CONFIG_HISAX_EURO=y
CONFIG_HISAX_1TR6=y
CONFIG_ISDN_DRV_SC=m
CONFIG_ISDN_DRV_AVMB1=m
do-it-all: modules
else
do-it-all: modconf-error
endif
else
do-it-all: unconf-error
endif

CFLAGS = -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce

ifdef CONFIG_CPP
CFLAGS := $(CFLAGS) -x c++
endif

ifdef SMP
CFLAGS += -D__SMP__
AFLAGS += -D__SMP__

ifdef SMP_PROF
CFLAGS += -D__SMP_PROF__
AFLAGS += -D__SMP_PROF__
endif
endif

include $(ARCHMAKE)

SUBDIRS := drivers/isdn

MODFLAGS = -DMODULE
ifdef CONFIG_MODVERSIONS
MODFLAGS += -DMODVERSIONS -include $(HPATH)/linux/modversions.h
endif

all: do-it-all

unconf-error:
	@echo ""
	@echo "Cannot find configured kernel."
	@echo "Make shure, you have our Kernel configured, and"
	@echo "the definition of KERNELDIR points to the proper location."
	@echo ""

modconf-error:
	@echo ""
	@echo "Your have disbled CONFIG_MODULES in your kernel configuration."
	@echo "Without that option, this package cannot compile."
	@echo "Reconfigure your kernel, then come back here and start again."
	@echo ""

$(KERNELDIR)/linux/version.h: $(KERNELDIR)/Makefile
	@cd $(KERNELDIR)
	$(MAKE) include/linux/version.h

modules: $(KERNELDIR)/include/linux/version.h
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) -C $$i CFLAGS="$(CFLAGS) $(MODFLAGS)" MAKING_MODULES=1 modules; \
	done

rootperm:
	@if [ `id -u` != 0 ] ; then \
		echo -e "\n\n      Need root permission for installation!\n\n"; \
		exit 1; \
	fi

kinstall: rootperm
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) -C $$i kinstall; \
	done

clean:
	rm -f `find . -name '*.[iso]' -print`
	rm -f `find . -type f -name '*~' -print`
	rm -f core `find . -type f -name 'core' -print`

include Rules.make
