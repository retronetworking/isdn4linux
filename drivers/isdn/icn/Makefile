MODULES=icn.o
CFLAGS=$(MCFLAGS) -I../$(INCLUDES)
all: $(MODULES)

icn.o:	icn.c icn.h ../$(INCLUDES)/linux/isdnif.h
	$(CC) $(CFLAGS) -c icn.c

install: all
	install -o root -g root -m 0644 $(MODULES) $(KERNELDIR)/modules

clean:
	rm -f *.[iso] *~ core

