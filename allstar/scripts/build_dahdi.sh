#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

##############################################
#                                            #
# Patch and build dahdi for AllStar Asterisk #
#                                            #
##############################################

# Install DAHDI
dpkg -i /srv/dkms/dahdi-linux-complete-dkms_2.10.2+2.10.2_all.deb


# change this to setup dahdi for quad card
# mv /etc/dahdi/modules /etc/dahdi/modules.old



