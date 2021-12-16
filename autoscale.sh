#!/bin/bash

MODE=$1
MINCPUS=$2
MAXCPUS=$3

for ((i = $MINCPUS; i <= $MAXCPUS; ++i))
do
	make $MODE NCPUS=$i
done