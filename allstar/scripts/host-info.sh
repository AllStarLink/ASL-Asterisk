#!/bin/bash
set -o errexit

# N4IRS 03/23/2018

#################################################
#                                               #
#                                               #
#                                               #
#################################################
#
# Functions must be defined before they are used
#

function get_pi_version() {
  declare -r REVCODE=$(awk '/Revision/ {print $3}' /proc/cpuinfo)
  declare -rA REVISIONS=(
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
# echo "Raspberry Pi ${REVISIONS[${REVCODE}]} (${REVCODE})"

RPI_REVCODE=${REVCODE}
RPI_REVISION=${REVISIONS[${REVCODE}]}
}

############### Start Here #######################################

# which lsb_release
id=$(lsb_release -is)
release=$(lsb_release -rs)
codename=$(lsb_release -cs)

# If exists /proc/device-tree/
if [[ -d "/proc/device-tree" && ! -L " /proc/device-tree" ]] ; then
	DEVTREE_MODEL=$(awk '{print $1} {print $2} {print $3} {print $4} {print $5} {print $6}' /proc/device-tree/model)
	DEVTREE_COMPATABLE=$(awk '{print $1} {print $2} {print $3} {print $4} {print $5} {print $6}' /proc/device-tree/compatible)
fi

# If exists /proc/cpuinfo
CPUINFO_MODEL=$(awk '/model name/ {print $4}' /proc/cpuinfo)
CPUINFO_HARDWARE=$(awk '/Hardware/ {print $3}' /proc/cpuinfo)
CPUINFO_REVISION=$(awk '/Revision/ {print $3}' /proc/cpuinfo)

# which uname
uname_kernel_name=$(uname -s)
uname_kernel_release=$(uname -r)
uname_kernel_version=$(uname -v)
uname_machine=$(uname -m)
uname_processor=$(uname -p)
uname_hardware_platform=$(uname -i)
uname_operating_system=$(uname -o)

FREE_MEM=$(free -m | awk 'NR==2{printf "%s MB of %s MB (%.2f%%)\n", $3,$2,$3*100/$2 }')
DISK_USED=$(df -h | awk '$NF=="/"{printf "%d GB of %d GB (%s)\n", $3,$2,$5}')
CPU_LOAD=$(top -bn1 | grep load | awk '{printf "%.2f\n", $(NF-2)}')

# Get Armbian info if available
if [ -f /etc/armbian-release ]
then
	source /etc/armbian-release
	system_type=armbian
fi

# Get TI Debian if info available
if [ -f /boot/SOC.sh ]
then
	source /boot/SOC.sh
	system_type=ti_debian
fi

# Get Raspbian info if available
if [ $id == "Raspbian" ]
then
#	get_pi_version
	system_type=rpi
fi


# Get Intel / AMD info if available
if [ $uname_machine == "x86_64" ] || [ $uname_machine == "i686" ]
then
        system_type=intel-amd
fi

echo ""
#
if [[ $system_type == rpi ]]
then
        echo "=== Raspbian ==="
        echo id = $id             # Raspbian
        echo release = $release   # 9.4
        echo codename = $codename # stretch
	echo Device Tree Compatable = $DEVTREE_COMPATABLE
        echo Device Tree Model = $DEVTREE_MODEL
        echo Free Memory = $FREE_MEM
        echo Disk Used = $DISK_USED
        echo CPU Load = $CPU_LOAD

        # echo # linux-headers = raspberrypi-kernel-headers
        # grep -i '^Revision'  /proc/cpuinfo | tr -d ' ' | cut -d ':' -f 2
        get_pi_version
	echo "Raspberry Pi $RPI_REVISION ($RPI_REVCODE)"

fi

if [[ $system_type == armbian ]]
then
        source /etc/armbian-release
        echo "=== Armbian ==="
        echo id=$id             # Debian
        echo release=$release   # 9.3
        echo codename=$codename # stretch
        echo Free Memory = $FREE_MEM
        echo Disk Used = $DISK_USED
        echo CPU Load = $CPU_LOAD

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

if [[ $system_type == ti_debian ]]
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
        echo Free Memory = $FREE_MEM
        echo Disk Used = $DISK_USED
        echo CPU Load = $CPU_LOAD

        echo format=$format
        echo board=$board       # omap3_beagle
        echo bootloader_location=$bootloader_location
        echo boot_fstype=$boot_fstype   # fat
        echo serial_tty=$serial_tty     # ttyO2
        echo usbnet_mem=$usbnet_mem     # 16384
        echo # linux-headers = linux-headers-uname -r -y
fi

if [[ $system_type == intel-amd ]]


then
        echo "=== Intel / AMD Debian ==="
        echo id = $id             # Debian
        echo release = $release   # 9.3
        echo codename = $codename # stretch
        echo kernel-name = $uname_kernel_name
        echo kernel-release = $uname_kernel_release
        echo kernel-version = $uname_kernel_version
        echo machine = $uname_machine
        echo operating-system = $uname_operating_system
	echo Free Memory = $FREE_MEM
	echo Disk Used = $DISK_USED
	echo CPU Load = $CPU_LOAD
        echo # linux-headers = linux-headers-uname -r -y
fi


# dpkg --get-selections | grep -q linux-headers-$(uname -r)

# if [ $? -eq 1 ]
# then
#         apt-get install linux-headers-$(uname -r)
# fi

