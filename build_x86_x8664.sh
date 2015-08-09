#!/bin/sh

gcc -O2 -o nmea_agent nmea_agent.c ntpshm.c nmeashm.c daemonize.c
gcc -O2 -o pps_agent pps_agent.c ntpshm.c daemonize.c
gcc -O2 -o pps_edgetest_x86 pps_edgetest_x86.c
#gcc -O2 -o pps_edgetest_rpi12 pps_edgetest_rpi12.c rpi2o.c
#needs libpng and libpng-devel
gcc -O2 -o nmea_plotter -lm -lpng nmea_plotter.c nmeashm.c
gcc -O2 -o nmea_dump nmea_dump.c nmeashm.c
