/*
 rpi2o - a barebones IO library for raspi.
 2013  Kasper Pedersen  Beerware License

 Inspired by bcm2835.c, but all redone.

 This library uses proper ARMv6 barriers, and is intended for speed,
 and for use by programmers used to bare metal hardware,  not for 
 'Arduino emulation'.

 Instead of trying to do a complete high level library, I have pulled
 in the definitions, and set up the various memory maps, so that
 when one needs to actually implement (and test!) support for another
 devices, its registers are there and ready for use.

 Currently GPIO is fully implemented, and GPCLK works with integer
 dividers (low jitter clocks).


 IMPORTANT: If connecting to the P1 pin header, use CP1_XX to get the gpio number,
 do not hardcode the bcm2835 gpio number. 3 of the pins are connected to different
 GPIO depending on board revision, and the CP1_XX macros will detect the actual
 board revision and return the corresponding gpio number for the board.


 Envisioned GPIO use:
 Set up the pins using the slow pin-by-pin calls.
 Calculate any delays using calcbdelay.
 Calculate pin masks for the pin that will be accessed a lot.
 When running, use bdelay() and either gpioXXXmask() or 'direct access+barriers'.


 About ARMv5/ARMv6 barriers:
 These are accessed through 'mcr' (move control register), and are at first glance
 privileged and thus not accessible from user mode. This is not quite true though;
 There are 3  encodings that are unprivileged, and thus we can do barriers from userspace.
 The bcm2835 documentation recommends 'readbarrier after read, writebarrier 
 before write', and historically the dmb() has been referred to as the read barrier,
 and the dsb() as the write barrier. The difference between them is that dsb() halts
 execution until prior memory transactions have completed and is really expensive,
 whereas dmb() only prevents new transactions from being started before all prior
 ones have finished, and is very cheap.
 You should use dmb() for all barriers; The only time dsb() is requireed is when you
 configure something memory mapped, and then do something that is not memory mapped
 that depends on it. An example of this would be configuring the memory mapped interrupt
 controller, and then afterwards enabling the interrupt mask in the processor control
 register. And since you can not do this from user mode, there is no need for dsb().
 It is used in exactly one place in the library: In the bdelay function.
 The intent of the bdelay function is to insert a short minimum delay between
 IO operations, and thus needs to wait for the prior transactions to have
 completed before starting the busy-loop delay.


 About bdelay() and calcbdelay():
 Current bdelay() resolution is ~4ns, and is suitable for 10ns-10us delays.
 For longer delays than that, there is usdelay(), which automatically
 yields to the OS if the delay is long enough.

*/

#include <stdint.h>

//threshold where we start yielding time to the OS.
//if set to 1000, and a 5000 us delay is requested,
//then usdelay() does a usleep(4000), and busy-waits
//for the remainder of the period.
//1000 is a good, safe value. If set below 450, the
//delays will be longer than requsted.
#define OSSLEEP_THRESHOLD 1000

//number of microseconds to use in tunebdelay.
//if set to 25, the delays may be up to 1/25, or 4%,
//longer than specified. Tuning takes 50 times this,
//so a value of 25 means that 1.3ms will be spent tuning.
#define BDELAYTUNELENGTH 25

typedef struct {
  unsigned fsel[6]; // 0000 GPIO Function Select
  unsigned res0x18;
  unsigned set[2];    // 001c GPIO Pin Output Set
  unsigned res0x24;
  unsigned clr[2];    // 0028 GPIO Pin Output clear
  unsigned res0x30;
  unsigned lev[2];    // 0034 GPIO Pin Level
  unsigned res0x3c;
  unsigned eds[2];    // 0040 GPIO Pin Event Detect Status
  unsigned res0x48;
  unsigned ren[2];    // 004C GPIO Pin Rising Edge Detect Enable
  unsigned res0x54;
  unsigned fen[2];    // 0058 GPIO Pin Falling Edge Detect Enable
  unsigned res0x60;
  unsigned hen[2];    // 0064 GPIO Pin High Detect Enable
  unsigned res0x6c;
  unsigned len[2];    // 0070 GPIO Pin Low Detect Enable
  unsigned res0x78;
  unsigned aren[2];   // 007C GPIO Pin Async. Rising Edge Detect
  unsigned res0x84;
  unsigned afen[2];   // 0088 GPIO Pin Async. Falling Edge Detect
  unsigned res0x90;
  unsigned pud;       // 0094 GPIO Pin Pull-up/down Enable
  unsigned pudclk[2]; // 0098 GPIO Pin Pull-up/down Enable Clock
                      // 00A0
} bcm2835_gpio_t;

