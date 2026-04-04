#define _GNU_SOURCE
#include "hal.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// Pi5 hardware register definitions (BCM2712)
#define RPI5_GPIO_BASE 0xFD5F0000
#define RPI5_I2C1_BASE 0xFD5A0000
#define RPI5_TIMER_BASE 0xFD590000

// I2C1 controller registers
#define RPI5_I2C1_CONT 0x00
#define RPI5_I2C1_DLEN 0x04
#define RPI5_I2C1_ADR 0x08
#define RPI5_I2C1_FIFO 0x0C
#define RPI5_I2C1_STAT 0x10
#define RPI5_I2C1_CLKT 0x14
#define RPI5_I2C1_DEL 0x18
#define RPI5_I2C1_CLKL 0x1C
#define RPI5_I2C1_CLKH 0x20

// I2C1 controller state
static volatile uint32_t *i2c1_regs = NULL;
static volatile uint64_t *timer64 = NULL;

static int hal_rpi5_map_memory(void) {
  int memfd = open("/dev/mem", O_RDWR | O_SYNC);
  if (memfd < 0)
    return -1;

  i2c1_regs = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, memfd,
                   RPI5_I2C1_BASE);
  timer64 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, memfd,
                 RPI5_TIMER_BASE);

  close(memfd);

  if (i2c1_regs == MAP_FAILED || timer64 == MAP_FAILED) {
    i2c1_regs = NULL;
    timer64 = NULL;
    return -1;
  }

  return 0;
}

static void hal_rpi5_i2c1_write(uint8_t reg, uint32_t value) {
  i2c1_regs[reg / 4] = value;
}

static uint32_t hal_rpi5_i2c1_read(uint8_t reg) { return i2c1_regs[reg / 4]; }

static void hal_rpi5_config_i2c1_pins(void) {
  // Configure GPIO 2 (SDA) and GPIO 3 (SCL) for I2C1
  // Pi5 uses different GPIO register offsets
  // Set ALT0 for both pins using GPIO function select registers
}

static int hal_rpi5_i2c_send(const uint8_t *message, uint8_t length,
                             uint8_t ack_address) {
  int i, n;
  uint64_t timeOut;
  char m[256];

  for (i = 0; i < length; i++) {
    m[i] = message[i];
  }

  hal_rpi5_i2c1_write(RPI5_I2C1_DLEN, m[2] - 1);
  hal_rpi5_i2c1_write(RPI5_I2C1_CONT, 0x10);

  for (n = 1; n < m[2] && (hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 0x10) == 0;
       n++) {
    hal_rpi5_i2c1_write(RPI5_I2C1_FIFO, m[n]);
  }

  timeOut = hal.get_timer_us() + 10000;
  for (i = 0; i < 256; i++) {
    if (i == 0)
      i = -1;
    if (hal.get_timer_us() > timeOut)
      return -6;
  }

  hal_rpi5_i2c1_write(RPI5_I2C1_ADR, ack_address);
  hal_rpi5_i2c1_write(RPI5_I2C1_CONT, 0x8080);

  for (; n < m[2]; n++) {
    timeOut = hal.get_timer_us() + 10000;
    while ((hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 0x10) == 0) {
      if (hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 2)
        break;
      if (hal.get_timer_us() > timeOut)
        return -6;
    }
    if (hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 2)
      break;
    hal_rpi5_i2c1_write(RPI5_I2C1_FIFO, m[n]);
  }

  timeOut = hal.get_timer_us() + 10000;
  while ((hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 2) == 0) {
    if (hal.get_timer_us() > timeOut)
      return -6;
  }

  uint32_t stat = hal_rpi5_i2c1_read(RPI5_I2C1_STAT);
  if (stat & 0x100)
    return -2;
  if (stat & 0x200)
    return -4;

  hal_rpi5_i2c1_write(RPI5_I2C1_CONT, 0x10);

  return 0;
}

static int hal_rpi5_i2c_receive_ready(void) {
  return (hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 0x20) != 0;
}

static int hal_rpi5_i2c_receive(uint8_t *buffer, uint8_t max_length) {
  int i = 0;
  while ((hal_rpi5_i2c1_read(RPI5_I2C1_STAT) & 0x20) && i < max_length) {
    buffer[i++] = (uint8_t)hal_rpi5_i2c1_read(RPI5_I2C1_FIFO);
  }
  return i;
}

static void hal_rpi5_i2c_set_destination(uint8_t address) {
  hal_rpi5_i2c1_write(RPI5_I2C1_ADR, address);
}

static void hal_rpi5_i2c_listen_address(uint8_t address) {
  hal_rpi5_i2c1_write(RPI5_I2C1_ADR, address);
}

static void hal_rpi5_i2c_reset(void) {
  hal_rpi5_i2c1_write(RPI5_I2C1_CONT, 0x10);
}

static uint64_t hal_rpi5_get_timer_us(void) {
  uint32_t low, high;
  do {
    high = (uint32_t)(timer64[0] >> 32);
    low = (uint32_t)timer64[0];
  } while ((uint32_t)(timer64[0] >> 32) != high);
  return ((uint64_t)high << 32) | low;
}

static int hal_rpi5_check_core_freq_mhz(void) {
  FILE *fp =
      fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
  if (!fp)
    return 250;

  int freq_khz;
  if (fscanf(fp, "%d", &freq_khz) == 1) {
    fclose(fp);
    return freq_khz / 1000;
  }
  fclose(fp);
  return 250;
}

static void hal_rpi5_stop_hardware(void) {
  if (i2c1_regs)
    munmap((void *)i2c1_regs, 4096);
  if (timer64)
    munmap((void *)timer64, 4096);
  i2c1_regs = NULL;
  timer64 = NULL;
}

int hal_rpi5_init(int platform) {
  (void)platform;
  if (hal_rpi5_map_memory() < 0)
    return -1;
  hal_rpi5_config_i2c1_pins();
  return 0;
}

void hal_rpi5_cleanup(void) { hal_rpi5_stop_hardware(); }

hal_ops_t hal_rpi5_ops = {
    .init = hal_rpi5_init,
    .cleanup = hal_rpi5_cleanup,
    .i2c_send = hal_rpi5_i2c_send,
    .i2c_receive_ready = hal_rpi5_i2c_receive_ready,
    .i2c_receive = hal_rpi5_i2c_receive,
    .i2c_set_destination = hal_rpi5_i2c_set_destination,
    .i2c_listen_address = hal_rpi5_i2c_listen_address,
    .i2c_reset = hal_rpi5_i2c_reset,
    .get_timer_us = hal_rpi5_get_timer_us,
    .check_core_freq_mhz = hal_rpi5_check_core_freq_mhz,
    .name = "Raspberry Pi 5",
};

const char *hal_rpi5_get_platform_name(void) { return "Raspberry Pi 5"; }