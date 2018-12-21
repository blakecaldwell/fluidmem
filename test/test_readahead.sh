#!/bin/bash

[[ "$ZOOKEEPER" ]] || ZOOKEEPER=10.0.1.1:2181

pid=
test_pid=

if [[ "$1" == "skip_random" ]]; then
  echo "Command line with skip_random. Skipping random workload tests"
  SKIP_RANDOM=true
fi

if [[ "DEBUG" = "$2" ]]; then
  echo "Command line with DEBUG. Logging to /monitor.log.DEBUG"
  LOG="/monitor.log.DEBUG"
else
  LOG="/monitor.log"
fi


# ( LRU_cache_size page_cache_size prefetch_size isSequential )
declare -a RA_TEST_SCENARIO_0=( 50 0 0 1 )
declare -a RA_TEST_SCENARIO_1=( 50 10 2 1 )
declare -a RA_TEST_SCENARIO_2=( 50 10 5 1 )
declare -a RA_TEST_SCENARIO_3=( 50 100 10 1 )
declare -a RA_TEST_SCENARIO_4=( 50 1000 10 1 )
declare -a RA_TEST_SCENARIO_5=( 50 10000 10 1 )
declare -a RA_TEST_SCENARIO_6=( 50 0 0 0 )
declare -a RA_TEST_SCENARIO_7=( 50 10 2 0 )
declare -a RA_TEST_SCENARIO_8=( 50 10 5 0 )
declare -a RA_TEST_SCENARIO_9=( 50 100 10 0 )
declare -a RA_TEST_SCENARIO_10=( 50 1000 10 0 )
declare -a RA_TEST_SCENARIO_11=( 50 10000 10 0 )

RA_TEST_SCENARIO_NUM=12
RA_TEST_MEM_SIZE=1 # in megabytes

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

declare -a test_case_0_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" "Cache Hit Count" "Cache Miss Count" "Writes Avoided" )
declare -a test_case_0_stats_value=( 50 1282 513 769 1232 0 1282 257 )
declare -a test_case_1_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" )
declare -a test_case_1_stats_value=( 50 1282 513 769 1232 )
declare -a test_case_2_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" )
declare -a test_case_2_stats_value=( 50 1282 513 769 1232 )
declare -a test_case_3_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" )
declare -a test_case_3_stats_value=( 50 1282 513 769 1232 )
declare -a test_case_4_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" )
declare -a test_case_4_stats_value=( 50 1282 513 769 1232 )
declare -a test_case_5_stats_name=( "LRU Buffer Capacity" "Total Page Fault Count" "Zero Page Count" "Placed Data Page Count" "Page Eviction Count" )
declare -a test_case_5_stats_value=( 50 1282 513 769 1232 )
# random tests 6-11
declare -a test_case_6_stats_name=( "LRU Buffer Capacity" "Zero Page Count" "Cache Hit Count" "Writes Avoided" )
declare -a test_case_6_stats_value=( 50 507 0 251 )
declare -a test_case_7_stats_name=( "LRU Buffer Capacity" "Zero Page Count" "Writes Avoided" )
declare -a test_case_7_stats_value=( 50 507 251 )
declare -a test_case_8_stats_name=( "LRU Buffer Capacity" "Zero Page Count" "Writes Avoided" )
declare -a test_case_8_stats_value=( 50 507 251 )
declare -a test_case_9_stats_name=( "LRU Buffer Capacity" "Zero Page Count" "Writes Avoided" )
declare -a test_case_9_stats_value=( 50 507 251 )
declare -a test_case_10_stats_name=( "LRU Buffer Capacity" "Zero Page Count" "Writes Avoided" )
declare -a test_case_10_stats_value=( 50 507 251 )
declare -a test_case_11_stats_name=( "LRU Buffer Capacity" "Zero Page Count" "Writes Avoided" )
declare -a test_case_11_stats_value=( 50 507 251 )
declare -a stats

