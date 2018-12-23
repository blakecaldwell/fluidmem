#!/bin/bash -xe

configure_cmd=$(grep -e "^  \$ \.\/configure " /fluidmem/config.log|sed 's/  \$ //')
cd /fluidmem
make distclean
eval "$configure_cmd --enable-debug --enable-trace --enable-lock-debug"
make -j5
make install
