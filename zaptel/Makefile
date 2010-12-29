#
# Makefile for Zaptel driver modules and utilities
#
# Copyright (C) 2001-2007 Digium, Inc.
#
#

ifneq ($(KBUILD_EXTMOD),)
# We only get in here if we're from kernel 2.6 <= 2.6.9 and going through 
# Kbuild. Later versions will include Kbuild instead of Makefile.
include $(src)/Kbuild

else

CFLAGS+=-DSTANDALONE_ZAPATA -DBUILDING_TONEZONE

ifeq ($(MAKELEVEL),0)
PWD:=$(shell pwd)
export PWD
endif

ifeq ($(ARCH),)
ARCH:=$(shell uname -m | sed -e s/i.86/i386/)
endif

ifeq ($(ARCH),armv7l)
ARCH:=arm
endif

ifeq ($(DEB_HOST_GNU_TYPE),)
UNAME_M:=$(shell uname -m)
else
UNAME_M:=$(DEB_HOST_GNU_TYPE)
endif

# If you want to build for a kernel other than the current kernel, set KVERS
ifndef KVERS
KVERS:=$(shell uname -r)
endif
ifndef KSRC
  ifneq (,$(wildcard /lib/modules/$(KVERS)/build))
    KSRC:=/lib/modules/$(KVERS)/build
  else
    KSRC_SEARCH_PATH:=/usr/src/linux-2.4 /usr/src/linux
    KSRC:=$(shell for dir in $(KSRC_SEARCH_PATH); do if [ -d $$dir ]; then echo $$dir; break; fi; done)
  endif
endif
KVERS_MAJ:=$(shell echo $(KVERS) | cut -d. -f1-2)
KVERS_POINT:=$(shell echo $(KVERS) | cut -d. -f3 | cut -d- -f1)
KINCLUDES:=$(KSRC)/include

# We use the kernel's .config file as an indication that the KSRC
# directory is indeed a valid and configured kernel source (or partial
# source) directory.
#
# We also source it, as it has the format of Makefile variables list.
# Thus we will have many CONFIG_* variables from there.
KCONFIG:=$(KSRC)/.config
ifneq (,$(wildcard $(KCONFIG)))
  HAS_KSRC=yes
  include $(KCONFIG)
else
  HAS_KSRC=no
endif

ifeq ($(KVERS_MAJ),2.4)
  BUILDVER:=linux24
else
  BUILDVER:=linux26
endif

# Set HOTPLUG_FIRMWARE=no to override automatic building with hotplug support
# if it is enabled in the kernel.
ifeq ($(BUILDVER),linux26)
  ifneq (,$(wildcard $(DESTDIR)/etc/udev/rules.d))
    DYNFS=yes
    UDEVRULES=yes
  endif
  ifeq (yes,$(HAS_KSRC))
    HOTPLUG_FIRMWARE:=$(shell if grep -q '^CONFIG_FW_LOADER=[ym]' $(KCONFIG); then echo "yes"; else echo "no"; fi)
  endif
endif

ifneq (,$(findstring $(CONFIG_DEVFS_FS),y m))
  DYNFS=yes
  HAS_DEVFS=yes
endif

# If the file .zaptel.makeopts is present in your home directory, you can
# include all of your favorite menuselect options so that every time you download
# a new version of Asterisk, you don't have to run menuselect to set them.
# The file /etc/zaptel.makeopts will also be included but can be overridden
# by the file in your home directory.

GLOBAL_MAKEOPTS=$(wildcard /etc/zaptel.makeopts)
USER_MAKEOPTS=$(wildcard ~/.zaptel.makeopts)

ifeq ($(strip $(foreach var,clean distclean dist-clean update,$(findstring $(var),$(MAKECMDGOALS)))),)
 ifneq ($(wildcard menuselect.makeopts),)
  include menuselect.makeopts
 endif
endif

ifeq ($(strip $(foreach var,clean distclean dist-clean update,$(findstring $(var),$(MAKECMDGOALS)))),)
 ifneq ($(wildcard makeopts),)
  include makeopts
 endif
endif

ifeq ($(BUILDVER),linux24)
INCOMPAT_MODULES:=xpp wcte12xp wctc4xxp wctdm24xxp zttranscode
endif

