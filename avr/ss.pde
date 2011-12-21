
#include <SoftwareSerial.h>
#include <IRremote.h>
#include <Sensirion.h>
#include <Wire.h>
#include <ds3231.h>

#include "ss.h"

#define SENSOR_ID "EHLO s1"

// arduino digital pins
#define pin_ir 2
#define pin_counter 3
#define pin_SHT_DATA 4
#define pin_SHT_SCK 5
#define pin_log_rx 6
#define pin_log_tx 7
#define pin_wireless_hib 12
#define pin_led 13

// analog pins
// rtc uses A5 for SCL and A4 for SDA
#define pin_light 3

// OP_AUTOMATIC - when the device is woken up by the RTC
// OP_MANUAL - when the device is woken up by a human
#define OP_MANUAL 0
#define OP_AUTOMATIC 1
unsigned char op_mode = OP_AUTOMATIC;

// how many minutes to sleep between automatic measurements
// must be an integer between 5 and 59
unsigned char sleep_period = 30;

// initial wait, warmup, counter, hum, post wait, sleep
unsigned long stage_timing[7] =
    { 5000, 1000, 60000, 1000, 20000, 5000, (sleep_period * 60000) - 108000 };
unsigned long stage_prev = 0;
boolean stage_started[7] = { false, false, false, false, false, false, false };

byte stage_num = 0;

// refresh in OP_MANUAL mode
unsigned long refresh_interval = 2000;
unsigned long refresh_prev = 0;

// not much space for misc buffers
#define BUFF_MAX 64

// sht15 variables
Sensirion sht = Sensirion(pin_SHT_DATA, pin_SHT_SCK);
uint16_t sht_raw;

float ext_temp = 0.0;
float ext_hum = 0.0;
float ext_dew = 0.0;
uint8_t ext_light = 0;

// rtc
struct ts t;
float int_temp;

// serial console
char recv[BUFF_MAX];
uint8_t recv_size = 0;

// shut down is here is no server interaction
unsigned long shtd_interval = 20000;
unsigned long shtd_prev = 0;

// openlog
#define BUFF_LOG 64
SoftwareSerial logger(pin_log_rx, pin_log_tx);
char day_prev[3];

// infrared remote
IRrecv irrecv(pin_ir);
decode_results results;
unsigned long result_last = 4294967295UL;       // -1
unsigned int ir_delay = 300;    // delay between repeated button presses
unsigned long ir_delay_prev = 0;

// counter
boolean counter_state = HIGH;
boolean counter_state_last = HIGH;
volatile unsigned long counter_c = 0;   //counts
unsigned long counter_c_last = 0;       //counts at start of counter_interval
unsigned long counter_cpm;      //counts per interval
//unsigned long counter_interval = 60000;
unsigned long counter_prev = 0;

#define BUFF_OUT 64
char output[BUFF_OUT];

void setup()
{
    pinMode(pin_counter, INPUT);
    pinMode(pin_log_rx, INPUT);
    pinMode(pin_log_tx, OUTPUT);
    //pinMode(pin_led, OUTPUT);
    pinMode(pin_wireless_hib, OUTPUT);

    // verify if Alarm2 woked us up  ( XXXX XX1X )
    if ((DS3231_get_sreg() & 0x02) == 0) {
        // powered up by manual switch, not by alarm
        op_mode = OP_MANUAL;
        measure_ext();
        measure_int();
    }

    DS3231_init(0x06);

    // the logger needs time to boot
    delay(300);
    logger.begin(9600);
    Serial.begin(9600);

    // the day when the program was first started
    DS3231_get(&t);
    snprintf(day_prev, 3, "%d", t.mday);

    wireless_off();
    //led_off();
    openlog_open_file();
    irrecv.enableIRIn();

}

