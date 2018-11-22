#!/bin/sh

cd /usr/local/sayip

modprobe snd_bcm2835 > /dev/null 2>&1
amixer cset numid=3 1 > /dev/null 2>&1
amixer cset numid=1 -- 100% > /dev/null 2>&1

DONE=0

while [ $DONE -eq 0 ]
do

    IPADDR=`ifconfig eth0 | grep 'inet addr' | cut -d: -f2 | cut -d' ' -f1`

    if [ ".$IPADDR" = "." ]
    then
	aplay bong.wav > /dev/null 2>&1
    else
        CMD="cat "

        for x in `echo "$IPADDR" | fold -w1`; do
          if [ "$x" = "." ]
          then
            x=dot
          fi
          CMD="$CMD$x".ulaw" "
        done

        `$CMD | aplay -fMU_LAW > /dev/null 2>&1`
	DONE=1
    fi
done

rmmod snd_bcm2835