let test_case=1
for ((test_case=0;test_case<RA_TEST_SCENARIO_NUM;test_case++))
do
  CACHE_SIZE="RA_TEST_SCENARIO_$test_case[0]"
  PAGE_CACHE_SIZE="RA_TEST_SCENARIO_$test_case[1]"
  PREFETCH_SIZE="RA_TEST_SCENARIO_$test_case[2]"
  IS_SEQUENTIAL="RA_TEST_SCENARIO_$test_case[3]"
  echo "***************"
  echo "Running test scenario $test_case"
  echo -e "\nTest Parameters:\n"
  echo "CACHE_SIZE ${!CACHE_SIZE}"
  echo "PAGE_CACHE_SIZE ${!PAGE_CACHE_SIZE}"
  echo "PREFETCH_SIZE ${!PREFETCH_SIZE}"
  echo "IS_SEQUENTIAL ${!IS_SEQUENTIAL}"

  # start monitor with cache_size of 1
  if [[ ${!PAGE_CACHE_SIZE} > 0 ]]; then
    start_monitor "$LOG" "$LOCATOR"  "$ZOOKEEPER" "${!PAGE_CACHE_SIZE}" "${!PREFETCH_SIZE}"
  else
    start_monitor "$LOG" "$LOCATOR"  "$ZOOKEEPER"
  fi

  for ((test_num=1;test_num<=2;test_num++))
  do
    if [[ "$test_num" -eq "2" ]]; then
      resize_monitor ${!CACHE_SIZE}
      filename="test_readahead_${!CACHE_SIZE}"
    else
      filename="test_readahead_1"
    fi

    filename="${filename}_${!PAGE_CACHE_SIZE}_${!PREFETCH_SIZE}"
    if [[ ${!IS_SEQUENTIAL} > 0 ]]; then
      filename="${filename}_sequential.txt"
    else
      if [[ $SKIP_RANDOM ]]; then
        echo -e "\nSKIPPING...\n"
        continue
      fi
      filename="${filename}_random.txt"
    fi

    cmd="time /fluidmem/build/bin/test_readahead ${RA_TEST_MEM_SIZE} "
    if [[ ${!IS_SEQUENTIAL} > 0 ]]; then
      cmd="${cmd} --sequential"
    else
      cmd="${cmd} --random"
    fi
    cmd="${cmd} > ${filename} &"
    printf "Running test command: ${cmd}\n"
    eval $cmd
    test_pid=$!

    wait_for_monitor 1 "$pid" "${test_pid}"
    # will return when test_pid is done

    set +e
    wait $test_pid
    if [ $? -eq 0 ]; then
      echo -e "\nTest passed\n"
    else
      echo "\nTest failed\n"
      cleanup
      exit 1
    fi
    set -e

    flush_monitor_buffers

    echo -e "\nStats from monitor:"
    stats=$(timeout 20s /fluidmem/build/bin/ui 127.0.0.1 s)

    SAVEIFS=$IFS
    IFS=$(echo -en "\n\b")
    for line in ${stats[@]}; do
      echo $line
    done

    if [[ "$test_num" -eq "2" ]]; then
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
        compare_stats_value ${stat_name} ${stat_value}
        if [[ "$?" -ne "0" ]]; then
          echo "Failed comparing $stat_name in test_case $test_case: expected value $stat_value"
          cleanup
          exit 1
        fi
        (( count ++ ))
      done
      IFS=$SAVEIFS
      set -e
    fi

    print_bucket_stats
    echo -e "\nMoving on to next step...\n"
    reset_counters
  done

  stop_monitor "$pid" "$LOG"

done


# now compare output
isFirst=0
firstFile=""
returnval=0
for entry in /test_*_sequential.txt
do
  echo "$entry"
  if [[ ${isFirst} == 0 ]]; then
    firstFile="${entry}"
    isFirst=1
  else
    if diff "${firstFile}" "${entry}" >/dev/null ; then
      echo Same
    else
      echo Different
      retrunval=$((returnval+1))
    fi
  fi
done

isFirst=0
for entry in /test_*_random.txt
do
  echo "$entry"
  if [[ ${isFirst} == 0 ]]; then
    firstFile="${entry}"
    isFirst=1
  else
    if diff "${firstFile}" "${entry}" >/dev/null ; then
      echo Same
    else
      echo Different
      retrunval=$((returnval+1))
    fi
  fi
done

rm test_*.txt

printf "returnval=${returnval}\n"
exit $returnval
