# Asterisk Source for AllStarLink

This is the Asterisk source package for AllStarLink and the files to build the ASL 2.0.0+ distribution.

---------------------------------------------------------------------------------------------------------------------------------

AllStarLink wiki: https://wiki.allstarlink.org

AllStarLink Portal:  https://www.allstarlink.org

Official AllStarLink mailing list - app_rpt-users: http://lists.allstarlink.org

AllStarLink Network/System status:  https://grafana.allstarlink.org

---------------------------------------------------------------------------------------------------------------------------------

## Prerequisites

#### Debian 10 Buster

* Install the ASL GPG Key

<pre>wget -q0 - http://apt.allstarlink.org/repos/repo_signing.key | | sudo apt-key add -
apt update</pre>

* Install apt dependencies
<pre>
apt -y install quilt libreadline-gplv2-dev libgsm1-dev libssl-dev libasound2-dev libpq-dev unixodbc-dev libpri-dev libvpb-dev asl-dahdi-source libnewt-dev libsqlite-dev libspeex-dev libspeexdsp-dev libcurl4-openssl-dev libpopt-dev libiksemel-dev freetds-dev libvorbis-dev libsnmp-dev libcap-dev libi2c-dev libjansson-dev raspberrypi-kernel-headers build-essential git cmake g++ libboost-all-dev libgmp-dev swig python3-numpy asl-dahdi-source</pre>

## Compiling
<pre>
git clone https://github.com/AllStarLink/ASL-Asterisk.git
cd ASL-Asterisk/asterisk
./configure
make
make install
</pre>

If all goes well, you will have cloned, configured, compiled and installed the Astersisk 1.4.23pre and app_rpt suite of programs that comprise the ASL 1.01+ release onto your system.

Packaging (.deb)

<pre>
git clone https://github.com/AllStarLink/ASL-Asterisk.git
cd ASL-Asterisk/asterisk
debuild -b -us -uc
</pre>

.debs will appear in the repository root folder after compiling and packaging

---------------------------------------------------------------------------------------------------------------------------------

## Help

Refer to the app_rpt-users mailing list and AllStarLink Wiki for information on the app_rpt suite of programs.

---------------------------------------------------------------------------------------------------------------------------------

## Contributing

Please refer to the [Contributing](https://wiki.allstarlink.org/wiki/Contributing) page on the AllStarLink Wiki.

## Copyright

Asterisk 1.4.23pre is copyright Digium (https://www.digium.com)

app_rpt and associated programs (app_rpt suite) are copyright Jim Dixon, WB6NIL; 2018-2021 AllStarLink, Inc., and contributors

_(Refer to each individual's file source code for full copyright information)_

## License

Asterisk, app_rpt, and all associated code/files are licensed, released, and distributed under the GNU General Public License v2 and cannot be relicensed without written permission of Digium and the copyright holders of the app_rpt suite of programs.
