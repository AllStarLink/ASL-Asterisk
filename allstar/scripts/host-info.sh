#!/bin/bash
set -o errexit

# N4IRS 03/23/2018

#################################################
#                                               #
#                                               #
#                                               #
#################################################
#

id=$(lsb_release -is)
release=$(lsb_release -rs)
codename=$(lsb_release -cs)

uname_kernel_name=$(uname -s)
uname_kernel_release=$(uname -r)
uname_kernel_version=$(uname -v)
uname_machine=$(uname -m)
uname_processor=$(uname -p)
uname_hardware_platform=$(uname -i)
uname_operating_system=$(uname -o)

function check_pi_version() {
  local -r REVCODE=$(awk '/Revision/ {print $3}' /proc/cpuinfo)
  local -rA REVISIONS=(
    [0002]="Model B Rev 1, 256 MB RAM"
    [0003]="Model B Rev 1 ECN0001, 256 MB RAM"
    [0004]="Model B Rev 2, 256 MB RAM"
    [0005]="Model B Rev 2, 256 MB RAM"
    [0006]="Model B Rev 2, 256 MB RAM"
    [0007]="Model A, 256 MB RAM"
    [0008]="Model A, 256 MB RAM"
    [0009]="Model A, 256 MB RAM"
    [000d]="Model B Rev 2, 512 MB RAM"
    [000e]="Model B Rev 2, 512 MB RAM"
    [000f]="Model B Rev 2, 512 MB RAM"
    [0010]="Model B+, 512 MB RAM"
    [0013]="Model B+, 512 MB RAM"
    [900032]="Model B+, 512 MB RAM"
    [0011]="Compute Module, 512 MB RAM"
    [0014]="Compute Module, 512 MB RAM"
    [0012]="Model A+, 256 MB RAM"
    [0015]="Model A+, 256 MB or 512 MB RAM"
    [a01041]="2 Model B v1.1, 1 GB RAM"
    [a21041]="2 Model B v1.1, 1 GB RAM"
    [a22042]="2 Model B v1.2, 1 GB RAM"
    [90092]="Zero v1.2, 512 MB RAM"
    [90093]="Zero v1.3, 512 MB RAM"
    [0x9000C1]="Zero W, 512 MB RAM"
    [a02082]="3 Model B, 1 GB RAM"
    [a22082]="3 Model B, 1 GB RAM"
  )
echo "Raspberry Pi ${REVISIONS[${REVCODE}]} (${REVCODE})"
}

#
if [ $id == "Raspbian" ]
then
        echo "=== Raspbian ==="
        echo id=$id             # Raspbian
        echo release=$release   # 9.4
        echo codename=$codename # stretch
        # echo # linux-headers = raspberrypi-kernel-headers
        # grep -i '^Revision'  /proc/cpuinfo | tr -d ' ' | cut -d ':' -f 2
        check_pi_version
fi

if [ -f /etc/armbian-release ]
then
        source /etc/armbian-release
        echo "=== Armbian ==="
        echo id=$id             # Debian
        echo release=$release   # 9.3
        echo codename=$codename # stretch

        echo BOARD=$BOARD
        echo BOARD_NAME=$BOARD_NAME
        echo BOARDFAMILY=$BOARDFAMILY
        echo VERSION=$VERSION
        echo LINUXFAMILY=$LINUXFAMILY
        echo BRANCH=$BRANCH
        echo ARCH=$ARCH
        # IMAGE_TYPE=user-built
        # BOARD_TYPE=conf
        # INITRD_ARCH=arm
        # KERNEL_IMAGE_TYPE=zImage
        echo # linux-headers = linux-headers-$BRANCH-$LINUXFAMILY
fi

if [ -f /boot/SOC.sh ]
then
        source /boot/SOC.sh
        echo "=== Beagle Debian ==="
        echo id=$id             # Debian
        echo release=$release   # 9.3
        echo codename=$codename # stretch
        echo kernel-name=$uname_kernel_name
        echo kernel-release=$uname_kernel_release
        echo kernel-version=$uname_kernel_version
        echo machine=$uname_machine
        # echo processor=$uname_processor
        # echo hardware-platform=$uname_hardware_platform
        echo operating-system=$uname_operating_system

        echo format=$format
        echo board=$board       # omap3_beagle
        echo bootloader_location=$bootloader_location
        echo boot_fstype=$boot_fstype   # fat
        echo serial_tty=$serial_tty     # ttyO2
        echo usbnet_mem=$usbnet_mem     # 16384
        echo # linux-headers = linux-headers-$BRANCH-$LINUXFAMILY
fi


