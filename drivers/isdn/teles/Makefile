CFLAGS=$(MCFLAGS) -I../$(INCLUDES)
OBJS=mod.o card.o config.o buffers.o tei.o isdnl2.o isdnl3.o llglue.o \
     q931.o callc.o fsm.o

all:	teles.o

isdnl3.o: l3_1TR6.c l3_1TR6.h isdnl3.c

$(OBJS): teles.h

teles.o: $(OBJS)
	ld -r -o teles.o $(OBJS)

clean:
	rm -f *.[iso] *~ core
