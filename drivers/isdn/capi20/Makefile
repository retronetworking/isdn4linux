# Makefile for capi20.

# The target object and module list name.

O_TARGET	:= capi_drv.o

# Objects that export symbols.

export-objs	:= capifs.o

# Multipart objects.

list-multi	:= 

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

# Ordering constraints: kernelcapi.o first

obj-$(CONFIG_ISDN_KCAPI_CAPI20)		+= capi.o
obj-$(CONFIG_ISDN_KCAPI_CAPIFS)		+= capifs.o

# The global Rules.make.

include $(TOPDIR)/drivers/isdn/Rules.make


