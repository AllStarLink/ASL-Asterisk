#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

#################################################
#                                               #
#                                               #
#                                               #
#################################################

# Get Kernel Headers
distributor=$(lsb_release -is)
if [[ $distributor = "Raspbian" ]]; then
apt-get install raspberrypi-kernel-headers -y
elif [[ $distributor = "Debian" ]]; then
apt-get install linux-headers-`uname -r` -y
fi

apt-get install dkms -y

###########################################################

# Get Asterisk
cd /usr/src
git clone https://github.com/AllStarLink/Asterisk.git astsrc-1.4.23-pre

# download uridiag
# svn co http://svn.ohnosec.org/svn/projects/allstar/uridiag/trunk uridiag
git clone https://github.com/AllStarLink/uridiag.git

# Clean out unneeded source
cd /usr/src/astsrc-1.4.23-pre
rm -rf libpri
rm -rf zaptel

##########################################################




