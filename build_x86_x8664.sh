#!/bin/sh

sh build_platformindependent.sh
gcc -O2 -o pps_edgetest_x86 pps_edgetest_x86.c
