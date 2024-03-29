#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for channel drivers
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include ../menuselect.makeopts ../menuselect.makedeps

MENUSELECT_CATEGORY=CHANNELS
MENUSELECT_DESCRIPTION=Channel Drivers

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard chan_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard chan_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_CHANNELS),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_CHANNELS),$(ALL_CC_MODS))

ifeq ($(OSARCH),OpenBSD)
  PTLIB=-lpt_OpenBSD_x86_r
  H323LIB=-lh323_OpenBSD_x86_r
endif

ifeq ($(OSARCH),linux-gnu)
  PTLIB=-lpt_linux_x86_r
  H323LIB=-lh323_linux_x86_r
  CHANH323LIB=-ldl
endif

ifeq ($(OSARCH),FreeBSD)
  PTLIB=-lpt_FreeBSD_x86_r
  H323LIB=-lh323_FreeBSD_x86_r
  CHANH323LIB=-pthread
endif

ifeq ($(OSARCH),NetBSD)
  PTLIB=-lpt_NetBSD_x86_r
  H323LIB=-lh323_NetBSD_x86_r
endif

ifeq ($(wildcard h323/libchanh323.a),)
  CC_MODS:=$(filter-out chan_h323,$(CC_MODS))
endif

ifndef OPENH323DIR
  OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
  PWLIBDIR=$(HOME)/pwlib
endif

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring channels,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	rm -f busy.h ringtone.h gentone
	$(MAKE) -C misdn clean

ifneq ($(wildcard h323/Makefile.ast),)
  include h323/Makefile.ast
H323LDFLAGS+=-Wl,--version-script=h323/noexport.map
clean::
	if [ -f h323/Makefile ]; then $(MAKE) -C h323 clean; fi
else
h323/libchanh323.a h323/Makefile.ast:
	$(CMD_PREFIX) $(MAKE) -C h323
	$(CMD_PREFIX) rm -f ../main/asterisk
	$(CMD_PREFIX) echo "***************************************************************"
	$(CMD_PREFIX) echo
	$(CMD_PREFIX) echo "********** Re-run 'make' to pick up H.323 parameters **********"
	$(CMD_PREFIX) echo
	$(CMD_PREFIX) echo "***************************************************************"
	$(CMD_PREFIX) exit 1
endif

dist-clean::
	rm -f h323/Makefile

gentone: gentone.c
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(HOST_CC) $(STATIC_BUILD) -o $@ $(HOST_CFLAGS) $(HOST_LDFLAGS) $^ $(LIBS)
gentone: LIBS+=-lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: busy.h ringtone.h

$(if $(filter chan_iax2,$(EMBEDDED_MODS)),modules.link,chan_iax2.so): iax2-parser.o iax2-provision.o

chan_iax2.so: LIBS+=-lcurl

chan_alsa.o: busy.h ringtone.h

ifeq ($(OSARCH),linux-gnu)
chan_h323.so: chan_h323.o h323/libchanh323.a h323/Makefile.ast
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CXX) $(PTHREAD_CFLAGS) $(ASTLDFLAGS) $(SOLINK) $(H323LDFLAGS) -o $@ $< h323/libchanh323.a $(H323LDLIBS)
else
chan_h323.so: chan_h323.o h323/libchanh323.a
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CXX) $(PTHREAD_CFLAGS) $(ASTLDFLAGS) $(SOLINK) -o $@ $< h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lcrypto -lssl -lexpat
endif

chan_misdn.o: ASTCFLAGS+=-Imisdn

misdn_config.o: ASTCFLAGS+=-Imisdn

misdn/isdn_lib.o: ASTCFLAGS+=-Wno-strict-aliasing

misdn_config.o misdn/isdn_lib.o misdn/isdn_msg_parser.o: ASTCFLAGS+=$(MENUSELECT_OPTS_chan_misdn:%=-D%) $(foreach dep,$(MENUSELECT_DEPENDS_chan_misdn),$(value $(dep)_INCLUDE))

$(if $(filter chan_misdn,$(EMBEDDED_MODS)),modules.link,chan_misdn.so): chan_misdn.o misdn_config.o misdn/isdn_lib.o misdn/isdn_msg_parser.o

chan_usbradio.o: chan_usbradio.c xpmr/xpmr.c xpmr/xpmr.h xpmr/xpmr_coef.h xpmr/sinetabx.h busy.h ringtone.h

chan_usbradio.so: LIBS+=-lusb -lasound

chan_simpleusb.o: chan_simpleusb.c busy.h ringtone.h

chan_simpleusb.so: LIBS+=-lusb -lasound

chan_beagle.so: LIBS+=-lasound

chan_pi.so: LIBS+=-lasound

