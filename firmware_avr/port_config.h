#ifndef __PORT_CONFIG_H__
#define __PORT_CONFIG_H__

#include <avr/io.h>

// i2c
#define I2C_MASTER_DIR  DDRC
#define I2C_MASTER_OUT  PORTC
#define I2C_MASTER_IN   PINC
#define I2C_MASTER_SCL  0x20
#define I2C_MASTER_SDA  0x10

// infrared RC
#define IR_DDR          DDRD
#define IR_PIN          PIND
#define IR              0x8

#endif
