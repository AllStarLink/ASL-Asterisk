#!/bin/bash
set -e

#get DPKG_BUILDOPTS from env var or use default
OPTS=${DPKG_BUILDOPTS:-"-b -uc -us"}

env

for t in "$BUILD_TARGETS"; do
  echo "$t"
  cd /src/$t
  pwd
  if [ "$t" == "asterisk" ]; then
    ./bootstrap.sh && ./configure
  fi
  debuild $OPTS
done
chown -R user ..
