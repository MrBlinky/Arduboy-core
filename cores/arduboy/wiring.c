/*
  wiring.c - Partial implementation of the Wiring API for the ATmega8.
  Part of Arduino - http://www.arduino.cc/

  Copyright (c) 2005-2006 David A. Mellis

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA
*/

#define ARDUBOY_CORE_WIRING_C

#include "wiring_private.h"

// the prescaler is set so that timer0 ticks every 64 clock cycles, and the
// the overflow handler is called every 256 ticks.
#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64 * 256))

// the whole number of milliseconds per timer0 overflow
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW / 1000)

// the fractional number of milliseconds per timer0 overflow. we shift right
// by three to fit these numbers into a byte. (for the clock speeds we care
// about - 8 and 16 MHz - this doesn't lose precision.)
#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW % 1000) >> 3)
#define FRACT_MAX (1000 >> 3)

volatile unsigned long timer0_overflow_count = 0;
volatile unsigned long timer0_millis = 0;
static   unsigned char timer0_fract = 0;

volatile unsigned char button_ticks_hold = 0;
volatile unsigned char button_ticks_now  = 0;
volatile unsigned char button_ticks_last = 0;
volatile unsigned char bootloader_timer  = 0;

#if defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
ISR(TIM0_OVF_vect)  
#else
ISR(TIMER0_OVF_vect, ISR_NAKED)
#endif
{
    // copy these to local variables so they can be stored in registers
    // (volatile variables must be read from memory on every access)
/*
    unsigned long m = timer0_millis;
    unsigned char f = timer0_fract;

    m += MILLIS_INC;
    f += FRACT_INC;
    if (f >= FRACT_MAX) {
        f -= FRACT_MAX;
        m += 1;
    }

    timer0_fract = f;
    timer0_millis = m;
    timer0_overflow_count++;
*/  
    // save registers and SREG
    asm volatile(
      "    push r16                     \n\t"
      "    in   r16, __SREG__           \n\t"
      "    push r16                     \n\t" //use as more functional temp reg
      "    push r1                      \n\t"
      "    clr  r1                      \n\t" //zero reg
      "    push r24                     \n\t"
      "    push r25                     \n\t"
      "    push r30                     \n\t"
      "    push r31                     \n\t"
    );
    asm volatile(
      "    lds  r24, %[fract]           \n\t" // f= timer0_fract;
      "    ldi  r25, %[millis_inc]      \n\t" // millis_inc = MILLIS_INC;
      "    subi r24, %[fract_inc]       \n\t" // f += FRACT_INC;
      "    cpi  r24, %[fract_max]       \n\t" // if (f >= FRACT_MAX) {
      "    brcs 1f                      \n\t" 
      
      "    subi r24, %[fract_max]       \n\t" // f -= FRACT_MAX;
      "    inc  r25                     \n\t" // millis_inc++ }
      "1:                               \n\t"
      "    sts  %[fract], r24           \n\t" // timer0_fract = f;
      "    ld   r24, z                  \n\t" //timer0_millis += millis_inc
      "    add  r24, r25                \n\t"
      "    st   z, r24                  \n\t"
      "    ldd  r25, z+1                \n\t" 
      "    adc  r25, r1                 \n\t" 
      "    std  z+1, r25                \n\t" // r25 = (millis >>8)
      "    ldd  r16, z+2                \n\t"  
      "    adc  r16, r1                 \n\t" 
      "    std  z+2, r16                \n\t" // r16 = (millis >>16)
      "    ldd  r24, z+3                \n\t" 
      "    adc  r24, r1                 \n\t"  
      "    std  z+3, r24                \n\t"
      
      "    eor  r16, r25                \n\t" //button_ticks_now = (millis >> 12)
      "    andi r16, 0x0F               \n\t"
      "    eor  r16, r25                \n\t"
      "    swap r16                     \n\t"
      "    sts  %[buttons_now], r16     \n\t"
      : 
      : [millis]      "z" (&timer0_millis),
        [fract]       ""  (&timer0_fract),
        [buttons_now] ""  (&button_ticks_now),
        [millis_inc]  "M" (MILLIS_INC),
        [fract_inc]   "M" (256 - FRACT_INC), // negated for subi instruction
        [fract_max]   "M" (FRACT_MAX)
      : 
    );
    //Arduboy bootloader and reset button feature
    asm volatile (
      "    rcall scan_buttons           \n\t" 
      "    cpse r24, r1                 \n\t" //if (buttons) button_ticks_last = (uint8_t)(Millis >> 12)
      "    sts  %[apd], r16             \n\t" 
#ifdef     AB_DEVKIT  
      "    cpi	r24, 0x33	            \n\t" // test LEFT+UP+A+B for bootloader
      "    breq 2f                      \n\t" 
      "    cpi  r24, 0x47               \n\t" // test RIGHT+DOWN+A+B for reset sketch
      "    brne 5f                      \n\t" 
#else
      "    cpi	r24, 0xAC	            \n\t" // test LEFT+UP+A+B for bootloader
      "    breq 2f                      \n\t" 
      "    cpi  r24, 0x5C               \n\t" // test RIGHT+DOWN+A+B for reset sketch
      "    brne 5f                      \n\t" 
#endif      
      "2:  lds  r16, %[hold]            \n\t" 
      "    sub  r25, r16                \n\t" // (uint8_t)(timer0_millis >> 8) - button_ticks_last
      "    cpi  r25, 8                  \n\t" 
      "    brcs 6f                      \n\t" // if ((millis - hold) < 8)
#ifdef     AB_DEVKIT  
      "    subi r24, 0x33 - 0x77        \n\t" //get bootloader key or reset key value
#else      
      "    subi r24, 0xAC - 0x77        \n\t" //get bootloader key or reset key value
#endif  
      "3:  sts	0x800, r24              \n\t" 
      "    sts	0x801, r24              \n\t" 
      "4:  rcall scan_buttons           \n\t" //wait for buttons to be released
      "    brne 4b                      \n\t" 
      "    ldi	r24, %[value1]          \n\t" 
      "    ldi	r25, %[value2]          \n\t" 
      "    sts   %[wdtcsr], r24         \n\t" 
      "    sts   %[wdtcsr], r25         \n\t" 
      "    rjmp .-2                     \n\t"
      "5:                               \n\t"
      "    sts  %[hold], r25            \n\t" //button_ticks_hold = (uint8_t)(Millis >> 8)
      "6:                               \n\t"
      "    lds  r24, %[btimer]          \n\t" //if (bootloader_timer--) {
      "    subi r24, 1                  \n\t" 
      "    brcs 7f                      \n\t"
      "    sts  %[btimer],r24           \n\t"
      "    ldi  r24, 0x77               \n\t"
      "    breq 3b                      \n\t" // if (bootloader_timer == 0) runBootLoader;
      "7:                               \n\t" //}
      :
      : [hold]      ""  (&button_ticks_hold),
        [apd]       ""  (&button_ticks_last),
        [btimer]    ""  (&bootloader_timer),
        [value1]    "M" ((uint8_t)(_BV(WDCE) | _BV(WDE))),
        [value2]    "M" ((uint8_t)(_BV(WDE))),                         
        [wdtcsr]    "M" (_SFR_MEM_ADDR(WDTCSR))
      : 
    );
    //timer0_overflow_count++;
    asm volatile (
      "    ld   r24, z                  \n\t"  
      "    ldd  r25, z+1                \n\t"  
      "    adiw r24, 1                  \n\t"
      "    st   z, r24                  \n\t"  
      "    std  z+1, r25                \n\t"  
      "    ldd  r24, z+2                \n\t"  
      "    ldd  r25, z+3                \n\t"  
      "    adc  r24, r1                 \n\t"  
      "    adc  r25, r1                 \n\t"  
      "    std  z+2, r24                \n\t"  
      "    std  z+3, r25                \n\t"  
    :
    : "z" (&timer0_overflow_count)
    );
    //restore registers and retirn from interrupt
    asm volatile (
      "    pop  r31                     \n\t"
      "    pop  r30                     \n\t"
      "    pop  r25                     \n\t"
      "    pop  r24                     \n\t"
      "    pop  r1                      \n\t"
      "    pop  r16                     \n\t"  
      "    out  __SREG__, r16           \n\t"
      "    pop  r16                     \n\t"
      "    reti                         \n\t"
      ".global scan_buttons             \n\t"
      "scan_buttons:                    \n\t"
#ifdef     AB_DEVKIT
      "    in	r24, %[pinb]            \n\t" // down, left, up buttons
      "    com  r24                     \n\t" 
      "    andi r24, 0x70               \n\t" 
      "    sbis %[pinc], 6	            \n\t" // right button
      "    ori	r24, 0x04	            \n\t" 
      "    sbis %[pinf], 7	            \n\t" // A button
      "    ori	r24, 0x02	            \n\t" 
      "    sbis %[pinf], 6	            \n\t" // B button
      "    ori	r24, 0x01	            \n\t" 
#else      
      "    in	r24, %[pinf]            \n\t" // directional buttons
      "    com  r24                     \n\t" 
      "    andi r24, 0xF0               \n\t" 
      "    sbis %[pine], 6	            \n\t" // A button
      "    ori	r24, 0x08	            \n\t" 
      "    sbis %[pinb], 4	            \n\t" // B button
      "    ori	r24, 0x04	            \n\t" 
#endif      
      "    ret          	            \n\t" // Z flag set from AND when no button is pressed
      :
      : [pinf]      "I" (_SFR_IO_ADDR(PINF)),
        [pine]      "I" (_SFR_IO_ADDR(PINE)),
        [pinc]      "I" (_SFR_IO_ADDR(PINC)),
        [pinb]      "I" (_SFR_IO_ADDR(PINB))
    );
}

