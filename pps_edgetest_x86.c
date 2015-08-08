/*

  pps-edgetest - a program for measuring interrupt latency when
  using PPS input. For ISA/PCI (16x50) serial ports only.

  Beerware License  2011-2015  Kasper Pedersen  <ntp@kasperkp.dk>

  TO COMPILE:
    gcc -O2 -o edgetest edgetest.c

  ABOUT:
    This program time stamps the PPS input through polling, and
    does it in a way that put hard upper and lower bounds on the
    timing of the event.
    
    This allows one to find the interrupt processing time incurred
    before the PPS timestamp is taken, as well as any interrupt
    generation delays in the hardware.
    On my 5000-series server, the delay is a constant 14us.
*/




#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/io.h>
#include <getopt.h>

unsigned paddr=0x3F8;

#define PBIT 7

#define PAOFFS 6


unsigned char rrr(void)
{
  return  inb(paddr+PAOFFS)&(1<<PBIT);
}


int main(int argc, char *argv[])
{

    struct timeval tv;
    int t1,t3,t5;
    unsigned char b2,b4;
    int i;


    char c;
        
        while ((c=getopt(argc,argv,"p:"))!=(char)-1) {
                switch (c) {
                        case 'p':
                                i=atoi(optarg);
                                paddr=i;
                                if (i==0) paddr=0x3F8;
                                if (i==1) paddr=0x2F8;
                                if (i==2) paddr=0x3E8;
                                if (i==3) paddr=0x2E8;
                                break;
                        default:
                                printf("usage: %s  -p serialportbase\n",argv[0]);
                                printf("This tool is dangerous. -p 0..3 maps to 0x3F8,0x2F8,0x3E8,0x2E8 \n");
                                return -1;
                }
        }



    if (ioperm(paddr, paddr+7, 1)) {
	printf("ioperm() failed - need to be root.\n");
	return -1;
    }
    

    printf("pps-edgetest  2012 Kasper Pedersen\n\n");

    printf("This program does a min/max time stamp of the DCD line.\n");
    printf("It reports a time window inside which the transition is guaranteed\n");
    printf("to have occurred. This is useful for adjusting out interrupt delay\n");
    printf("associated with the serial port and LinuxPPS.\n\n");
    
    printf("Two things to be aware of: First, The program assumes that the port\n");
    printf("is at IOaddr 0x%x. Second, the act of reading the port clears the\n",paddr);
    printf("interrupt status; This will cause lost PPS events.\n\n");

    printf("Intended usage is to let your ntp daemon of choice stabilize, then\n");
    printf("run this program to observe the interrupt latency, and then manually\n");
    printf("tweak the offset in the configuration. Repeat until you are happy.\n");

    t1=rrr();
    gettimeofday(&tv,0);
    t1=tv.tv_usec;
    b2=rrr();
    gettimeofday(&tv,0);
    t3=tv.tv_usec;
    b4=rrr();
    gettimeofday(&tv,0);
    t5=tv.tv_usec;
    do {
	t1=t3;
	b2=b4;
	t3=t5;
	b4=rrr();
	gettimeofday(&tv,0);
	t5=tv.tv_usec;
    } while (!(b4&&!b2));
    if (t1>500000) t1-=1000000;
    if (t3>500000) t3-=1000000;
    if (t5>500000) t5-=1000000;
    printf("Rising edge:\n\
                    ___________________\n\
                   /         /\n\
                  /         /\n\
     ____________/_________/\n\
\n\
                |      |      |\n\
 %7i -------+      |      |\n\
 %7i --------------+      |\n\
 %7i ---------------------+\n\
\n",t1,t3,t5);


    t1=rrr();
    gettimeofday(&tv,0);
    t1=tv.tv_usec;
    b2=rrr();
    gettimeofday(&tv,0);
    t3=tv.tv_usec;
    b4=rrr();
    gettimeofday(&tv,0);
    t5=tv.tv_usec;
    do {
	t1=t3;
	b2=b4;
	t3=t5;
	b4=rrr();
	gettimeofday(&tv,0);
	t5=tv.tv_usec;
    } while (!(b2&&!b4));
    if (t1>500000) t1-=1000000;
    if (t3>500000) t3-=1000000;
    if (t5>500000) t5-=1000000;
    printf("Falling edge:\n\
     ______________________\n\
                 \\         \\ \n\
                  \\         \\\n\
                   \\_________\\_________\n\
\n\
                |      |      |\n\
 %7i -------+      |      |\n\
 %7i --------------+      |\n\
 %7i ---------------------+\n\
",t1,t3,t5);

}
