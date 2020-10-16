#!/bin/bash
set -e

ARCHS="amd64 armhf"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PDIR=$(dirname $DIR)
echo $DIR
echo $PDIR

for A in $ARCHS; do
	docker build -f $DIR/Dockerfile.$A -t asl-asterisk_builder.$A $DIR
	docker run -v $PDIR:/src asl-asterisk_builder.$A
	docker image rm --force asl-asterisk_builder.$A
done
