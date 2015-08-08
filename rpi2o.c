#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "rpi2o.h"


bcm2835_st bcm2835;

int boardrev_i(void);

int boardrev(void)
{
  static int rev=0;
  if (!rev) rev=boardrev_i();
  return rev;
}

int boardrev_i(void) 
{
  FILE *f;
  int n;
  char buf[512], *bufp;

  if (f= fopen("/proc/cpuinfo","r")) {
    n=fread(buf,1,sizeof(buf)-1,f);
    fclose(f);
  } else
    return -1;
  buf[n]=0;
  if (n<11) return -1;
  bufp=buf;
  while (bufp[11]) {
    if (!memcmp(bufp,"Revision\t: ",11)) {
      bufp+=11;
      n=0;
      while (*bufp>' ') buf[n++]=*bufp++;
      buf[n]=0;
      sscanf(buf,"%x",&n);
      return n&0xFFFFFF; //mask overclock bit
    }
    ++bufp;
  }
  return -1;
}


//#define CM_GP0CTL bcm2835_clk[0x70/4]
//#define CM_GP0DIV bcm2835_clk[0x74/4]

void shutdownclock0(void)
{
  unsigned n;
  dmb();
  n=bcm2835.clk->ctl0;
  n&=0x70F;
  n|=0x5a000000;
  bcm2835.clk->ctl0=n;
  dmb();
  while (bcm2835.clk->ctl0&0x80); // Wait for busy flag to turn off.
  dmb();
}

void setupclock0_integer(int frq)
{
  unsigned di=(500000000+(frq/2)) / frq; //closest frequency.
  unsigned n;

  shutdownclock0();
  gpio_fsel(4, GPIO_FSEL_ALT0);
  dmb();
  bcm2835.clk->div0=0x5a000000|(di<<12); // Configure divider.
  n=0x5a000006; // Source=PLLD (500 MHz), integer divider
  bcm2835.clk->ctl0=n;
  bcm2835.clk->ctl0=n|0x10; // Enable clock.
  dmb();
  while(!(bcm2835.clk->ctl0&0x80)); // Wait for busy flag to turn on.
  dmb();
}

uint64_t usec64(void) //64 bit microseconds
{
  volatile unsigned h1,h2,lv;
  dmb();
  h1 = bcm2835.st->chi;
  lv = bcm2835.st->clo;
  h2 = bcm2835.st->chi;
  dmb();
  if (h1!=h2) lv=0;
  return (((uint64_t)h2)<<32)|lv;
}

void realtime(void)
{
  struct sched_param sp;
  memset(&sp, 0, sizeof(sp));
  sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
  sched_setscheduler(0, SCHED_FIFO, &sp);
  mlockall(MCL_CURRENT | MCL_FUTURE);
}


void bdelay(unsigned n)
{
  dsb(); //force writes to have retired before the delay timer starts
  while (n) {
    asm volatile ("nop\n");
    --n;
  }
  dmb();
}

unsigned tunebdelay(void)
{
  unsigned t0,t1,dt,n,cts;
  cts=1;
  for (n=0; n<50; ++n) {
    dsb();
    t0=bcm2835.st->clo;
    bdelay(cts);
    t1=bcm2835.st->clo;
    dmb();
    dt=t1-t0;
    if (dt<(BDELAYTUNELENGTH/2)) {
      cts<<=1;
      n=0;
    } else 
    if (dt<BDELAYTUNELENGTH) {
      cts = (cts*BDELAYTUNELENGTH+(dt/2))/dt; //will up the delay at least 1/BDELAYTUNELENGTH, or 4% at 25us.
      n=0;
    }
  }
  return cts;
}

unsigned calcbdelay(unsigned ns) //calculate load count for bdelay function.
{
  static unsigned tuned=0;
  if (!tuned) tuned=tunebdelay();
  return (tuned*((uint64_t)ns)+(500*BDELAYTUNELENGTH))/(1000*BDELAYTUNELENGTH);
}


