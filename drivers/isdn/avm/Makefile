# Makefile for the AVM ISDN device drivers

O_TARGET	:= avm.o

export-objs	:= b1dma.o b1.o # b1pcmcia.o

#obj-$(CONFIG_ISDN_DRV_AVM_B1ISA)	+= b1isa.o b1.o
obj-$(CONFIG_ISDN_DRV_AVM_B1PCI)	+= b1pci.o b1.o b1dma.o
#obj-$(CONFIG_ISDN_DRV_AVM_B1PCMCIA)	+= b1pcmcia.o b1.o
#obj-$(CONFIG_ISDN_DRV_AVM_AVM_CS)	+= avm_cs.o
#obj-$(CONFIG_ISDN_DRV_AVM_T1ISA)	+= t1isa.o b1.o
#obj-$(CONFIG_ISDN_DRV_AVM_T1PCI)	+= t1pci.o b1.o b1dma.o
#obj-$(CONFIG_ISDN_DRV_AVM_C4)		+= c4.o b1.o

include $(TOPDIR)/Rules.make