ifeq ($(BUILDVER),linux26)
ifneq ($(findstring $(KVERS_POINT),1 2 3 4 5 6 7 8),)
INCOMPAT_MODULES:=wcte12xp wctc4xxp wctdm24xxp zttranscode
endif
endif

MENUSELECT_MODULES+=$(sort $(INCOMPAT_MODULES))

ifeq ($(findstring xpp,$(MENUSELECT_MODULES)),)
  BUILD_XPP:=yes
endif

SUBDIRS_UTILS_ALL:= kernel/xpp/utils ppp
SUBDIRS_UTILS	:=
ifeq ($(BUILD_XPP),yes)
  SUBDIRS_UTILS	+= kernel/xpp/utils
endif
#SUBDIRS_UTILS	+= ppp

TOPDIR_MODULES:=pciradio tor2 torisa wcfxo wct1xxp wctdm wcte11xp wcusb zaptel ztd-eth ztd-loc ztdummy ztdynamic zttranscode
SUBDIR_MODULES:=wct4xxp wctc4xxp xpp wctdm24xxp wcte12xp
TOPDIR_MODULES+=$(MODULES_EXTRA)
SUBDIR_MODULES+=$(SUBDIRS_EXTRA)
BUILD_TOPDIR_MODULES:=$(filter-out $(MENUSELECT_MODULES),$(TOPDIR_MODULES))
BUILD_SUBDIR_MODULES:=$(filter-out $(MENUSELECT_MODULES),$(SUBDIR_MODULES))
BUILD_MODULES:=$(BUILD_TOPDIR_MODULES) $(BUILD_SUBDIR_MODULES)

MOD_DESTDIR:=zaptel

KERN_DIR:=kernel

#NOTE NOTE NOTE
#
# all variables set before the include of Makefile.kernel26 are needed by the 2.6 kernel module build process

ifneq ($(KBUILD_EXTMOD),)

obj-m:=$(BUILD_TOPDIR_MODULES:%=%.o)
obj-m+=$(BUILD_SUBDIR_MODULES:%=%/)

include $(src)/Makefile.kernel26

else
KBUILD_OBJ_M=$(BUILD_TOPDIR_MODULES:%=%.o) $(BUILD_SUBDIR_MODULES:%=%/)

ifeq ($(BUILDVER),linux24)
  INSTALL_MODULES:=$(BUILD_TOPDIR_MODULES:%=$(KERN_DIR)/%.o)
  INSTALL_MODULES+=$(foreach mod,$(BUILD_SUBDIR_MODULES),$(KERN_DIR)/$(mod)/$(mod).o)
  ALL_MODULES:=$(TOPDIR_MODULES:%=$(KERN_DIR)/%.o)
  ALL_MODULES+=$(SUBDIR_MODULES:%=$(KERN_DIR)/%/%.o)
else
  ALL_MODULES:=$(TOPDIR_MODULES:%=%.ko)
  ALL_MODULES+=$(foreach mod,$(filter-out xpp,$(SUBDIR_MODULES)),$(mod)/$(mod).ko)
  ALL_MODULES+=$(patsubst %,xpp/%.ko,xpp_usb xpd_fxo xpd_fxs xpp)
endif

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
ifeq ($(KVERS_MAJ),2.4)
  ifneq (,$(findstring x86_64,$(UNAME_M)))
    KFLAGS+=-mcmodel=kernel
  endif
endif

#
# Features are now configured in zconfig.h
#

MODULE_ALIASES=wcfxs wctdm8xxp wct2xxp

KFLAGS+=-DSTANDALONE_ZAPATA
CFLAGS+=-DSTANDALONE_ZAPATA
ifeq ($(BUILDVER),linux24)
KMAKE	= $(MAKE) -C kernel HOTPLUG_FIRMWARE=no \
  HOSTCC=$(HOSTCC) ARCH=$(ARCH) KSRC=$(KSRC) LD=$(LD) CC=$(CC) \
  UNAME_M=$(UNAME_M) \
  BUILD_TOPDIR_MODULES="$(BUILD_TOPDIR_MODULES)" BUILD_SUBDIR_MODULES="$(BUILD_SUBDIR_MODULES)"
