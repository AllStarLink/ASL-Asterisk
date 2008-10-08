# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

m4_define([ZAP_VERSION],
          m4_bpatsubst(m4_esyscmd([build_tools/make_version .]),
                       [\([0-9.]*\)\(\w\|\W\)*],
                       [\1]))
AC_INIT(zaptel, ZAP_VERSION, www.asterisk.org)

# check existence of the package
AC_CONFIG_SRCDIR([kernel/zaptel-base.c])

AC_COPYRIGHT("Zaptel")
AC_REVISION($Revision: 3793 $)

ac_default_prefix=/usr
if test ${sysconfdir} = '${prefix}/etc'; then
   sysconfdir=/etc
fi
if test ${mandir} = '${prefix}/man'; then
   mandir=/usr/share/man
fi

if test ${localstatedir} = '${prefix}/var'; then
     localstatedir=/var
fi

# This needs to be before any macros that use the C compiler
AC_GNU_SOURCE

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AST_CHECK_GNU_MAKE

test_obj=conftest.o
AC_COMPILE_IFELSE(AC_LANG_SOURCE(),[
	BDFNAME=`LANG=C objdump -f $test_obj | grep -e "$test_obj:" | sed "s/.*file format \(.*\)/\1/"`
	BDFARCH=`LANG=C objdump -f $test_obj | grep -e "architecture:" | sed "s/.*ture: \(.*\),.*/\1/"`	   
],[])
AC_SUBST(BDFNAME)
AC_SUBST(BDFARCH)

# Set the default value of HOSTCC from CC if --host was not provided:
HOSTCC=${HOSTCC:=${CC}}
AC_SUBST(HOSTCC)

AC_PATH_PROG([GREP], [grep], :)
AC_PATH_PROG([SHELL], [sh], :)
AC_PATH_PROG([LN], [ln], :)

AC_PATH_PROG([WGET], [wget], :)
if test "${WGET}" != ":" ; then
  DOWNLOAD=${WGET}
else
  AC_PATH_PROG([FETCH], [fetch], [:])
  DOWNLOAD=${FETCH}
fi
AC_SUBST(DOWNLOAD)

AC_LANG(C)

AST_EXT_LIB([curses], [initscr], [curses.h], [CURSES], [curses], [])
AST_EXT_LIB([ncurses], [initscr], [curses.h], [NCURSES], [ncurses], [])
AST_EXT_LIB([newt], [newtBell], [newt.h], [NEWT], [newt])
AST_EXT_LIB([usb], [usb_init], [usb.h], [USB], [libusb])

AC_ARG_WITH(selinux,
	[AS_HELP_STRING([--with-selinux],
			[enable (with) / disable (without) SELinux])],
	[USE_SELINUX=$withval],
	[ if test ! -x /usr/sbin/sestatus; then 
	   	USE_SELINUX=no;
	  elif /usr/sbin/sestatus | grep "SELinux status:" | grep -q "enabled"; then
		USE_SELINUX=yes
	  fi
	]
)


AC_SUBST(USE_SELINUX)

# for asciidoc before ver. 7, the backend must be stated explicitly:
ASCIIDOC='asciidoc'
asciidoc_ver=`asciidoc --version 2>&1 | awk '/^asciidoc /{print $2}' | cut -d. -f 1 | head -n 1`
if test "$asciidoc_ver" != '' && test $asciidoc_ver -lt 7; then
	ASCIIDOC="asciidoc -b xhtml"
fi
AC_SUBST(ASCIIDOC)

AC_ARG_WITH(ppp,
	    [AS_HELP_STRING([--with-ppp=PATH],[Use ppp support from PATH])],
	    [],
	    [with_ppp=check]
	    )
# somebody will fix that
default_ppp_path=/usr

case "$with_ppp" in
	yes|check) ppp_path="$default_ppp_path";;
	no) ppp_path='' ;;
	*) ppp_path="$with_ppp" ;;
esac

level_file="$ppp_path/include/pppd/patchlevel.h"
PPP_VERSION=
if test "$ppp_path" != '' && test -r "$level_file"; then
	PPPD_VERSION=`awk -F '"' '/VERSION/ { print $$2; }' $level_file`
fi

case "$with_ppp" in 
	check|no) :;;
	*) 
	# If we asked explicitly for ppp support
	if test "$PPPD_VERSION" = ''; then
		# but have not detected it
		AC_MSG_ERROR(failed to find pppd/patchlevel.h: no ppp support.)
	fi
	;;
esac

AC_SUBST(PPPD_VERSION)

AC_CONFIG_FILES([build_tools/menuselect-deps makeopts build_tools/make_firmware_object])
AC_OUTPUT
chmod a+x  build_tools/make_firmware_object

AC_MSG_NOTICE(*** Zaptel build successfully configured ***)

