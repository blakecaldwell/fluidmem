#!/bin/bash

# Test cases
# 1) allocation test, cache size 1, 100 regions, 1 fault each
# 2) fault test, cache size 1, region size 1, unique writes 1, cycles 1
# 3) fault test, cache size 1000, region size 1000, unique writes 1000, cycles 100
# 4) fault test, cache size 2048, region size 4096, unique writes 4096, cycles 1
# 5) threaded fault test, cache size 2048, region size 4096, cycles 1
# 6) threaded-threaded fault test, cache size 2048, region size 20*1000, cycles 5
# 7) fault test, cache size 2048, region size 4096, cycles 5, reduce lru to 1

if [[ "$1" == "skip_threaded" ]]; then
  echo "Command line with skip_threaded. Skipping multi-threaded (and again threaded) workload test"
  SKIP_THREADED=true
fi

FLUIDMEM_PREFIX=$HOME/fluidmem

# DEBUG is passed as second agument
if [[ "DEBUG" = "$2" ]]; then
  echo "Command line with DEBUG. Logging to ${FLUIDMEM_PREFIX}/monitor.log.DEBUG"
  LOG="${FLUIDMEM_PREFIX}/monitor.log.DEBUG"
else
  LOG="${FLUIDMEM_PREFIX}/monitor.log"
fi

pid=
test_pid=

[[ "$ZOOKEEPER" ]] || ZOOKEEPER=10.0.1.1:2181

function cleanup {
  set +e
  echo "cleaning up..."
  if [ -n $pid ]; then
    bash -c "kill -9 $pid > /dev/null 2>&1 ; exit 0"
  fi

  if [ -n $test_pid ]; then
    bash -c "kill -9 $test_pid > /dev/null 2>&1 ; exit 0"
  fi

  echo "processes still running:"
  bash -c 'ps auxw| grep "monitor\|test_cases"|grep -v grep; exit 0'
}

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${SCRIPT_DIR}/test_common.sh

declare -a test_case_1_stats_name=( "LRU Buffer Capacity" "Registered ufds" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Miss Count" )
declare -a test_case_1_stats_value=( 1 100 100 100 0 99 100 )
declare -a test_case_2_stats_name=( "LRU Buffer Capacity" "Registered ufds" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Miss Count" )
declare -a test_case_2_stats_value=( 1 1 1 1 0 0 1 )
declare -a test_case_3_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Miss Count" )
declare -a test_case_3_stats_value=( 1000 1000 1000 0 0 1000 )
declare -a test_case_4_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Miss Count" )
declare -a test_case_4_stats_value=( 2048 8192 4096 4096 6144 8192 )
declare -a test_case_5_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Miss Count" )
declare -a test_case_5_stats_value=( 2048 8192 4096 4096 6144 8192 )
declare -a test_case_6_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Hit Count" "Cache Miss Count" "Writes Avoided" )
declare -a test_case_6_stats_value=( 2048 199672 20450 179222 197605 0 199672 450 )
declare -a test_case_7_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Miss Count" )
declare -a test_case_7_stats_value=( 1 40960 4096 36864 40959 40960 )

declare -a stats


# start monitor with cache_size of 1
start_monitor "$LOG" "$LOCATOR"  "$ZOOKEEPER"

# check that monitor is running
interval=1
while true; do
  echo "looking for monitor running at $pid"
  if  kill -0 $pid > /dev/null 2>&1; then
  echo "found monitor running $pid"
    if [ -e /var/run/fluidmem/monitor.socket ]; then
      timeout 20s ${FLUIDMEM_PREFIX}/build/bin/ui 127.0.0.1 s
      if [ $? -ne 0 ]; then
        echo "failed to get stats from monitor"
      else
        break
      fi
    fi
  fi
  sleep $interval
  echo "waiting $interval second for monitor to start up"
done

let test_case=1
for cache_size in 1 1 1000 2048 2048 2048 2048; do
  echo "***************"
  echo "running test $test_case with cache size $cache_size"
  echo

  if [[ ${test_case} -eq "6" ]] && [[ $SKIP_THREADED ]]; then
    echo -e "\nSKIPPING...\n"
    continue
  fi

  resize_monitor $cache_size

  echo "Running test_cases for case $test_case"
  time ${FLUIDMEM_PREFIX}/build/bin/test_cases $test_case &
  test_pid=$!

  if [[ ${test_case} -eq "7" ]]; then
    sleep 2
    resize_monitor 1
  fi

  wait_for_monitor 1 "$pid" "${test_pid}" $LOG
  # will return when test_pid is done

  set +e
  wait $test_pid
  if [ $? -eq 0 ]; then
    echo "test passed"
  else
    echo "test failed"
    cleanup
    exit 1
  fi
  set -e

  echo -e "\nStats from monitor:"
  stats=$(timeout 20s ${FLUIDMEM_PREFIX}/build/bin/ui 127.0.0.1 s)

  SAVEIFS=$IFS
  IFS=$(echo -en "\n\b")
  for line in ${stats[@]}; do
    echo $line
  done

  STATS_NAME="test_case_${test_case}_stats_name"
  STATS_VALUE="test_case_${test_case}_stats_value"
  NAME_ARRAY=${STATS_NAME}[@]

  set +e
  echo
  echo "Comparing stats..."
  let count=0

  for i in ${!NAME_ARRAY}; do
    stat_name=$i
    VALUE="${STATS_VALUE}[$count]"
    stat_value=${!VALUE}

    fudge=
    if [[ "$stat_name" = "Page Eviction Count" ]]; then
      stat_value=$(( $( get_stat_value "Total Page Fault Count" ) - $(get_stat_value "LRU Buffer Capacity" )))
      fudge=$(get_stat_value "Zero Pages Encountered" )
      if [[ "$fudge" -eq "0" ]]; then
        # don't pass 0 to compare stats and suppress output
        fudge=
      fi
    elif [[ "$stat_name" = "Cache Miss Count" ]]; then
      stat_value=$( get_stat_value "Total Page Fault Count" )
    elif [[ "$stat_name" = "Placed Data Page Count" ]]; then
      stat_value=$(( $( get_stat_value "Total Page Fault Count" ) - $(get_stat_value "Zero Page Count" )))
    fi

    # set fudge factor for test case 6
    if [[ "$test_case" -eq "6" ]]; then
      if [[ "$stat_name" = "Total Page Fault Count" ]]; then
        fudge=100
      elif [[ "$stat_name" = "Zero Page Count" ]]; then
        stat_value=$(( $( get_stat_value "Writes Avoided" ) + 20000 ))
      elif [[ "$stat_name" = "Writes Avoided" ]]; then
        fudge=100
      fi
    fi

    compare_stats_value ${stat_name} ${stat_value} $fudge

    # check return of comparing stats
    if [[ "$?" -ne "0" ]]; then
      echo "Failed comparing $stat_name in test_case $test_case: expected value $stat_value"
      cleanup
      exit 160
    fi
    (( count ++ ))
  done
  IFS=$SAVEIFS
  set -e

  echo "Comparing stats was succesful."

  if [[ ${test_case} -eq "1" ]]; then
    # flush monitor
    flush_monitor_buffers
  fi

  if [[ ${test_case} -eq "6" ]]; then
    # TODO right now usage caused core dump
    # print_externram_usage
    flush_monitor_buffers
    # print_externram_usage
  fi

  print_bucket_stats
  echo -e "\nMoving on to next step\n"
  reset_counters

  ((test_case++))
done

stop_monitor "$pid" "$LOG"

cleanup
exit 0
