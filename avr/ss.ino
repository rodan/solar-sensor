
/*

  Solar sensor program that implements the following:

   - read temperature, humidity and geiger counter sensors
   - read and set alarms in the real time clock
   - log sensor data on an microSD card
   - use a communication protocol to send data to a server
   - control room heating or cooling via RF power switches (based on internal temperatures)

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

#include "config.h"

#ifdef IR_REMOTE
#include <IRremote.h>
#include <IRremoteInt.h>
#endif

#ifdef SHT15
#include <Sensirion.h>
#endif

#ifdef MAX6675
#include <max6675.h>
#endif

#ifdef BMP085
#include <bmp085.h>
#endif

#ifdef INTERTECHNO
#include <intertechno.h>
#endif

#include <Wire.h>
#include <ds3231.h>
#include <SPI.h>

#ifdef SDFAT
#include <SdFat.h>
#include <SdFatUtil.h>
#endif

#include "ss.h"

#define SENSOR_ID 1

// XBEE_ACTIVE_LOW must be 1 with the following jumper settings:
//   J2 set, J3 open, J4 set, J5 open 
//            (xbee always receives power, hibernating if ATSM1 set - this is the default state)
//   J2 set, J3 open, J4 open, J5 set 
//            (xbee always receives power, always active if ATSM1 set) 
//
// XBEE_ACTIVE_LOW must be 0 with the following jumper settings:
//   J2 open, J3 set, J4 open, J5 set
//            (xbee receives power only when pin_xbee_ctrl is HIGH)
//
#define XBEE_ACTIVE_LOW "1"

// arduino digital pins
#define pin_counter 2
#define pin_ir 3
#define pin_boost_sw 4
#define pin_rf 5
#define pin_SHT_SCK 8
#define pin_cs_kterm 9

// analog pins
#define pin_xbee_ctrl A2
#define pin_light A3
#define pin_SHT_DATA A4
//SDA   A4
//SCL   A5

// OP_AUTOMATIC - when the device is woken up by the RTC
// OP_MANUAL - when the device is woken up by a human
#define OP_MANUAL 0
#define OP_AUTOMATIC 1
unsigned char op_mode = OP_AUTOMATIC;

// how many minutes to sleep between automatic measurements
// must be an integer between 5 and 59

#ifdef DATALOGGER
unsigned char sleep_period = 1;
#else
unsigned char sleep_period = 30;
#endif

unsigned long stage_prev = 0;
boolean stage_started[7] = { false, false, false, false, false, false, false };

byte stage_num = 0;

// refresh in OP_MANUAL mode
unsigned long refresh_prev = 0;

// not much space for misc buffers
#define BUFF_MAX 64

float ext_temp = 0.0;
float ext_hum = 0.0;
float ext_dew = 0.0;
uint32_t ext_pressure = 0;
uint8_t ext_light = 0;

// Ktherm
float kth_temp = 0.0;

#ifdef BMP085
// bmp085 pressure sensor
struct bmp085 b;
#endif

#ifdef INTERTECHNO
// intertechno rf switch
struct it its;
#endif

// rtc
struct ts t;
float int_temp;

// serial console
char recv[BUFF_MAX];
uint8_t recv_size = 0;

unsigned long shtd_prev = 0;

#ifdef SDFAT
#define BUFF_LOG 64
// uSD
SdFat sd;
SdFile f;
#endif

#ifdef IR_REMOTE
// infrared remote
IRrecv irrecv(pin_ir);
unsigned long result_last = 4294967295UL;       // -1
unsigned int ir_delay = 300;    // delay between repeated button presses
unsigned long ir_delay_prev = 0;
#endif

// counter
boolean counter_state = HIGH;
boolean counter_state_last = HIGH;
volatile unsigned long counter_c = 0;   //counts
unsigned long counter_c_last = 0;       //counts at start of counter_interval
unsigned long counter_cpm;      //counts per interval
//unsigned long counter_interval = 60000;
unsigned long counter_prev = 0;

#define BUFF_OUT 80
char output[BUFF_OUT];

void setup()
{
    pinMode(pin_counter, INPUT);
    pinMode(pin_xbee_ctrl, OUTPUT);
    pinMode(pin_boost_sw, OUTPUT);

    xbee_off();
    Wire.begin();
    TWBR = 0xFF;                // slow down hardware i2c clock
    // since we use longer than recommended wires

    // verify if Alarm2 woked us up  ( status register XXXX XX1X )
    if ((DS3231_get_sreg() & 0x02) == 0) {
        // powered up by manual switch, not by alarm
        op_mode = OP_MANUAL;
        //stage4();
    }
        
    setup_a2();
    
    // this will pull up D9 - needed for later SPI
#ifdef MAX6675
    tk_init(pin_cs_kterm);
#else
    pinMode(pin_cs_kterm, OUTPUT);
    digitalWrite(pin_cs_kterm, HIGH);
#endif

#ifdef SDFAT
    if (!sd.init(SPI_HALF_SPEED))
        sd.initErrorHalt();
#endif

    DS3231_init(0x06);
    Serial.begin(9600);

#ifdef BMP085
    bmp085_init(&b);
#endif

#ifdef INTERTECHNO
    its.pin = pin_rf;
#if defined(F_CPU) && F_CPU == 8000000
    its.rf_cal_on = -7;
    its.rf_cal_off = -7;
#elif defined(F_CPU) && F_CPU == 16000000
    its.rf_cal_on = -1;
    its.rf_cal_off = -1;
#else
    // mkaaaaay
    its.rf_cal_on = -1;
    its.rf_cal_off = -1;
#endif
    pinMode(its.pin, OUTPUT);
    digitalWrite(its.pin, LOW);

#endif

#ifdef IR_REMOTE
    irrecv.enableIRIn();
#endif
    //Serial.print("ram ");
    //Serial.println(FreeRam());
    //Serial.println("?");
}

void loop()
{

    // initial wait, warmup, counter, hum, post wait, sleep
    //unsigned long stage_timing[7] = { 2000, 4000, 60000, 1000, 20000, 5000, (sleep_period * 60000) - 108000 };
#ifdef DATALOGGER
    unsigned long stage_timing[7] = { 2000, 4000, 1, 1000, 20000, 5000, (sleep_period * 60000) - 108000 };
#else
    unsigned long stage_timing[7] = { 2000, 4000, 60000, 1000, 20000, 5000, (sleep_period * 60000) - 108000 };
#endif

    unsigned long now = millis();

#ifdef IR_REMOTE
    ir_decode();
#endif

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
            if (!stage_started[6]
                && ((now - stage_prev > stage_timing[5])
                    && (now - shtd_prev > 20000))) {
                // if there is more than 20s of inactivity, shutdown
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
    } else {                    // manual operation

        read_counter();

        if ((now - counter_prev > 60000)) {
            counter_cpm = counter_c - counter_c_last;
            counter_c_last = counter_c;
            counter_prev = now;
        }
        // once a while (10s) refresh stuff
        if ((now - refresh_prev > 10000)) {
            refresh_prev = now;

            // if the RTC woke up the device in the meantime
            if ((DS3231_get_sreg() & 0x02) == 2) {
                // clear all alarms and thus disable RTC poweron
                DS3231_set_sreg(0x00);
                setup_a2();
            }
        }
    }

}

// IR

#ifdef IR_REMOTE
void ir_decode()
{
    decode_results results;
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
            Serial.print("EHLO s");
            Serial.println(SENSOR_ID);
            break;
/*
        case 36: // red
          break;
        case 35: // green
          break;
        case 14: // yellow
          break; */
        case 12:               // power
            op_mode = OP_MANUAL;
            if ((DS3231_get_sreg() & 0x02) == 2) {
                // clear all alarms and thus disable RTC poweron
                DS3231_set_sreg(0x00);
            }
            break;
