#
# Zaptel kernel 2.4 makefile
#
# Copyright (C) 2008 Digium, Inc.
#

CFLAGS+=-DSTANDALONE_ZAPATA

ifeq ($(MAKELEVEL),0)
PWD:=$(shell pwd)
endif

KINCLUDES:=$(KSRC)/include

BUILDVER:=linux24

HOTPLUG_FIRMWARE:=no

ifeq ($(HOTPLUG_FIRMWARE),yes)
  CFLAGS+=-DHOTPLUG_FIRMWARE
endif

ifeq ($(ARCH),i386)
ifneq ($(wildcard $(PWD)/hpec/hpec_x86_32.o_shipped),)
HPEC_PRESENT=yes
endif
endif

ifeq ($(ARCH),x86_64)
ifneq ($(wildcard $(PWD)/hpec/hpec_x86_64.o_shipped),)
HPEC_PRESENT=yes
endif
endif

BUILD_MODULES:=$(BUILD_TOPDIR_MODULES) $(BUILD_SUBDIR_MODULES)
ALL_MODULES	:= $(BUILD_TOPDIR_MODULES:%=%.o) \
	$(join $(BUILD_SUBDIR_MODULES:%=%/), $(BUILD_SUBDIR_MODULES:%=%.o))

OPTFLAG=-O2
CFLAGS+=-I. $(OPTFLAGS) -g -fPIC -Wall -DBUILDING_TONEZONE #-DTONEZONE_DRIVER
ifneq (,$(findstring ppc,$(UNAME_M)))
CFLAGS_PPC:=-fsigned-char
endif
ifneq (,$(findstring x86_64,$(UNAME_M)))
CFLAGS_x86_64:=-m64
endif
CFLAGS+=$(CFLAGS_PPC) $(CFLAGS_x86_64)
KFLAGS=-I$(KINCLUDES) -O6
KFLAGS+=-DMODULE -D__KERNEL__ -DEXPORT_SYMTAB -I$(KSRC)/drivers/net \
	-Wall -I. -Wstrict-prototypes -fomit-frame-pointer -I$(KSRC)/drivers/net/wan -I$(KINCLUDES)/net
ifneq (,$(wildcard $(KINCLUDES)/linux/modversions.h))
  KFLAGS+=-DMODVERSIONS -include $(KINCLUDES)/linux/modversions.h
endif
ifneq (,$(findstring ppc,$(UNAME_M)))
KFLAGS_PPC:=-msoft-float -fsigned-char
endif
KFLAGS+=$(KFLAGS_PPC)
  ifneq (,$(findstring x86_64,$(UNAME_M)))
    KFLAGS+=-mcmodel=kernel
  endif

#
# Features are now configured in zconfig.h
#

MODULE_ALIASES=wcfxs wctdm8xxp wct2xxp

KFLAGS+=-DSTANDALONE_ZAPATA

MOD_DIR:=/lib/modules/$(KVERS)/misc

MAKE_SUBDIR	= $(MAKE) CC=$(CC) LD=$(LD) \
		  KFLAGS="$(KFLAGS) -I.." CFLAGS="$(CFLAGS) -I.."

all: modules

ifeq ($(HPEC_PRESENT),yes)
ifeq ($(ARCH),i386)
ZAPTEL_HPEC:=hpec/hpec_x86_32.o_shipped
endif

ifeq ($(ARCH),x86_64)
ZAPTEL_HPEC:=hpec/hpec_x86_64.o_shipped
endif

KFLAGS+=-DECHO_CAN_HPEC -I$(PWD)/hpec
zaptel-base.o: hpec/hpec_zaptel.h hpec/hpec_user.h
endif

prereq: tor2fw.h radfw.h version.h

tor2fw.h: tormenta2.rbt makefw 
	./makefw $< tor2fw > $@

radfw.h: pciradio.rbt makefw
	./makefw $< radfw > $@

makefw: makefw.c
	$(HOSTCC) -o $@ $^
			

modules: $(ALL_MODULES)

wct4xxp/wct4xxp.o:
	$(MAKE_SUBDIR) -C wct4xxp

tor2.o: tor2-hw.h tor2fw.h

zaptel-base.o: digits.h arith.h sec.h sec-2.h kb1ec.h mg2ec.h zconfig.h

wcusb.o: wcusb.h

wctdm.o: wctdm.h

wctdm24xxp/wctdm24xxp.o:
	$(MAKE_SUBDIR) -C wctdm24xxp

wcte12xp/wcte12xp.o:
	$(MAKE_SUBDIR) -C wcte12xp

pciradio.o: radfw.h

ztdummy.o: ztdummy.h

zaptel.o: zaptel-base.o $(ZAPTEL_HPEC)
	$(LD) -r -o $@ $< $(ZAPTEL_HPEC)

$(filter-out zaptel.o,$(BUILD_TOPDIR_MODULES:%=%.o)) zaptel-base.o: %.o: %.c zaptel.h
	$(CC) $(KFLAGS) -o $@ -c $<


clean:
	rm -f makefw tor2fw.h radfw.h
	rm -f *.o 
	$(MAKE_SUBDIR) -C wct4xxp clean
	rm -rf .tmp_versions
	rm -f core

distclean: dist-clean

dist-clean: clean

.PHONY: distclean dist-clean clean all modules

