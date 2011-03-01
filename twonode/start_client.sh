#!/bin/bash

CLIENT_OUT="benchmark.`date +%s`.`hostname`.out"

# Header info
DATE=`date`
IP=`/sbin/ifconfig eth0 | grep "inet addr" | cut -d " " -f 12 | cut -d ":" -f 2`
echo ${DATE} > $CLIENT_OUT
echo ${IP} >> $CLIENT_OUT

# Start client benchmark
./node benchmark $1 $2 $3 $4 $5 >> $CLIENT_OUT

