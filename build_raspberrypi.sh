#!/bin/sh

sh build_platformindependent.sh
gcc -O2 -o pps_edgetest_rpi12 pps_edgetest_rpi12.c rpi2o.c
