I4LVERSION=2.0.28
xxx=xxx
KERNELDIR = /usr/src/linux

######### NOTHING TO CHANGE BELOW ################
.EXPORT_ALL_VARIABLES:

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	else if [ -x /bin/bash ]; then echo /bin/bash; \
	else echo sh; fi ; fi)
KCONFIG         = $(KERNELDIR)/.config

TOPDIR   := $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
ISDNINC  := $(ISDNTOP)/include

#
# Get VERSION, PATCHLEVEL, SUBLEVEL, ARCH, SMP and SMP_PROF from Kerneltree
#
VERSION    = $(shell head -9 $(KERNELDIR)/Makefile |grep VERSION |awk '{print $$3}')
PATCHLEVEL = $(shell head -9 $(KERNELDIR)/Makefile |grep PATCHLEVEL |awk '{print $$3}')
SUBLEVEL   = $(shell head -9 $(KERNELDIR)/Makefile |grep SUBLEVEL |awk '{print $$3}')
ARCH       = $(shell head -9 $(KERNELDIR)/Makefile |grep ARCH |awk '{print $$3}')
ifneq ("$(shell egrep '^ *SMP *=.*' $(KERNELDIR)/Makefile)","")
	SMP = 1
endif
ifneq ("$(shell egrep '^ *SMP_PROF *=.*' $(KERNELDIR)/Makefile)","")
	SMP_PROF = 1
endif
ARCHMAKE        := $(KERNELDIR)/arch/$(ARCH)/Makefile
MODDEST			=/lib/modules/$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)/misc
HPATH			=$(KERNELDIR)/include
HOSTCC			=gcc -I$(HPATH) -I$(ISDNINC)
HOSTCFLAGS		=-O2 -fomit-frame-pointer
CROSS_COMPILE	=
AS				=$(CROSS_COMPILE)as
LD				=$(CROSS_COMPILE)ld
CC				=$(CROSS_COMPILE)gcc -g -D__KERNEL__ -I$(HPATH)
CPP				=$(CC) -E
AR				=$(CROSS_COMPILE)ar
NM				=$(CROSS_COMPILE)nm
STRIP			=$(CROSS_COMPILE)strip
MAKE			=make

ifeq ($(KCONFIG),$(wildcard $(KCONFIG)))
include $(KCONFIG)
ifeq ($(CONFIG_ISDN),m)
include .config
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
	@echo 'main(int argc,char**argv){unlink(argv[0]);return(getuid()==0);}'>g
	@if gcc -x c -o G g && rm -f g && ./G ; then \
		echo -e "\n\n      Need root permission for installation!\n\n"; \
		exit 1; \
	fi

modules_install: rootperm
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) -C $$i modules_install; \
	done
	depmod -a $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)

clean:
	rm -f `find . -name '*.[iso]' -print`
	rm -f `find . -type f -name '*~' -print`
	rm -f core `find . -type f -name 'core' -print`

include Rules.make
