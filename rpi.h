#ifndef RPI_H
#define RPI_H

#include "dgtpicom_dgt3000.h"
#include <stdint.h>

#define GPIO_BASE 0x200000
#define TIMER_BASE 0x003000
#define I2C_SLAVE_BASE 0x214000
#define I2C_MASTER_BASE 0x804000

// pointers to BCM2708/9 registers
extern volatile unsigned *gpio, *gpioset, *gpioclr, *gpioin;
extern volatile unsigned *i2cSlave, *i2cSlaveRSR, *i2cSlaveSLV, *i2cSlaveCR,
    *i2cSlaveFR;
extern volatile unsigned *i2cMaster, *i2cMasterS, *i2cMasterDLEN, *i2cMasterA,
    *i2cMasterFIFO, *i2cMasterDiv, *i2cMasterDel;

#define SDA1IN ((*gpioin >> 2) & 1) // SDA1 = GPIO 2
#define SCL1IN ((*gpioin >> 3) & 1) // SCL1 = GPIO 3

void setPiModel(int model);
int initHw();

void stopHw();

void i2cDestination(char);

void i2cListenAddress(char);

int i2cReadyToRead();

/* get message from I2C receive buffer
        m[] = message buffer of 256 bytes
        timeOut = time to wait for packet in us (0=dont wait)
        returns:
        -6 = CRC Error
        -5 = I2C buffer overrun, at least 16 bytes received succesfully. rest is
   missing. -4 = our buffer overrun (should not happen) -3 = timeout -2 = I2C
   Error >0 = packet length*/
int i2cReceive(char m[]);

/* configure IO pins and I2C Master and Slave
 */
void i2cReset();

int checkCoreFreq();

uint64_t *timer();

#endif
