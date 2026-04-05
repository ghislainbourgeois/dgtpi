#include "clock_proto.h"
#include "dgtpicom_dgt3000.h"
#include "hal.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// Forward declarations
static int checkCoreFreq(void);
static uint64_t hal_rpi_get_timer_us(void);
static void i2cDestination(char addr);
static void i2cListenAddress(char addr);
static int i2cReadyToRead(void);
static int i2cReceive(char m[]);
static void i2cReset(void);

// Hardware register pointers (from rpi.c)
volatile unsigned *gpio, *gpioset, *gpioclr, *gpioin;
volatile unsigned *i2cSlave, *i2cSlaveRSR, *i2cSlaveSLV, *i2cSlaveCR,
    *i2cSlaveFR;
volatile unsigned *i2cMaster, *i2cMasterS, *i2cMasterDLEN, *i2cMasterA,
    *i2cMasterFIFO, *i2cMasterDiv, *i2cMasterDel;
uint32_t *timerh;
uint32_t *timerl;
char piModel;

#define GPIO_BASE 0x200000
#define TIMER_BASE 0x003000
#define I2C_SLAVE_BASE 0x214000
#define I2C_MASTER_BASE 0x804000

#define SDA1IN ((*gpioin >> 2) & 1)
#define SCL1IN ((*gpioin >> 3) & 1)

static unsigned int dummyRead(volatile unsigned int *addr) { return *addr; }

void i2cReset() {
  int freq;

  *i2cSlaveCR = 0;
  *i2cMaster = 0x10;
  *i2cMaster = 0x0000;

  *gpio &= 0xfffff03f;
  if (piModel == 4) {
    *(gpio + 1) &= 0xffffffc0;
  } else {
    *(gpio + 1) &= 0xc0ffffff;
  }
  *i2cMasterDLEN = 0;
  while ((*i2cSlaveFR & 2) == 0) {
    dummyRead(i2cSlave);
  }
  usleep(2000);
  *i2cSlaveCR = 0x285;
  *i2cMasterS = 0x302;
  *i2cMaster = 0x8010;
  *gpio |= 0x900;
  if (piModel == 4) {
    *(gpio + 1) |= 0x0000003f;
  } else {
    *(gpio + 1) |= 0x3f000000;
  }

  usleep(1000);

  *i2cSlaveCR = 0x80;
  *i2cSlaveCR = 0x205;
  *i2cSlaveSLV = 0x0;
  *i2cSlaveRSR = 0;

  freq = checkCoreFreq();
  *i2cMasterDiv = 1000 * freq / 95;
  if (freq > 300)
    *i2cMasterDel = 0x600060;
}

int initHw(int platform) {
  int memfd;
  uint32_t base;
  void *gpio_map, *timer_map, *i2c_slave_map, *i2c_master_map;

  piModel = platform;
  if (piModel == 4)
    base = 0xfe000000;
  else if (piModel == 1)
    base = 0x20000000;
  else
    base = 0x3f000000;

  memfd = open("/dev/mem", O_RDWR | O_SYNC);
  if (memfd < 0) {
    return ERROR_MEM;
  }

  gpio_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, memfd,
                  GPIO_BASE + base);
  timer_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, memfd,
                   TIMER_BASE + base);
  i2c_slave_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, memfd,
                       I2C_SLAVE_BASE + base);
  i2c_master_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, memfd,
                        I2C_MASTER_BASE + base);

  close(memfd);

  if (gpio_map == MAP_FAILED || timer_map == MAP_FAILED ||
      i2c_slave_map == MAP_FAILED || i2c_master_map == MAP_FAILED) {
    return ERROR_MEM;
  }

  gpio = (volatile unsigned *)gpio_map;
  gpioset = gpio + 7;
  gpioclr = gpio + 10;
  gpioin = gpio + 13;

  timerh = (uint32_t *)((char *)timer_map + 4);
  timerl = (uint32_t *)((char *)timer_map + 8);

  i2cSlave = (volatile unsigned *)i2c_slave_map;
  i2cSlaveRSR = i2cSlave + 1;
  i2cSlaveSLV = i2cSlave + 2;
  i2cSlaveCR = i2cSlave + 3;
  i2cSlaveFR = i2cSlave + 4;

  i2cMaster = (volatile unsigned *)i2c_master_map;
  i2cMasterS = i2cMaster + 1;
  i2cMasterDLEN = i2cMaster + 2;
  i2cMasterA = i2cMaster + 3;
  i2cMasterFIFO = i2cMaster + 4;
  i2cMasterDiv = i2cMaster + 5;
  i2cMasterDel = i2cMaster + 6;

  if ((*gpio & 0x1c0) == 0x40) {
    return ERROR_LINES;
  }
  if ((*gpio & 0xe00) == 0x200) {
    return ERROR_LINES;
  }
  if (piModel == 4) {
    if ((*(gpio + 1) & 0x07) == 0x01) {
      return ERROR_LINES;
    }
    if ((*(gpio + 1) & 0x38) == 0x08) {
      return ERROR_LINES;
    }
  } else {
    if ((*(gpio + 1) & 0x07000000) == 0x01000000) {
      return ERROR_LINES;
    }
    if ((*(gpio + 1) & 0x38000000) == 0x08000000) {
      return ERROR_LINES;
    }
  }
  *gpio &= 0xfffff03f;
  if (piModel == 4) {
    *(gpio + 1) &= 0xffffffc0;
  } else {
    *(gpio + 1) &= 0xc0ffffff;
  }
  usleep(1);
  if (piModel == 4) {
    if ((*gpioin & 0x0c0c) != 0x0c0c) {
      return ERROR_LINES;
    }
  } else {
    if ((*gpioin & 0xc000c) != 0xc000c) {
      return ERROR_LINES;
    }
  }

  i2cReset();
  i2cDestination(0x08);
  return ERROR_OK;
}

