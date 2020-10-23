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
  BASENAME=$(head -1 debian/changelog | sed 's/^\([^ ]*\) (\([0-9]*:\)\?\([^)]*\)).*/\1_\3/g')
  cd ..
  mkdir -p build/$BASENAME
  mv *.deb build/$BASENAME
  mv *.build build/$BASENAME
  mv *.buildinfo build/$BASENAME
  mv *.changes build/$BASENAME
done
chown -R user .
