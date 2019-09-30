#!/bin/bash

if [[ $# != 4 ]]; then
  echo "Usage: $(basename $0) /path/to/guess/dir /path/to/pin/dir /path/to/pintool /path/to/test/binaries"
  exit
fi

CURR_DIR=$(pwd)
CMD_FILE=$CURR_DIR/cmd.txt
echo "$0 $1 $2 $3 $4" >$CMD_FILE
echo "Starting at $(date)" >>$CMD_FILE

IGNORE_PATH=$(realpath $(dirname $PINTOOL)/../../../tests/ignored.txt)
DATA_DIR=$(realpath $1)
PIN=$(realpath $2)
PINTOOL=$(realpath $3)
TESTS=$(realpath $4)
ID_SCRIPT=$(realpath $(dirname $PINTOOL)/../../../src/fosbin-sleuth/python/IdentifyFunction.py)
echo $CURR_DIR

for test in $(find $TESTS -type f -executable); do
  if [ -d "$(basename $test)" ]; then
    echo "Skipping $test"
    continue
  fi
  mkdir $(basename $test)
  cd $(basename $test)
  echo "Evaluating $test"
  echo "Starting $test: $(date)" >>"$CMD_FILE"
  cmd="$ID_SCRIPT -t $DATA_DIR/tree.bin -pindir $PIN -tool $PINTOOL -b $test -ignore $IGNORE_PATH"
  echo "CMD: $cmd" >>"$CMD_FILE"
  $cmd
  cd $CURR_DIR

done

echo "Ended at $(date)" >>"$CMD_FILE"
