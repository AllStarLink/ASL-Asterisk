#! /bin/bash
# sudo -u nobody ./web-server.sh
sudo -u nobody /usr/bin/php -S 0.0.0.0:8080 -t . router.php

