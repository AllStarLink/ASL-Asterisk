prefix = /usr

install:
	install -m 755 post_install/asl-menu $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/astdb.php $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/astdn.sh $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/astres.sh $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/astup.sh $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/clear-log $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/first-time $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/flush-log $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/net-setup $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/node-setup $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/restore-node $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/save-node $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/setclock $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/set-host $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/termsize.py $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/wifi-setup $(DESTDIR)$(prefix)/sbin/
	install -m 755 post_install/write-node-callsigns $(DESTDIR)$(prefix)/sbin/
	install -d $(DESTDIR)$(prefix)/bin/backup-server
	install -m 755 backup-server/* $(DESTDIR)$(prefix)/bin/backup-server/

uninstall: 
	-rm -f $(DESTDIR)$(prefix)/sbin/asl-menu
	-rm -f $(DESTDIR)$(prefix)/sbin/astdb.php
	-rm -f $(DESTDIR)$(prefix)/sbin/astdn.sh
	-rm -f $(DESTDIR)$(prefix)/sbin/astres.sh
	-rm -f $(DESTDIR)$(prefix)/sbin/astup.sh
	-rm -f $(DESTDIR)$(prefix)/sbin/clear-log
	-rm -f $(DESTDIR)$(prefix)/sbin/first-time
	-rm -f $(DESTDIR)$(prefix)/sbin/flush-log
	-rm -f $(DESTDIR)$(prefix)/sbin/net-setup
	-rm -f $(DESTDIR)$(prefix)/sbin/node-setup
	-rm -f $(DESTDIR)$(prefix)/sbin/restore-node
	-rm -f $(DESTDIR)$(prefix)/sbin/save-node
	-rm -f $(DESTDIR)$(prefix)/sbin/setclock
	-rm -f $(DESTDIR)$(prefix)/sbin/set-host
	-rm -f $(DESTDIR)$(prefix)/sbin/termsize.py
	-rm -f $(DESTDIR)$(prefix)/sbin/wifi-setup
	-rm -f $(DESTDIR)$(prefix)/sbin/write-node-callsigns
	-rm -f $(DESTDIR)$(prefix)/bin/backup-server/*
	-rmdir $(DESTDIR)$(prefix)/bin/backup-server
	-rm -f $(DESTDIR)$(prefix)/bin/rc.updatenodelist

.PHONY: install uninstall
