#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
    
#include "nmeashm.h"
#include "sys/time.h"
#include <getopt.h>



int main ( int argc, char *argv[] )
{
	char sbuf[8192];
    	struct nmea_shm *sh;
	int debug=0;
	int shmunit_nmea=0;
	int sh_p=-1;

	char c;

	while ((c=getopt(argc,argv,"u:d"))!=(char)-1) {
		switch (c) {
			case 'd':
				debug=1;
				break;
			case 'u':
				shmunit_nmea = atoi(optarg);
				break;
			default:
				printf("usage: %s  -u shmunit \n",argv[0]);
				return -1;
		}
	}

	sh=attach_nmea_shm(shmunit_nmea);
	if (!sh) {
		fprintf(stderr,"shm failed\n");
		return -1;
	}

	for (;;) {
		while (nmea_gets(sbuf,sh,&sh_p,8192)<=0) {
			sleep(1);
		}
		//fgets(sbuf,8192,fp);
		printf("%s\n",sbuf);
	}
}

