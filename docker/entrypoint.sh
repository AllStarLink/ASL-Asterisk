#!/bin/bash
set -e

#if the debian changelong changed for asterisk in the last commit, then build the asterisk package
if git diff --name-only HEAD HEAD~1 | grep -q asterisk/debian/changelog; then
  cd /src/asterisk
  ./bootstrap.sh && ./configure
  debuild -b -uc -us
fi

#if the debian changelong changed for allstar in the last commit, then build the allstar package
if git diff --name-only HEAD HEAD~1 | grep -q allstar/debian/changelog; then
  cd /src/allstar
  debuild -b -uc -us
fi
