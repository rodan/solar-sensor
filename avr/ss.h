
#ifndef __ss_h_
#define __ss_h_

int measure_ext();
void measure_int();

void read_counter();

void stage1();
void stage2();
void stage3();
void stage4();
void stage5();
void stage6();

void ir_decode();

void console_decode();
void parse_cmd(char *cmd, uint8_t cmdsize);

void console_send_ok();
void console_send_err();

void xbee_on();
void xbee_off();

void setup_a2();

#endif