void loop()
{
    unsigned long now = millis();

    ir_decode();
    console_decode();

    if (op_mode == OP_AUTOMATIC) {
        switch (stage_num) {
        case 0:
            if (!stage_started[1] && (now - stage_prev > stage_timing[0])) {
                stage_prev = now;
                stage_started[0] = false;
                stage_started[1] = true;
                stage1();
            }
            break;
        case 1:
            if (!stage_started[2] && (now - stage_prev > stage_timing[1])) {
                stage_prev = now;
                stage_started[1] = false;
                stage_started[2] = true;
                stage2();
            }
            break;
        case 2:
            read_counter();
            if (!stage_started[3] && (now - stage_prev > stage_timing[2])) {
                stage_prev = now;
                stage_started[2] = false;
                stage_started[3] = true;
                stage3();
            }
            break;
        case 3:
            if (!stage_started[4] && (now - stage_prev > stage_timing[3])) {
                stage_prev = now;
                stage_started[3] = false;
                stage_started[4] = true;
                stage4();
            }
            break;
        case 4:
            if (!stage_started[5] && (now - stage_prev > stage_timing[4])) {
                stage_prev = now;
                stage_started[4] = false;
                stage_started[5] = true;
                stage5();
            }
            break;
        case 5:
            if (!stage_started[6] && ((now - stage_prev > stage_timing[5]) && (now - shtd_prev > shtd_interval) )) {
                stage_prev = now;
                stage_started[5] = false;
                stage_started[6] = true;
                stage6();
            }
        case 6:
            if (!stage_started[0] && (now - stage_prev > stage_timing[6])) {
                stage_prev = now;
                stage_started[6] = false;
                stage_started[0] = true;
                stage_num = 0;
            }
            break;

        }                       // switch
    } else {

        read_counter();

        if ((now - counter_prev > 60000)) {
            counter_cpm = counter_c - counter_c_last;
            counter_c_last = counter_c;
            counter_prev = now;
        }
        // once a while refresh stuff
        if ((now - refresh_prev > refresh_interval)) {
            refresh_prev = now;

            // if the RTC woke up the device in the meantime
            if ((DS3231_get_sreg() & 0x02) == 2) {
                // clear all alarms and thus go to sleep
                DS3231_set_sreg(0x00);
            }
        }

    }

}

// IR

void ir_decode()
{
    unsigned long now = millis();

    //int ir_number = -1;

    if (irrecv.decode(&results)) {

        if (results.value >= 2048)
            results.value -= 2048;

        if (result_last == results.value && now - ir_delay_prev < ir_delay) {
            results.value = 11111;
        }

        switch (results.value) {
/*      // RC5 codes
        case 1: // 1
            ir_number = 1;
            break;
        case 2: // 2
            ir_number = 2;
            break;
        case 3: // 3
            ir_number = 3;
            break;
        case 4: // 4
            ir_number = 4;
            break;
        case 5: // 5
            ir_number = 5;
            break;
        case 6: // 6
            ir_number = 6;
            break;
        case 7: // 7
            ir_number = 7;
            break;
        case 8: // 8
            ir_number = 8;
            break;
        case 9: // 9
            ir_number = 9;
            break;
        case 0: // 0
            ir_number = 0;
            break;
        case 10: // 10
            ir_number = 10;
            break;
*/
        case 56:               // AV
            Serial.println("");
            Serial.println(SENSOR_ID);
            break;
/*
        case 36: // red
          break;
        case 35: // green
          break;
        case 14: // yellow
          break;
        case 12: // power
            break;
        case 50: // zoom
            break;
        case 39: // sub
            break;
        case 44: // slow
            break;
        case 60: // repeat
            break;
        case 15: // disp
            break;
        case 38: // sleep
            break;
        case 32: // up
            break;
        case 33: // down
            break;
        case 16: // right
            break;
        case 17: // left
            break;
        case 59: // ok
            break;
        case 34: // back
            break;
        case 19: // exit
            break;
        case 18: // menu
            break;
        case 13: // mute
            break;
        case 16: // vol+
            break;
        case 17: // vol-
            break;
        case 28: // ch+
            break;
        case 29: // ch-
            break;
*/
        case 36:               // record
            Serial.println("GET time");
            break;
/*
        case 54: // stop
            break;
        case 14: // play
            break;
        case 31: // pause
            break;
        case 35: // rew
            break;
        case : // fwd
            break;
*/
        }                       // case

        if (results.value != 11111) {
            result_last = results.value;
            ir_delay_prev = now;
        }
        irrecv.resume();        // Receive the next keypress
    }

}

//   sensors

int measure_ext()
{
    sht.meas(TEMP, &sht_raw, BLOCK);
    ext_temp = sht.calcTemp(sht_raw);
    if (ext_temp == -40.1) {
        ext_temp = 0;
        return 1;
    }
    sht.meas(HUMI, &sht_raw, BLOCK);
    ext_hum = sht.calcHumi(sht_raw, ext_temp);
    ext_dew = sht.calcDewpoint(ext_hum, ext_temp);

    ext_light = analogRead(pin_light) / 4;
    return 0;
}

void measure_int()
{
    int_temp = DS3231_get_treg();
}

void read_counter()
{
    counter_state = digitalRead(pin_counter);
    if (counter_state != counter_state_last) {
        if (counter_state == LOW) {
            counter_c++;
        }
        counter_state_last = counter_state;
    }
}