else
KMAKE  = $(MAKE) -C $(KSRC) ARCH=$(ARCH) SUBDIRS=$(PWD)/kernel \
  HOTPLUG_FIRMWARE=$(HOTPLUG_FIRMWARE) KBUILD_OBJ_M="$(KBUILD_OBJ_M)"
endif
KMAKE_INST = $(KMAKE) \
  INSTALL_MOD_PATH=$(DESTDIR) INSTALL_MOD_DIR=misc modules_install

ROOT_PREFIX=

CONFIG_FILE=/etc/zaptel.conf
CFLAGS+=-DZAPTEL_CONFIG=\"$(CONFIG_FILE)\"

# sample makefile "trace print"
#tracedummy=$(shell echo ====== GOT HERE ===== >&2; echo >&2)

CHKCONFIG	:= $(wildcard /sbin/chkconfig)
UPDATE_RCD	:= $(wildcard /usr/sbin/update-rc.d)
ifeq (,$(DESTDIR))
  ifneq (,$(CHKCONFIG))
    ADD_INITD	:= $(CHKCONFIG) --add zaptel
  else
    ifndef (,$(UPDATE_RCD))
      ADD_INITD	:= $(UPDATE_RCD) zaptel defaults 15 30
    endif
  endif
endif

INITRD_DIR	:= $(firstword $(wildcard /etc/rc.d/init.d /etc/init.d))
ifneq (,$(INITRD_DIR))
  INIT_TARGET	:= $(DESTDIR)$(INITRD_DIR)/zaptel
  COPY_INITD	:= install -D zaptel.init $(INIT_TARGET)
endif
RCCONF_DIR	:= $(firstword $(wildcard /etc/sysconfig /etc/default))

NETSCR_DIR	:= $(firstword $(wildcard /etc/sysconfig/network-scripts ))
ifneq (,$(NETSCR_DIR))
  NETSCR_TARGET	:= $(DESTDIR)$(NETSCR_DIR)/ifup-hdlc
  COPY_NETSCR	:= install -D ifup-hdlc $(NETSCR_TARGET)
endif

ifneq ($(wildcard .version),)
  ZAPTELVERSION:=$(shell cat .version)
else
ifneq ($(wildcard .svn),)
  ZAPTELVERSION=SVN-$(shell build_tools/make_svn_branch_name)
endif
endif

LTZ_A:=libtonezone.a
LTZ_A_OBJS:=zonedata.o tonezone.o
LTZ_SO:=libtonezone.so
LTZ_SO_OBJS:=zonedata.lo tonezone.lo
LTZ_SO_MAJOR_VER:=1
LTZ_SO_MINOR_VER:=0

# libdir, includedir and mandir are defined in makeopts (from
# configure).
# we use /sbin, rather than configure's $(sbindir) because we use /sbin
# for historical reasons.
BIN_DIR:=/sbin
LIB_DIR:=$(libdir)
INC_DIR:=$(includedir)/zaptel
MAN_DIR:=$(mandir)/man8
MOD_DIR:=$(DESTDIR)/lib/modules/$(KVERS)/misc

# Utilities we build with a standard build procedure:
UTILS		= zttool zttest ztmonitor ztspeed sethdlc-new ztcfg \
		  ztcfg-dude usbfxstest fxstest fxotune ztdiag torisatool \
		  ztscan


# Makefile mentions them. Source is not included (anynore?)
UTILS		+= fxsdump ztprovision

# some tests:
UTILS		+= patgen pattest patlooptest hdlcstress hdlctest hdlcgen \
		   hdlcverify timertest

UTILSO		= $(UTILS:%=%.o)

BINS:=fxotune fxstest sethdlc-new ztcfg ztdiag ztmonitor ztspeed zttest ztscan
ifeq (1,$(PBX_LIBNEWT))
  BINS+=zttool
endif
BINS:=$(filter-out $(MENUSELECT_UTILS),$(BINS))
MAN_PAGES:=$(wildcard $(BINS:%=doc/%.8))

