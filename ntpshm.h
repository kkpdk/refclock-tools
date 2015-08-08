#ifndef H_ntpshm_included
#define H_ntpshm_included

struct time_shm
{
	int shm_mode;
	int stamp_count;
	time_t clock_sec;
	int clock_usec;
	time_t receive_sec;
	int receive_usec;
	int leap_indicator;
	int precision;
	int number_of_samples;
	int is_valid;
	unsigned clock_nsec;
	unsigned receive_nsec;
	int dummy[8];
};

struct time_shm *attach_shm(int unit);
void set_shm_ns(struct time_shm *sh, unsigned r_sec, unsigned r_nsec, unsigned c_sec, unsigned c_nsec, unsigned prec);

#endif