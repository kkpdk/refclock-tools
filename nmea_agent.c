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
//#include <math.h>
#include <getopt.h>

#include "ntpshm.h"
#include "nmeashm.h"





char portname[256]="/dev/ttyS0";
unsigned baudrate=9600;
int clkadd_us=114000;
unsigned shmunit=0;
unsigned microsec_per_byte;


struct s_tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;//0 - 11
	int tm_year;/*since 1900*/
	int tm_wday;
	int tm_yday;
};

unsigned long RtctoUnix(struct s_tm *tb)
{
	unsigned long u, year, mon;
	year = tb->tm_year; // 70..299
	mon = tb->tm_mon;   //0..11
	/* compute elapsed days minus one, to the given month, including february leap day*/
	u=(mon<<5)-(mon<<1)+((mon+1+(mon>>3))>>1)-1;
	if (mon>1) {
		u-=2;
		if (!(year&3)) u+=1;
	}
	/* compute elapsed days since base until year */
	u+=(year-70)*365+((year-69)>>2);
	/* and date in month */
	u+=tb->tm_mday; /* 1..31 */
	/* this is total days. convert to and add hours, min, sec*/
	u=u*24+tb->tm_hour;
	u=u*60+tb->tm_min;
	u=u*60+tb->tm_sec;
	return u;
}

int openport(void)
{
    int fd=open(portname, O_RDWR|O_NOCTTY|O_NDELAY);
    if (fd==-1) 
        fprintf(stderr,"unable to open port\n");
    else
        fcntl(fd, F_SETFL, 0);

    return fd;
}

int initport(int fd)
{
	speed_t spd;
	int portstatus = 0;
	struct termios options;

	microsec_per_byte=10000000/baudrate; //10 bit frame, 1Musec/sec, so 1e7/baudrate

	if (baudrate==115200) spd=B115200;
	else
	if (baudrate==57600) spd=B57600;
	else
	if (baudrate==38400) spd=B38400;
	else
	if (baudrate==19200) spd=B19200;
	else
	if (baudrate==9600) spd=B9600;
	else
	if (baudrate==4800) spd=B4800;
	else
	spd=baudrate;

	tcgetattr(fd, &options);
	options.c_iflag = IGNBRK | IGNPAR;
	options.c_oflag = 0;
	options.c_cflag = CS8 | CLOCAL | CREAD;
	options.c_lflag = 0;
	options.c_cc[VMIN] = 1;     //min to read
	options.c_cc[VTIME] = 0;    //time to wait
	cfsetispeed(&options, spd);
	cfsetospeed(&options, spd);
	if (tcsetattr(fd, TCSANOW, &options)==-1) 
		return 0;

	tcflush(fd, TCIFLUSH);
	return 1;
}


unsigned char rxbuf[1024];
unsigned rxinp,rxoutp;
int serial_fd;
struct timeval msgstarttime;


