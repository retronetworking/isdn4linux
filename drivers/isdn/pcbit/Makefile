HDRS=pcbit.h edss1.h callbacks.h layer2.h capi.h
SRCS=module.c edss1.c drv.c layer2.c capi.c callbacks.c
OBJS=module.o edss1.o drv.o layer2.o capi.o callbacks.o

CFLAGS=$(MCFLAGS) -I../$(INCLUDES)
#-DDEBUG
NFLAGS=-g -I../$(INCLUDES) -DDEBUG

all: pcbit.o

pcbit.o: $(OBJS)
	ld -r -o pcbit.o $(OBJS)

module.o: pcbit.h
layer2.o: pcbit.h layer2.h 
drv.o: pcbit.h edss1.h callbacks.h layer2.h capi.h
edss1.o: pcbit.h edss1.h callbacks.h
capi.o: pcbit.h capi.h edss1.h
callbacks.o: pcbit.h edss1.h callbacks.h layer2.h capi.h

clean:
	rm -f $(OBJS) *.[iso] *~