unsigned char buttonsIdleTime()
{
  return button_ticks_now - button_ticks_last;
}

unsigned char millisChar()
{
  return *(unsigned char*)&timer0_millis;
}

unsigned long millis()
{
    unsigned long m;
    uint8_t oldSREG = SREG;

    // disable interrupts while we read timer0_millis or we might get an
    // inconsistent value (e.g. in the middle of a write to timer0_millis)
    cli();
    m = timer0_millis;
    SREG = oldSREG;

    return m;
}

unsigned long micros() {
/*
      unsigned long m;
      uint8_t oldSREG = SREG, t;
      
      cli();
      m = timer0_overflow_count;
  #if defined(TCNT0)
      t = TCNT0;
  #elif defined(TCNT0L)
      t = TCNT0L;
  #else
      #error TIMER 0 not defined
  #endif
  
  #ifdef TIFR0
      if ((TIFR0 & _BV(TOV0)) && (t < 255))
          m++;
  #else
      if ((TIFR & _BV(TOV0)) && (t < 255))
          m++;
  #endif
      SREG = oldSREG;
      return ((m << 8) + t) * (64 / clockCyclesPerMicrosecond());
*/
      //assembly optimalisation
      asm volatile(
      "    in   r18, %[sreg]   \n" //oldSREG = SREG
      "    cli                 \n" //
      "    ld   r23, x+        \n" // m = timer0_overflow_count << 8
      "    ld   r24, x+        \n" 
      "    ld   r25, x         \n" 
      "    in   r22, %[tcnt]   \n" // (m << 8) | t 
      "    out  %[sreg], r18   \n" //SREG = oldSREG
      "    sbis %[tif], %[tov] \n" // if ((TIFR & _BV(TOV) && 
      "    rjmp 1f             \n"
      "    cpi  r22, 0xFF      \n" //  t < 0xFF)
      "    brcc 1f             \n"
      "                        \n"
      "    subi r23, 0xFF      \n" // m++ (m+=256)
      "    sbci r24, 0xFF      \n" //
      "    sbci r25, 0xFF      \n" //
      "1:                      \n"
      "    ldi  r18,%[fm]      \n" // *( 64 / clockCyclesPerMicrosecond()
      "2:                      \n"
      "    add  r22, r22       \n"
      "    adc  r23, r23       \n"
      "    adc  r24, r24       \n"
      "    adc  r25, r25       \n"
      "    dec  r18            \n"
      "    brne 2b             \n"
      :
      : [sreg] "I" (_SFR_IO_ADDR(SREG)),
  #if defined(TCNT0)
        [tcnt] "I" (_SFR_IO_ADDR(TCNT0)),
  #elif defined(TCNT0L)
        [tcnt] "I" (_SFR_IO_ADDR(TCNT0L)),
  #else
      #error TIMER 0 not defined
  #endif
  #ifdef TIFR0  
        [tif] "I" (_SFR_IO_ADDR(TIFR0)),
  #else        
        [tif] "I" (_SFR_IO_ADDR(TIFR)),
  #endif
        [tov] "M" (TOV0),
  #if (F_CPU == 8000000L)
        [fm] "M" (4),
  #elif (F_CPU ==16000000L)
        [fm] "M" (2), 
  #else  
    #error this version of wiring.c only supports 8MHz and 16MHz CPU clock
  #endif  
        "x" (&timer0_overflow_count)
      : "r18", "r22", "r23", "r24", "r25"
      );
  }

