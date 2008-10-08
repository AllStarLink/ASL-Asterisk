#
# libpri: An implementation of Primary Rate ISDN
#
# Written by Mark Spencer <markster@linux-support.net>
#
# Copyright (C) 2001, Linux Support Services, Inc.
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
#
#
# Uncomment if you want libpri not send PROGRESS_INDICATOR w/ALERTING
#ALERTING=-DALERTING_NO_PROGRESS

# Uncomment if you want libpri to count number of Q921/Q931 sent/received
#LIBPRI_COUNTERS=-DLIBPRI_COUNTERS

CC=gcc
GREP=grep
AWK=awk

OSARCH=$(shell uname -s)
PROC?=$(shell uname -m)

# SONAME version; should be changed on every ABI change
# please don't change it needlessly; it's perfectly fine to have a SONAME
# of 1.2 and a version of 1.4.x
SONAME:=1.4

STATIC_LIBRARY=libpri.a
DYNAMIC_LIBRARY:=libpri.so.$(SONAME)
STATIC_OBJS=copy_string.o pri.o q921.o prisched.o q931.o pri_facility.o version.o
DYNAMIC_OBJS=copy_string.lo pri.lo q921.lo prisched.lo q931.lo pri_facility.lo version.lo
CFLAGS=-Wall -Werror -Wstrict-prototypes -Wmissing-prototypes -g -fPIC $(ALERTING) $(LIBPRI_COUNTERS)
INSTALL_PREFIX=$(DESTDIR)
INSTALL_BASE=/usr
SOFLAGS:=-Wl,-h$(DYNAMIC_LIBRARY)
LDCONFIG = /sbin/ldconfig
ifneq (,$(findstring X$(OSARCH)X, XLinuxX XGNU/kFreeBSDX))
LDCONFIG_FLAGS=-n
else
ifeq (${OSARCH},FreeBSD)
LDCONFIG_FLAGS=-m
CFLAGS += -I../zaptel -I../zapata
INSTALL_BASE=/usr/local
endif
endif
ifeq (${OSARCH},SunOS)
CFLAGS += -DSOLARIS -I../zaptel-solaris
LDCONFIG = 
LDCONFIG_FLAGS = \# # Trick to comment out the period in the command below
#INSTALL_PREFIX = /opt/asterisk  # Uncomment out to install in standard Solaris location for 3rd party code
endif

export PRIVERSION

PRIVERSION:=$(shell GREP=$(GREP) AWK=$(AWK) build_tools/make_version .)

#The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
#This works for even old (2.96) versions of gcc and provides a small boost either way.
#A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesnt support it.
ifeq ($(PROC),sparc64)
PROC=ultrasparc
CFLAGS += -mtune=$(PROC) -O3 -pipe -fomit-frame-pointer -mcpu=v8
endif

all: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

update:
	@if [ -d .svn ]; then \
		echo "Updating from Subversion..." ; \
		fromrev="`svn info | $(AWK) '/Revision: / {print $$2}'`"; \
		svn update | tee update.out; \
		torev="`svn info | $(AWK) '/Revision: / {print $$2}'`"; \
		echo "`date`  Updated from revision $${fromrev} to $${torev}." >> update.log; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -b4- ; \
		fi ; \
		rm -f update.out; \
	else \
		echo "Not under version control";  \
	fi

install: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)
	mkdir -p $(INSTALL_PREFIX)$(INSTALL_BASE)/lib
	mkdir -p $(INSTALL_PREFIX)$(INSTALL_BASE)/include
ifneq (${OSARCH},SunOS)
	install -m 644 libpri.h $(INSTALL_PREFIX)$(INSTALL_BASE)/include
	install -m 755 $(DYNAMIC_LIBRARY) $(INSTALL_PREFIX)$(INSTALL_BASE)/lib
	if [ -x /usr/sbin/sestatus ] && ( /usr/sbin/sestatus | grep "SELinux status:" | grep -q "enabled"); then /sbin/restorecon -v $(INSTALL_PREFIX)$(INSTALL_BASE)/lib/$(DYNAMIC_LIBRARY); fi
	( cd $(INSTALL_PREFIX)$(INSTALL_BASE)/lib ; ln -sf libpri.so.$(SONAME) libpri.so)
	install -m 644 $(STATIC_LIBRARY) $(INSTALL_PREFIX)$(INSTALL_BASE)/lib
	if test $$(id -u) = 0; then $(LDCONFIG) $(LDCONFIG_FLAGS) $(INSTALL_PREFIX)$(INSTALL_BASE)/lib; fi
else
	install -f $(INSTALL_PREFIX)$(INSTALL_BASE)/include -m 644 libpri.h
	install -f $(INSTALL_PREFIX)$(INSTALL_BASE)/lib -m 755 $(DYNAMIC_LIBRARY)
	( cd $(INSTALL_PREFIX)$(INSTALL_BASE)/lib ; ln -sf libpri.so.$(SONAME) libpri.so)
	install -f $(INSTALL_PREFIX)$(INSTALL_BASE)/lib -m 644 $(STATIC_LIBRARY)
endif

uninstall:
	@echo "Removing Libpri"
	rm -f $(INSTALL_PREFIX)$(INSTALL_BASE)/lib/libpri.so.$(SONAME)
	rm -f $(INSTALL_PREFIX)$(INSTALL_BASE)/lib/libpri.so
	rm -f $(INSTALL_PREFIX)$(INSTALL_BASE)/lib/libpri.a
	rm -f $(INSTALL_PREFIX)$(INSTALL_BASE)/include/libpri.h

pritest: pritest.o
	$(CC) -o pritest pritest.o -L. -lpri -lzap $(CFLAGS)

testprilib.o: testprilib.c
	$(CC) $(CFLAGS) -D_REENTRANT -D_GNU_SOURCE -o $@ -c $<

testprilib: testprilib.o
	$(CC) -o testprilib testprilib.o -L. -lpri -lpthread $(CFLAGS)

pridump: pridump.o
	$(CC) -o pridump pridump.o -L. -lpri $(CFLAGS)

MAKE_DEPS= -MD -MT $@ -MF .$(subst /,_,$@).d -MP

%.o: %.c
	$(CC) $(CFLAGS) $(MAKE_DEPS) -c -o $@ $<

%.lo: %.c
	$(CC) $(CFLAGS) $(MAKE_DEPS) -c -o $@ $<

$(STATIC_LIBRARY): $(STATIC_OBJS)
	ar rcs $(STATIC_LIBRARY) $(STATIC_OBJS)
	ranlib $(STATIC_LIBRARY)

$(DYNAMIC_LIBRARY): $(DYNAMIC_OBJS)
	$(CC) -shared $(SOFLAGS) -o $@ $(DYNAMIC_OBJS)
	$(LDCONFIG) $(LDCONFIG_FLAGS) .
	ln -sf libpri.so.$(SONAME) libpri.so

version.c: FORCE
	@build_tools/make_version_c > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

clean:
	rm -f *.o *.so *.lo *.so.$(SONAME)
	rm -f testprilib $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)
	rm -f pritest pridump
	rm -f .*.d

.PHONY:

FORCE:

ifneq ($(wildcard .*.d),)
   include .*.d
endif
