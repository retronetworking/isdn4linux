L_OBJS :=
M_OBJS :=
O_OBJS := isdnl1.o tei.o isdnl2.o isdnl3.o \
          lmgr.o q931.o callc.o fsm.o

# EXTRA_CFLAGS += -S

ifeq ($(CONFIG_HISAX_EURO),y)
	 O_OBJS += l3dss1.o
endif

ifeq ($(CONFIG_HISAX_NI1),y)
       O_OBJS += l3ni1.o
endif

ifeq ($(CONFIG_HISAX_1TR6),y)
	 O_OBJS += l3_1tr6.o
endif

ISAC_OBJ :=
HSCX_OBJ :=
HFC_OBJ :=

ifeq ($(CONFIG_HISAX_16_0),y)
	 O_OBJS += teles0.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_16_3),y)
	 O_OBJS += teles3.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_AVM_A1),y)
	 O_OBJS += avm_a1.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_ELSA),y)
	 O_OBJS += elsa.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_IX1MICROR2),y)
	 O_OBJS += ix1_micro.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_DIEHLDIVA),y)
	 O_OBJS += diva.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_DYNALINK),y)
	 O_OBJS += dynalink.o
	 ISAC_OBJ := isac.o
	 HSCX_OBJ := hscx.o
endif

ifeq ($(CONFIG_HISAX_TELEINT),y)
	 O_OBJS += teleint.o
	 ISAC_OBJ := isac.o
	 HFC_OBJ := hfc_2bs0.o
endif

O_OBJS += config.o $(ISAC_OBJ) $(HSCX_OBJ) $(HFC_OBJ)

O_TARGET :=

ifeq ($(CONFIG_ISDN_DRV_HISAX),y)
  O_TARGET += hisax.o
else
  ifeq ($(CONFIG_ISDN_DRV_HISAX),m)
    O_TARGET += hisax.o
    M_OBJS += hisax.o
  endif
endif

include $(TOPDIR)/Rules.make