void delay(unsigned long ms)
{
    /*
    uint32_t start = micros();
    while (ms > 0) {
        yield();
        while ( ms > 0 && (micros() - start) >= 1000) {
            ms--;
            start += 1000;
        }
    }
    */
    //assembly optimalisation
    asm volatile(
      "    movw  r20, %A0  \n" //ms
      "    movw  r30, %C0  \n"
      "    call micros     \n" //endMicros = micros()
      "1:                  \n" 
      "    subi r20, 1     \n" //while (ms > 0)
      "    sbc  r21, r1    \n" 
      "    sbc  r30, r1    \n" 
      "    sbc  r31, r1    \n" 
      "    brcs 2f         \n" 
      "                    \n" 
      "    subi r22, 0x18  \n" //endMicros += 1000
      "    sbci r23, 0xFC  \n" 
      "    sbci r24, 0xFF  \n" 
      "    sbci r25, 0xFF  \n" 
      "    rjmp 1b         \n" 
      "2:                  \n" 
      "    movw  r20, r22  \n" 
      "    movw  r30, r24  \n" 
      "3:                  \n" 
      "    call micros     \n" //while (micros() < endMicros);
      "    cp   r22, r20   \n" 
      "    cpc  r23, r21   \n" 
      "    cpc  r24, r30   \n" 
      "    cpc  r25, r31   \n" 
      "    brcs 3b         \n" 
      : 
      : "d" (ms),
        "" (micros)
        
      : "r20", "r21", "r30", "r31", /*from micros: */ "r18", "r26", "r27"
    );
}

