#!/bin/bash

# Copyright 2015 Blake Caldwell, University of Colorado,  All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited
# Proprietary and confidential
# Written by Blake Caldwell <blake.caldwell@colorado.edu>, May 2015
#

list=( $(ls -dvr /sys/devices/system/memory/memory*) )


for memdir in ${list[@]}; do
  base=$(basename $memdir)
  if [[ "${base##memory}" -lt 31 ]]; then
    continue
  fi
  if [[ "$(cat $memdir/state)" =~ "online" ]]; then
    continue
  fi
  echo "online_movable" > $memdir/state
  if [[ "$?" -ne "0" ]]; then
    echo "failed to online $base. trying online_kernel"
    echo "online_kernel" > $memdir/state
    if [[ "$?" -ne "0" ]]; then
      echo "failed to online_kernel $base"
    else
      echo "onlined $base as zone kernel"
    fi
  else
    echo "onlined $base as zone normal and movable"
  fi
done
