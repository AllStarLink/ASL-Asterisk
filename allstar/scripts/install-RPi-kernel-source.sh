#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

#################################################
#                                               #
# install kernel source for Raspberry Pi 4.1.19 #
#                                               #
#################################################

cd /usr/src
unzip /srv/download/cirrus-4.1.19.zip

ln -sf /usr/src/rpi-linux-cirrus-4.1.19 /lib/modules/$(uname -r)/build
ln -sf /usr/src/rpi-linux-cirrus-4.1.19 /lib/modules/$(uname -r)/source

cd /usr/src/rpi-linux-cirrus-4.1.19/
make mrproper
echo + >.scmversion
modprobe configs
gzip -dc /proc/config.gz > .config

wget https://github.com/raspberrypi/firmware/raw/845eb064cb52af00f2ea33c0c9c54136f664a3e4/extra/Module7.symvers
mv Module7.symvers Module.symvers

make modules_prepare
depmod


