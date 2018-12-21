#!/bin/bash

trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

[[ $COORDINATOR ]] || {
  echo "requires \$COORDINATOR environment variable"
  exit 1
}

[[ $SERVER]] || {
  echo "requires \$SERVER environment variable"
  exit 1
}

ZOOKEEPER="zk:${COORDINATOR}:2181"
RAMCLOUD_DIR=$HOME/RAMCloud
if [ ! -e "$RAMCLOUD_DIR/obj.master" ]; then
  echo "Could not find RAMCloud installation at $RAMCLOUD_DIR"
  exit 1
fi

[[ $TRANSPORT ]] || TRANSPORT=infrc
[[ $REPLICAS ]] || REPLICAS=1
[[ $NUM_SERVERS ]] || NUM_SERVERS=2
[[ $TOTAL_MEM ]] || TOTAL_MEM=8000

if [[ ${NUM_SERVERS} -le $REPLICAS ]]; then
  EXTRA_SERVER_OPTS+="--allowLocalBackup"
fi

# segments are 8MB, so divide total mem by 8
SEGMENTS=$(( ${TOTAL_MEM} / 8 ))

# start the coordinator
if [[ "$COORDINATOR" == "$SERVER" ]]; then
  $RAMCLOUD_DIR/obj.master/coordinator -C ${TRANSPORT}:host=$COORDINATOR,port=12246 -L ${TRANSPORT}:host=${COORDINATOR},port=12243 -x $ZOOKEEPER --reset &
  COORD_PID=$!
fi

# start the server
$RAMCLOUD_DIR/obj.master/server -L ${TRANSPORT}:host=$SERVER,port=12241 -x $ZOOKEEPER --totalMasterMemory ${TOTAL_MEM} --segmentFrames ${SEGMENTS} -r $REPLICAS $EXTRA_SERVER_OPTS &
SERVER_PID=$!

wait
