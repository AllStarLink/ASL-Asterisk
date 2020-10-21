#!/bin/bash
set -e

#get DPKG_BUILDOPTS from env var or use default
OPTS=${DPKG_BUILDOPTS:-"-b -uc -us"}


#if the debian changelong changed for asterisk in the last commit, then build the asterisk package
cd /src
if git diff --name-only HEAD HEAD~1 | grep -q asterisk/debian/changelog; then
  cd /src/asterisk
  ./bootstrap.sh && ./configure
  debuild "$OPTS"
fi

#if the debian changelong changed for allstar in the last commit, then build the allstar package
cd /src
if git diff --name-only HEAD HEAD~1 | grep -q allstar/debian/changelog; then
  cd /src/allstar
  debuild "$OPTS"
fi