void usdelay(unsigned n)
{
  unsigned t0,t1,dt;
  dmb();
  t0=bcm2835.st->clo;
  dsb();
#ifdef OSSLEEP_THRESHOLD
  if (n>OSSLEEP_THRESHOLD) usleep(n-OSSLEEP_THRESHOLD);
#endif
  do {
    t1=bcm2835.st->clo;
    dmb();
    dt=t1-t0;
  } while (dt<n);
}


void gpio_fsel(int pin, int fn)
{
  volatile unsigned *p = & bcm2835.gpio->fsel[pin/10];
  int shift=(pin%10)*3;
  unsigned n;
  n=*p;
  dmb(); //after read, before write
  *p=(n&~(7U<<shift))|(fn<<shift);
}

void gpio_pud(int pin, unsigned pupmode) //0=none, 1=down, 2=up
{
  dmb(); //after read, before write
  bcm2835.gpio->pud=pupmode; //WE HOPE we do not get clobbered by interrupt here.
  bcm2835.gpio->pudclk[pin>>5]=1<<(pin&31);
}

//default is (8,1,1)
void gpio_set_padgroup(int group, unsigned mA, int fastslew, int hyst) //groups: 0-27 28-45 46-53
{
  mA=(mA>>1)-1; // input: 2 4...14 16
  dmb(); //after read, before write
  bcm2835.pads->group[group] = 0x5A000000 + (mA&7) + ((fastslew&1)<<4) + ((hyst&1)<<3);
}

int rpiio_init(void)
{
  int fd=-1;
  if ((fd=open("/dev/mem",O_RDWR|O_SYNC))<0) {
    fprintf(stderr,"failed to open /dev/mem: %s\n",strerror(errno));
    return -1;
  }

  unsigned base,cpu;

    base=0x20000000UL;
    cpu=(boardrev()>>12)&15;
    if (cpu==1) base=0x3F000000UL;
    


//mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,

  bcm2835.gpio = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0200000);
  bcm2835.pwm  = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x020C000);
  bcm2835.clk  = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0101000);
  bcm2835.pads = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0100000);
  bcm2835.spi0 = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0204000);
  bcm2835.bsc0 = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0205000);
  bcm2835.bsc1 = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0804000);
  bcm2835.st   = mmap(NULL,4096,(PROT_READ|PROT_WRITE),MAP_SHARED,fd,base+0x0003000);

  if ( (bcm2835.gpio==MAP_FAILED)||(bcm2835.pwm==MAP_FAILED)||(bcm2835.clk==MAP_FAILED)||
       (bcm2835.pwm==MAP_FAILED)||(bcm2835.clk==MAP_FAILED)||(bcm2835.pads==MAP_FAILED)||
       (bcm2835.spi0==MAP_FAILED)||(bcm2835.bsc0==MAP_FAILED)||(bcm2835.bsc1==MAP_FAILED)||
       (bcm2835.st==MAP_FAILED) ) {

    close(fd);
    return 0;
  }
  close(fd);
  return 1;
}

void rpiio_done(void)
{
    munmap((void*)bcm2835.gpio,4096);
    munmap((void*)bcm2835.pwm, 4096);
    munmap((void*)bcm2835.clk, 4096);
    munmap((void*)bcm2835.spi0,4096);
    munmap((void*)bcm2835.bsc0,4096);
    munmap((void*)bcm2835.bsc1,4096);
    munmap((void*)bcm2835.st,  4096);
}

void gpio_setpin(int pin, int lev) //only 0..31 !
{
  unsigned mask=1<<(pin&31);
  dmb(); //after read, before write
  if (lev)
    bcm2835.gpio->set[0]=mask;
  else
    bcm2835.gpio->clr[0]=mask;
}

int gpio_getpin(int pin) //only 0..31 !
{
  unsigned n;
  n=bcm2835.gpio->lev[0];
  dmb(); //after read, before write
  if (n&(1<<pin)) return 1;
  return 0;  
}