void stage1()
{
    stage_num = 1;
    //debug_status = "s1 warmup";
}

void stage2()
{
    stage_num = 2;
    //debug_status = "s2";

    counter_c_last = counter_c;
}

void stage3()
{
    stage_num = 3;
    //debug_status = "s3";

//  counter_cpm = ( counter_c - counter_c_last ) * 60000.0 / counter_interval;
    counter_cpm = counter_c - counter_c_last;
    wireless_on();
}

void stage4()
{
    stage_num = 4;
    //debug_status = "s4 save";
    char day_now[3];

    //led_on();

    measure_ext();
    measure_int();

    DS3231_get(&t);
    snprintf(day_now, 3, "%d", t.mday);

    if (strncmp(day_now, day_prev, 2) != 0) {
        openlog_open_file();
        strncpy(day_prev, day_now, 3);
    }

    char tmp1[7], tmp2[7], tmp3[7], tmp4[7];

    snprintf(output, BUFF_OUT,
             "%d-%02d-%02dT%02d:%02d:%02d %sC %s%% %sC %dL %sC %dcpm", t.year,
             t.mon, t.mday, t.hour, t.min, t.sec, dtostrf(ext_temp, 2, 2, tmp1),
             dtostrf(ext_hum, 2, 2, tmp2), dtostrf(ext_dew, 2, 2, tmp3),
             ext_light, dtostrf(int_temp, 2, 2, tmp4), counter_cpm);

    logger.println(output);
}

void stage5()
{
    // try to talk with the server
    stage_num = 5;
    //debug_status = "s5 transf";

    Serial.println("");
    Serial.println(SENSOR_ID);
    shtd_prev = millis();
}

void stage6()
{
    stage_num = 6;
    //debug_status = "s6 sleep";

    wireless_off();
    //led_off();

    // set Alarm2 to wake up the device
    unsigned char wakeup_min;
    DS3231_get(&t);
    wakeup_min = (t.min / sleep_period + 1) * sleep_period;
    if (wakeup_min > 59)
        wakeup_min -= 60;

    boolean flags[4] = { 0, 1, 1, 1 };

    // set Alarm2
    DS3231_set_a2(wakeup_min, 0, 0, flags);

    // activate Alarm2
    DS3231_init(0x06);

    // clear all alarms and thus go to sleep
    DS3231_set_sreg(0x00);
    //system_sleep();

}

//   console related

void console_decode()
{
    char in;
    //char buff[BUFF_MAX];

    if (Serial.available() > 0) {
        in = Serial.read();

        //Serial.println(in);

        if ((in == 10 || in == 13) && (recv_size > 0)) {
            parse_cmd(recv, recv_size);
            recv_size = 0;
            recv[0] = 0;
        } else if (in < 31 || in > 122) {       // ~[0-9A-Za-z ]
            // ignore 
        } else if (recv_size > BUFF_MAX - 2) {
            // drop
            recv_size = 0;
            recv[0] = 0;
        } else if (recv_size < BUFF_MAX - 2) {
            recv[recv_size] = in;
            recv[recv_size + 1] = 0;
            //snprintf(buff,BUFF_MAX,"partial,%d: %s,%d,%d\n",recv_size,recv,recv[recv_size],in);
            //Serial.println(buff);
            recv_size += 1;
        }
    }                           // Serial.available
}

