#!/bin/bash -xe

configure_cmd=$(grep -e "^  \$ \.\/configure " /scaleos/config.log|sed 's/  \$ //')
cd /scaleos
make distclean
eval "$configure_cmd --enable-debug --enable-trace --enable-lock-debug"
make -j5
make install
