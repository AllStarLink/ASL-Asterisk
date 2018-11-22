#!/usr/bin/env bash
set -o errexit

# N4IRS 07/26/2017

###################################################
#                                                 #
# script was built for Debian Jessie NetInstall   #
#                                                 #
###################################################

echo "[Match]" >>/etc/systemd/network/eth0.network
echo "Name=eth0" >>/etc/systemd/network/eth0.network
echo >>/etc/systemd/network/eth0.network
echo "[Network]" >>/etc/systemd/network/eth0.network
echo "DHCP=v4" >>/etc/systemd/network/eth0.network
echo >>/etc/systemd/network/eth0.network

