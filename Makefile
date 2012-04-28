# Limey Linux Makefile for Asterisk, Zaptel and Libpri

-include /etc/sysinfo #include if it exists, else use defaults

ASTSRC_VERS:=1.1.6
KVERS?=$(shell uname -r)
PROCESSOR?=i586

/usr/include/linux:
	-umount /mnt/cf
	mount /mnt/cf
	(cd /lib/modules/$(KVERS)/build; tar xvzf /mnt/cf/kdev.tgz; rm -rf arch/blackfin arch/mn10300 arch/powerpc arch/avr32 arch/xtensa arch/x86/boot arch/x86/kernel arch/x86/pci firmware security)
	umount /mnt/cf
	rm -f /usr/include/linux
	ln -s /lib/modules/$(KVERS)/build/include/linux /usr/include/linux 


llastate:
	-@mkdir -p llastate
	
	
llastate/zaptel-built: /usr/include/linux	
	(cd zaptel; ./configure --build=$(PROCESSOR)-pc-linux)
	(MAKELEVEL=0; $(MAKE) -C zaptel); # Why does MAKELEVEL need to be 0 for zaptel to build correctly?
	touch llastate/zaptel-built

llastate/zaptel-installed:	llastate/zaptel-built
	(MAKELEVEL=0; $(MAKE) -C zaptel install)
	touch llastate/zaptel-installed

llastate/libpri-built:
	$(MAKE) -C libpri
	touch llastate/libpri-built
	
llastate/libpri-installed:	llastate/libpri-built
	$(MAKE) -C libpri install
	touch llastate/libpri-installed

llastate/asterisk-configured:
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux)
	touch llastate/asterisk-configured

llastate/asterisk-installed: llastate/asterisk-configured
	-rm -rf /usr/lib/asterisk/modules/*
	$(MAKE) -C asterisk install DEBUG=
	touch llastate/asterisk-installed
	
llastate/rpt-sounds-installed:
	mkdir -p /var/lib/asterisk/sounds/rpt
	cp -a sounds/* /var/lib/asterisk/sounds
	touch llastate/rpt-sounds-installed

llastate/id_file: llastate
	-mkdir -p /etc/asterisk
	-cp id.gsm /etc/asterisk
	touch llastate/id_file

.PHONY:	help upgrade ast-build-only install_pciradio install_usbradio install_wcte11xp install_wct1xxp clean release svsrc genconfig 

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
	@echo "make ast_build_only    - build asterisk only, do not install"
	@echo "make clean             - delete all object files, intermediate files, and executables"

#
# Upgrade Asterisk Zaptel, and LIBPRI only, do not install configs!
#

upgrade:	llastate /usr/include/linux llastate/zaptel-installed llastate/libpri-installed llastate/asterisk-installed llastate/rpt-sounds-installed
	-rm llastate/asterisk-installed
	-umount /mnt/cf
	svastbin
	mount /mnt/cf
	mv -f /root/astbin.tgz /mnt/cf
	umount /mnt/cf
#
# Asterisk test build without install
#
ast-build-only: llastate llastate/zaptel-installed llastate/libpri-installed llastate/asterisk-configured
	$(MAKE)  menuconfig
	$(MAKE) -C asterisk DEBUG=
#
# Build and install for Quad Radio PCI card
#
install_pciradio: llastate/id_file upgrade
	-@mkdir .-p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/pciradio/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/pciradio
	-@svcfg
#
# Build and install for USB fobs
#.
install_usbradio: llastate/id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/usbradio/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/usbradio
	-@svcfg

#
# Build and install for TE110P T1 card channel bank and ARIBS
#
install_wcte11xp: llastate/id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/wct1xxp/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wcte11xp
	-@svcfg

#
# Build and install for T100P T1 card channel bank and ARIBS
#
install_wct1xxp: llastate/id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/wct1xxp/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wct1xxp
	-@svcfg

#
# Remove all object files on the target
#
clean:	/usr/include/linux
	-@rm -rf llastate
	$(MAKE) -C libpri clean
	$(MAKE) -C asterisk clean
	(MAKELEVEL=0; $(MAKE) -C zaptel clean)
#
# Generate the release tarballs from the development host (not the target!)
#
release:
	tar cvzf ../astsrc-vers-$(ASTSRC_VERS).tar.gz  --exclude='.svn' allstar asterisk libpri zaptel sounds configs extras Makefile id.gsm README LICENSE 
	-@rm ../files.tar.gz
	(cd ..; ln -s astsrc-vers-$(ASTSRC_VERS).tar.gz files.tar.gz)
	(cd ..; sha256sum files.tar.gz | cut -d ' ' -f 1 >files.tar.gz.sha256sum)
#
# Save the source files on the compact flash on the target
#
svsrc:
	-rm llastate/libpri-installed llastate/zaptel-installed llastate/asterisk-installed
	-umount /mnt/cf
	mount /mnt/cf
	tar cvfz /mnt/cf/astsrc.tgz *
	sync
	umount /mnt/cf
#
# Used only to set the configuration for the asterisk distribution while compiling on the target
#
genconfig:	/usr/include/linux
	(cd zaptel; ./configure --build=$(PROCESSOR)-pc-linux)
	-(MAKELEVEL=0; $(MAKE) -C zaptel menuselect)
	(MAKELEVEL=0; $(MAKE) -C zaptel install); # Why does MAKELEVEL need to be 0 for zaptel to build correctly?
	$(MAKE) -C libpri install
	-rm -rf /usr/lib/asterisk/modules/*
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux)
	-$(MAKE) -C asterisk menuselect
	-$(MAKE) -C asterisk install DEBUG=


upgrade-acid:
	(cd zaptel; ./configure)
	(MAKELEVEL=0; $(MAKE) -C zaptel install)
	$(MAKE) -C libpri install
	-rm -rf /usr/lib/asterisk/modules/*
	(cd asterisk; ./configure CXX=gcc)
	-$(MAKE) -C asterisk install
	mkdir -p /var/lib/asterisk/sounds/rpt
	cp -a sounds/* /var/lib/asterisk/sounds


upgrade-pickle:
	(cd zaptel; ./configure)
	(MAKELEVEL=0; $(MAKE) -C zaptel install KBUILD_OBJ_M="zaptel.o ztd-eth.o ztd-loc.o ztdummy.o ztdynamic.o")
	$(MAKE) -C libpri install
	-rm -rf /usr/lib/asterisk/modules/*
	(cd asterisk; ./configure CXX=gcc)
	-$(MAKE) -C asterisk install
	mkdir -p /var/lib/asterisk/sounds/rpt
	cp -a sounds/* /var/lib/asterisk/sounds
	sync