void stopHw() {
  *i2cSlaveCR = 0;

  *gpio &= 0xfffff03f;
  if (piModel == 4) {
    *(gpio + 1) &= 0xffffffc0;
  } else {
    *(gpio + 1) &= 0xc0ffffff;
  }
}

void i2cDestination(char addr) { *i2cMasterA = addr; }

void i2cListenAddress(char addr) { *i2cSlaveSLV = addr; }

int i2cReadyToRead() {
  if ((*i2cSlaveFR & 0x20) != 0 || (*i2cSlaveFR & 2) == 0) {
    return 1;
  }
  return 0;
}

int i2cReceive(char m[]) {
  int i = 1;
  uint64_t timeOut;

  m[0] = *i2cSlaveSLV * 2;

  timeOut = hal_rpi_get_timer_us() + 10000;

  while (((*i2cSlaveFR & 0x20) != 0) || ((*i2cSlaveFR & 2) == 0)) {

    if (timeOut < hal_rpi_get_timer_us()) {
      return ERROR_TIMEOUT;
    }

    if ((*i2cSlaveFR & 2) == 0) {
      m[i] = *i2cSlave & 0xff;
      i++;
      if (i > 2 && i >= m[2])
        break;
      if (i >= RECEIVE_BUFFER_LENGTH) {
        return ERROR_SWB_FULL;
      }
    } else {
      usleep(10);
    }
  }

  *i2cSlaveSLV = 0x00;

  m[i] = -1;

  if (i == 1)
    return ERROR_OK;

  if (i == 3 && m[1] == 0 && m[2] == 0)
    return ERROR_OK;

  if (m[1] != 16) {
    return ERROR_NACK;
  }

  if (*i2cSlaveRSR & 1 || i < 5 || i != m[2]) {
    *i2cSlaveRSR = 0;
    return ERROR_HWB_FULL;
  }

  if (crc_calc(m)) {
    return ERROR_CRC;
  }

  return i;
}

int checkCoreFreq() {
  FILE *fp;
  char line[100];

  fp = popen("vcgencmd measure_clock core", "r");
  if (fp == NULL) {
    return 250;
  }

  fgets(line, sizeof(line), fp);

  pclose(fp);

  return atoi(line + 13) / 1000000;
}

static uint64_t hal_rpi_get_timer_us(void) {
  static uint64_t val;
  val = ((uint64_t)*timerl << 32) + *timerh;
  return val;
}

