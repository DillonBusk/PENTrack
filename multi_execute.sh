#!/bin/bash
num_runs=$1
config="in/config.in"
output="out/"
echo "Doing $num_runs runs"
echo "Config from $config"
echo "Writing results to $output"
for ((i=0; i<$num_runs; i++))
do
    ./PENTrack $i $config $output $i &
done
wait
echo "All processes finished running"
