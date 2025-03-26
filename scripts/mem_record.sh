start=$(($(date +%s%N)/1000000))

#name to measure
name=$1
delay=$2
where=$3
	

while true; do
	pids=(`ps -u | grep $name | awk '{print $2}'`)
	if [ ${#pids[@]} -le 1 ]
	then
		exit
	fi
	# scan as frequent as possible
	# a new scan starts immediately after the previous scan ends
	sum=0
	for pid in "${pids[@]}"
	do
		if [ -e /proc/$pid/smaps_rollup ]
		then
			impact=$((`cat /proc/$pid/smaps_rollup | grep '^Pss:' | awk '{print $2}'`))
			sum=$((impact + sum))
			#echo $pid, $impact, $sum
		fi
	done
	end=$(($(date +%s%N)/1000000))
	take=$(( end - start ))
	echo $take, $sum >> $where
	sleep $delay
done


