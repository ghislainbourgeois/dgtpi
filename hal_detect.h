#ifndef HAL_DETECT_H
#define HAL_DETECT_H

#include "hal.h"

// Platform HAL operations - add new platforms here
// To add a new platform:
// 1. Add extern declaration below
// 2. Add to hal_init() in hal_detect.c
extern hal_ops_t hal_rpi_stubs_ops;  // Raspberry Pi 1-4
extern hal_ops_t hal_rpi5_ops;      // Raspberry Pi 5

// Add new platforms here:
// extern hal_ops_t hal_xxx_ops;

#endif
