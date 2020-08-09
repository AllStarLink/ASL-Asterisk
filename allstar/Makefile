prefix = /usr

install:
	install -D post_install/asl-menu $(DESTDIR)$(prefix)/bin
	install -D post_install/astdb.php $(DESTDIR)$(prefix)/bin
	install -D post_install/astdn.sh $(DESTDIR)$(prefix)/bin
	install -D post_install/astres.sh $(DESTDIR)$(prefix)/bin
	install -D post_install/astup.sh $(DESTDIR)$(prefix)/bin
	install -D post_install/clear-log $(DESTDIR)$(prefix)/bin
	install -D post_install/first-time $(DESTDIR)$(prefix)/bin
	install -D post_install/flush-log $(DESTDIR)$(prefix)/bin
	install -D post_install/net-setup $(DESTDIR)$(prefix)/bin
	install -D post_install/node-setup $(DESTDIR)$(prefix)/bin
	install -D post_install/restore-node $(DESTDIR)$(prefix)/bin
	install -D post_install/save-node $(DESTDIR)$(prefix)/bin
	install -D post_install/setclock $(DESTDIR)$(prefix)/bin
	install -D post_install/set-host $(DESTDIR)$(prefix)/bin
	install -D post_install/termsize.py $(DESTDIR)$(prefix)/bin
	install -D post_install/wifi-setup $(DESTDIR)$(prefix)/bin
	install -D post_install/write-node-callsigns $(DESTDIR)$(prefix)/bin

uninstall: 
	-rm -f $(DESTDIR)$(prefix)/bin/asl-menu
	-rm -f $(DESTDIR)$(prefix)/bin/astdb.php
	-rm -f $(DESTDIR)$(prefix)/bin/astdn.sh
	-rm -f $(DESTDIR)$(prefix)/bin/astres.sh
	-rm -f $(DESTDIR)$(prefix)/bin/astup.sh
	-rm -f $(DESTDIR)$(prefix)/bin/clear-log
	-rm -f $(DESTDIR)$(prefix)/bin/first-time
	-rm -f $(DESTDIR)$(prefix)/bin/flush-log
	-rm -f $(DESTDIR)$(prefix)/bin/net-setup
	-rm -f $(DESTDIR)$(prefix)/bin/node-setup
	-rm -f $(DESTDIR)$(prefix)/bin/restore-node
	-rm -f $(DESTDIR)$(prefix)/bin/save-node
	-rm -f $(DESTDIR)$(prefix)/bin/setclock
	-rm -f $(DESTDIR)$(prefix)/bin/set-host
	-rm -f $(DESTDIR)$(prefix)/bin/termsize.py
	-rm -f $(DESTDIR)$(prefix)/bin/wifi-setup
	-rm -f $(DESTDIR)$(prefix)/bin/write-node-callsigns

.PHONY: install uninstall
