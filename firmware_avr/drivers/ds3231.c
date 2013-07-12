
/*
  DS3231 library for the Arduino.

  This library implements the following features:

   - read/write of current time, both of the alarms, 
   control/status registers, aging register
   - read of the temperature register, and of any address from the chip.

  Author:          Petre Rodan <petre.rodan@simplex.ro>
  Available from:  https://github.com/rodan/ds3231
 
  The DS3231 is a low-cost, extremely accurate I2C real-time clock 
  (RTC) with an integrated temperature-compensated crystal oscillator 
  (TCXO) and crystal.

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

#include <stdio.h>
#include "ds3231.h"
#include "serial_bitbang.h"

/* control register 0Eh/8Eh
bit7 EOSC   Enable Oscillator (1 if oscillator must be stopped when on battery)
bit6 BBSQW  Battery Backed Square Wave
bit5 CONV   Convert temperature (1 forces a conversion NOW)
bit4 RS2    Rate select - frequency of square wave output
bit3 RS1    Rate select
bit2 INTCN  Interrupt control (1 for use of the alarms and to disable square wave)
bit1 A2IE   Alarm2 interrupt enable (1 to enable)
bit0 A1IE   Alarm1 interrupt enable (1 to enable)
*/

void DS3231_init(const uint8_t ctrl_reg)
{
    DS3231_set_creg(ctrl_reg);
}

void DS3231_set(struct ts t)
{
    uint8_t i, century;

    if (t.year > 2000) {
        century = 0x80;
        t.year_s = t.year - 2000;
    } else {
        century = 0;
        t.year_s = t.year - 1900;
    }

    uint8_t TimeDate[7] = { t.sec, t.min, t.hour, t.wday, t.mday, t.mon, t.year_s };

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx((uint8_t) DS3231_TIME_CAL_ADDR, I2C_NO_ADDR_SHIFT);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_TIME_CAL_ADDR);
    for (i = 0; i <= 6; i++) {
        TimeDate[i] = dectobcd(TimeDate[i]);
        if (i == 5)
            TimeDate[5] += century;
        i2cm_tx(TimeDate[i], I2C_NO_ADDR_SHIFT);
        //Wire.write(TimeDate[i]);
    }
    //Wire.endTransmission();
    i2cm_stop();
}

void DS3231_get(struct ts *t)
{
    uint8_t TimeDate[7];        //second,minute,hour,dow,day,month,year
    uint8_t century = 0;
    uint8_t i, n[7];
    uint16_t year_full;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx((uint8_t) DS3231_TIME_CAL_ADDR, I2C_NO_ADDR_SHIFT);
    i2cm_stop();

    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_TIME_CAL_ADDR);
    //Wire.endTransmission();

    i2cm_rxfrom((uint8_t) DS3231_I2C_ADDR, n, (uint8_t) 7);
    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 7);

    for (i = 0; i <= 6; i++) {
        //n = Wire.read();
        if (i == 5) {
            TimeDate[5] = bcdtodec(n[i] & 0x1F);
            century = (n[i] & 0x80) >> 7;
        } else
            TimeDate[i] = bcdtodec(n[i]);
    }

    if (century == 1)
        year_full = 2000 + TimeDate[6];
    else
        year_full = 1900 + TimeDate[6];

    t->sec = TimeDate[0];
    t->min = TimeDate[1];
    t->hour = TimeDate[2];
    t->mday = TimeDate[4];
    t->mon = TimeDate[5];
    t->year = year_full;
    t->wday = TimeDate[3];
    t->year_s = TimeDate[6];
}

void DS3231_set_addr(const uint8_t addr, const uint8_t val)
{
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write(addr);
    //Wire.write(val);
    //Wire.endTransmission();
    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(addr, I2C_NO_ADDR_SHIFT);
    i2cm_tx(val, I2C_NO_ADDR_SHIFT);
    i2cm_stop();
}

uint8_t DS3231_get_addr(const uint8_t addr)
{
    uint8_t rv;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(addr, I2C_NO_ADDR_SHIFT);
    i2cm_stop();

    i2cm_rxfrom(DS3231_I2C_ADDR, &rv, 1);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) addr);
    //Wire.endTransmission();

    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 1);
    //rv = Wire.read();

    return rv;
}



// control register

