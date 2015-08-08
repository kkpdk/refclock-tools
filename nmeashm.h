#ifndef H_nmeashm_included
#define H_nmeashm_included

#define NMEASHMBUFSIZE 16000

struct nmea_shm
{
	int ringsize;
	int head;
	float debug_skew;
	int debug_bantimer;
	int res1,res2 ,res3 ,res4 ,res5 ,res6 ,res7 ,res8;
	int res9,res10,res11,res12,res13,res14,res15,res16;
	
	unsigned char buf[NMEASHMBUFSIZE];
};

struct nmea_shm *attach_nmea_shm(int unit);
void add_nmea_shm_string(struct nmea_shm *sh, unsigned char *s);
int nmea_gets(char *s, struct nmea_shm *sh, int *optr, int maxtot);

#endif