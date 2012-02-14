
/*

  Solar sensor program that implements the following:

   - read temperature, humidity and geiger counter sensors
   - read and set alarms in the real time clock
   - log sensor data on an microSD card
   - use a communication protocol to send data to a server

  see the README file for more info.

  Author:          Petre Rodan <petre.rodan@simplex.ro>
  Revision:        03
  Available from:  https://github.com/rodan/solar-sensor
  License:         GNU GPLv3

  GNU GPLv3 license:
  
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
   
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
   
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
   
*/

#include <SoftwareSerial.h>
#include <IRremote.h>
#include <Sensirion.h>
#include <Wire.h>
#include <ds3231.h>
#include <SPI.h>
#include <SdFat.h>
#include <SdFatUtil.h>

#include "ss.h"

#define SENSOR_ID "EHLO s1"

// arduino digital pins
#define pin_ir 2
#define pin_counter 3
#define pin_SHT_DATA 4
#define pin_SHT_SCK 5
#define pin_log_rx 6
#define pin_log_tx 7

// analog pins
// rtc uses A5 for SCL and A4 for SDA
#define pin_wireless_hib A2
#define pin_light A3

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

// uSD
#define BUFF_LOG 64
SdFat sd;
SdFile f;

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
    pinMode(pin_wireless_hib, OUTPUT);

    // verify if Alarm2 woked us up  ( status register XXXX XX1X )
    if ((DS3231_get_sreg() & 0x02) == 0) {
        // powered up by manual switch, not by alarm
        op_mode = OP_MANUAL;
        stage4();
        setup_a2();
    }

    DS3231_init(0x06);
    Serial.begin(9600);

    // change to SPI_FULL_SPEED on proper PCB setup
    if (!sd.init(SPI_HALF_SPEED)) sd.initErrorHalt();

    wireless_off();
    //led_off();
    irrecv.enableIRIn();
    //Serial.print("ram ");
    //Serial.println(FreeRam());
    //Serial.println("?");
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
    char f_name[9];
    char tmp1[7], tmp2[7], tmp3[7], tmp4[7];
    stage_num = 4;
    //debug_status = "s4 save";

    //led_on();

    measure_ext();
    measure_int();

    DS3231_get(&t);

    snprintf(output, BUFF_OUT,
             "%d-%02d-%02dT%02d:%02d:%02d %sC %s%% %sC %dL %sC %dcpm\r\n", t.year,
             t.mon, t.mday, t.hour, t.min, t.sec, dtostrf(ext_temp, 2, 2, tmp1),
             dtostrf(ext_hum, 2, 2, tmp2), dtostrf(ext_dew, 2, 2, tmp3),
             ext_light, dtostrf(int_temp, 2, 2, tmp4), counter_cpm);

    if ( op_mode == OP_AUTOMATIC ) {
        snprintf(f_name, 9, "%d%02d%02d", t.year, t.mon, t.mday);
        f.open(f_name, O_RDWR | O_CREAT | O_AT_END);
        if (f.write(output, sizeof(output)) != sizeof(output)) {
            //error("write failed");
        }
        f.close();
    }
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

    setup_a2();

    // clear all alarms and thus go to sleep
    DS3231_set_sreg(0x00);
    //system_sleep();

}

// set Alarm2 to wake up the device
void setup_a2()
{
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
    int rrv = BUFF_LOG;
    char f_name[9];
    //unsigned int f_pos = 0;
    unsigned int f_size;
    //unsigned int f_buffsize = BUFF_LOG;

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
            f.open(f_name, O_READ);
            f_size = f.fileSize();
            Serial.print("LEN ");
            Serial.print(f_name);
            Serial.print(" ");
            Serial.println(f_size);

            if (f_size > 0) {
                while (rrv == BUFF_LOG) {
                    rrv = f.read(&f_c[0], BUFF_LOG);
                    for (i = 0; i < rrv; i++) {
                        Serial.print(f_c[i]);
                    }
                }
                console_send_ok();
            } else {
                console_send_err();
            }

            f.close();
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

/*
int open_file(char fname[10])
{
    //int wait = 200;
    //char fname[9];

    //DS3231_get(&t);
    //snprintf(fname, 9, "%d%02d%02d", t.year, t.mon, t.mday);

    // open the file for write at end like the Native SD library
    return f.open(fname, O_RDWR | O_CREAT | O_AT_END);
}
*/
