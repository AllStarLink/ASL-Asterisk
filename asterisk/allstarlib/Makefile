#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for Allstar lib functions
#
# Copyright (C).
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#
TODO
-include ../menuselect.makeopts ../menuselect.makedeps

MENUSELECT_CATEGORY=ALLSTARLIB
MENUSELECT_DESCRIPTION=Allstar Radio Lib

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard *.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard *.cc))

C_MODS:=$(filter-out $(MENUSELECT_CDR),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_CDR),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring cdr,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
