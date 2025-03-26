#!/bin/bash

#declare base_dir="$(dirname $(dirname $(realpath $0)))"

#cd ${base_dir}/results/mpi_speed_results


write_out() {
	filename=$1.txt
	if [ -f $filename ]; then
		awk -F ' ' 'BEGIN {ORS=","}NR==1{print $2}' $filename >> $2
		awk -F ' ' 'BEGIN {ORS=","}NR==2{print $2}' $filename >> $3

	else
		echo -n "0," >> $2
		echo -n "0," >> $3
	fi
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

updates="UPDATES.txt"
queries="QUERIES.txt"
rm $updates
rm $queries

for i in $(seq 0 13);
do
	write_out ${streams[$i]} $updates $queries
done

echo "" >> $updates
echo "" >> $queries

updates="UPDATES_FF.txt"
queries="QUERIES_FF.txt"
rm $updates
rm $queries

for i in $(seq 14 27);
do
	write_out ${streams[$i]} $updates $queries
done

echo "" >> $updates
echo "" >> $queries