void delayShort(unsigned short ms)
{
    asm volatile(
      "    call micros     \n" //endMicros = micros()
      "1:                  \n" 
      "    sbiw r30, 1     \n" //while (ms > 0)
      "    brcs 2f         \n" 
      "                    \n" 
      "    subi r22, 0x18  \n" //endMicros += 1000
      "    sbci r23, 0xFC  \n" 
      "    sbci r24, 0xFF  \n" 
      "    sbci r25, 0xFF  \n" 
      "    rjmp 1b         \n" 
      "2:                  \n" 
      "    movw  r20, r22  \n" 
      "    movw  r30, r24  \n" 
      "3:                  \n" 
      "    call micros     \n" //while (micros() < endMicros);
      "    cp   r22, r20   \n" 
      "    cpc  r23, r21   \n" 
      "    cpc  r24, r30   \n" 
      "    cpc  r25, r31   \n" 
      "    brcs 3b         \n" 
      : 
      : "z" (ms),
        "" (micros)
        
      : "r20", "r21", "r22", "r23",  /*from micros: */ "r18", "r26", "r27"
    );
}

/* Delay for the given number of microseconds.  Assumes a 1, 8, 12, 16, 20 or 24 MHz clock. */
void delayMicroseconds(unsigned int us)
{
    // call = 4 cycles + 2 to 4 cycles to init us(2 for constant delay, 4 for variable)

    // calling avrlib's delay_us() function with low values (e.g. 1 or
    // 2 microseconds) gives delays longer than desired.
    //delay_us(us);
#if F_CPU >= 24000000L
    // for the 24 MHz clock for the aventurous ones, trying to overclock

    // zero delay fix
    if (!us) return; //  = 3 cycles, (4 when true)

    // the following loop takes a 1/6 of a microsecond (4 cycles)
    // per iteration, so execute it six times for each microsecond of
    // delay requested.
    us *= 6; // x6 us, = 7 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 22 (24) cycles above, remove 5, (5*4=20)
    // us is at least 6 so we can substract 5
    us -= 5; //=2 cycles

#elif F_CPU >= 20000000L
    // for the 20 MHz clock on rare Arduino boards

    // for a one-microsecond delay, simply return.  the overhead
    // of the function call takes 18 (20) cycles, which is 1us
    __asm__ __volatile__ (
        "nop" "\n\t"
        "nop" "\n\t"
        "nop" "\n\t"
        "nop"); //just waiting 4 cycles
    if (us <= 1) return; //  = 3 cycles, (4 when true)

    // the following loop takes a 1/5 of a microsecond (4 cycles)
    // per iteration, so execute it five times for each microsecond of
    // delay requested.
    us = (us << 2) + us; // x5 us, = 7 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 26 (28) cycles above, remove 7, (7*4=28)
    // us is at least 10 so we can substract 7
    us -= 7; // 2 cycles

#elif F_CPU >= 16000000L
    // for the 16 MHz clock on most Arduino boards

    // for a one-microsecond delay, simply return.  the overhead
    // of the function call takes 14 (16) cycles, which is 1us
    if (us <= 1) return; //  = 3 cycles, (4 when true)

    // the following loop takes 1/4 of a microsecond (4 cycles)
    // per iteration, so execute it four times for each microsecond of
    // delay requested.
    us <<= 2; // x4 us, = 4 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 19 (21) cycles above, remove 5, (5*4=20)
    // us is at least 8 so we can substract 5
    us -= 5; // = 2 cycles,

#elif F_CPU >= 12000000L
    // for the 12 MHz clock if somebody is working with USB

    // for a 1 microsecond delay, simply return.  the overhead
    // of the function call takes 14 (16) cycles, which is 1.5us
    if (us <= 1) return; //  = 3 cycles, (4 when true)

    // the following loop takes 1/3 of a microsecond (4 cycles)
    // per iteration, so execute it three times for each microsecond of
    // delay requested.
    us = (us << 1) + us; // x3 us, = 5 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 20 (22) cycles above, remove 5, (5*4=20)
    // us is at least 6 so we can substract 5
    us -= 5; //2 cycles

#elif F_CPU >= 8000000L
    // for the 8 MHz internal clock

    // for a 1 and 2 microsecond delay, simply return.  the overhead
    // of the function call takes 14 (16) cycles, which is 2us
    if (us <= 2) return; //  = 3 cycles, (4 when true)

    // the following loop takes 1/2 of a microsecond (4 cycles)
    // per iteration, so execute it twice for each microsecond of
    // delay requested.
    us <<= 1; //x2 us, = 2 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 17 (19) cycles above, remove 4, (4*4=16)
    // us is at least 6 so we can substract 4
    us -= 4; // = 2 cycles

#else
    // for the 1 MHz internal clock (default settings for common Atmega microcontrollers)

    // the overhead of the function calls is 14 (16) cycles
    if (us <= 16) return; //= 3 cycles, (4 when true)
    if (us <= 25) return; //= 3 cycles, (4 when true), (must be at least 25 if we want to substract 22)

    // compensate for the time taken by the preceeding and next commands (about 22 cycles)
    us -= 22; // = 2 cycles
    // the following loop takes 4 microseconds (4 cycles)
    // per iteration, so execute it us/4 times
    // us is at least 4, divided by 4 gives us 1 (no zero delay bug)
    us >>= 2; // us div 4, = 4 cycles
    

#endif

    // busy wait
    __asm__ __volatile__ (
        "1: sbiw %0,1" "\n\t" // 2 cycles
        "brne 1b" : "=w" (us) : "0" (us) // 2 cycles
    );
    // return = 4 cycles
}