// Copy of i2cSend from dgtpicom.c (line 843-1007)
// This is the complex I2C send implementation
static int hal_rpi_i2c_send(const uint8_t *message, uint8_t msg_length,
                            uint8_t ack_address) {
  int i, n;
  uint64_t timeOut;
  char m[256];

  // Convert uint8_t to char for compatibility
  for (i = 0; i < msg_length; i++) {
    m[i] = message[i];
  }

  // set length
  *i2cMasterDLEN = m[2] - 1;

  // clear buffer
  *i2cMaster = 0x10;

  // fill the buffer
  for (n = 1; n < m[2] && *i2cMasterS & 0x10; n++) {
    *i2cMasterFIFO = m[n];
  }

  // check 256 times if the bus is free
  timeOut = hal_rpi_get_timer_us() + 10000;
  for (i = 0; i < 256; i++) {
    if ((SCL1IN == 0) || (SDA1IN == 0)) {
      i = 0;
    }
    if (((*i2cSlaveFR & 0x20) != 0) || ((*i2cSlaveFR & 2) == 0)) {
      i = 0;
    }
    if (hal_rpi_get_timer_us() > timeOut) {
      return ERROR_TIMEOUT;
    }
  }
  pthread_mutex_lock(&receiveMutex);

  // clear ack and hello so we can receive a new ack or hello
  dgtRx.ack[0] = 0;
  dgtRx.hello = 0;

  // listen to ack adress
  *i2cSlaveSLV = ack_address;

  // start sending
  *i2cMasterS = 0x302;
  *i2cMaster = 0x8080;

  // write the rest of the message
  for (; n < m[2]; n++) {
    timeOut = hal_rpi_get_timer_us() + 10000;
    while ((*i2cMasterS & 0x10) == 0) {
      if (*i2cMasterS & 2) {
        *i2cSlaveSLV = 0x00;
        break;
      }
      if (hal_rpi_get_timer_us() > timeOut) {
        *i2cSlaveSLV = 0x00;
        pthread_mutex_unlock(&receiveMutex);
        return ERROR_TIMEOUT;
      }
    }
    if (*i2cMasterS & 2)
      break;
    *i2cMasterFIFO = m[n];
  }

  // wait for done
  timeOut = hal_rpi_get_timer_us() + 10000;
  while ((*i2cMasterS & 2) == 0)
    if (hal_rpi_get_timer_us() > timeOut) {
      *i2cSlaveSLV = 0x00;
      pthread_mutex_unlock(&receiveMutex);
      return ERROR_TIMEOUT;
    }

  // succes?
  if ((*i2cMasterS & 0x300) == 0) {
    pthread_mutex_unlock(&receiveMutex);
    return ERROR_OK;
  }

  *i2cSlaveSLV = 0x00;

  // collision or clock off
  if (*i2cMasterS & 0x100) {
    *i2cMasterS = 0x100;
  }
  if (*i2cMasterS & 0x200) {
    *i2cMasterS = 0x200;
    pthread_mutex_unlock(&receiveMutex);
    return ERROR_CST;
  }

  // clear fifo
  *i2cMaster |= 0x10;

  if ((SCL1IN == 0) || (SDA1IN == 0) || ((*i2cSlaveFR & 0x20) != 0) ||
      ((*i2cSlaveFR & 2) == 0)) {
    pthread_mutex_unlock(&receiveMutex);
    return ERROR_LINES;
  }

  pthread_mutex_unlock(&receiveMutex);
  return ERROR_SILENT;
}

static int hal_rpi_i2c_receive_ready(void) { return i2cReadyToRead(); }

static int hal_rpi_i2c_receive(uint8_t *buffer, uint8_t max_length) {
  return i2cReceive((char *)buffer);
}

static void hal_rpi_i2c_set_destination(uint8_t address) {
  i2cDestination(address);
}

static void hal_rpi_i2c_listen_address(uint8_t address) {
  i2cListenAddress(address);
}

static void hal_rpi_i2c_reset(void) { i2cReset(); }

static int hal_rpi_check_core_freq_mhz(void) { return checkCoreFreq(); }

// Export HAL instance for runtime detection (Pi1-Pi4)
hal_ops_t hal_rpi_stubs_ops = {
    .init = initHw,
    .cleanup = stopHw,
    .i2c_send = hal_rpi_i2c_send,
    .i2c_receive_ready = hal_rpi_i2c_receive_ready,
    .i2c_receive = hal_rpi_i2c_receive,
    .i2c_set_destination = hal_rpi_i2c_set_destination,
    .i2c_listen_address = hal_rpi_i2c_listen_address,
    .i2c_reset = hal_rpi_i2c_reset,
    .get_timer_us = hal_rpi_get_timer_us,
    .check_core_freq_mhz = hal_rpi_check_core_freq_mhz,
    .name = "Raspberry Pi (1-4)",
};
