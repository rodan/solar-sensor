#ifndef __PLAYA_H__
#define __PLAYA_H__

#include "inttypes.h"
#include "config.h"

#define PIN_JACK_DETECT 1       // detect if stereo jack is not connected
#define PIN_VBAT_DETECT 5       // battery voltage readout
#define PIN_RANDOM      6       // seed the pseudo RNG by reading this unconnected pin

#define JACK_DETECT     0b00000010
#define VBAT_DETECT     0b00100000
#define P_RANDOM        0b01000000
#define ANALOG_DDR      DDRC
#define ANALOG_PIN      PINC

#define CARD_BUFF_SZ    128     // how much data to read from the uSD card in one go
#define MAX_PATH        40      // enough for 4 parent dirs for one file

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#define HIGH    1
#define LOW     0
#define true    1
#define false   0

#define BIT0    0x01
#define BIT1    0x02
#define BIT2    0x04
#define BIT3    0x08
#define BIT4    0x10
#define BIT5    0x20
#define BIT6    0x40
#define BIT7    0x80

#define clockCyclesPerMicrosecond() ( F_CPU / 1000000L )
#define clockCyclesToMicroseconds(a) ( (a) / clockCyclesPerMicrosecond() )
#define microsecondsToClockCycles(a) ( (a) * clockCyclesPerMicrosecond() )

void setup(void);
void loop(void);

int measure_ext(void);
void measure_int(void);

void read_counter(void);

void stage1(void);
void stage2(void);
void stage3(void);
void stage4(void);
void stage5(void);
void stage6(void);

uint8_t ui_ir_decode(void);

void console_decode(void);
void parse_cmd(char *cmd, uint8_t cmdsize);

void console_send_ok(void);
void console_send_err(void);

void xbee_on(void);
void xbee_off(void);
void boost_on(void);
void boost_off(void);

void setup_a2(void);

#endif
