#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

#################################################
#                                               #
# Install build tools required to compile       #
# AllStarLink Asterisk                          #
#                                               #
#################################################

# need to add install g++-4.9

apt-get install g++ -y
apt-get install make -y
apt-get install build-essential -y

apt-get install git -y
apt-get install debhelper -y
apt-get install pkg-config -y
apt-get install checkinstall -y

# Moved to get-src so that the proper kernel header
# package is installed BEFORE DKMS
# apt-get install dkms -y
