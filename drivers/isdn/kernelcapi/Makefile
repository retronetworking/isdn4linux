# Makefile for kernelcapi.

# The target object and module list name.

O_TARGET	:= kcapi_drv.o

# Objects that export symbols.

export-objs	:= kcapi.o capiutil.o

# Multipart objects.

list-multi	:= kernelcapi.o
kernelcapi-objs	:= kcapi.o capiutil.o

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

# Ordering constraints: kernelcapi.o first

obj-$(CONFIG_ISDN_KCAPI)		+= kernelcapi.o

# The global Rules.make.

include $(TOPDIR)/drivers/isdn/Rules.make

# Link rules for multi-part drivers.

kernelcapi.o: $(kernelcapi-objs)
	$(LD) -r -o $@ $(kernelcapi-objs)

