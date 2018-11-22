#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

##############################################
#                                            #
# Patch and build libpri for Asterisk        #
#                                            #
##############################################

cd /usr/src/astsrc-1.4.23-pre/libpri/

# Patch dahdi for use with AllStar Asterisk
# https://allstarlink.org/dude-dahdi-2.10.0.1-patches-20150306
# Soon to be included in the official release of DAHDI from Digium.
patch </srv/patches/patch-libpri-makefile

make
make install
