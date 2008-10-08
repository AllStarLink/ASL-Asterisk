#
# Zaptel
# 
# Makefile for firmware downloading/installation
#
# Copyright (C) 2007, Digium, Inc.
#
# Joshua Colp <jcolp@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.PHONY: dist-clean all uninstall have_download install

-include ../menuselect.makeopts ../makeopts

PWD:=$(shell pwd)

OCT6114_064_VERSION:=1.05.01
OCT6114_128_VERSION:=1.05.01
TC400M_VERSION:=MR6.12
VPMADT032_VERSION:=1.07

FIRMWARE_URL:=http://downloads.digium.com/pub/telephony/firmware/releases

# Firmware files should use the naming convention: zaptel-fw-<base name>-<sub name>-<version> or zaptel-fw-<base name>-<version>
# First example: zaptel-fw-oct6114-064-1.05.01
# This means this is version 1.05.01 of the oct6114 64 firmware
# Second example: zaptel-fw-tc400m-MR5.6
# This means this is version MR5.6 of the tc400m firmware

# Build a list of firmware package filenames we need
FIRMWARE:=$(MENUSELECT_FIRMWARE:FIRMWARE-OCT6114-064=zaptel-fw-oct6114-064-$(OCT6114_064_VERSION).tar.gz)
FIRMWARE:=$(FIRMWARE:FIRMWARE-OCT6114-128=zaptel-fw-oct6114-128-$(OCT6114_128_VERSION).tar.gz)
FIRMWARE:=$(FIRMWARE:FIRMWARE-TC400M=zaptel-fw-tc400m-$(TC400M_VERSION).tar.gz)
FIRMWARE:=$(FIRMWARE:FIRMWARE-VPMADT032=zaptel-fw-vpmadt032-$(VPMADT032_VERSION).tar.gz)

# Build a list of object files if hotplug will not be used
OBJECT_FILES:=$(MENUSELECT_FIRMWARE:FIRMWARE-OCT6114-064=zaptel-fw-oct6114-064.o)
OBJECT_FILES:=$(OBJECT_FILES:FIRMWARE-OCT6114-128=zaptel-fw-oct6114-128.o)
OBJECT_FILES:=$(OBJECT_FILES:FIRMWARE-TC400M=zaptel-fw-tc400m.o)
OBJECT_FILES:=$(OBJECT_FILES:FIRMWARE-VPMADT032=zaptel-fw-vpmadt032.o)

# If "fetch" is used, --continue is not a valid option.
ifeq ($(WGET),wget)
WGET_ARGS:=--continue
endif

all: $(FIRMWARE)

# Clean up any downloaded/extracted firmware packages
dist-clean: clean
	rm -f zaptel-fw-*.bin
	rm -f zaptel-fw-*.tar.gz

# Clean up anything we built
clean:
	rm -f zaptel-fw-*.o

have_download:
	@if test "$(DOWNLOAD)" = ":" ; then \
		echo "**************************************************"; \
		echo "***                                            ***"; \
		echo "*** You must have either wget or fetch to be   ***"; \
		echo "*** able to automatically download and install ***"; \
		echo "*** the requested firmware packages.           ***"; \
		echo "***                                            ***"; \
		echo "**************************************************"; \
		exit 1; \
	fi

# Download and extract firmware tarballs
zaptel-fw-%.tar.gz:
	@if ( [ "$(HOTPLUG_FIRMWARE)" = "no" ] ) || ( [ -d $(DESTDIR)/usr/lib/hotplug/firmware ] && ! [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.$(subst .tar.gz,,$*) ] ) || ( [ -d $(DESTDIR)/lib/firmware ] && ! [ -f $(DESTDIR)/lib/firmware/.$(subst .tar.gz,,$*) ] ); then \
		echo "Attempting to download $@"; \
		if test ! -f $@; then $(DOWNLOAD) $(WGET_ARGS) $(FIRMWARE_URL)/$@; fi; \
		if test ! -f $@; then exit 1; fi; \
		(cat $@ | gzip -d | tar -xof -) \
	fi