/*        case 50: // zoom
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
            rf_tx_cmd(its, 0xb6, INTERTECHNO_CMD_ON);
            break;
        case 29: // ch-
            rf_tx_cmd(its, 0xb6, INTERTECHNO_CMD_OFF);
            break;
*/
        case 36:               // record
            Serial.println("GET time");
            break;
        case 54:               // stop
            xbee_off();
            boost_off();
            break;
        case 14:               // play
            xbee_on();
            boost_on();
            break;
/*        case 31: // pause
            break;
*/
        case 35:               // rew
            stage4();
            break;
/*        case : // fwd
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
#endif

//   sensors

int measure_ext()
{

#ifdef SHT15
    uint16_t sht_raw;
    Sensirion sht = Sensirion(pin_SHT_DATA, pin_SHT_SCK);

    // disable hardware i2c
    TWCR &= ~(1 << 2);
    pinMode(pin_SHT_SCK, OUTPUT);

    sht.meas(TEMP, &sht_raw, BLOCK);
    ext_temp = sht.calcTemp(sht_raw);
    if (ext_temp == -40.1) {
        ext_temp = 0;
        return 1;
    }
    sht.meas(HUMI, &sht_raw, BLOCK);
    ext_hum = sht.calcHumi(sht_raw, ext_temp);
    ext_dew = sht.calcDewpoint(ext_hum, ext_temp);

    TWCR |= (1 << 2);           // enable hardware i2c
    //Wire.begin();
    //TWBR = 0xFF;
#else
    ext_hum = 0;
    ext_temp = 0;
    ext_dew = 0;
#endif

#ifdef BMP085
    b.oss = 3;
    bmp085_read_sensors(&b);
    ext_pressure = b.ppa;
#endif

    ext_light = analogRead(pin_light) / 4;

    return 0;
}

void measure_int()
{
    int_temp = DS3231_get_treg();
#ifdef MAX6675
    kth_temp = 0.25 * tk_get_raw(pin_cs_kterm);
#endif
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

    Serial.println(millis());
    Serial.println("s1");
    boost_on();
}

void stage2()
{
    stage_num = 2;
    //debug_status = "s2";

    Serial.println(millis());
    Serial.println("s2");
    counter_c_last = counter_c;
}

void stage3()
{
    stage_num = 3;
    //debug_status = "s3";
    Serial.println(millis());
    Serial.println("s3");

//  counter_cpm = ( counter_c - counter_c_last ) * 60000.0 / counter_interval;
    counter_cpm = counter_c - counter_c_last;
    boost_off();
    xbee_on();
}

void stage4()
{
    char tmp1[7], tmp2[7], tmp3[7], tmp4[7], tmp5[9];
    stage_num = 4;
    //debug_status = "s4 save";
    Serial.println(millis());
    Serial.println("s4");

    measure_ext();
    measure_int();

    DS3231_get(&t);

    // floats cannot be used as %f in *printf()
    dtostrf(ext_temp, 2, 2, tmp1);
    dtostrf(ext_hum, 2, 2, tmp2);
    dtostrf(ext_dew, 2, 2, tmp3);
    dtostrf(int_temp, 2, 2, tmp4);
    dtostrf(kth_temp, 2, 2, tmp5);

    snprintf(output, BUFF_OUT,
             "s%d %d-%02d-%02dT%02d:%02d:%02d %s %s %s %ld %d %ld %s %s\r\n",
             SENSOR_ID, t.year, t.mon, t.mday, t.hour, t.min, t.sec, tmp1, tmp2,
             tmp3, ext_pressure, ext_light, counter_cpm, tmp4, tmp5);

    if (op_mode == OP_AUTOMATIC) {
#ifdef SDFAT
        char f_name[9];
        snprintf(f_name, 9, "%d%02d%02d", t.year, t.mon, t.mday);
        f.open(f_name, O_RDWR | O_CREAT | O_AT_END);
        f.write(output, strlen(output));
        //if (f.write(output, strlen(output)) != strlen(output)) {
        //error("write failed");
        //}
        f.close();
#endif
    } else {
        Serial.println(output);
    }
}

void stage5()
{
    // try to talk with the server
    stage_num = 5;
    //debug_status = "s5 transf";

#ifdef IR_REMOTE
    // zero out the Timer Interrupt Mask Register TIMSK IRremote is using
    // this will enable much more precise transmission but disable IR commands
    TIMER_DISABLE_INTR;
#endif

    Serial.println("");
    Serial.print("EHLO s");
    Serial.println(SENSOR_ID);
    shtd_prev = millis();
}

void stage6()
{
    stage_num = 6;
    //debug_status = "s6 sleep";

    Serial.println(millis());
    Serial.println("s6");

#ifdef INTERTECHNO
    // home automation
    // use the radio switches to start/stop the fan or the heater

    // june/july/august heat
    if ((t.mon > 5) && (t.mon < 9)) {
        if ((int_temp > 27) && (ext_temp < 24)) {
            // start the fan/AC/chiller
            rf_tx_cmd(its, 0xb7, INTERTECHNO_CMD_ON);
        } else {
            // stop the fan/AC/chiller
            rf_tx_cmd(its, 0xb7, INTERTECHNO_CMD_OFF);
        }
    }

    // nov/dec/jan cold
    if ((t.mon > 10) && (t.mon < 2)) {
        if ((int_temp < 19) && (ext_temp < 10)) {
            // start the heater
            rf_tx_cmd(its, 0xb6, INTERTECHNO_CMD_ON);
        } else {
            // stop the heater
            rf_tx_cmd(its, 0xb6, INTERTECHNO_CMD_OFF);
        }
    }
#endif

    xbee_off();

    setup_a2();

    // clear all alarms and thus disable RTC poweron
    DS3231_set_sreg(0x00);
}

// set Alarm2 to wake up the device
void setup_a2()
{
    unsigned char wakeup_min;
    DS3231_get(&t);
#ifdef DATALOGGER
    wakeup_min = t.min + sleep_period;
#else
    wakeup_min = (t.min / sleep_period + 1) * sleep_period;
#endif

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
    char f_name[9];

#ifdef SDFAT
    int i;
    unsigned int f_size;
    char f_c[BUFF_LOG];
    int rrv = BUFF_LOG;
#endif

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
#ifdef SDFAT
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
                    // xbees start loosing data after the first 2k if no delay is given
                    delay(100);
                }
                console_send_ok();
            } else {
                console_send_err();
            }

            f.close();
#else
            console_send_err();
#endif
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

void boost_on()
{
    digitalWrite(pin_boost_sw, HIGH);
}

void boost_off()
{
    digitalWrite(pin_boost_sw, LOW);
}

void xbee_on()
{
    if (XBEE_ACTIVE_LOW) {
        digitalWrite(pin_xbee_ctrl, LOW);
    } else {
        digitalWrite(pin_xbee_ctrl, HIGH);
    }
}

void xbee_off()
{
    if (XBEE_ACTIVE_LOW) {
        digitalWrite(pin_xbee_ctrl, HIGH);
    } else {
        digitalWrite(pin_xbee_ctrl, LOW);
    }
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
