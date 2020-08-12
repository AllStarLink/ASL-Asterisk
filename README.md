# Asterisk source package for AllStarLink

---------------------------------------------------------------------------------------------------------------------------------

This is the Asterisk source package for AllStarLink and the files to build the ASL 1.01+ distribution.

---------------------------------------------------------------------------------------------------------------------------------

AllStarLink wiki: https://wiki.allstarlink.org

AllStarLink Portal:  https://www.allstarlink.org

Official AllStarLink mailing list - app_rpt-users: http://lists.allstarlink.org

AllStarLink Network/System status:  https://grafana.allstarlink.org

---------------------------------------------------------------------------------------------------------------------------------

## Copyright

Asterisk 1.4.23pre is copyright Digium (https://www.digium.com)

app_rpt and associated programs (app_rpt suite) are copyright Jim Dixon, WB6NIL; AllStarLink, Inc.; and contributors

_(Refer to each individual's file source code for full copyright information)_

## License

Asterisk, app_rpt and all associated code/files are licensed, released, and distributed under the GNU General Public License v2 and cannot be relicensed without written permission of Digium and the copyright holders of the app_rpt suite of programs.

---------------------------------------------------------------------------------------------------------------------------------

## Prerequisites

Install gcc and g++ on your system
_Refer to your Linux distribution's documentation for information on how to do this_

## Compiling
_Make sure the Asterisk 1.4 prerequisites are installed on your system before attempting to compile_

<pre>
git clone https://github.com/ajpaul25/ASL-Asterisk.git
cd ASL-Asterisk/asterisk
./configure
make
make install
</pre>

If all goes well, you will have cloned, configured, compiled and installed the Astersisk 1.4.23pre and app_rpt suite of programs that comprise the ASL 1.01+ release onto your system.

Packaging

<pre>
git clone https://github.com/ajpaul25/ASL-Asterisk.git
cd ASL-Asterisk/asterisk
debuild -b -us -uc
</pre>
---------------------------------------------------------------------------------------------------------------------------------

## Help

Refer to the app_rpt-users mailing list and AllStarLink Wiki for information on the app_rpt suite of programs.

---------------------------------------------------------------------------------------------------------------------------------

## Contributing

Please refer to the [Contributing](https://wiki.allstarlink.org/wiki/Contributing) page on the AllStarLink Wiki.
