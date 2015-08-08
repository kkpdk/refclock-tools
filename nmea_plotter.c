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


#define CTRV 320.0
#define RADV 310.0

#define BASL 838.0
#define SCAL 2.2

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t padding;
} pixel_t;

typedef struct  {
	pixel_t *pixels;
	size_t width;
	size_t height;
} bitmap_t;

pixel_t *pixel_ptr(bitmap_t *bitmap, int x, int y)
{
	return bitmap->pixels + bitmap->width*y + x;
}

int save_png_to_file(bitmap_t *bitmap, const char *path)
{
	FILE *fp;
	png_structp png_ptr=NULL;
	png_infop info_ptr=NULL;
	size_t x,y;
	png_byte **row_pointers=NULL;
	int status=-1;

	int pixel_size=3;
	int depth=8;
	fp=fopen(path, "wb");
	if (!fp)
		goto fopen_failed;

	png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr)
		goto png_create_write_struct_failed;

	info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr)
		goto png_create_info_struct_failed;

	// Set up error handler for png gen
	if (setjmp (png_jmpbuf(png_ptr))) {
		goto png_failure;
	}

	png_set_IHDR(png_ptr, info_ptr, bitmap->width, bitmap->height, depth,
	             PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	             PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	row_pointers=png_malloc(png_ptr, bitmap->height*sizeof(png_byte *));
	for (y=0; y<bitmap->height; ++y) {
		png_byte *row=png_malloc(png_ptr, sizeof(uint8_t)*bitmap->width*pixel_size);
		row_pointers[y] = row;
		pixel_t *pixel = pixel_ptr(bitmap, 0, y);
		for (x=0; x<bitmap->width; ++x) {
			*row++=pixel->red;
			*row++=pixel->green;
			*row++=pixel->blue;
			++pixel;
		}
	}

	png_init_io(png_ptr, fp);
	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
	status = 0;

	for (y=0; y<bitmap->height; y++) 
		png_free(png_ptr, row_pointers[y]);
	png_free (png_ptr, row_pointers);
    
 png_failure:
 png_create_info_struct_failed:
	png_destroy_write_struct(&png_ptr, &info_ptr);
 png_create_write_struct_failed:
	fclose (fp);
 fopen_failed:
	return status;
}

/* Given "value" and "max", the maximum value which we expect "value"
   to take, this returns an integer between 0 and 255 proportional to
   "value" divided by "max". */

int pix(int value, int max)
{
    if (value<0) 
	return 0;
    return (int) (256.0 *((double) (value)/(double) max));
}


char *srcp;
char *cutp(void) 
{
  char *start = srcp;
  while ((*srcp) && (*srcp!=',') && (*srcp!='*')) ++srcp;
  if (*srcp==',') {
    *srcp=0;
    ++srcp;
  } else
  if (*srcp=='*') {
    *srcp=0;
  } else {
    //srcp -> null
  }
  return start;
}



#define RINGSIZE (3600*24)
unsigned ringdata1[RINGSIZE];
unsigned ringdata2[RINGSIZE];

char pngname[1024]="/var/www/html/zzz.png"; //override with -p

//$GPGSV,3,1,11, 03,10,038,31, 05,62,259,44, 06,09,025,26, 07,55,070,43 *74
//$GPGSV,3,2,11, 10,33,166,44, 13,16,096,37, 26,39,277,40, 28,17,158, 38*75
//$GPGSV,3,3,11, 02,07,224,00, 15,02,280,00, 16,02,018,00 *48