void init() //assembly optimized by 68 bytes
{
    // this needs to be called before setup() or some functions won't
    // work there
    sei();
    
    // on the ATmega168, timer 0 is also used for fast hardware pwm
    // (using phase-correct PWM would mean that timer 0 overflowed half as often
    // resulting in different millis() behavior on the ATmega8 and ATmega168)
#if defined(TCCR0A) && defined(WGM01)
    //sbi(TCCR0A, WGM01);
    //sbi(TCCR0A, WGM00);
  asm volatile(
      "    in   r24, %[tccr0a]          \n\t"          
      "    ori  r24, %[wgm01]           \n\t"          
      "    out  %[tccr0a], r24          \n\t"          
      "    ori  r24, %[wgm00]           \n\t"          
      "    out  %[tccr0a], r24          \n\t"          
      :
      : [tccr0a] "I" (_SFR_IO_ADDR(TCCR0A)),
        [wgm01]  "M" (_BV(WGM01)),
        [wgm00]  "M" (_BV(WGM00))
      : "r24"
    );
#endif

    // set timer 0 prescale factor to 64
#if defined(__AVR_ATmega128__)
    // CPU specific: different values for the ATmega128
    sbi(TCCR0, CS02);
#elif defined(TCCR0) && defined(CS01) && defined(CS00)
    // this combination is for the standard atmega8
    sbi(TCCR0, CS01);
    sbi(TCCR0, CS00);
#elif defined(TCCR0B) && defined(CS01) && defined(CS00)
    // this combination is for the standard 168/328/1280/2560
    //sbi(TCCR0B, CS01);
    //sbi(TCCR0B, CS00);
    asm volatile(
      "    in   r24, %[tccr0b]          \n\t"          
      "    ori  r24, %[cs01]            \n\t"          
      "    out  %[tccr0b], r24          \n\t"          
      "    ori  r24, %[cs00]            \n\t"          
      "    out  %[tccr0b], r24          \n\t"          
      :
      : [tccr0b] "I" (_SFR_IO_ADDR(TCCR0B)),
        [cs01]   "M" (_BV(CS01)),
        [cs00]   "M" (_BV(CS00))
      : "r24"
    );
#elif defined(TCCR0A) && defined(CS01) && defined(CS00)
    // this combination is for the __AVR_ATmega645__ series
    sbi(TCCR0A, CS01);
    sbi(TCCR0A, CS00);
#else
    #error Timer 0 prescale factor 64 not set correctly
#endif

    // enable timer 0 overflow interrupt
#if defined(TIMSK) && defined(TOIE0)
    sbi(TIMSK, TOIE0);
#elif defined(TIMSK0) && defined(TOIE0)
    //sbi(TIMSK0, TOIE0);
    asm volatile(
      "    ldi  r30, %[timsk0]          \n\t"          
      "    ldi  r31, 0x00               \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[toie0]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [timsk0] "M" (_SFR_MEM_ADDR(TIMSK0)),
        [toie0]  "M" (_BV(TOIE0))
      : "r24", "r30", "r31"
    );
#else
    #error  Timer 0 overflow interrupt not set correctly
#endif

    // timers 1 and 2 are used for phase-correct hardware pwm
    // this is better for motors as it ensures an even waveform
    // note, however, that fast pwm mode can achieve a frequency of up
    // 8 MHz (with a 16 MHz clock) at 50% duty cycle

#if defined(TCCR1B) && defined(CS11) && defined(CS10)
    //TCCR1B = 0;

    // set timer 1 prescale factor to 64
    //sbi(TCCR1B, CS11);
    asm volatile(
      "    ldi  r30, %[tccr1b]          \n\t"          
      "    st   z, r1                   \n\t"          
      "    ldi  r24, %[cs11]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr1b] "M" (_SFR_MEM_ADDR(TCCR1B)),
        [cs11]   "M" (_BV(CS11))
      : "r24", "r30", "r31"
    );
    
#if F_CPU >= 8000000L
    //sbi(TCCR1B, CS10);
    asm volatile(
      "    ori  r24, %[cs10]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [cs10] "M" (_BV(CS10))
      : "r24", "r30", "r31"
    );
#endif
#elif defined(TCCR1) && defined(CS11) && defined(CS10)
    sbi(TCCR1, CS11);
#if F_CPU >= 8000000L
    sbi(TCCR1, CS10);
#endif
#endif
    // put timer 1 in 8-bit phase correct pwm mode
#if defined(TCCR1A) && defined(WGM10)
    //sbi(TCCR1A, WGM10);
    asm volatile(
      "    ldi  r30, %[tccr1a]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[wgm10]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr1a] "M" (_SFR_MEM_ADDR(TCCR1A)),
        [wgm10]  "M" (_BV(WGM10))
      : "r24", "r30", "r31"
    );
#endif

    // set timer 2 prescale factor to 64
#if defined(TCCR2) && defined(CS22)
    sbi(TCCR2, CS22);
#elif defined(TCCR2B) && defined(CS22)
    sbi(TCCR2B, CS22);
//#else
    // Timer 2 not finished (may not be present on this CPU)
#endif

    // configure timer 2 for phase correct pwm (8-bit)
#if defined(TCCR2) && defined(WGM20)
    sbi(TCCR2, WGM20);
#elif defined(TCCR2A) && defined(WGM20)
    sbi(TCCR2A, WGM20);
//#else
    // Timer 2 not finished (may not be present on this CPU)
#endif

#if defined(TCCR3B) && defined(CS31) && defined(WGM30)
    //sbi(TCCR3B, CS31);      // set timer 3 prescale factor to 64
    //sbi(TCCR3B, CS30);
    asm volatile(
      "    ldi  r30, %[tccr3b]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[cs31]            \n\t"          
      "    st   z, r24                  \n\t"          
      "    ori  r24, %[cs30]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr3b] "M" (_SFR_MEM_ADDR(TCCR3B)),
        [cs31]   "M" (_BV(CS31)),
        [cs30]   "M" (_BV(CS30))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR3A, WGM30);     // put timer 3 in 8-bit phase correct pwm mode
    asm volatile(
      "    ldi  r30, %[tccr3a]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[wgm30]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr3a] "M" (_SFR_MEM_ADDR(TCCR3A)),
        [wgm30]  "M" (_BV(WGM30))
      : "r24", "r30", "r31"
    );
#endif

#if defined(TCCR4A) && defined(TCCR4B) && defined(TCCR4D) /* beginning of timer4 block for 32U4 and similar */
    //sbi(TCCR4B, CS42);      // set timer4 prescale factor to 64
    //sbi(TCCR4B, CS41);
    //sbi(TCCR4B, CS40);
    asm volatile(
      "    ldi  r30, %[tccr4b]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[cs42]            \n\t"          
      "    st   z, r24                  \n\t"          
      "    ori  r24, %[cs41]            \n\t"          
      "    st   z, r24                  \n\t"          
      "    ori  r24, %[cs40]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4b] "M" (_SFR_MEM_ADDR(TCCR4B)),
        [cs42]   "M" (_BV(CS42)),
        [cs41]   "M" (_BV(CS41)),
        [cs40]   "M" (_BV(CS40))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR4D, WGM40);     // put timer 4 in phase- and frequency-correct PWM mode 
    asm volatile(
      "    ldi  r30, %[tccr4d]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[wgm40]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4d] "M" (_SFR_MEM_ADDR(TCCR4D)),
        [wgm40]  "M" (_BV(WGM40))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR4A, PWM4A);     // enable PWM mode for comparator OCR4A
    asm volatile(
      "    ldi  r30, %[tccr4a]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[pwm4a]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4a] "M" (_SFR_MEM_ADDR(TCCR4A)),
        [pwm4a]  "M" (_BV(PWM4A))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR4C, PWM4D);     // enable PWM mode for comparator OCR4D
    asm volatile(
      "    ldi  r30, %[tccr4c]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[pwm4d]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4c] "M" (_SFR_MEM_ADDR(TCCR4C)),
        [pwm4d]  "M" (_BV(PWM4D))
      : "r24", "r30", "r31"
    );
#else /* beginning of timer4 block for ATMEGA1280 and ATMEGA2560 */
#if defined(TCCR4B) && defined(CS41) && defined(WGM40)
    sbi(TCCR4B, CS41);      // set timer 4 prescale factor to 64
    sbi(TCCR4B, CS40);
    sbi(TCCR4A, WGM40);     // put timer 4 in 8-bit phase correct pwm mode
#endif
#endif /* end timer4 block for ATMEGA1280/2560 and similar */   

#if defined(TCCR5B) && defined(CS51) && defined(WGM50)
    sbi(TCCR5B, CS51);      // set timer 5 prescale factor to 64
    sbi(TCCR5B, CS50);
    sbi(TCCR5A, WGM50);     // put timer 5 in 8-bit phase correct pwm mode
#endif

#if defined(ADCSRA)
    // set a2d prescaler so we are inside the desired 50-200 KHz range.
    #if F_CPU >= 16000000 // 16 MHz / 128 = 125 KHz
        //sbi(ADCSRA, ADPS2);
        //sbi(ADCSRA, ADPS1);
        //sbi(ADCSRA, ADPS0);
        asm volatile(
          "    ldi  r30, %[adcsra]          \n\t"          
          "    ld   r24, z                  \n\t"          
          "    ori  r24, %[adps2]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    ori  r24, %[adps1]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    ori  r24, %[adps0]           \n\t"          
          "    st   z, r24                  \n\t"          
          :
          : [adcsra] "M" (_SFR_MEM_ADDR(ADCSRA)),
            [adps2]  "M" (_BV(ADPS2)),
            [adps1]  "M" (_BV(ADPS1)),
            [adps0]  "M" (_BV(ADPS0))
          : "r24", "r30", "r31"
        );
    #elif F_CPU >= 8000000 // 8 MHz / 64 = 125 KHz
        //sbi(ADCSRA, ADPS2);
        //sbi(ADCSRA, ADPS1);
        //cbi(ADCSRA, ADPS0);
        asm volatile(
          "    ldi  r30, %[adcsra]          \n\t"          
          "    ld   r24, z                  \n\t"          
          "    ori  r24, %[adps2]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    ori  r24, %[adps1]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    andi r24, %[adps0]           \n\t"          
          "    st   z, r24                  \n\t"          
          :
          : [adcsra] "M" (_SFR_MEM_ADDR(ADCSRA)),
            [adps2]  "M" (_BV(ADPS2)),
            [adps1]  "M" (_BV(ADPS1)),
            [adps0]  "M" (~_BV(ADPS0))
          : "r24", "r30", "r31"
        );
    #elif F_CPU >= 4000000 // 4 MHz / 32 = 125 KHz
        sbi(ADCSRA, ADPS2);
        cbi(ADCSRA, ADPS1);
        sbi(ADCSRA, ADPS0);
    #elif F_CPU >= 2000000 // 2 MHz / 16 = 125 KHz
        sbi(ADCSRA, ADPS2);
        cbi(ADCSRA, ADPS1);
        cbi(ADCSRA, ADPS0);
    #elif F_CPU >= 1000000 // 1 MHz / 8 = 125 KHz
        cbi(ADCSRA, ADPS2);
        sbi(ADCSRA, ADPS1);
        sbi(ADCSRA, ADPS0);
    #else // 128 kHz / 2 = 64 KHz -> This is the closest you can get, the prescaler is 2
        cbi(ADCSRA, ADPS2);
        cbi(ADCSRA, ADPS1);
        sbi(ADCSRA, ADPS0);
    #endif
    // enable a2d conversions
    //sbi(ADCSRA, ADEN);
        asm volatile(
          "    ori  r24, %[aden]            \n\t" 
          "    st   z, r24                  \n\t"          
          :
          : [aden] "M" (_BV(ADEN))
          : "r24", "r30", "r31"
        );
#endif

    // the bootloader connects pins 0 and 1 to the USART; disconnect them
    // here so they can be used as normal digital i/o; they will be
    // reconnected in Serial.begin()
#if defined(UCSRB)
    UCSRB = 0;
#elif defined(UCSR0B)
    UCSR0B = 0;
#endif
}