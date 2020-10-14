#!/bin/bash

# Clear out the /build and /release directory
#rm -rf /release/*

# Re-pull the repository
#git fetch 

# Configure, make, make install
cd /src/asterisk
./bootstrap.sh && ./configure
debuild -b -uc -us

cd /src/allstar
debuild -b -uc -us
