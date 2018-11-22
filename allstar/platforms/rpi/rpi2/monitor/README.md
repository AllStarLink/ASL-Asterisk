Add a local monitor speaker to a RPi2


add snd_bcm2835 to /etc/modules 

apt-get install aumix

cd /usr/src/astsrc-1.4.23-pre/asterisk/

make menuselect

add the chan_oss channel driver

exit menuselect

make

make install

copy oss.conf to /etc/asterisk

replace 1999 with your node number in oss.conf

cat extensions.conf.inc >>/etc/asterisk/extensions.conf

reboot


asterisk -r

at the cli prompt console dial

You should have audio from the local speaker.

console hangup to disconnect the monitor.

use aumix to adjust speaker audio


I will be adding this to the RPi scripts.


73, Steve N4IRS
