#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

#################################################
#                                               #
# Patch and build asterisk for AllStar Asterisk #
#                                               #
#################################################

cd /usr/src/astsrc-1.4.23-pre/asterisk/

./configure

# Build and install Asterisk
make
make install
make samples
make config

# add the sound files for app_rpt
# No longer needed
# cd /usr/src/astsrc-1.4.23-pre
# mkdir -p /var/lib/asterisk/sounds/rpt
# cp -a /usr/src/astsrc-1.4.23-pre/sounds/* /var/lib/asterisk/sounds

# Add "AllStar Node Enabled" sound file
# No longer needed
# cp /srv/sounds/node_enabled.ulaw /var/lib/asterisk/sounds/rpt/

# Build URI diag
cd /usr/src/astsrc-1.4.23-pre/uridiag
make
chmod +x uridiag
cp uridiag /usr/local/bin/uridiag

# Clean out and replace samples
# No longer needed
# cd /etc/asterisk/
# rm *
# cp -r /srv/configs/* .

# Install Nodelist updater
cp /usr/src/astsrc-1.4.23-pre/allstar/rc.updatenodelist /usr/local/bin/rc.updatenodelist

# echo " If all looks good, edit iax.conf extensions.conf and rpt.conf"
# echo " Pay attention to the top of rpt.conf"

