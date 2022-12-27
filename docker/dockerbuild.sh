#!/bin/bash

set -e

while [[ $# -gt 0 ]]; do
  case $1 in
    -c|--check-changelog)
      CHECK_CHANGELOG=YES
      shift
      ;;
    -a|--architectures)
      ARCHS="$2"
      shift
      shift
      ;;
    -t|--targets)
      TARGETS="$2"
      shift
      shift
      ;;
    -*|--*|*)
      echo "Unknown option $1"
      exit 1
      ;;
  esac
done

if [ -z "$ARCHS" ]
then
  ARCHS="amd64 armhf"
fi

if [ -z "$TARGETS" ]
then
  TARGETS="asterisk allstar"
fi

echo "Architectures: $ARCHS"
echo "Targets: $TARGETS"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PDIR=$(dirname $DIR)

#get the build targets
cd $PDIR
BUILD_TARGETS=""
ANYCOUNT=0
for t in $TARGETS; do
  if [ -z "$CHECK_CHANGELOG" ] || git diff --name-only HEAD HEAD~1 | grep -q $t/debian/changelog; then
    BUILD_TARGETS+="$t "
    c=$(grep "^Architecture:" $t/debian/control | egrep -v "^Architecture: ?all" | wc -l)
    ANYCOUNT=$((c+ANYCOUNT))
  fi
done
BUILD_TARGETS=$(echo "$BUILD_TARGETS" | xargs)


#if 'any' = 0, only run for one arch (there are no arch specific packages)
if [ "$ANYCOUNT" -eq "0" ] ; then
  set -- $ARCHS
  ARCHS=$1
fi

#run --build=any for following arch's after the first to prevent re-creating 'all' packages
DPKG_BUILDOPTS="-b -uc -us"
for A in $ARCHS; do
       docker build -f $DIR/Dockerfile.$A -t asl-asterisk_builder.$A --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) $DIR
       docker run -v $PDIR:/src -e DPKG_BUILDOPTS="$DPKG_BUILDOPTS" -e BUILD_TARGETS="$BUILD_TARGETS" asl-asterisk_builder.$A
       docker image rm --force asl-asterisk_builder.$A
       DPKG_BUILDOPTS="--build=any -uc -us"
done
