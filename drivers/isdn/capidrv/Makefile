# Makefile for capidrv.

# The target object and module list name.

O_TARGET	:= capidrv_drv.o

# Objects that export symbols.

export-objs	:=

# Multipart objects.

list-multi	:= 

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

# Ordering constraints: kernelcapi.o first

obj-$(CONFIG_ISDN_KCAPI_CAPIDRV)	+= capidrv.o

# The global Rules.make.

include $(TOPDIR)/drivers/isdn/Rules.make


