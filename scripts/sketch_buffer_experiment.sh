#!/bin/bash

declare base_dir="$(dirname $(dirname $(realpath $0)))"
echo "Testing with buffer size $1"

cd ${base_dir}/build
#set -e
#cmake -DSKETCH_BUFFER_SIZE=$1 ..
#make -j
#set +e

mkdir -p ./../results
mkdir -p ./../results/mpi_speed_results

run_test() {
	cat	binary_streams/$1 > /dev/null
	mpirun -np $2 --bind-to hwthread ./mpi_dynamicCC_tests binary_streams/$1 0 0 --gtest_filter=*mpi_mixed_speed_test*
}

run_test kron_16_query10_binary 28
run_test kron_16_ff_query10_binary 28
run_test twitter_query10_binary 28
run_test twitter_ff_query10_binary 28

