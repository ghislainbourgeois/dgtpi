#ifndef HAL_H
#define HAL_H

#include <pthread.h>
#include <stdint.h>

// Hardware interface - all platform-specific code goes here
// Protocol layer (dgtpicom.c) uses ONLY this interface

// Hardware register access (abstracted)
typedef struct {
  // Initialization (called by hal_init with platform id)
  int (*init)(int platform);

  // Cleanup (called by hal_cleanup)
  void (*cleanup)(void);

  // I2C master operations (for sending messages)
  int (*i2c_send)(const uint8_t *message, uint8_t length, uint8_t ack_address);

  // I2C slave operations (for receiving messages)
  int (*i2c_receive_ready)(void);
  int (*i2c_receive)(uint8_t *buffer, uint8_t max_length);

  // I2C configuration
  void (*i2c_set_destination)(uint8_t address);
  void (*i2c_listen_address)(uint8_t address);
  void (*i2c_reset)(void);

  // Timer (microsecond counter)
  uint64_t (*get_timer_us)(void);

  // Clock frequency check
  int (*check_core_freq_mhz)(void);

  // Platform name (static string)
  const char *name;

} hal_ops_t;

// Global HAL instance (set during initialization)
extern hal_ops_t hal;

// Initialize hardware and detect platform
// Returns 0 on success, negative on error
int hal_init(void);

// Cleanup hardware
void hal_cleanup(void);

// Get platform name (for testing)
const char *hal_get_platform_name(void);

#endif