typedef struct {
  unsigned res0x00[0x0b]; 
  unsigned group[3]; //0..27 28..45 46..53
} bcm2835_pads_t;

typedef struct {
  unsigned cs;
  unsigned fifo;
  unsigned clk;
  unsigned dlen;
  unsigned ltoh;
  unsigned dc;
  //0018
} bcm2835_spi_t;

typedef struct {
  unsigned c;
  unsigned s;
  unsigned dlen;
  unsigned a;
  unsigned fifo;
  unsigned div;
  unsigned del;
  unsigned clkt; 
  //0020
} bcm2835_bsc_t;

typedef struct {
  unsigned cs;
  unsigned clo;
  unsigned chi;
} bcm2835_st_t;

typedef struct {

} bcm2835_pwm_t;

typedef struct {
  unsigned filler[0x1C];
  unsigned ctl0; //0070
  unsigned div0;
} bcm2835_clk_t;

#define GPIO_FSEL_INPT  0
#define GPIO_FSEL_OUTP  1
#define GPIO_FSEL_ALT0  4
#define GPIO_FSEL_ALT1  5
#define GPIO_FSEL_ALT2  6
#define GPIO_FSEL_ALT3  7
#define GPIO_FSEL_ALT4  3
#define GPIO_FSEL_ALT5  2

typedef struct {
  volatile bcm2835_st_t *st;
  volatile bcm2835_gpio_t *gpio;
  volatile bcm2835_pwm_t *pwm;
  volatile bcm2835_clk_t *clk;
  volatile bcm2835_pads_t *pads;
  volatile bcm2835_spi_t *spi0;
  volatile bcm2835_bsc_t *bsc0;
  volatile bcm2835_bsc_t *bsc1;
} bcm2835_st;

extern bcm2835_st bcm2835;

int boardrev(void);

//raspberry pi board-autosensing pin definitions.
#define CP1_03 ((boardrev()<4) ?  0 :  2)
#define CP1_05 ((boardrev()<4) ?  1 :  3)
#define CP1_07 4
#define CP1_08 14
#define CP1_10 15
#define CP1_11 17
#define CP1_12 18
#define CP1_13 ((boardrev()<4) ? 21 : 27)
#define CP1_15 22
#define CP1_16 23
#define CP1_18 24
#define CP1_19 10
#define CP1_21 9
#define CP1_22 25
#define CP1_23 11
#define CP1_24 8
#define CP1_26 7
//the connector is only on V2, but we can still solder to the resistor pads on v1.
#define CP5_03 28
#define CP5_04 29
#define CP5_05 30
#define CP5_06 31

//memory barriers for ARMv6. These 3 are special and non-privileged.
#ifndef barrierdefs
 #define barrierdefs
 #define isb() __asm__ __volatile__ ("mcr p15, 0, %0, c7,  c5, 4" : : "r" (0) : "memory")
 #define dmb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" : : "r" (0) : "memory")
 //ARMv6 DSB (DataSynchronizationBarrier): also known as DWB (drain write buffer / data write barrier) on ARMv5
 #define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory")
#endif
//use dmb() around the accesses. ("before write, after read")

static inline void gpiosetmask(unsigned n)
{
  dmb(); 
  bcm2835.gpio->set[0]=n;
}

static inline void gpioclearmask(unsigned n)
{
  dmb(); 
  bcm2835.gpio->clr[0]=n; 
}

static inline unsigned gpioreadmask(void)
{
  unsigned n=bcm2835.gpio->lev[0];
  dmb(); 
  return n; 
}

static inline usec32(void)
{
  unsigned u;
  dmb();
  u=bcm2835.st->clo;
  dmb();
  return u;
}


uint64_t usec64(void);
unsigned tunedelay(void);
void bdelay(unsigned n);
void usdelay(unsigned n);

void gpio_fsel(int pin, int fn);
void gpio_pud(int pin, unsigned pupmode); //0=none, 1=down, 2=up
void gpio_set_padgroup(int group, unsigned mA /*8*/, int fastslew/*1*/, int hyst/*1*/); //groups: 0/1/2 -> io 0-27 28-45 46-53

void gpio_setpin(int pin, int lev);
int gpio_getpin(int pin);
int rpiio_init(void);
void rpiio_done(void);



typedef struct {
  unsigned mask,inverted;
} pinmasks_t;

static void pinctlset(pinmasks_t *p, int level)
{
  if (p->inverted) level=!level;
  if (level) {
    bcm2835.gpio->set[0] = p->mask;
  } else {
    bcm2835.gpio->clr[0] = p->mask;
  }
}
