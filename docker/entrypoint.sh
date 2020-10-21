#!/bin/bash
set -e

#get DPKG_BUILDOPTS from env var or use default
OPTS=${DPKG_BUILDOPTS:-"-b -uc -us"}

for t in "$BUILD_TARGETS"; do
  cd /src/$t
  if [ "$t" -eq "asterisk" ]; then
    ./bootstrap.sh && ./configure
  fi
  debuild "$OPTS"
done
