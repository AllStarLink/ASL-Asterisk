# Limey Linux Makefile for Asterisk, Zaptel and Libpri

-include /etc/sysinfo #include if it exists, else use defaults

ASTSRC_VERS:=1.0.7-test
KVERS?=$(shell uname -r)
PROCESSOR?=i586

.kernel-dev-installed:
	-umount /mnt/cf
	mount /mnt/cf
	(cd /lib/modules/$(KVERS)/build; tar xvzf /mnt/cf/kdev.tgz)
	umount /mnt/cf
	rm -f /usr/include/linux
	ln -s /lib/modules/$(KVERS)/build/include/linux /usr/include/linux 
	touch .kernel-dev-installed
	
.zaptel-built:	.kernel-dev-installed
	(cd zaptel; ./configure --build=$(PROCESSOR)-pc-linux)
	(MAKELEVEL=0; $(MAKE) -C zaptel); # Why does MAKELEVEL need to be 0 for zaptel to build correctly?
	touch .zaptel-built

.zaptel-installed:	.zaptel-built
	(MAKELEVEL=0; $(MAKE) -C zaptel install)
	touch .zaptel-installed

.libpri-built:
	$(MAKE) -C libpri
	touch .libpri-built
	
.libpri-installed:	.libpri-built
	$(MAKE) -C libpri install
	touch .libpri-installed

build-only: .zaptel-installed .libpri-installed .asterisk-configured
	$(MAKE)  menuconfig
	$(MAKE) -C asterisk DEBUG=

	
.asterisk-configured:
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux)

.asterisk-installed: .asterisk-configured
	-rm -rf /usr/lib/asterisk/modules/*
	$(MAKE) -C asterisk install DEBUG=
	touch .asterisk-installed
	
.rpt-sounds-installed:
	mkdir -p /var/lib/asterisk/sounds/rpt
	cp -a sounds/* /var/lib/asterisk/sounds

.id_file:
	-mkdir -p /etc/asterisk
	-cp id.gsm /etc/asterisk
	touch .id_file

.PHONY:	help upgrade test-build install_pciradio install_usbradio install_wcte11xp clean release svsrc genconfig 

#
# Default goal: Print Help
#

help:
	@echo "make upgrade           - build and install sources only"
	@echo "make install_pciradio  - build and install sources plus pciradio config files"
	@echo "make install_usbradio  - build and install sources plus usbradio config files"
	@echo "make install_wcte11xp  - build and install sources plus single span wcte11xp config files"
	@echo "make install_wct1xxp   - build and install sources plus single span t100p config files"
	@echo "make svsrc             - save the sources on the CF as astsrc.tgz"
	@echo "make build_only        - build only, do not install"
	@echo "make clean             - delete all object files, intermediate files, and executables"

#
# Upgrade Asterisk Zaptel, and LIBPRI only, do not install configs!
#

upgrade:	.kernel-dev-installed .zaptel-installed .libpri-installed .asterisk-installed .rpt-sounds-installed
	-umount /mnt/cf
	svastbin
	mount /mnt/cf
	mv -f /root/astbin.tgz /mnt/cf
	umount /mnt/cf
#
# Asterisk test build without install
#
test-build: .zaptel-installed .libpri-installed .asterisk-configured
	$(MAKE)  menuconfig
	$(MAKE) -C asterisk DEBUG=
#
# Build and install for Quad Radio PCI card
#
install_pciradio: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/pciradio/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/pciradio
	-@svcfg
#
# Build and install for USB fobs
#
install_usbradio: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/usbradio/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/usbradio
	-@svcfg

#
# Build and install for TE110P T1 card channel bank and ARIBS
#
install_wcte11xp: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/wct1xxp/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wcte11xp
	-@svcfg

#
# Build and install for T100P T1 card channel bank and ARIBS
#
install_wct1xxp: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/wct1xxp/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wct1xxp
	-@svcfg

#
# Remove all object files on the target
#
clean:	.kernel-dev-installed
	-rm .zaptel-installed \
	 .libpri-installed .asterisk-installed \
	 .rpt-sounds-installed .installed .zaptel-built \
         .libpri-built .asterisk-built .id_file
	$(MAKE) -C libpri clean
	$(MAKE) -C asterisk clean
	(MAKELEVEL=0; $(MAKE) -C zaptel clean)

#
# Generate the release tarballs from the development host (not the target!)
#
release:
	tar cvzf ../astsrc-vers-$(ASTSRC_VERS).tar.gz  --exclude='.svn' allstar asterisk libpri zaptel sounds configs Makefile id.gsm README LICENSE 
	-@rm ../files.tar.gz
	(cd ..; ln -s astsrc-vers-$(ASTSRC_VERS).tar.gz files.tar.gz)
	(cd ..; sha256sum files.tar.gz | cut -d ' ' -f 1 >files.tar.gz.sha256sum)
#
# Save the source files on the compact flash on the target
#
svsrc:
	-umount /mnt/cf
	mount /mnt/cf
	tar cvfz /mnt/cf/astsrc.tgz *
	sync
	umount /mnt/cf
#
# Used only to set the configuration for the asterisk distribution while compiling on the target
#
genconfig:	.kernel-dev-installed
	(cd zaptel; ./configure --build=$(PROCESSOR)-pc-linux)
	-(MAKELEVEL=0; $(MAKE) -C zaptel menuselect)
	(MAKELEVEL=0; $(MAKE) -C zaptel install); # Why does MAKELEVEL need to be 0 for zaptel to build correctly?
	$(MAKE) -C libpri install
	-rm -rf /usr/lib/asterisk/modules/*
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux)
	-$(MAKE) -C asterisk menuselect
	-$(MAKE) -C asterisk install DEBUG=