int main ( int argc, char *argv[] )
{
	bitmap_t skyplot;
	int x;
	int y;
	int fixnum=0;
//	FILE *fp;
	char sbuf[8192];
	int nlines,nline, prn,elev,azi,snr,r,g,b;
	char *d;
	double ra;
	unsigned ringptr=0;
    
	struct nmea_shm *sh;
	int sh_p=-1;
	struct timeval tv;
	int oldtv,delta;
	int debug=0;
	int shmunit_nmea=0;
	int interval=100;

	char c;

	while ((c=getopt(argc,argv,"i:p:u:d"))!=(char)-1) {
		switch (c) {
			case 'd':
				debug=1;
				break;
			case 'i':
				interval = atoi(optarg);
				break;
			case 'u':
				shmunit_nmea = atoi(optarg);
				break;
			case 'p':
				if (strlen(optarg)<sizeof(pngname))
					strcpy(pngname,optarg);
				break;
			default:
				printf("usage: %s  -u shmunit -p filename.png -i interval [-d]\n",argv[0]);
				return -1;
		}
	}

	sh=attach_nmea_shm(shmunit_nmea);
	if (!sh) {
		fprintf(stderr,"shm failed\n");
		return -1;
	}


	skyplot.width=CTRV*2;
	skyplot.height=/*CTRV*2+200*/BASL+2;

	skyplot.pixels=calloc(sizeof(pixel_t), skyplot.width*skyplot.height);

	for (y=0; y<skyplot.height; y++) {
		for (x=0; x<skyplot.width; x++) {
			pixel_t *pixel=pixel_ptr(&skyplot, x, y);
			pixel->red=pix(x, skyplot.width);
			pixel->green=pix(y, skyplot.height);
			pixel->red=0xff*0;
			pixel->green=0xff*0;
			pixel->blue=0xff*0;
		}
	}

	for (ra=0; ra<(3.14159266*2); ra+=0.001) {
		x=CTRV+RADV*cos(ra);
		y=CTRV+RADV*sin(ra);
		pixel_t * pixel = pixel_ptr (&skyplot, x, y);
		pixel->red=0;
		pixel->green=0;
		pixel->blue=0xff;

		x=CTRV+RADV*cos(ra) * 0.5;
		y=CTRV+RADV*sin(ra) * 0.5;
		pixel = pixel_ptr (&skyplot, x, y);
		pixel->red=0;
		pixel->green=0;
		pixel->blue=0xff;
	}


    //fp=fopen(argv[1],"rt");
//	fp=0;
	oldtv=0;

    /*while (!feof(fp))*/ 
	for (;;) {
		while (nmea_gets(sbuf,sh,&sh_p,8192)<=0) {
			//  printf("sleep %i\n",sh->head);
			gettimeofday(&tv,NULL);
			delta=tv.tv_sec-oldtv;
			if ((oldtv==0)||(delta<0)||(delta>interval)) {
				oldtv=tv.tv_sec;
				save_png_to_file (&skyplot, pngname);
			}
			sleep(1);
		}
		//fgets(sbuf,8192,fp);
		//printf("%s\n",sbuf);
		srcp=sbuf;
		
		//$GPGGA,170551,x,N,x,E,2,09,1.0,32.2,M,41.7,M,,*72
		d=cutp();
		if (strcmp("$GPGGA",d)==0) {
			//$GPGGA,173831,x,N,x,E,2,07,1.2,40.7,M,41.7,M,,*70
			cutp(); //drop time
			cutp(); //lat
			cutp(); //lat
			cutp(); //lon
			cutp(); //lon
			x=atoi(cutp()); //fix
			y=atoi(cutp()); //sats
			///printf("%i %i %i\n",++fixnum,x,y);
			ringdata1[ringptr]=x;
			ringdata2[ringptr]=y;
			++ringptr;
			if (ringptr>=RINGSIZE) ringptr=0;
		} else
		if (strcmp("$GPGSV",d)==0) {
			nlines=atoi(cutp());
			nline=atoi(cutp());
//			    printf("%i %i\n",nlines,nline);
			cutp();
			d=cutp();
			do {
				if (*d) {
					prn=atoi(d);
					elev=atoi(cutp());
					azi=atoi(cutp());
					snr=atoi(cutp());
					//printf("%i %i %i %i \n",prn,elev,azi,snr);
					ra=azi/180.0 * 3.14159265;
					y=CTRV-RADV*cos(ra) * (90.0-elev)/90.0;
					x=CTRV+RADV*sin(ra) * (90.0-elev)/90.0;
			
					r=0;
					g=0;
					b=0;
					if (snr==0) r=0xFF;
					else
					if (snr<20) b=0xFF;
					else
					if (snr>50) r=g=b=0xFF;
					else
						r=g=b=255.0/50.0*snr;

					pixel_t *pixel;
					
					#define SINGLEPIXEL(px,py,pr,pg,pb) \
						pixel= pixel_ptr(&skyplot, px, py); \
						pixel->red=pr; \
						pixel->green=pg; \
						pixel->blue=pb; 
					#define FATPIXEL(px,py,pr,pg,pb) \
						SINGLEPIXEL(px-1,py-1,pr,pg,pb); SINGLEPIXEL(px ,py-1,pr,pg,pb); SINGLEPIXEL(px+1,py-1,pr,pg,pb); \
						SINGLEPIXEL(px-1,py  ,pr,pg,pb); SINGLEPIXEL(px ,py  ,pr,pg,pb); SINGLEPIXEL(px+1,py  ,pr,pg,pb); \
						SINGLEPIXEL(px-1,py+1,pr,pg,pb); SINGLEPIXEL(px ,py+1,pr,pg,pb); SINGLEPIXEL(px+1,py+1,pr,pg,pb);
					
					
					FATPIXEL(x,y,r,g,b);

					if (snr<10) pixel->red=0xFF;

					y=BASL-SCAL*elev;
					x=CTRV+RADV*((azi/180.0)-1.0);
					r=g=b=255.0/50.0*snr;

					if (snr>=50) g=255;
					FATPIXEL(x,y,r,g,b);
				}
				d=cutp();
			} while (*d);
		}
	}
//    fclose(fp);
//    save_png_to_file (&skyplot, argv[2]);
    return 0;
}

