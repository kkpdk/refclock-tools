#include <stdio.h> /* Standard input/output definitions */
#include <stdlib.h>
#include <string.h> /* String function definitions */
#include <unistd.h> /* UNIX standard function definitions */
#include <fcntl.h> /* File control definitions */
#include <errno.h> /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <sched.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <getopt.h>

#include "ntpshm.h"

#include <sys/timepps.h>

//adjustment for pps. If pps is delayed 1ms relative to UTC-PPS, the value should be +1000.
int clkadd_ns=100000;
char path[256]="/dev/pps0";
int edge=1;
int shmunit_out=1;
int shmunit_ref=0;





int main(int argc, char *argv[])
{
	int ret, avail_mode;
	pps_params_t params;
	struct timespec timeout;
	pps_info_t infobuf;
	int i;
	struct time_shm sref;
	int ofs_s,ofs_us,ofs_ns, ofs_good=0;
	int clk_s,clk_ns;
	struct time_shm *shm_ref;
	pps_handle_t pps_fd;
	struct time_shm *shm_out;
	struct timespec ppstime;
	int debug=0;


        char c;

	while ((c=getopt(argc,argv,"fo:u:r:p:d"))!=(char)-1) {
//		printf("-%c-\n",c);
		switch (c) {
			case 'f':
				edge=0;
				break;
			case 'd':
				debug=1;
				break;
			case 'o':
				clkadd_ns = atof(optarg)*1000000000.0;
				break;
			case 'u':
				shmunit_out = atoi(optarg);
				break;
			case 'r':
				shmunit_ref = atoi(optarg);
				break;
			case 'p':
				if (strlen(optarg)<sizeof(path))
					strcpy(path,optarg);
				break;
			default:
				printf("usage: %s  -u outputshmunit -r referenceshmunit -p ppsdevice -o offset [-d] [-f]\n",argv[0]);
				printf("real world example:\n" \
				       "%s -u 1 -r 0 -p /dev/pps0 -o 0.000015 -f\n",argv[0]);
				printf("  -u 0..99  the shm the time is exported on. 0..3 can be seen by ntpd.\n"\
				       "  -r 0..99  the shm from which coarse time is taken\n"\
				       "  -p dev    the pps device to open. Created by ldattach 18 <serialport>\n"\
				       "  -o sec    The offset to add to the PPS time. Given in seconds\n"\
				       "  -d        Turns on debug mode and does not daemonize\n"\
				       "  -f        capture on the falling edge, not the rising edge.\n");
				return -1;
		}
	}

	//open the PPS we will be using
	ret = open(path, O_RDWR);
	if (ret<0) {
		fprintf(stderr, "unable to open device \"%s\" (%m)\n", path);
		return ret;
	}

	ret = time_pps_create(ret, &pps_fd);
	if (ret < 0) {
		fprintf(stderr, "cannot create a PPS source from device \"%s\" (%m)\n", path);
		return -1;
	}
	printf("found PPS source \"%s\"\n", path);
	/* Find out what features are supported */
	ret = time_pps_getcap(pps_fd, &avail_mode);
	if (ret < 0) {
		fprintf(stderr, "cannot get capabilities (%m)\n");
		return -1;
	}
	if (edge) { //rising edge
		if ((avail_mode & PPS_CAPTUREASSERT) == 0) {
			fprintf(stderr, "cannot CAPTUREASSERT\n");
			return -1;
		}
		if ((avail_mode & PPS_OFFSETASSERT) == 0) {
			fprintf(stderr, "cannot OFFSETASSERT\n");
			return -1;
		}
	} else { //falling edge
		if ((avail_mode & PPS_CAPTURECLEAR) == 0) {
			fprintf(stderr, "cannot CAPTUREASSERT\n");
			return -1;
		}
		if ((avail_mode & PPS_OFFSETCLEAR) == 0) {
			fprintf(stderr, "cannot OFFSETASSERT\n");
			return -1;
		}
	}

	ret = time_pps_getparams(pps_fd, &params);
	if (ret < 0) {
		fprintf(stderr, "cannot get parameters (%m)\n");
		return -1;
	}

	params.assert_offset.tv_sec = 0;
	params.assert_offset.tv_nsec = 0;
	if (edge) { //rising edge
		params.mode |= PPS_CAPTUREASSERT | PPS_OFFSETASSERT;
	} else { //falling edge
		params.mode |= PPS_CAPTURECLEAR | PPS_OFFSETCLEAR;
	}
	ret = time_pps_setparams(pps_fd, &params);
	if (ret < 0) {
		fprintf(stderr, "cannot set parameters (%m)\n");
		return -1;
	}

	//also open the shm from which we be using for solving integer second ambiguity.
	shm_ref=attach_shm(shmunit_ref);
        if (!shm_ref) {
                fprintf(stderr,"Error attaching to the reference shm\n");
                return 0;
        }

	//and the output shm
	shm_out=attach_shm(shmunit_out);
        if (!shm_out) {
                fprintf(stderr,"Error attaching to the output shm\n");
                return 0;
        }

	for (;;) {
		//get the pps event
		timeout.tv_sec = 13;
		timeout.tv_nsec = 0;
retrypps:
		if (avail_mode & PPS_CANWAIT)
			ret = time_pps_fetch(pps_fd, PPS_TSFMT_TSPEC, &infobuf, &timeout);
		else {
			sleep(1);
			ret = time_pps_fetch(pps_fd, PPS_TSFMT_TSPEC, &infobuf, &timeout);
		}
		if (ret<0) {
			if (ret == -EINTR) {
				fprintf(stderr, "time_pps_fetch() got a signal!\n");
				goto retrypps;
			}
			if (debug) 
				fprintf(stderr, "time_pps_fetch() error %d (%m)\n", ret);
			//return -1;
			goto retrypps;
		}

		if (debug) {
			printf(	"assert %ld.%09ld, sequence: %ld - "
				"clear  %ld.%09ld, sequence: %ld\n",
				infobuf.assert_timestamp.tv_sec,
				infobuf.assert_timestamp.tv_nsec,
				infobuf.assert_sequence,
				infobuf.clear_timestamp.tv_sec,
				infobuf.clear_timestamp.tv_nsec, infobuf.clear_sequence);
			fflush(stdout);
		}

		if (edge) {
			ppstime=infobuf.assert_timestamp;
		} else {
			ppstime=infobuf.clear_timestamp;
		}

		//quickcheck for updated value from the coarse source
		__asm__ __volatile__("": : :"memory");
		if (sref.stamp_count!=shm_ref->stamp_count) {
			//now check if the coarse source is available
			__asm__ __volatile__("": : :"memory");
			i=shm_ref->stamp_count;
			__asm__ __volatile__("": : :"memory");
			if (shm_ref->shm_mode!=1) goto shm_collision;
			__asm__ __volatile__("": : :"memory");
			memcpy(&sref,shm_ref,sizeof(sref));
			__asm__ __volatile__("": : :"memory");
			if (shm_ref->stamp_count!=i) goto shm_collision;
			if (sref.stamp_count!=i) goto shm_collision;
			//check the age. -10 seconds is history, positive is future.
			//If the reference is older than 10s, the pps code will start
			//tracking the integer part itself.
			i=sref.receive_sec - ppstime.tv_sec;
			if ((i>=-15)&&(i<=3)) {
				//it is. Get the offset to the upstream refclock
				ofs_s =sref.clock_sec - sref.receive_sec;
				ofs_us=sref.clock_usec - sref.receive_usec;
				//trim to -1M to 0. This is nice when calculating pps time.
				while (ofs_us<=-1000000) {
					ofs_us+=1000000;
					ofs_s-=1;
				}
				while (ofs_us>0) {
					ofs_us-=1000000;
					ofs_s+=1;
				}
				ofs_ns=ofs_us*1000;
				ofs_good=1;
				if (debug)
					printf("coarse adder: %i s + %i ns\n",ofs_s,ofs_ns);
			}
shm_collision:
			if (ofs_good) {
				//calculate the pps time according to the refclock
				clk_s=ofs_s+ppstime.tv_sec;
				clk_ns=ofs_ns+ppstime.tv_nsec;
				while (clk_ns<-500000000) {
					clk_ns+=1000000000;
					clk_s-=1;
				}
				while (clk_ns>=500000000) {
					clk_ns-=1000000000;
					clk_s+=1;
				}
				//track the pulse so we can coast without reference.
				if (clk_ns>10000000) ofs_ns-=10000000;
				else
				if (clk_ns<-10000000) ofs_ns+=10000000;
				else
					ofs_ns-=clk_ns >>4; //if <10ms, lowpass filter. It looks neat, not really needed.
				//handle overflow/underflow in the integer second tracking
				if (ofs_ns<=-1000000000) {
					ofs_ns+=1000000000;
					ofs_s-=1;
				}
				if (ofs_ns>0) {
					ofs_ns-=1000000000;
					ofs_s+=1;
				}
				//clk_s is the second number. clk_ns is defined as 0 at this point.
				
				//leapsecond code would go here, but is not needed nor wanted:
				//When leapsecond occurs, the system clock is adjusted 1 second.
				//This means that the integer part of the PPS loop ALSO shifts 1 second.
				//So, we must not handle leap seconds, ntpd or such will do that,
				//provided it is running. If it not running, the first update from the coarse
				//reference will correct it.
			
				//add clkadd to compensate for propagation delays
				clk_ns=clkadd_ns;
				//and normalize
				while (clk_ns<0) {
					clk_ns+=1000000000;
					clk_s-=1;
				}
				if (debug)
					printf("rxtime= %i.%09i  clock= %i.%09i\n",ppstime.tv_sec,ppstime.tv_nsec,  clk_s,clk_ns);
				set_shm_ns(shm_out,ppstime.tv_sec,ppstime.tv_nsec,clk_s,clk_ns, 20);
			}
		}
	}
	return 0;
}