# Create object files suitable for linking against
object-build: $(FIRMWARE) $(OBJECT_FILES)

$(DESTDIR)/usr/lib/hotplug/firmware $(DESTDIR)/lib/firmware:
	mkdir -p $@

# Install all downloaded firmware images for hotplug usage
hotplug-install: $(DESTDIR)/usr/lib/hotplug/firmware $(DESTDIR)/lib/firmware $(FIRMWARE)
ifneq ($(findstring zaptel-fw-oct6114-064,$(FIRMWARE)),)
ifeq ($(shell if ( [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-oct6114-064-$(OCT6114_064_VERSION) ] ) && ( [ -f $(DESTDIR)/lib/firmware/.zaptel-fw-oct6114-064-$(OCT6114_064_VERSION) ] ); then echo "no"; else echo "yes"; fi),yes)
	@echo "Installing zaptel-fw-oct6114-064.bin to hotplug firmware directories"
	@$(INSTALL) -m 644 zaptel-fw-oct6114-064.bin $(DESTDIR)/usr/lib/hotplug/firmware
	@rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-oct6114-064-*
	@touch $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-oct6114-064-$(OCT6114_064_VERSION)
	@$(INSTALL) -m 644 zaptel-fw-oct6114-064.bin $(DESTDIR)/lib/firmware
	@rm -rf $(DESTDIR)/lib/firmware/.zaptel-fw-oct6114-064-*
	@touch $(DESTDIR)/lib/firmware/.zaptel-fw-oct6114-064-$(OCT6114_064_VERSION)
else
	@echo "Firmware zaptel-fw-oct6114-064.bin is already installed with required version $(OCT6114_064_VERSION)"
endif
endif
ifneq ($(findstring zaptel-fw-oct6114-128,$(FIRMWARE)),)
ifeq ($(shell if ( [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-oct6114-128-$(OCT6114_128_VERSION) ] ) && ( [ -f $(DESTDIR)/lib/firmware/.zaptel-fw-oct6114-128-$(OCT6114_128_VERSION) ] ); then echo "no"; else echo "yes"; fi),yes)
	@echo "Installing zaptel-fw-oct6114-128.bin to hotplug firmware directories"
	@$(INSTALL) -m 644 zaptel-fw-oct6114-128.bin $(DESTDIR)/usr/lib/hotplug/firmware
	@rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-oct6114-128-*
	@touch $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-oct6114-128-$(OCT6114_128_VERSION)
	@$(INSTALL) -m 644 zaptel-fw-oct6114-128.bin $(DESTDIR)/lib/firmware
	@rm -rf $(DESTDIR)/lib/firmware/.zaptel-fw-oct6114-128-*
	@touch $(DESTDIR)/lib/firmware/.zaptel-fw-oct6114-128-$(OCT6114_128_VERSION)
else
	@echo "Firmware zaptel-fw-oct6114-128.bin is already installed with required version $(OCT6114_128_VERSION)"
endif
endif
ifneq ($(findstring zaptel-fw-tc400m,$(FIRMWARE)),)
ifeq ($(shell if ( [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-tc400m-$(TC400M_VERSION) ] ) && ( [ -f $(DESTDIR)/lib/firmware/.zaptel-fw-tc400m-$(TC400M_VERSION) ] ); then echo "no"; else echo "yes"; fi),yes)
	@echo "Installing zaptel-fw-tc400m.bin to hotplug firmware directories"
	@$(INSTALL) -m 644 zaptel-fw-tc400m.bin $(DESTDIR)/usr/lib/hotplug/firmware
	@rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-tc400m-*
	@touch $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-tc400m-$(TC400M_VERSION)
	@$(INSTALL) -m 644 zaptel-fw-tc400m.bin $(DESTDIR)/lib/firmware
	@rm -rf $(DESTDIR)/lib/firmware/.zaptel-fw-tc400m-*
	@touch $(DESTDIR)/lib/firmware/.zaptel-fw-tc400m-$(TC400M_VERSION)
