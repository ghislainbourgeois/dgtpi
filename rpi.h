#ifndef RPI_H
#define RPI_H

#define GPIO_BASE  0x200000
#define TIMER_BASE 0x003000
#define I2C_SLAVE_BASE 0x214000
#define I2C_MASTER_BASE 0x804000

// pointers to BCM2708/9 registers
volatile unsigned *gpio, *gpioset, *gpioclr, *gpioin;
volatile unsigned *i2cSlave, *i2cSlaveRSR, *i2cSlaveSLV, *i2cSlaveCR, *i2cSlaveFR;
volatile unsigned *i2cMaster, *i2cMasterS, *i2cMasterDLEN, *i2cMasterA, *i2cMasterFIFO, *i2cMasterDiv, *i2cMasterDel;
uint32_t *timerh;
uint32_t *timerl;

#define SDA1IN ((*gpioin >> 2) & 1)    // SDA1 = GPIO 2
#define SCL1IN ((*gpioin >> 3) & 1)    // SCL1 = GPIO 3

// enable debug pins
#ifdef debug
#define WAIT_FOR_FREE_BUS_PIN_HI *gpioset = (1 << 17)  // GPIO 17
#define WAIT_FOR_FREE_BUS_PIN_LO *gpioclr = (1 << 17)
#define RECEIVE_THREAD_RUNNING_PIN_HI *gpioset = (1 << 27)  // GPIO 27
#define RECEIVE_THREAD_RUNNING_PIN_LO *gpioclr = (1 << 27)
#define ERROR_PIN_HI *gpioset = (1 << 22)  // GPIO 22
#define ERROR_PIN_LO *gpioclr = (1 << 22)
#endif

#endif
