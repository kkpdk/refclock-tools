/*

  This is a shared memory buffer that exports the NMEA data from the
  receiver. Doing so allows other programs to peek at the same
  data stream, and show things like current number of satellites.

*/



#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "nmeashm.h"

#define NMEASHMBASE 0x6d47b5f8

struct nmea_shm *attach_nmea_shm(int unit)
{
	int shm_id;
	struct nmea_shm *sh;
	shm_id=shmget(NMEASHMBASE+unit, sizeof(struct nmea_shm), IPC_CREAT|0700);
	if(shm_id==-1) {
		fprintf(stderr,"shmget failed.\n");
		return 0;
	}
	sh=(struct nmea_shm *) shmat(shm_id, 0, 0);
	if((sh==(void *) -1) || (sh==0)) {
		fprintf(stderr,"shmat failed.\n");
		return 0;
	}
	return sh;
}

void add_nmea_shm_string(struct nmea_shm *sh, unsigned char *s)
{
	unsigned p;
	unsigned c;

	p=sh->head;
	if (p>=NMEASHMBUFSIZE) p=0;
	if (sh->ringsize!=NMEASHMBUFSIZE) {
		memset(sh->buf, 0, NMEASHMBUFSIZE);
		p=0;
		__asm__ __volatile__("": : :"memory");
		sh->ringsize=NMEASHMBUFSIZE;
	}
	do {
		c=*s++;
		sh->buf[p]=c;
		if (++p>=NMEASHMBUFSIZE) p=0;
	} while (c);
	__asm__ __volatile__("": : :"memory");
	sh->head=p;
}

int nmea_gets(char *s, struct nmea_shm *sh, int *optr, int maxtot)
{
	int h,t,rs,c;
	char *sbuf;

	
	h=sh->head;
	rs=sh->ringsize;
	if (rs<=2) return -1;
	if (rs>NMEASHMBUFSIZE) return -1;
	if ((h<0)||(h>=rs)) return -1;

	//backstep p 1

	t=h-1;
	if (t<0) t+=rs;
	sbuf=sh->buf;
	
	if (*optr<0) {
		if (!sbuf[t]) //the last message in the buffer is terminated.
			*optr=h;
		else
			return -2;
	}

	s[0]=0;
	if (!sbuf[t]) { //the last message is terminated
		t=*optr;
		if (h==t) return 0;
		do {
			c=sbuf[t];
			if (++t>=rs) t=0;
			if (maxtot) {
				--maxtot;
				*s++=c;
				if (!c) {
					*optr=t;
					return 1;
				}
			}
		} while (h!=t);
		*optr=t;
	}
	return 0;
}