void DS3231_set_creg(const uint8_t val)
{
    DS3231_set_addr(DS3231_CONTROL_ADDR, val);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_CONTROL_ADDR);
    //Wire.write(val);
    //Wire.endTransmission();
}

// status register 0Fh/8Fh

/*
bit7 OSF      Oscillator Stop Flag (if 1 then oscillator has stopped and date might be innacurate)
bit3 EN32kHz  Enable 32kHz output (1 if needed)
bit2 BSY      Busy with TCXO functions
bit1 A2F      Alarm 2 Flag - (1 if alarm2 was triggered)
bit0 A1F      Alarm 1 Flag - (1 if alarm1 was triggered)
*/

void DS3231_set_sreg(const uint8_t val)
{
    DS3231_set_addr(DS3231_STATUS_ADDR, val);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_STATUS_ADDR);
    //Wire.write(val);
    //Wire.endTransmission();
}

uint8_t DS3231_get_sreg(void)
{
    uint8_t rv;

    rv = DS3231_get_addr(DS3231_STATUS_ADDR);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_STATUS_ADDR);
    //Wire.endTransmission();

    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 1);
    //rv = Wire.read();

    return rv;
}

// aging register

void DS3231_set_aging(const int8_t val)
{
    uint8_t reg;

    if (val >= 0)
        reg = val;
    else
        reg = ~(-val) + 1;      // 2C

    DS3231_set_addr(DS3231_AGING_OFFSET_ADDR, reg);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_AGING_OFFSET_ADDR);
    //Wire.write(reg);
    //Wire.endTransmission();
}

int8_t DS3231_get_aging(void)
{
    uint8_t reg;
    int8_t rv;

    reg = DS3231_get_addr(DS3231_AGING_OFFSET_ADDR);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_AGING_OFFSET_ADDR);
    //Wire.endTransmission();

    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 1);
    //reg = Wire.read();

    if ((reg & 0x80) != 0)
        rv = reg | ~((1 << 8) - 1);     // if negative get two's complement
    else
        rv = reg;

    return rv;
}

// temperature register

float DS3231_get_treg()
{
    float rv;
    uint8_t temp_msb, temp_lsb, n[2];
    int8_t nint;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(DS3231_TEMPERATURE_ADDR, I2C_NO_ADDR_SHIFT);
    i2cm_stop();

    i2cm_rxfrom(DS3231_I2C_ADDR, n, 2);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_TEMPERATURE_ADDR);
    //Wire.endTransmission();

    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 2);
    temp_msb = n[0];
    temp_lsb = n[1] >> 6;

    if ((temp_msb & 0x80) != 0)
        nint = temp_msb | ~((1 << 8) - 1);      // if negative get two's complement
    else
        nint = temp_msb;

    rv = 0.25 * temp_lsb + nint;

    return rv;
}

// alarms

// flags are: A1M1 (seconds), A1M2 (minutes), A1M3 (hour), 
// A1M4 (day) 0 to enable, 1 to disable, DY/DT (dayofweek == 1/dayofmonth == 0)
void DS3231_set_a1(const uint8_t s, const uint8_t mi, const uint8_t h, const uint8_t d, const uint8_t * flags)
{
    uint8_t t[4] = { s, mi, h, d };
    uint8_t i;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(DS3231_ALARM1_ADDR, I2C_NO_ADDR_SHIFT);

    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_ALARM1_ADDR);

    for (i = 0; i <= 3; i++) {
        if (i == 3) {
            i2cm_tx(dectobcd(t[3]) | (flags[3] << 7) | (flags[4] << 6), I2C_NO_ADDR_SHIFT);
            //Wire.write(dectobcd(t[3]) | (flags[3] << 7) | (flags[4] << 6));
        } else
            i2cm_tx(dectobcd(t[i]) | (flags[i] << 7), I2C_NO_ADDR_SHIFT);
            //Wire.write(dectobcd(t[i]) | (flags[i] << 7));
    }

    i2cm_stop();
    //Wire.endTransmission();
}

