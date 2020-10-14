#!/bin/bash

ARCHS="amd64 armhf"
ARCHS=amd64


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PDIR=$(dirname $DIR)
echo $DIR
echo $PDIR

for A in $ARCHS; do
	docker build -f Dockerfile.$A -t asl-asterisk_builder.$A $DIR
	docker run -v $PDIR:/src asl-asterisk_builder.$A
	docker image rm --force asl-asterisk_builder.$A
done
