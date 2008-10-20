# Makefile for Asterisk, Zaptel and Libpri

-include /etc/sysinfo #include if it exists, else use defaults

ASTSRC_VERS:=1.0.6
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
	
.zaptel-genconfig:	.kernel-dev-installed
	(cd zaptel; ./configure --build=$(PROCESSOR)-pc-linux)
	-(MAKELEVEL=0; make -C zaptel menuselect)
	(MAKELEVEL=0; make -C zaptel); # Why does MAKELEVEL need to be 0 for zaptel to build correctly?
	touch .zaptel-built

.zaptel-built:	.kernel-dev-installed
	(cd zaptel; ./configure --build=$(PROCESSOR)-pc-linux)
	(MAKELEVEL=0; make -C zaptel); # Why does MAKELEVEL need to be 0 for zaptel to build correctly?
	touch .zaptel-built

.zaptel-installed:	.zaptel-built
	(MAKELEVEL=0; make -C zaptel install)
	#make -C zaptel clean
	touch .zaptel-installed

.libpri-built:
	make -C libpri
	touch .libpri-built
	
.libpri-installed:	.libpri-built
	make -C libpri install
	#make -C libpri clean
	touch .libpri-installed

build-only: .zaptel-installed .libpri-installed
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux; make menuconfig)
	make -C asterisk DEBUG=
	
.asterisk-genconfig:
	-rm -rf /usr/lib/asterisk/modules/*
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux)
	-make -C asterisk menuselect
	make -C asterisk install DEBUG=
	touch .asterisk-installed
	
.asterisk-installed:
	-rm -rf /usr/lib/asterisk/modules/*
	(cd asterisk; ./configure CXX=gcc --build=$(PROCESSOR)-pc-linux)
	make -C asterisk install DEBUG=
	touch .asterisk-installed
	
.rpt-sounds-installed:
	mkdir -p /var/lib/asterisk/sounds/rpt
	cp -a sounds/* /var/lib/asterisk/sounds

.id_file:
	-mkdir -p /etc/asterisk
	-cp id.gsm /etc/asterisk
	touch .id_file

.PHONY:	help archive svsrc

help:
	@echo "make upgrade           - build and install sources only"
	@echo "make install_pciradio  - build and install sources plus pciradio config files"
	@echo "make install_usbradio  - build and install sources plus usbradio config files"
	@echo "make install_wcte11xp  - build and install sources plus single span wcte11xp config files"
	@echo "make install_wct1xxp   - build and install sources plus single span t100p config files"
	@echo "make install_wctdm     - build and install sources plus tdm400 config files"
	@echo "make svsrc             - save the sources on the CF as astsrc.tgz"
	@echo "make build_only        - build only, do not install"
	@echo "make clean             - delete all object files, intermediate files, and executables"

upgrade:	.kernel-dev-installed .zaptel-installed .libpri-installed .asterisk-installed .rpt-sounds-installed
	-umount /mnt/cf
	svastbin
	mount /mnt/cf
	mv -f /root/astbin.tgz /mnt/cf
	umount /mnt/cf

install_pciradio: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/pciradio/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/pciradio
	-@svcfg
	
install_usbradio: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/usbradio/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/usbradio
	-@svcfg

install_wcte11xp: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/wct1xxp/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wcte11xp
	-@svcfg

install_wct1xxp: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/wct1xxp/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wct1xxp
	-@svcfg

install_wctdm: .id_file upgrade
	-@mkdir -p /etc/asterisk
	-@cp configs/* /etc/asterisk
	-@cp configs/tdm400p/* /etc/asterisk
	-@mv /etc/asterisk/zaptel.conf /etc
	-@touch /etc/init.d/wctdm

clean:	.kernel-dev-installed
	-rm .zaptel-installed \
	 .libpri-installed .asterisk-installed \
	 .rpt-sounds-installed .installed .zaptel-built \
	 .libpri-built .asterisk-built .id_file
	make -C libpri clean
	make -C asterisk clean
	(MAKELEVEL=0; make -C zaptel clean)

release:
	tar cvzf ../astsrc-vers-$(ASTSRC_VERS).tar.gz  --exclude='.svn' allstar asterisk libpri zaptel sounds configs Makefile id.gsm README LICENSE 
	-@rm ../files.tar.gz
	(cd ..; ln -s astsrc-vers-$(ASTSRC_VERS).tar.gz files.tar.gz)
	(cd ..; sha256sum files.tar.gz | cut -d ' ' -f 1 >files.tar.gz.sha256sum)



svsrc:
	-umount /mnt/cf
	mount /mnt/cf
	tar cvfz /mnt/cf/astsrc.tgz *
	sync
	umount /mnt/cf
