#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "ntpshm.h"

struct time_shm *attach_shm(int unit)
{
	int shm_id;
	struct time_shm *sh;
	shm_id=shmget(0x4e545030+unit, sizeof(struct time_shm), IPC_CREAT|0700);
	if(shm_id==-1) {
		fprintf(stderr,"shmget failed.\n");
		return 0;
	}
	sh=(struct time_shm *) shmat(shm_id, 0, 0);
	if((sh==(void *) -1) || (sh==0)) {
		fprintf(stderr,"shmat failed.\n");
		return 0;
	}
	return sh;
}

void set_shm_ns(struct time_shm *sh, unsigned r_sec, unsigned r_nsec, unsigned c_sec, unsigned c_nsec, unsigned prec)
{
	sh->shm_mode=1;
	sh->leap_indicator=0;
	sh->is_valid=0;
	__asm__ __volatile__("": : :"memory");
	sh->stamp_count++;
	__asm__ __volatile__("": : :"memory");
	sh->precision=prec;
	sh->clock_sec=c_sec;
	sh->clock_usec=c_nsec/1000;
	sh->clock_nsec=c_nsec;
	sh->receive_sec=r_sec;
	sh->receive_usec=r_nsec/1000;
	sh->receive_nsec=r_nsec;
	__asm__ __volatile__("": : :"memory");
	sh->stamp_count++;
	__asm__ __volatile__("": : :"memory");
	sh->is_valid=1;
}

