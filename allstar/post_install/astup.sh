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
	echo "Asterisk is currently running!"
else
	echo "Starting asterisk..."
        echo "systemctl start asterisk.service"
	systemctl start asterisk.service
fi

# asterisk service status: systemctl status asterisk.service
# asterisk start service: systemctl start asterisk.service
# asterisk restart service: systemctl restart asterisk.service
# asterisk stop service: systemctl stop asterisk.service
# asterisk disable service: systemctl disable asterisk.service
# asterisk enable service: systemctl enable asterisk.service
