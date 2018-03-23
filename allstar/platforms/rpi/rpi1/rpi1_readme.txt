How to install AllStar Asterisk on a Raspberry Pi 1 (PiStar)

Start with at leat 4GB card
Copy stock Raspbian image to SD card
We used the 12-24-2014 image

Boot Raspberry Pi on the SD card.
raspi-config will come up on the first time boot.
	1: Expand the file system to use the full SD card.
	2: Change the User Password to something you will remember.
	8: Advanced Options
		A2: Set the Hostname (pistar)
Finish raspi-config (reboot now)

Raspberry Pi will show the DHCP assigned address.
At this point you can either continue on the USB console or
switch over to SSH login on your LAN.
easier since you can now copy and paste from this document to the SSH screen.

pistar login: pi
Password: Your_Secret_Password_From_Above

# set the root password
pi@pistar~$ sudo -s
root@pistar:/home/pi# passwd root
	Enter new UNIX password: Your_very_secret_password
	Retype new UNIX password: Your_very_secret_password

# run rpi1_allstar_asterisk_install.sh

# edit /etc/asterisk/iax.conf
#       change 1999 to your node number

# edit /etc/asterisk/rpt.conf
#       change 1999 to your node number

# edit /etc/asterisk/extensions.conf
#       change 1999 to your node number

# Patch /boot/config.txt
# This will disable USB Keyboard

# reboot