void DS3231_get_a1(char *buf, const uint8_t len)
{
    uint8_t n[4];
    uint8_t t[4];               //second,minute,hour,day
    uint8_t f[5];               // flags
    uint8_t i;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(DS3231_ALARM1_ADDR, I2C_NO_ADDR_SHIFT);
    i2cm_stop();

    i2cm_rxfrom(DS3231_I2C_ADDR, n, 4);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_ALARM1_ADDR);
    //Wire.endTransmission();

    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 4);

    for (i = 0; i <= 3; i++) {
        //n[i] = Wire.read();
        f[i] = (n[i] & 0x80) >> 7;
        t[i] = bcdtodec(n[i] & 0x7F);
    }

    f[4] = (n[3] & 0x40) >> 6;
    t[3] = bcdtodec(n[3] & 0x3F);

    snprintf(buf, len,
             "s%02d m%02d h%02d d%02d fs%d m%d h%d d%d wm%d %d %d %d %d",
             t[0], t[1], t[2], t[3], f[0], f[1], f[2], f[3], f[4], n[0],
             n[1], n[2], n[3]);

}

// when the alarm flag is cleared the pulldown on INT is also released
void DS3231_clear_a1f(void)
{
    uint8_t reg_val;

    reg_val = DS3231_get_sreg() & ~DS3231_A1F;
    DS3231_set_sreg(reg_val);
}

uint8_t DS3231_triggered_a1(void)
{
    return  DS3231_get_sreg() & DS3231_A1F;
}

// flags are: A2M2 (minutes), A2M3 (hour), A2M4 (day) 0 to enable, 1 to disable, DY/DT (dayofweek == 1/dayofmonth == 0) - 
void DS3231_set_a2(const uint8_t mi, const uint8_t h, const uint8_t d, const uint8_t * flags)
{
    uint8_t t[3] = { mi, h, d };
    uint8_t i;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(DS3231_ALARM2_ADDR, I2C_NO_ADDR_SHIFT);
    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_ALARM2_ADDR);

    for (i = 0; i <= 2; i++) {
        if (i == 2) {
            //Wire.write(dectobcd(t[2]) | (flags[2] << 7) | (flags[3] << 6));
            i2cm_tx(dectobcd(t[2]) | (flags[2] << 7) | (flags[3] << 6), I2C_NO_ADDR_SHIFT);
        } else
            i2cm_tx(dectobcd(t[i]) | (flags[i] << 7), I2C_NO_ADDR_SHIFT);
            //Wire.write(dectobcd(t[i]) | (flags[i] << 7));
    }

    i2cm_stop();
    //Wire.endTransmission();
}

void DS3231_get_a2(char *buf, const uint8_t len)
{
    uint8_t n[3];
    uint8_t t[3];               //second,minute,hour,day
    uint8_t f[4];               // flags
    uint8_t i;

    i2cm_start();
    i2cm_tx(DS3231_I2C_ADDR, I2C_WRITE);
    i2cm_tx(DS3231_ALARM2_ADDR, I2C_NO_ADDR_SHIFT);
    i2cm_stop();

    i2cm_rxfrom(DS3231_I2C_ADDR, n, 3);

    //Wire.beginTransmission((uint8_t) DS3231_I2C_ADDR);
    //Wire.write((uint8_t) DS3231_ALARM2_ADDR);
    //Wire.endTransmission();

    //Wire.requestFrom((uint8_t) DS3231_I2C_ADDR, (uint8_t) 3);

    for (i = 0; i <= 2; i++) {
        //n[i] = Wire.read();
        f[i] = (n[i] & 0x80) >> 7;
        t[i] = bcdtodec(n[i] & 0x7F);
    }

    f[3] = (n[2] & 0x40) >> 6;
    t[2] = bcdtodec(n[2] & 0x3F);

    snprintf(buf, len, "m%02d h%02d d%02d fm%d h%d d%d wm%d %d %d %d", t[0],
             t[1], t[2], f[0], f[1], f[2], f[3], n[0], n[1], n[2]);

}

// when the alarm flag is cleared the pulldown on INT is also released
void DS3231_clear_a2f(void)
{
    uint8_t reg_val;

    reg_val = DS3231_get_sreg() & ~DS3231_A2F;
    DS3231_set_sreg(reg_val);
}

uint8_t DS3231_triggered_a2(void)
{
    return  DS3231_get_sreg() & DS3231_A2F;
}

// helpers

uint8_t dectobcd(const uint8_t val)
{
    return ((val / 10 * 16) + (val % 10));
}

uint8_t bcdtodec(const uint8_t val)
{
    return ((val / 16 * 10) + (val % 16));
}

uint8_t inp2toi(char *cmd, const uint16_t seek)
{
    uint8_t rv;
    rv = (cmd[seek] - 48) * 10 + cmd[seek + 1] - 48;
    return rv;
}

