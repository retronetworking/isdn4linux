#
# Makefile for the hisax ISDN device driver
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
MOD_IN_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS)

# The target object and module list name.

O_TARGET	:= hisax_drv.o
M_OBJS		:=
O_OBJS		:=

# Objects that export symbols.

export-objs	:= config.o

# Multipart objects.

list-multi	:= hisax.o
hisax-objs	:= config.o isdnl1.o tei.o isdnl2.o isdnl3.o \
		   lmgr.o q931.o callc.o fsm.o cert.o

# Optional parts of multipart objects.

hisax-objs-$(CONFIG_HISAX_EURO) += l3dss1.o
hisax-objs-$(CONFIG_HISAX_NI1) += l3ni1.o
hisax-objs-$(CONFIG_HISAX_1TR6) += l3_1tr6.o

hisax-objs-$(CONFIG_HISAX_16_0) += teles0.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_16_3) += teles3.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_TELESPCI) += telespci.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_S0BOX) += s0box.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_AVM_A1) += avm_a1.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_AVM_A1_PCMCIA) += avm_a1p.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_FRITZPCI) += avm_pci.o isac.o arcofi.o
hisax-objs-$(CONFIG_HISAX_AVM_A1) += avm_a1.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_ELSA) += elsa.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_IX1MICROR2) += ix1_micro.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_DIEHLDIVA) += diva.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_ASUSCOM) += asuscom.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_TELEINT) += teleint.o isac.o arcofi.o hfc_2bs0.o
hisax-objs-$(CONFIG_HISAX_SEDLBAUER) += sedlbauer.o isac.o arcofi.o hscx.o isar.o
hisax-objs-$(CONFIG_HISAX_SPORTSTER) += sportster.o isac.o arcofi.o hfc_2bs0.o
hisax-objs-$(CONFIG_HISAX_MIC) += mic.o isac.o arcofi.o hfc_2bs0.o
hisax-objs-$(CONFIG_HISAX_NETJET) += nj_s.o netjet.o isac.o arcofi.o
hisax-objs-$(CONFIG_HISAX_NETJET_U) += nj_u.o netjet.o icc.o
hisax-objs-$(CONFIG_HISAX_HFCS) += hfcscard.o hfc_2bds0.o
hisax-objs-$(CONFIG_HISAX_HFC_PCI) += hfc_pci.o
hisax-objs-$(CONFIG_HISAX_HFC_SX) += hfc_sx.o
hisax-objs-$(CONFIG_HISAX_NICCY) += niccy.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_ISURF) += isurf.o isac.o arcofi.o isar.o
hisax-objs-$(CONFIG_HISAX_HSTSAPHIR) += saphir.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_BKM_A4T) += bkm_a4t.o isac.o arcofi.o jade.o
hisax-objs-$(CONFIG_HISAX_SCT_QUADRO) += bkm_a8.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_GAZEL) += gazel.o isac.o arcofi.o hscx.o
hisax-objs-$(CONFIG_HISAX_W6692) += w6692.o
#hisax-objs-$(CONFIG_HISAX_TESTEMU) += testemu.o

hisax-objs += $(sort $(hisax-objs-y))

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_HISAX)	+= hisax.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
OX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

# The global Rules.make.

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

hisax.o: $(hisax-objs)
	$(LD) -r -o $@ $(hisax-objs)

MD5FILES += isac.c isdnl1.c isdnl2.c isdnl3.c \
	    tei.c callc.c cert.c l3dss1.c l3_1tr6.c \
	    elsa.c diva.c sedlbauer.c

CERT = $(shell md5sum -c md5sums.asc >> /dev/null;echo $$?)

cert.o:	$(MD5FILES) md5sums.asc
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -D CERTIFICATION=$(CERT) -c -o cert.o cert.c
