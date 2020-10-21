#!/bin/bash
set -e

ARCHS="amd64 armhf"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PDIR=$(dirname $DIR)

#get number of 'all' packages
ALLCOUNT=$(egrep "^Architecture: ?all" debian/control | wc -l)

#get number of 'any'(non-any) packages
ANYCOUNT=$(grep "^Architecture:" debian/control | egrep -v "^Architecture: ?all" | wc -l)

#if 'any' = 0, only run for one arch (there are no arch specific packages)
if [ "$ANYCOUNT" -eq "0" ] ; then
  set -- $ARCHS
  ARCHS=$1
fi

#run --build=any for following arch's after the first to prevent re-creating 'all' packages
DPKG_BUILDOPTS="-b -uc -us"
for A in $ARCHS; do
	docker build -f $DIR/Dockerfile.$A -t asl-asterisk_builder.$A $DIR
	docker run -v $PDIR:/src asl-asterisk_builder.$A --env DPKG_BUILDOPTS="$DPKG_BUILDOPTS"
	docker image rm --force asl-asterisk_builder.$A
	DPKG_BUILDOPTS="--build=any -uc -us"
done