int rx_serial(void)
{
	int rv;
	int maxfd;
	int msgstartadjust;
	if (rxinp==rxoutp) {
		struct timeval tv;
		fd_set rfds;
		int i,go;

		FD_ZERO(&rfds);
		FD_SET(serial_fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		maxfd=serial_fd;
		i=select(maxfd+1,&rfds,NULL,NULL,&tv);
		if (FD_ISSET(serial_fd,&rfds)) {
			go=sizeof(rxbuf)-rxinp;
			if (go==sizeof(rxbuf)) --go;
			i=read(serial_fd,rxbuf+rxinp,go);
			rxinp+=i;
			if (rxinp==sizeof(rxbuf)) rxinp=0;
		}
	}

	if (rxinp!=rxoutp) {
		rv=rxbuf[rxoutp++];
		if (rxoutp==sizeof(rxbuf)) rxoutp=0;
		if (rv=='$') {
			gettimeofday(&msgstarttime,NULL);
			msgstartadjust=rxinp-rxoutp; //what has arrived after
			if (msgstartadjust<0) msgstartadjust+=sizeof(rxbuf);
			msgstartadjust+=1; //the $

			msgstartadjust*=microsec_per_byte;
			if (msgstartadjust>msgstarttime.tv_usec) {
				msgstarttime.tv_usec+=1000000;
				msgstarttime.tv_sec-=1;
			}
			msgstarttime.tv_usec-=msgstartadjust;
		}

		return rv;
	} else {
		return -1;
	}
}

unsigned char msg[256];
unsigned msglen;
unsigned msgp;

unsigned rd_int(void)
{
	unsigned h=0;
	unsigned v;
	while (msgp<msglen) {
		v=msg[msgp];
		if ((v<'0')||(v>'9')) return h;
		++msgp;
		h=h*10+v-'0';
		}
	return h;
}

unsigned rd_intus(void)
{
	unsigned h=0;
	unsigned v;
	int digits=0;
	while (msgp<msglen) {
		v=msg[msgp];
		if ((v<'0')||(v>'9')) break;
		++msgp;
		h=h*10+v-'0';
		++digits;
	}
	while (digits<6) {
		digits++;
		h*=10;
	}
	return h;
}

void rd_eat(unsigned char c)
{
	unsigned v;
	while (msgp<msglen) {
	v=msg[msgp++];
	if (v==c) break;
	}
}



int main(int argc, char *argv[])
{

	struct timeval tmp_tv;
	struct sched_param sp;
	fd_set rfds;
	int i;
	float err;

	unsigned gga_hms;
	struct timeval gga_tv;
	unsigned rmc_hms,rmc_date;
	struct timeval rmc_tv;
	int rmc_us, gga_us;

	struct timeval ban_tv;
	int ban_timer=0;
	unsigned last_reported_time;
	int last_reported_usec;
	
	int startupblanker;

	struct time_shm *shm_out;
	struct nmea_shm *shm_nmea;
	float  avgsum=0.0; //for easy setup
	int    averages=0;   //for easy setup

	char c;
	int debug=0;
	
	while ((c=getopt(argc,argv,"o:u:b:p:d"))!=(char)-1) {
		switch (c) {
			case 'd':
				++debug;
				break;
			case 'o':
				clkadd_us = atof(optarg)*1000000.0;
				break;
			case 'u':
				shmunit = atoi(optarg);
				break;
			case 'b':
				baudrate = atoi(optarg);
				break;
			case 'p':
				if (strlen(optarg)<sizeof(portname))
					strcpy(portname,optarg);
				break;
			default:
				printf("usage: %s -u shmunit -p serialport -b baudrate -o offset\n",argv[0]);
				printf("real world example:\n" \
				       " %s -u 0 -p /dev/ttyS0 -b 9600 -o 0.125\n",argv[0]);
				printf("  -u 0..99   this sets the shm the time is exported on. 0..3 can be seen by ntpd.\n" \
				       "  -p port    sets the serial device to open\n"\
				       "  -b baud    sets the baud rate. 4800,9600,38400,115200 are common\n"\
				       "  -o offset  sets the clock adjustment, given in seconds.\n"\
				       "  -d         enabled debug output. Once enables offset measurement,\n"\
				       "             twice turns everything on.\n");
				return -1;
		}
	}

	serial_fd=openport();
	if(serial_fd==-1) {
		fprintf(stderr,"Error opening serial port \n");
		return -1;
	}

	if (!initport(serial_fd))
	{
		fprintf(stderr,"Error Initializing serial port\n");
		close(serial_fd);
		return 0;
	}

	mlockall(MCL_FUTURE);
	errno = 0;
	setpriority(PRIO_PROCESS, getpid(), -20);
	if (errno) {
		fprintf(stderr,"setpriority failed.\n");
	}
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority=sched_get_priority_max(SCHED_FIFO);
	if(sched_setscheduler(0, SCHED_FIFO, &sp)!=0) {
		fprintf(stderr,"unable to set RR scheduling.\n");
	}

	unsigned f = TIOCM_DTR;
	ioctl(serial_fd, TIOCMBIS, &f);
	msglen=0;

	shm_out=attach_shm(0);
	if (!shm_out) {
		fprintf(stderr,"Error attaching to the ntp shm\n");
		close(serial_fd);
		return 0;
	}

	shm_nmea=attach_nmea_shm(0);
	if (!shm_nmea) {
		fprintf(stderr,"Error attaching to the nmea shm\n");
		close(serial_fd);
		return 0;
	}

	last_reported_time=0;
	last_reported_usec=0;

	gettimeofday(&ban_tv,NULL);
	ban_timer=0;
	gga_tv.tv_sec=0;
	//startupblanker makes it so we always receive either
	//a GPGGA and then a GPRMC, or two GPRMCs before we
	//use the time from it. If the receiver sends both
	//GPGGA and GPRMC, and we start in the middle of a
	//message and hear the GPRMC, we cannot use that
	//message since we do not have the start-of-message
	//time.
	startupblanker=1;

	for (;;) {
restart_loop:
		i=rx_serial();
		if (i!=-1) {
			if (i=='$') msglen=0;
			if (msglen<sizeof(msg)) {
				if ((i==13)||(i==10)) i=0;
				msg[msglen++]=i;
				if (!i) {
					if (msglen>5) {
						if (debug>1)
							printf("%s\n",msg);
						add_nmea_shm_string(shm_nmea, msg);
					}
					msgp=7;
					if (!memcmp(msg,"$GPGGA,",7)) {
						gga_tv=msgstarttime;
						gga_hms=rd_int();
						gga_us=0;
						if (msg[msgp]=='.') {
							++msgp;
							gga_us=rd_intus();
						}
						if (debug>1) printf("gga %u.%06i   %u.%06i\n",gga_hms,gga_us, gga_tv.tv_sec, gga_tv.tv_usec);
						startupblanker=0;
					}
					if (!memcmp(msg,"$GPRMC,",7)) {
						rmc_tv=msgstarttime;
						rmc_hms=rd_int();
						rmc_us=0;
						if (msg[msgp]=='.') {
							++msgp;
							rmc_us=rd_intus();
						}
						rd_eat(',');
						rd_eat(',');
						rd_eat(',');
						rd_eat(',');
						rd_eat(',');
						rd_eat(',');
						rd_eat(',');
						rd_eat(',');
						rmc_date=rd_int();
						if (debug>1) printf("rmc %u.%06i  %u\n",rmc_hms,rmc_us,rmc_date);
						//calculate the delta from gga to rmc, according to the report
						i=(rmc_hms-gga_hms)*1000000 +rmc_us-gga_us;
						//if this time is zero, the timestamp to use is the one that came first
						if (!i) {
						i=rmc_tv.tv_sec - gga_tv.tv_sec;
						if (!i) i=rmc_tv.tv_usec - gga_tv.tv_usec;
						if (i>0) {
							//printf("rmc after gga\n");
							//rmc comes after gga, but they the same reported time. gga is the start of the message block.
							rmc_tv=gga_tv;
							}
						}
						if (debug>1) printf("ts %i.%06i\n",rmc_tv.tv_sec,rmc_tv.tv_usec);


						//decompose into hours, minutes...
						struct s_tm tmp;
						i=rmc_hms;
						tmp.tm_sec=i%100; i/=100;
						tmp.tm_min=i%100; i/=100;
						tmp.tm_hour=i%100;
						i=rmc_date;
						tmp.tm_year=100+(i%100); i/=100;
						tmp.tm_mon=(i%100)-1; i/=100;
						tmp.tm_mday=i%100; i/=100;
	        
						if (debug>1)
							printf("bantimer %i\n",ban_timer);
						shm_nmea->debug_bantimer=ban_timer;
							
						//decrement the ban timer
						if (ban_timer) {
							gettimeofday(&tmp_tv,NULL);
							i=tmp_tv.tv_sec-ban_tv.tv_sec;
							ban_tv=tmp_tv;
							if ((i>0) && (i<19)) { //time has advanced at least a second, and not stepped far ahead.
								ban_timer-=i;
								if (ban_timer<0) ban_timer=0;
							}
						}

						//validate input - allow 61 seconds in a minute
						if ((tmp.tm_sec>60)||(tmp.tm_min>59)||(tmp.tm_hour>23)||
						    (tmp.tm_mon>11)||(tmp.tm_mday>31)||(tmp.tm_mday==0)) {
							//the receiver reported an impossible value. Ban it for (another) 30 seconds.
							if (ban_timer<30) {
								gettimeofday(&ban_tv,NULL);
								ban_timer=30;
							}
							//complain about input, then start over
							if (debug>1)
								if (debug)fprintf(stderr,"ERROR: unparseable time format\n");
							add_nmea_shm_string(shm_nmea, "$PPPPE,unparseable time format");
							goto restart_loop;
						}

						//if the time is far in the past, the receiver is not
						//yet receiving signal, likely does not have a valid
						//almanac, and very likely not ephemeris.
						//Unless the test further down in the code decides
						//that the NMEA timestamp matches system time,
						//we should probably not use the time. The almanac
						//contains the leap second count, and the receiver
						//is likely outputting bad time. Wait long enough for
						//an almanac update. Do note that for receivers with
						//built-in batteries and real time clocks, this code
						//will only be triggered if you remove the battery,
						//or possibly a cold reset.
						if ((tmp.tm_year<115)||((tmp.tm_year==115)&&(tmp.tm_mon<7))) { //time is before this was compiled
							//Ban if for long enough that is has time to get correct leapsecond data.
							if (ban_timer<900) {
								gettimeofday(&ban_tv,NULL);
								ban_timer=900;
							}
							if (debug>1)
								fprintf(stderr,"ERROR: time is in the past\n");
							add_nmea_shm_string(shm_nmea, "$PPPPE,time is in the past");
							goto restart_loop;
						}

						//now the time is safe to convert
						i=RtctoUnix(&tmp);
						err=(i-rmc_tv.tv_sec) + 0.000001*((rmc_us+clkadd_us)-rmc_tv.tv_usec);
						shm_nmea->debug_skew=err;

						//if the reported time is close to the system time, it is likely sound,
						//and we can use the value.
						if (ban_timer) {
							if ((err>-0.5)&&(err<0.5))
							ban_timer=0;
						}

						//Some receivers step backwards one minute for one second
						//when the leap occurs. If that happens, ban for 3 seconds.
						//When the receiver recovers, even if the 3 seconds has not
						//elapsed, the code above will reset the ban.
						if (i<last_reported_time) { //backstep to previous integer second
							if (debug>1)
								printf("ERROR: time stepped backwards\n");
							add_nmea_shm_string(shm_nmea, "$PPPPE,time stepped backwards");
							if (ban_timer<3) {
								gettimeofday(&ban_tv,NULL);
								ban_timer=3;
							}
						}
						//generate the time we report. Add in the trim.
						last_reported_time=i;
						last_reported_usec=rmc_us+clkadd_us;
						while (last_reported_usec<0) {
							last_reported_usec+=1000000;
							last_reported_time-=1;
						}
						while (last_reported_usec>=1000000) {
							last_reported_usec-=1000000;
							last_reported_time+=1;
						}
						//if the receiver is not banned, report
						if ((!ban_timer)&&(!startupblanker)) {
							set_shm_ns( shm_out, rmc_tv.tv_sec, rmc_tv.tv_usec*1000,  last_reported_time, last_reported_usec*1000,   2);
							if (debug>1)
								printf("$PPPPP,%i,%i,%i,%f\n",i,rmc_tv.tv_sec, i-rmc_tv.tv_sec,err);
								
							if (debug>0) {
								if (averages<86400) ++averages; //tau cannot become more than a day.
								avgsum-=avgsum/averages;
								avgsum+=err/averages;
								printf("averaged %i samples, error is %f,  -o %f\n",averages,err,0.000001*clkadd_us-avgsum);
							}
								
								

						}
						startupblanker=0;
					}
					msglen=sizeof(msg); //stop receiving until another $
				} //(!i)  we received a terminator character
			} //the message was at least 7 characters
		} //any characters received
	} //forever
	return 0;
}


