#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

#################################################
#                                               #
# Patch asterisk for AllStar Asterisk           #
#                                               #
#################################################

cd /usr/src/astsrc-1.4.23-pre/asterisk/

# patch for ulaw Core and Extras Sound Packages
patch < /srv/patches/patch-asterisk-menuselect.makedeps
patch < /srv/patches/patch-asterisk-menuselect.makeopts

# patch for SSL used in res_crypto
patch < /srv/patches/patch-configure
patch < /srv/patches/patch-configure.ac

# patch for LSB used in Debian init scripts
patch -p1 < /srv/patches/patch-rc-debian
patch < /srv/patches/patch-asterisk-makefile

# No longer needed patch to Makefile updated
# codename=$(lsb_release -cs)
# if [[ $codename == 'jessie' ]]; then
#   echo "codename is Jessie, using systemd units"
#   sed -i -e 's/debian_version/debian_version_7/g' /usr/src/astsrc-1.4.23-pre/asterisk/Makefile
# elif [[ $codename == 'wheezy' ]]; then
#   echo "codename is Wheezy, using init scripts"
# fi

