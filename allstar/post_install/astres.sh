#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

#################################################
#                                               #
#                                               #
#                                               #
#################################################

if [ -e /var/run/asterisk.ctl ]
then
	echo "Restarting Asterisk"
        echo "systemctl restart asterisk.service"
	systemctl restart asterisk.service
else
	echo "Asterisk is not running!"
fi

# asterisk service status: systemctl status asterisk.service
# asterisk start service: systemctl start asterisk.service
# asterisk restart service: systemctl restart asterisk.service
# asterisk stop service: systemctl stop asterisk.service
# asterisk disable service: systemctl disable asterisk.service
# asterisk enable service: systemctl enable asterisk.service

