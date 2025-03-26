#!/bin/bash

declare base_dir="$(dirname $(dirname $(realpath $0)))"

cd ${base_dir}/build
set -e
#cmake -DSKETCH_BUFFER_SIZE=25 ..
#make -j
#set +e

mkdir -p ./../results
mkdir -p ./../results/mpi_speed_results

run_test() {
	cat	binary_streams/$1 > /dev/null
	mpirun -np $2 --bind-to hwthread ./mpi_dynamicCC_tests binary_streams/$1 0 $3 --gtest_filter=*mpi_mixed_speed_test*
}

declare -a streams=(
[0]="kron_13_query10_binary"
[1]="kron_15_query10_binary"
[2]="kron_16_query10_binary"
[3]="kron_17_query10_binary"
[4]="kron_18_query10_binary"
#
[5]="dnc_query10_binary"
[6]="tech_query10_binary"
[7]="enron_query10_binary"
#
[8]="twitter_query10_binary"
[9]="stanford_query10_binary"
[10]="random2N_query10_binary"
[11]="randomNLOGN_query10_binary"
[12]="randomNSQRTN_query10_binary"
[13]="randomDIV_query10_binary"
# Fixed Forest
[14]="kron_13_ff_query10_binary"
[15]="kron_15_ff_query10_binary"
[16]="kron_16_ff_query10_binary"
[17]="kron_17_ff_query10_binary"
[18]="kron_18_ff_query10_binary"
#
[19]="dnc_ff_query10_binary"
[20]="tech_ff_query10_binary"
[21]="enron_ff_query10_binary"
#
[22]="twitter_ff_query10_binary"
[23]="stanford_ff_query10_binary"
[24]="random2N_ff_query10_binary"
[25]="randomNLOGN_ff_query10_binary"
[26]="randomNSQRTN_ff_query10_binary"
[27]="randomDIV_ff_query10_binary"
)

declare -a nps=(
[0]=23
[1]=26
[2]=28
[3]=30
[4]=31
#
[5]=19
[6]=26
[7]=29
#
[8]=28
[9]=31
[10]=32
[11]=29
[12]=25
[13]=29
# Fixed Forest
[14]=23
[15]=26
[16]=28
[17]=30
[18]=31
#
[19]=19
[20]=26
[21]=29
#
[22]=28
[23]=31
[24]=32
[25]=29
[26]=25
[27]=29
)


run_test ${streams[$1]} ${nps[$1]} $2

# Test run
# mpirun -np 23 --bind-to hwthread ./mpi_dynamicCC_tests binary_streams/kron_13_query10_binary 0 0 --gtest_filter=*mpi_mixed_speed_test*


exit
# Tests including memory measurement
run_mem_test() {
	mpirun -np $1 --bind-to hwthread ./mpi_dynamicCC_tests binary_streams/$2 0 0 --gtest_filter=*mpi_mixed_speed_test* &
	./../scripts/mem_record.sh mpi_dynamicCC_tests 2 ./../results/mpi_space_results/batch_size_sweep/$2_$3_mem.txt
	wait
}
	
