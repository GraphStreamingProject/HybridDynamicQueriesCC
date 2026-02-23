#!/bin/bash

if [ $# -lt 2 ]; then
    echo "Usage: $0 <stream_path> <num_repeats>"
    exit 1
fi

stream="$1"
repeats="$2"
base_dir="$(dirname "$(dirname "$(realpath "$0")")")"

for run in $(seq 1 "$repeats"); do
    value=$(mpirun -x LD_PRELOAD=/usr/lib/libmimalloc.so -np 23 --oversubscribe \
        "${base_dir}/build/mpi_dynamicCC_tests" \
        "$stream" 100 0 0 \
        --gtest_filter="*memory*" 2>&1 \
        | grep "Maximal tier across stream samples:" \
        | grep -oP '\-?[0-9]+$')
    echo "Run ${run}: ${value}"
done