# All the man pages. Not just installed ones:
GROFF_PAGES	:= $(wildcard doc/*.8 kernel/xpp/utils/*.8)
GROFF_HTML	:= $(GROFF_PAGES:%=%.html)

all: menuselect.makeopts 
	@$(MAKE) _all

_all: $(if $(BUILD_MODULES),modules) programs

libs: $(LTZ_SO) $(LTZ_A)

utils-subdirs:
	@for dir in $(SUBDIRS_UTILS); do \
		$(MAKE) -C $$dir; \
	done

programs: libs utils

utils: $(BINS) utils-subdirs

modules: prereq
ifeq (no,$(HAS_KSRC))
	echo "You do not appear to have the sources for the $(KVERS) kernel installed."
	exit 1
endif
	$(KMAKE) modules

version.h: FORCE
	@ZAPTELVERSION="${ZAPTELVERSION}" build_tools/make_version_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then :; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

tests: patgen pattest patlooptest hdlcstress hdlctest hdlcgen hdlcverify timertest

zonedata.o: tonezone.h

zonedata.lo: zonedata.c tonezone.h
	$(CC) -c $(CFLAGS) -o $@ $<

tonezone.o: kernel/zaptel.h tonezone.h

tonezone.lo: tonezone.c tonezone.h kernel/zaptel.h
	$(CC) -c $(CFLAGS) -o $@ $<

prereq: config.status version.h

zttool.o: kernel/zaptel.h
zttool.o: CFLAGS+=$(NEWT_INCLUDE)
zttool: LDLIBS+=$(NEWT_LIB)

ztscan.o: kernel/zaptel.h

ztprovision.o: kernel/zaptel.h

ztmonitor.o: kernel/zaptel.h

ztspeed: CFLAGS=

sethdlc-new: CFLAGS+=-I$(KINCLUDES)

$(LTZ_A): $(LTZ_A_OBJS)
	ar rcs $@ $^
	ranlib $@

$(LTZ_SO): $(LTZ_SO_OBJS)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$(LTZ_SO).$(LTZ_SO_MAJOR_VER).$(LTZ_SO_MINOR_VER) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lm

ztcfg.o: ztcfg.h kernel/zaptel.h
ztcfg: ztcfg.o $(LTZ_A)
ztcfg: LDLIBS+=-lm

ztcfg-shared: ztcfg.o $(LTZ_SO)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lm

ztcfg-dude: ztcfg-dude.o mknotch.o complex.o $(LTZ_SO)
ztcfg-dude: LDLIBS+=-lm -lstdc++

data:
	$(MAKE) -C datamods datamods

# FIXME: we assume CC can build the C++ modules:
complex.o mknotch.o: %.o: %.cc
	$(CC) $(CFLAGS) -o $@ -c $<

usbfxstest: LDLIBS+=-lzap
fxstest: $(LTZ_SO)
fxstest: LDLIBS+=-lm
fxotune: LDLIBS+=-lm
fxsdump: LDLIBS+=-lm

stackcheck: checkstack modules
	./checkstack kernel/*.ko kernel/*/*.ko


tonezones.txt: zonedata.c
	perl -ne 'next unless (/\.(country|description) = *"([^"]*)/); \
		print (($$1 eq "country")? "* $$2\t":"$$2\n");' $<  \
	>$@

zaptel.conf.asciidoc: zaptel.conf.sample
	perl -n -e \
		'if (/^#($$|\s)(.*)/){ if (!$$in_doc){print "\n"}; $$in_doc=1; print "$$2\n" } else { if ($$in_doc){print "\n"}; $$in_doc=0; print "  $$_" }' \
		$< \
	| perl -p -e 'if (/^  #?(\w+)=/ && ! exists $$cfgs{$$1}){my $$cfg = $$1; $$cfgs{$$cfg} = 1; s/^/\n[[cfg_$$cfg]]\n/}'  >$@

README.html: README zaptel.conf.asciidoc tonezones.txt
	$(ASCIIDOC) -n -a toc -a toclevels=3 $<

kernel/xpp/README.Astribank.html: kernel/xpp/README.Astribank
	cd $(@D); $(ASCIIDOC) -o $(@F) -n -a toc -a toclevels=4 $(<F)

# on Debian: this requires the full groof, not just groff-base.
%.8.html: %.8
	man -Thtml $^ >$@

htmlman: $(GROFF_HTML)


MISDNVERSION=1_1_7_2
MISDNUSERVERSION=1_1_7_2
b410p:
	@if test "$(DOWNLOAD)" = ":" ; then \
		echo "**************************************************"; \
		echo "***                                            ***"; \
		echo "*** You must have either wget or fetch to be   ***"; \
		echo "*** able to automatically download and install ***"; \
		echo "*** b410p support.                             ***"; \
		echo "***                                            ***"; \
		echo "*** Please install one of these.               ***"; \
		echo "***                                            ***"; \
		echo "**************************************************"; \
		exit 1; \
	fi
	[ -f mISDN-$(MISDNVERSION).tar.gz ] || $(DOWNLOAD) http://downloads.digium.com/pub/zaptel/b410p/mISDN-$(MISDNVERSION).tar.gz
	tar -zxf mISDN-$(MISDNVERSION).tar.gz
	$(MAKE) -C mISDN-$(MISDNVERSION) install
	[ -f mISDNuser-$(MISDNUSERVERSION).tar.gz ] || $(DOWNLOAD) http://downloads.digium.com/pub/zaptel/b410p/mISDNuser-$(MISDNUSERVERSION).tar.gz
	tar -zxf mISDNuser-$(MISDNUSERVERSION).tar.gz
	$(MAKE) -C mISDNuser-$(MISDNUSERVERSION) install

$(UTILS): %: %.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(UTILSO): %.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install: all devices install-modules install-programs install-firmware
	@echo "###################################################"
	@echo "###"
	@echo "### Zaptel installed successfully."
	@echo "### If you have not done so before, install init scripts with:"
	@echo "###"
	@echo "###   make config"
	@echo "###"
	@echo "###################################################"
ifneq ($(INCOMPAT_MODULES),)
	@echo "***************************************************"
	@echo "***"
	@echo "*** WARNING:"
	@echo "*** The following modules were not installed due to"
	@echo "*** being incompatible with your $(KVERS) kernel."
	@echo "***"
	@echo "*** $(INCOMPAT_MODULES)"
	@echo "***"
	@echo "***************************************************"
endif

install-programs: install-utils install-libs install-include

install-utils: utils install-utils-subdirs
ifneq (,$(BINS))
	install -d $(DESTDIR)$(BIN_DIR)
	install  $(BINS) $(DESTDIR)$(BIN_DIR)/
	install -d $(DESTDIR)$(MAN_DIR)
	install -m 644 $(MAN_PAGES) $(DESTDIR)$(MAN_DIR)/
endif
ifeq (,$(wildcard $(DESTDIR)$(CONFIG_FILE)))
	$(INSTALL) -D -m 644 zaptel.conf.sample $(DESTDIR)$(CONFIG_FILE)
endif

# Pushing those two to a separate target that is not used by default:
install-modconf:
	build_tools/genmodconf $(BUILDVER) "$(ROOT_PREFIX)" "$(filter-out zaptel ztdummy xpp zttranscode ztdynamic,$(BUILD_MODULES)) $(MODULE_ALIASES)"
	@if [ -d /etc/modutils ]; then \
		/sbin/update-modules ; \
	fi

install-firmware: menuselect.makeopts
ifeq ($(HOTPLUG_FIRMWARE),yes)
	$(MAKE) -C firmware hotplug-install DESTDIR=$(DESTDIR) HOTPLUG_FIRMWARE=$(HOTPLUG_FIRMWARE)
endif

install-libs: libs
	$(INSTALL) -D -m 755 $(LTZ_A) $(DESTDIR)$(LIB_DIR)/$(LTZ_A)
	$(INSTALL) -D -m 755 $(LTZ_SO) $(DESTDIR)$(LIB_DIR)/$(LTZ_SO).$(LTZ_SO_MAJOR_VER).$(LTZ_SO_MINOR_VER)
ifeq (,$(DESTDIR))
	if [ `id -u` = 0 ]; then \
		/sbin/ldconfig || : ;\
	fi
endif
	rm -f $(DESTDIR)$(LIB_DIR)$(LTZ_SO)
	$(LN) -sf $(LTZ_SO).$(LTZ_SO_MAJOR_VER).$(LTZ_SO_MINOR_VER) \
		$(DESTDIR)$(LIB_DIR)/$(LTZ_SO).$(LTZ_SO_MAJOR_VER)
	$(LN) -sf $(LTZ_SO).$(LTZ_SO_MAJOR_VER).$(LTZ_SO_MINOR_VER) \
		$(DESTDIR)$(LIB_DIR)/$(LTZ_SO)
ifneq (no,$(USE_SELINUX))
  ifeq (,$(DESTDIR))
	/sbin/restorecon -v $(DESTDIR)$(LIB_DIR)/$(LTZ_SO)
  endif
endif

install-utils-subdirs:
	@for dir in $(SUBDIRS_UTILS); do \
		$(MAKE) -C $$dir install; \
	done

install-include:
	$(INSTALL) -D -m 644 kernel/zaptel.h $(DESTDIR)$(INC_DIR)/zaptel.h
	$(INSTALL) -D -m 644 tonezone.h $(DESTDIR)$(INC_DIR)/tonezone.h
	@rm -rf $(DESTDIR)$(includedir)/dahdi

devices:
ifneq (yes,$(DYNFS))
	mkdir -p $(DESTDIR)/dev/zap
	rm -f $(DESTDIR)/dev/zap/ctl
	rm -f $(DESTDIR)/dev/zap/channel
	rm -f $(DESTDIR)/dev/zap/pseudo
	rm -f $(DESTDIR)/dev/zap/timer
	rm -f $(DESTDIR)/dev/zap/transcode
	rm -f $(DESTDIR)/dev/zap/253
	rm -f $(DESTDIR)/dev/zap/252
	rm -f $(DESTDIR)/dev/zap/251
	rm -f $(DESTDIR)/dev/zap/250
	mknod $(DESTDIR)/dev/zap/ctl c 196 0
	mknod $(DESTDIR)/dev/zap/transcode c 196 250
	mknod $(DESTDIR)/dev/zap/timer c 196 253
	mknod $(DESTDIR)/dev/zap/channel c 196 254
	mknod $(DESTDIR)/dev/zap/pseudo c 196 255
	N=1; \
	while [ $$N -lt 250 ]; do \
		rm -f $(DESTDIR)/dev/zap/$$N; \
		mknod $(DESTDIR)/dev/zap/$$N c 196 $$N; \
		N=$$[$$N+1]; \
	done
else # DYNFS
  ifneq (yes,$(UDEVRULES)) #!UDEVRULES
	@echo "**** Dynamic filesystem detected -- not creating device nodes"
  else # UDEVRULES
	install -d $(DESTDIR)/etc/udev/rules.d
	build_tools/genudevrules > $(DESTDIR)/etc/udev/rules.d/zaptel.rules
  endif
endif

install-udev: devices

uninstall-hotplug:
	$(MAKE) -C firmware hotplug-uninstall DESTDIR=$(DESTDIR)

ifeq ($(BUILDVER),linux24)
install-modules: $(INSTALL_MODULES)
	$(INSTALL) -d $(DESTDIR)$(MOD_DIR)
	$(INSTALL) -m 644 $(INSTALL_MODULES) $(DESTDIR)$(MOD_DIR)
else
install-modules:
ifndef DESTDIR
	@if modinfo dahdi > /dev/null 2>&1; then \
		echo -n "Removing DAHDI modules for kernel $(KVERS), please wait..."; \
		build_tools/uninstall-modules dahdi $(KVERS); \
		rm -rf /lib/modules/$(KVERS)/dahdi; \
		echo "done."; \
	fi
	build_tools/uninstall-modules dahdi $(KVERS)
endif
	$(KMAKE_INST)
endif
	[ `id -u` = 0 ] && /sbin/depmod -a $(KVERS) || :

uninstall-modules:
ifneq ($(BUILDVER),linux24)
ifdef DESTDIR
	echo "Uninstalling modules is not supported with a DESTDIR specified."
	exit 1
else
	@if modinfo zaptel > /dev/null 2>&1 ; then \
		echo -n "Removing Zaptel modules for kernel $(KVERS), please wait..."; \
		build_tools/uninstall-modules zaptel $(KVERS); \
		rm -rf /lib/modules/$(KVERS)/zaptel; \
		echo "done."; \
	fi
	[ `id -u` = 0 ] && /sbin/depmod -a $(KVERS) || :
endif
endif

config:
ifneq (,$(COPY_INITD))
	$(COPY_INITD)
endif
ifneq (,$(RCCONF_DIR))
  ifeq (,$(wildcard $(DESTDIR)$(RCCONF_DIR)/zaptel))
	$(INSTALL) -D -m 644 zaptel.sysconfig $(DESTDIR)$(RCCONF_DIR)/zaptel
  endif
endif
ifneq (,$(COPY_NETSCR))
	$(COPY_NETSCR)
endif
ifneq (,$(ADD_INITD))
	$(ADD_INITD)
endif
	@echo "Zaptel has been configured."
	@echo ""
	@echo "If you have any zaptel hardware it is now recommended to "
	@echo "edit /etc/default/zaptel or /etc/sysconfig/zaptel and set there an "
	@echo "optimal value for the variable MODULES ."
	@echo ""
	@echo "I think that the zaptel hardware you have on your system is:"
	@kernel/xpp/utils/zaptel_hardware || true


update:
	@if [ -d .svn ]; then \
		echo "Updating from Subversion..." ; \
		svn update | tee update.out; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -b4- ; \
		fi ; \
		rm -f update.out; \
	else \
		echo "Not under version control";  \
	fi

clean:
	-@$(MAKE) -C menuselect clean
	rm -f torisatool
	rm -f $(BINS)
	rm -f *.o ztcfg tzdriver sethdlc sethdlc-new
	rm -f $(LTZ_SO) $(LTZ_A) *.lo
ifeq (yes,$(HAS_KSRC))
	$(KMAKE) clean
else
	rm -f kernel/*.o kernel/*.ko kernel/*/*.o kernel/*/*.ko
endif
	@for dir in $(SUBDIRS_UTILS_ALL); do \
		$(MAKE) -C $$dir clean; \
	done
	$(MAKE) -C firmware clean
	rm -rf .tmp_versions
	rm -f gendigits tones.h
	rm -f libtonezone*
	rm -f fxotune
	rm -f core
	rm -f ztcfg-shared fxstest
	rm -rf misdn*
	rm -rf mISDNuser*
	rm -rf $(GROFF_HTML)
	rm -rf README.html xpp/README.Astribank.html zaptel.conf.asciidoc

distclean: dist-clean

dist-clean: clean
	@$(MAKE) -C menuselect dist-clean
	@$(MAKE) -C firmware dist-clean
	rm -f makeopts menuselect.makeopts menuselect-tree
	rm -f config.log config.status

config.status: configure
	@CFLAGS="" ./configure
	@echo "****"
	@echo "**** The configure script was just executed, so 'make' needs to be"
	@echo "**** restarted."
	@echo "****"
	@exit 1

menuselect.makeopts: menuselect/menuselect menuselect-tree
	@menuselect/menuselect --check-deps ${GLOBAL_MAKEOPTS} ${USER_MAKEOPTS} $@

menuconfig: menuselect

menuselect: menuselect/menuselect menuselect-tree
	-@menuselect/menuselect $(GLOBAL_MAKEOPTS) $(USER_MAKEOPTS) menuselect.makeopts && echo "menuselect changes saved!" || echo "menuselect changes NOT saved!"

menuselect/menuselect: menuselect/menuselect.c menuselect/menuselect_curses.c menuselect/menuselect_stub.c menuselect/menuselect.h menuselect/linkedlists.h config.status
	@CFLAGS="" $(MAKE) -C menuselect CC=$(HOSTCC)

menuselect-tree: zaptel.xml firmware/firmware.xml
	@echo "Generating input for menuselect ..."
	@build_tools/make_tree > $@

.PHONY: menuselect distclean dist-clean clean all _all install b410p devices programs modules tests devel data stackcheck install-udev config update install-programs install-modules install-include install-libs install-utils-subdirs utils-subdirs uninstall-modules

FORCE:

endif

#end of: ifneq ($(KBUILD_EXTMOD),)
endif