void parse_cmd(char *cmd, uint8_t cmdsize)
{
    char f_c[BUFF_LOG];
    int i;
    int rrv = 1;
    char f_name[9];
    unsigned int f_pos = 0;
    unsigned int f_size;
    unsigned int f_buffsize = BUFF_LOG;

/*     
    Serial.print("DBG cmd='");
    Serial.print(cmd[0], DEC);
    Serial.print("' len=");
    Serial.print(cmdsize, DEC);
    Serial.print(" cmd=");
    for (i=0;i<cmdsize;i++) {
        Serial.print(cmd[i]);
    }
    Serial.println("");
*/

    if (cmd[0] == 71 && cmdsize < 13 && cmdsize > 4) {
        // GET 20111020
        // this cmd will read that file from the uSD

        if (stage_num == 5) 
            shtd_prev = millis();

        strncpy(f_name, &cmd[4], 9);
        if (strncmp(f_name, "NOW", 3) == 0) {
            Serial.println(output);
            console_send_ok();
        } else {
            f_size = openlog_get_fsize(f_name);
            Serial.print("LEN ");
            Serial.print(f_name);
            Serial.print(" ");
            Serial.println(f_size);

            if (f_size > 2) {
                while (f_buffsize > 1) {
                    rrv = openlog_read_file(f_name, f_pos, f_buffsize, &f_c[0]);
                    f_pos = f_pos + rrv;
                    if (f_pos + BUFF_LOG > f_size)
                        f_buffsize = f_size - f_pos;

                    for (i = 0; i < rrv; i++) {
                        // 0x1a (26) is ^Z - used as control char
                        // but openlog's read file translates it to 46 !??!
                        //if (f_c[i] != 26)
                        Serial.print(f_c[i]);
                    }
                }
                console_send_ok();
            } else {
                console_send_err();
            }
        }
    } else if (cmd[0] == 72) {
        if (strncmp(cmd, "HALT", 4) == 0) {
            // go' sleep some
            stage_started[5] = false;
            stage_started[6] = true;
            stage6();
        }
    } else if ((cmd[0] == 84) && (cmdsize == 16)) {
        //T355720619112011
        t.sec = inp2toi(cmd, 1);
        t.min = inp2toi(cmd, 3);
        t.hour = inp2toi(cmd, 5);
        t.wday = inp2toi(cmd, 7);
        t.mday = inp2toi(cmd, 8);
        t.mon = inp2toi(cmd, 10);
        t.year = inp2toi(cmd, 12) * 100 + inp2toi(cmd, 14);
        DS3231_set(t);
        console_send_ok();
    } else {
        console_send_err();
    }

}

void console_send_ok()
{
    Serial.println("OK");
}

void console_send_err()
{
    Serial.println("ERR");
}

void wireless_on()
{
    digitalWrite(pin_wireless_hib, LOW);
    delay(14);
}

void wireless_off()
{
    digitalWrite(pin_wireless_hib, HIGH);
}

void led_on()
{
    digitalWrite(pin_led, HIGH);
}

void led_off()
{
    digitalWrite(pin_led, LOW);
}

//   OpenLog related

void openlog_open_file()
{
    int wait = 200;
    char fname[9];

    DS3231_get(&t);
    snprintf(fname, 9, "%d%02d%02d", t.year, t.mon, t.mday);

    delay(wait);
    logger.print(26, BYTE);
    logger.print(26, BYTE);
    logger.print(26, BYTE);
    // just in case we were already in command mode
    logger.print(13, BYTE);
    delay(wait);
    logger.print("new ");
    logger.print(fname);
    logger.print(13, BYTE);
    delay(wait);
    logger.print("append ");
    logger.print(fname);
    logger.print(13, BYTE);
    delay(wait);
}

unsigned int openlog_read_file(char fname[40], unsigned int offset,
                               unsigned int len, char *buff)
{
    unsigned int rv;
    char in;
    int wait = 50;

    delay(wait);
    logger.print(26, BYTE);
    logger.print(26, BYTE);
    logger.print(26, BYTE);
    // just in case we were already in command mode
    logger.print(13, BYTE);
    delay(wait);
    logger.print("read ");
    logger.print(fname);
    logger.print(" ");
    logger.print(offset);
    logger.print(" ");
    logger.print(len);
    logger.flush();
    //Now send the enter command to OpenLog to actually initiate the read command
    logger.print(13, BYTE);
    delay(wait);

    buff[0] = 0;
    rv = 0;

    while (logger.available() > 0 && rv < len) {

/*    
        // openlog v1 has this problem
        if ( rv == 2 ) {
            // a stupid 13 10 creeps in here
            if ( buff[0] == 13 && buff[1] == 10 )
                rv = 0;
        }
*/
        // openlog v2 has this other problem
        if (rv == 3) {
            // a stupid 13 10 creeps in here
            if (buff[0] == 13 && buff[1] == 13 && buff[2] == 10)
                rv = 0;
        }

        in = logger.read();
        // someone transforms /n into /r/n
        //if (in != 13) {
        buff[rv] = in;
        rv++;
        //}
    }

    return rv;
}

unsigned int openlog_get_fsize(char fname[40])
{
    unsigned int rv = 0;
    int wait = 200;
    char in;

    delay(wait);
    logger.print(26, BYTE);
    logger.print(26, BYTE);
    logger.print(26, BYTE);
    // just in case we were already in command mode
    logger.print(13, BYTE);
    delay(wait);
    logger.print("size ");
    logger.print(fname);
    logger.flush();
    //Now send the enter command to OpenLog to actually initiate the read command
    logger.print(13, BYTE);
    delay(wait);

    while (logger.available() > 0) {
        in = logger.read();
        if (in > 47 && in < 59)
            rv = (rv * 10) + in - 48;
    }

    return rv;
}
