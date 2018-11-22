cd /tmp

wget https://github.com/AllStarLink/Asterisk/raw/develop/allstar/repository/install-repository

chmod +x /install-repository

./install-repository

If Raspbian:
apt-get install raspberrypi-kernel-headers -y

Otherwise:
apt-get install linux-headers-`uname -r` -y

apt-get update

apt-get install allstarlink -y

