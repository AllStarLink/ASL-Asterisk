#!/usr/bin/env bash
set -o errexit

# N4IRS 127/18/2017

#####################################################
#                                                   #
# Install libs required to install                  #
# AllStarLink Asterisk from source                  #
#                                                   #
#####################################################
#
# Whare these all on single lines? It's much more
# efficient to apt-get in a single line.
#
# By keeping each install on it's own line, it's
# easier to copy and paste.

# depends
apt-get install libusb-dev -y

apt-get install libnewt-dev -y
apt-get install libeditline0 -y
apt-get install libncurses5-dev -y

apt-get install libssl-dev -y

apt-get install libasound2-dev -y
apt-get install libvorbis-dev -y

apt-get install libcurl4-gnutls-dev -y

apt-get install libiksemel-dev -y

apt-get install libsnmp-dev -y

# These are untested
# apt-get install speex -y
# apt-get install libspeex -y
# apt-get install libspeexdsp -y

# recommends utilities and tools
apt-get install sox -y
apt-get install usbutils -y
apt-get install alsa-utils -y
apt-get install bc -y
apt-get install dnsutils -y
apt-get install lsb-release -y

apt-get install bison -y
apt-get install curl -y

# Check for Debian distribution
# Need to make sure lsb-release is installed first
codename=$(lsb_release -cs)
 if [[ $codename == 'jessie' ]]; then
   echo "codename is Jessie, using php5-cli"
   apt-get install php5-cli -y
 elif [[ $codename == 'stretch' ]]; then
   echo "codename is Stretch, using php-cli"
   apt-get install php-cli -y
 fi
