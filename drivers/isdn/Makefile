MODULES=isdn.o
OBJS=isdn_common.o isdn_net.o isdn_tty.o isdn_ppp.o isdn_audio.o
PROGS=debugvar
SUBDIRS=teles icn pcbit hisax

CFLAGS=$(MCFLAGS)

all: $(MODULES) $(PROGS) subdirs

subdirs:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

isdn.o:	$(OBJS)
	ld -r -o isdn.o $(OBJS)

isdn_common.o:	isdn_common.c $(INCLUDES)/linux/isdn.h $(INCLUDES)/linux/isdnif.h
	$(CC) $(CFLAGS) -c $<

isdn_net.o:	isdn_net.c $(INCLUDES)/linux/isdn.h $(INCLUDES)/linux/isdnif.h
	$(CC) $(CFLAGS) -c $<

isdn_tty.o:	isdn_tty.c $(INCLUDES)/linux/isdn.h $(INCLUDES)/linux/isdnif.h
	$(CC) $(CFLAGS) -c $<

isdn_ppp.o:	isdn_ppp.c $(INCLUDES)/linux/isdn.h $(INCLUDES)/linux/isdnif.h
	$(CC) $(CFLAGS) -c $<

isdn_audio.o:	isdn_audio.c $(INCLUDES)/linux/isdn.h $(INCLUDES)/linux/isdnif.h
	$(CC) $(CFLAGS) -c $<

debugvar: debugvar.c $(INCLUDES)/linux/isdn.h $(INCLUDES)/linux/isdnif.h
	$(CC) -m486 -O6 -Wall -D__KERNEL__ -DLINUX -D__DEBUGVAR__ -o debugvar debugvar.c

install: all
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i install; done
	install -o root -g bin -m 0750 $(PROGS) /sbin
	install -o root -g root -m 0644 $(MODULES) $(KERNELDIR)/modules

clean:
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
	rm -f *.[iso] $(PROGS) *~ core
