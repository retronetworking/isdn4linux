VERSION=3.1.91
SUBDIRS=drivers/isdn
# Where to install the modules ($KERNELDIR/modules)
export KERNELDIR = /usr/src/linux
export INCLUDES=../../include
export CC = gcc -I $(INCLUDES)
export MCFLAGS = -m486 -O6 -Wall -D__KERNEL__ -DLINUX -DMODULE
#
# Use next Definition of CFLAGS to get Assembler-Listings for extensive
# debugging
#export MCFLAGS = -m486 -g -v --save-temps -O6 -Wall -D__KERNEL__ -DLINUX -DMODULE
export PCFLAGS = -O2 -Wall

all: subdirs

subdirs:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

clean:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
	rm -f *.[iso] *~ core include/*~

install: all
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i install; done
	cd $(KERNELDIR); make modules_install; /sbin/depmod -a

