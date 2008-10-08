#!/bin/sh

check_for_app() {
	$1 --version 2>&1 >/dev/null
	if [ $? != 0 ]
	then
		echo "Please install $1 and run bootstrap.sh again!"
		exit 1
	fi
}

AUTOCONF_VERSION=2.59
AUTOMAKE_VERSION=1.9
export AUTOCONF_VERSION
export AUTOMAKE_VERSION

check_for_app autoconf
check_for_app automake
check_for_app aclocal
echo "Generating the configure script ..."
aclocal 2>/dev/null
autoconf
automake --add-missing --copy 2>/dev/null

exit 0
