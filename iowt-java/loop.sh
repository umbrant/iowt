#!/bin/bash
for i in {1..1000}
do
	java Requester $1 >> loop.out
done
