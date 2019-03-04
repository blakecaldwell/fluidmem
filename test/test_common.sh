compare_stats_value ()
{
  if [ -z "$1" ] || [ -z "$2" ]; then
    echo "bad arguments passed to compare_stats_value"
    cleanup
    exit 1
  fi

  failed=
  echo "Checking that $1 is $2..."

  fudge=0
  if [ -n "$3" ]; then
    fudge="$3"
    echo "with a fudge factor of ${fudge}..."
  fi

  SAVEDIFS=$IFS
  while IFS=$(echo -en "\n\b") read -ra LINE; do
    while IFS=':' read stat value; do
      if [[ "$stat" = "$1" ]]; then
        value=$(echo -e "${value}" | tr -d '[:space:]')
        expected_value=$(echo -e "$2" | tr -d '[:space:]')
        if [[ "$value" -ne "$expected_value" ]]; then
          (( upper = value + fudge))
          (( lower = value - fudge))
          if [[ "$upper" -ge "${expected_value}" ]] && [[ "$lower" -le "${expected_value}" ]]; then
            echo "Returned value $value is within fudge factor $fudge of ${expected_value}"
            failed=0
            break         
          else
            echo "Expected $expected_value, but got $value"
            failed=1
            break
          fi
        else
          failed=0
          break
        fi
      fi
    done <<< "${LINE[@]}"
    if [ -n "$failed" ]; then
      break
    fi
  done <<< "${stats[@]}"
  IFS=$SAVEDIFS

  if [[ "$failed" -eq "1" ]]; then
    false
  else
    true
  fi
}

function flush_monitor_buffers {
  # this can take a long time with virtualized ramcloud
  timeout 120s /fluidmem/build/bin/ui 127.0.0.1 f
  if [ $? -ne 0 ]; then
    echo "failed to flush monitor's buffers"
    monitor_failed $LOG
  fi
}

function reset_counters {
  timeout 20s /fluidmem/build/bin/ui 127.0.0.1 c > /dev/null
  if [ $? -ne 0 ]; then
    echo "failed to reset monitor's counters"
    monitor_failed $LOG
  fi
}

function monitor_failed {
  if [[ -z "$1" ]]; then
    echo "bad arguments passed to monitor_failed"
    exit 0
  else
    LOG="$1"
  fi
  echo
  echo "Logs from monitor:"
  sync
  tail -200 "$LOG"|grep -v "^[[:space:]]*$"|tail -30
  echo
  exit 1
}

function stop_monitor {
  set +e
  if [[ -z "$1" ]] || [[ -z "$2" ]]; then
    echo "bad argument passed to stop_monitor"
    cleanup
    exit 1
  fi
  pid="$1"
  LOG="$2"

  echo -n "Stopping monitor using ui."
  /fluidmem/build/bin/ui 127.0.0.1 t
  if [ $? -ne 0 ]; then
    echo "failed to stop monitor"
    monitor_failed $LOG
  fi

  wait $pid
  if [ $? -ne 0 ]; then
    echo "monitor had exit code $?"
    monitor_failed $LOG
    # exits
  fi

  echo
  echo "Logs from monitor:"
  sync
  tail -200 "$LOG" |grep -v "^[[:space:]]*$"|tail -30

  let count=0

  while ps a | grep -v grep | grep -q monitor; do
    sleep 1
    (( count++ ))
  done

  if [ $count -eq 10 ]; then
    echo "monitor was not killed"
    ps a | grep -v grep | grep monitor
    exit 1
  fi

  rm -f /var/run/fluidmem/monitor.socket
  set -e
}

function resize_monitor {
  if [ -z "$1" ]; then
    echo "bad argument passed to resize_monitor"
    cleanup
    exit 1
  fi

  echo "resizing monitor to cache size $1"
  timeout 20s /fluidmem/build/bin/ui 127.0.0.1 r $1
  if [ $? -ne 0 ]; then
    echo "failed to resize lru list"
    monitor_failed $LOG
  fi
}

function start_monitor {
  if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
    echo "bad arguments passed to start_monitor"
    cleanup
    exit 1
  fi

  if [ -z "$4" ] || [ -z "$5" ]; then
    prefetch_args=
  else
    prefetch_args="--enable_prefetch=1 \
    --page_cache_size=$4 \
    --prefetch_size=$5"
  fi

  # start with initial cache size of 1
  cmd="/fluidmem/build/bin/monitor $2 --cache_size=1 \
    --zookeeper=$3 \
    --print_info \
    --exit-on-recoverable-error \
    --test_readahead \
    ${prefetch_args} \
    >> ${LOG} 2>&1 &"
  echo "Starting monitor with command:"
  echo $cmd
  eval $cmd
  pid=$!
  sleep 1

  set +e
  let count=0
  set -e
  while [ ! -e /var/run/fluidmem/monitor.socket ]; do
    echo "Waiting for monitor.socket"
    sleep 1
    (( count++ ))
    if [ $count -eq 10 ]; then
      echo "monitor failed to start"
      cleanup
      exit 1
    fi
  done
}

function wait_for_monitor {
  if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ] || [ -z "$4" ]; then
    echo "bad arguments passed to wait_for_monitor"
    cleanup
    exit 1
  fi

  interval="$1"
  pid="$2"
  test_pid="$3"
  LOG="$4"

  # Ensure that monitor is still running
  failed=0
  while true; do
    sleep $interval
    if ! kill -0 $pid > /dev/null 2>&1; then
      failed=1
      break
    else
      timeout 20s /fluidmem/build/bin/ui 127.0.0.1 s > /monitor.stats.new
      if [ $? -ne 0 ]; then
        echo "failed to get stats from monitor"
        monitor_failed $LOG
      fi
      if [ -e /monitor.stats ]; then
        if diff /monitor.stats /monitor.stats.new > /dev/null; then
            echo "Check num $count: no progress made in last $interval seconds. Current time:"
            date
        fi
      fi
      timeout 20s mv /monitor.stats.new /monitor.stats
    fi
    if ! kill -0 $test_pid > /dev/null 2>&1; then
      break
    fi
  done

  if [[ $failed -ne "0" ]]; then
    echo "Monitor failed before test was complete!!"
    PGRP=$(ps -o pgid= $test_pid | grep -o [0-9]*)
    if ! kill -- -"$PGRP" > /dev/null 2>&1; then
      echo "Could not send SIGTERM to process group $PGRP" >&2
    fi
    monitor_failed $LOG
    exit 1
  fi
}

function print_bucket_stats {
  echo -e "\nBucket Stats:"
  timeout 20s /fluidmem/build/bin/ui 127.0.0.1 b
  if [ $? -ne 0 ]; then
    echo "failed to get bucket stats from monitor"
    monitor_failed $LOG
  fi
}