else
	@echo "Firmware zaptel-fw-tc400m.bin is already installed with required version $(TC400M_VERSION)"
endif
endif
ifneq ($(findstring zaptel-fw-vpmadt032,$(FIRMWARE)),)
ifeq ($(shell if ( [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-vpmadt032-$(VPMADT032_VERSION) ] ) && ( [ -f $(DESTDIR)/lib/firmware/.zaptel-fw-vpmadt032-$(VPMADT032_VERSION) ] ); then echo "no"; else echo "yes"; fi),yes)
	@echo "Installing zaptel-fw-vpmadt032.bin to hotplug firmware directories"
	@$(INSTALL) -m 644 zaptel-fw-vpmadt032.bin $(DESTDIR)/usr/lib/hotplug/firmware
	@rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-vpmadt032-*
	@touch $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw-vpmadt032-$(VPMADT032_VERSION)
	@$(INSTALL) -m 644 zaptel-fw-vpmadt032.bin $(DESTDIR)/lib/firmware
	@rm -rf $(DESTDIR)/lib/firmware/.zaptel-fw-vpmadt032-*
	@touch $(DESTDIR)/lib/firmware/.zaptel-fw-vpmadt032-$(VPMADT032_VERSION)
else
	@echo "Firmware zaptel-fw-vpmadt032.bin is already installed with required version $(VPMADT032_VERSION)"
endif
endif

# Uninstall any installed zaptel firmware images from hotplug firmware directories
hotplug-uninstall:
	if [ -d $(DESTDIR)/usr/lib/hotplug/firmware ]; then \
		rm -f $(DESTDIR)/usr/lib/hotplug/firmware/zaptel-fw-*.bin; \
		rm -f $(DESTDIR)/usr/lib/hotplug/firmware/.zaptel-fw*; \
	fi
	if [ -d $(DESTDIR)/lib/firmware ]; then \
		rm -f $(DESTDIR)/lib/firmware/zaptel-fw-*.bin; \
		rm -f $(DESTDIR)/lib/firmware/.zaptel-fw*; \
	fi

# Build object file of an oct6114 064 firmware image for linking
zaptel-fw-oct6114-064.o: zaptel-fw-oct6114-064-$(OCT6114_064_VERSION).tar.gz zaptel-fw-oct6114-064.bin
	@echo Making firmware object file for zaptel-fw-oct6114-064.bin
	../build_tools/make_firmware_object zaptel-fw-oct6114-064.bin $@

# Build object file of an oct6114 128 firmware image for linking
zaptel-fw-oct6114-128.o: zaptel-fw-oct6114-128-$(OCT6114_128_VERSION).tar.gz zaptel-fw-oct6114-128.bin
	@echo Making firmware object file for zaptel-fw-oct6114-128.bin
	../build_tools/make_firmware_object zaptel-fw-oct6114-128.bin $@

# Build object file of a TC400M firmware image for linking
zaptel-fw-tc400m.o: zaptel-fw-tc400m-$(TC400M_VERSION).tar.gz zaptel-fw-tc400m.bin
	@echo Making firmware object file for zaptel-fw-tc400m.bin
	../build_tools/make_firmware_object zaptel-fw-tc400m.bin $@

# Build object file of a VPMADT032 firmware image for linking
zaptel-fw-vpmadt032.o: zaptel-fw-vpmadt032-$(VPMADT032_VERSION).tar.gz zaptel-fw-vpmadt032.bin
	@echo Making firmware object file for zaptel-fw-vpmadt032.bin
	../build_tools/make_firmware_object zaptel-fw-vpmadt032.bin $@
