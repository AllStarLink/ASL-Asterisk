iax.conf:
; register=1999:123456@register.allstarlink.org ; This must be changed to your node number, password
change this to:
register=1999:123456@register.allstarlink.org ; This must be changed to your node number, password
remove the first ; (called un-commenting)
change 1999 to you node number
change 123456 to you node password

rpt.conf
find and replace all occurences of 1999 with your node number
; ** For ACID and Debian ***
;statpost_program=/usr/bin/wget,-q,--timeout=15,--tries=1,--output-document=/dev/null                      
;statpost_url=http://stats.allstarlink.org/uhandler.php ; Status updates
remove the ; from the 2 statpost lines above

extensions.conf
[globals]
HOMENPA=619
NODE=1999
change 619 to your area code. (not really required)
change 1999 to your node number.

# cd /srv
# run rpi2_allstar_asterisk_install.sh

# edit /etc/asterisk/iax.conf
#	change 1999 to your node number	

# edit /etc/asterisk/rpt.conf
#	change 1999 to your node number

# edit /etc/asterisk/extensions.conf
#	change 1999 to your node number

# reboot


Fix KEYEXPIRED errors during apt-get update

cd /tmp
wget https://repositories.collabora.co.uk/debian/pool/rpi2/c/collabora-obs-archive-keyring/collabora-obs-archive-keyring_0.5+b1_all.deb
dpkg -i collabora-obs-archive-keyring_0.5+b1_all.deb
apt-get update

