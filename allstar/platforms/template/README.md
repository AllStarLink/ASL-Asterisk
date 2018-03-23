# AllStar
Patches and files to compile AllStar Asterisk on a Debian ARM amd64 and i386 Platforms

Quick and dirty:

Download the zip file from the link at the right

cd /tmp

wget --no-check-certificate https://github.com/N4IRS/AllStar/archive/master.zip

apt-get install unzip -y

rm -f AllStar-master

ln -s /srv AllStar-master

unzip master.zip

rm AllStar-master

cd /srv

Copy the platform specific file to /srv

Example: cp platforms/rpi/rpi2/* /srv

RPI 1 Base image is at:

http://downloads.raspberrypi.org/raspbian/images/raspbian-2014-12-25/2014-12-24-wheezy-raspbian.zip

RPI 2 base image is at:

https://images.collabora.co.uk/rpi2/jessie-rpi2-20150202.img.gz

login: root	password: debian


Debian Bare Metal x86 installer (Current)

http://tinyurl.com/x86-DIAL

Debian Bare Metal Debian amd64 and i386 installer (Release Candidate)

http://tinyurl.com/amd64-i386-DIAL-RC1

Raspberry Pi 2/3 disk image (Current)

http://tinyurl.com/rpi2-dial-V101

Raspberry Pi 2/3 Raspbian disk image (Release Candidate)

http://tinyurl.com/DIAL-RAT-RC1

Android IAX Client

https://play.google.com/store/apps/details?id=org.dvswitch

