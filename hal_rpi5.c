#define _GNU_SOURCE
#include "hal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/*
 * Raspberry Pi 5 Hardware Abstraction Layer (HAL)
 *
 * The Pi 5 uses a fundamentally different architecture from Pi 1-4:
 * - BCM2712 processor with RP1 southbridge chip
 * - I2C uses Synopsys DW_apb_i2c controller (completely different from BCM2835 BSC)
 * - GPIO controlled via RP1, not directly via BCM2712
 *
 * Key differences from Pi 1-4:
 * 1. Peripherals are accessed through RP1 southbridge at base 0x1F000000
 * 2. I2C uses the Synopsys DesignWare APB I2C controller (v2.02)
 * 3. Different GPIO register layout and function selection
 * 4. I2C slave mode is handled differently
 *
 * References:
 * - Synopsys DW_apb_i2c databook
 * - RP1 datasheet / BCM2712 peripherals documentation
 * - Gpio5 library: https://github.com/IOPress/Gpio5
 */

/*
 * ============================================================================
 * RP1 Southbridge Memory Map
 * ============================================================================
 *
 * The Pi 5 uses RP1 as a southbridge chip that handles GPIO, I2C, SPI, etc.
 * The RP1 registers are mapped at a different address than the old BCM2835
 * peripherals.
 *
 * Memory layout (documented in RP1 datasheet):
 *   0x1F000000 - Base of RP1 registers
 *   + 0x000000 - GPIO registers
 *   + 0x070000 - I2C0
 *   + 0x074000 - I2C1 (used for DGT board - GPIO 2/3)
 *   + 0x078000 - I2C2
 *   + 0x07C000 - I2C3
 *   + 0x080000 - I2C4
 *   + 0x084000 - I2C5
 *
 * Note: Unlike Pi 1-4 where the timer was in the peripheral base, the Pi 5
 * uses the ARM generic timer (architectural feature) or the system timer.
 */
#define RP1_BASE         0x1F00000000UL
#define RP1_GPIO_OFFSET  0x000000
#define RP1_I2C0_OFFSET  0x070000
#define RP1_I2C1_OFFSET 0x074000  // I2C1 on GPIO 2/3 (SDA/SCL) - Master
#define RP1_I2C2_OFFSET 0x078000
#define RP1_I2C3_OFFSET 0x07C000  // I2C3 on GPIO 6/7 - Slave for receiving

// Size of each mapped region (4KB page alignment)
#define RP1_I2C_MAP_SIZE 4096
#define RP1_GPIO_MAP_SIZE 4096

/*
 * ============================================================================
 * GPIO Register Map (RP1)
 * ============================================================================
 *
 * The RP1 GPIO controller has a different register layout than BCM2835.
 * Each GPIO pin can be configured to one of several "alternative functions".
 *
 * GPIO Register offsets from RP1_GPIO_BASE:
 *   0x00 - GPFSEL0   - Function select 0 (pins 0-9)
 *   0x04 - GPFSEL1   - Function select 1 (pins 10-19)
 *   0x08 - GPFSEL2   - Function select 2 (pins 20-29)
 *   0x0C - GPFSEL3   - Function select 3 (pins 30-39)
 *   0x10 - GPFSEL4   - Function select 4 (pins 40-49)
 *   0x14 - GPFSEL5   - Function select 5 (pins 50-53)
 *   0x18 - Reserved
 *   0x1C - GPSET0    - Pin output set 0 (pins 0-31)
 *   0x20 - GPSET1    - Pin output set 1 (pins 32-63)
 *   0x24 - Reserved
 *   0x28 - GPCLR0    - Pin output clear 0 (pins 0-31)
 *   0x2C - GPCLR1    - Pin output clear 1 (pins 32-63)
 *   0x30 - Reserved
 *   0x34 - GPLEV0    - Pin level 0 (pins 0-31)
 *   0x38 - GPLEV1    - Pin level 1 (pins 32-63)
 *   0x3C - Reserved
 *   0x40 - GPEDS0    - Pin event detect status 0
 *   0x44 - GPEDS1    - Pin event detect status 1
 *   0x48 - Reserved
 *   0x4C - GPREN0    - Pin rising edge detect enable 0
 *   0x50 - GPREN1    - Pin rising edge detect enable 1
 *   0x54 - Reserved
 *   0x58 - GPFEN0    - Pin falling edge detect enable 0
 *   0x5C - GPFEN1    - Pin falling edge detect enable 1
 *   0x60 - Reserved
 *   0x64 - GPHEN0    - Pin high detect enable 0
 *   0x68 - GPHEN1    - Pin high detect enable 1
 *   0x6C - Reserved
 *   0x70 - GPLEN0    - Pin low detect enable 0
 *   0x74 - GPLEN1    - Pin low detect enable 1
 *   0x78 - Reserved
 *   0x7C - GPPUD     - Pin pull-up/down enable
 *   0x80 - GPPUDCLK0 - Pin pull-up/down clock 0
 *   0x84 - GPPUDCLK1 - Pin pull-up/down clock 1
 *
 * Function select values (each pin has 3 bits in GPFSELn):
 *   000 = Input (default)
 *   001 = Output
 *   010 = Alternate function 0
 *   011 = Alternative function 1
 *   100 = Alternative function 2
 *   101 = Alternative function 3
 *   110 = Alternative function 4
 *   111 = Alternative function 5
 *
 * For I2C on GPIO 2 (SDA) and GPIO 3 (SCL), we use ALT0 (function 2)
 */
#define RP1_GPIO_FSEL_OFFSET  0x00  // Function selection
#define RP1_GPIO_SET_OFFSET  0x1C  // Set pins high
#define RP1_GPIO_CLR_OFFSET  0x28  // Clear pins low
#define RP1_GPIO_LEV_OFFSET  0x34  // Read pin levels
#define RP1_GPIO_PUD_OFFSET  0x7C  // Pull-up/down control
#define RP1_GPIO_PUDCLK_OFFSET 0x80 // Pull-up/down clock

/*
 * ============================================================================
 * Synopsys DW_apb_i2c Controller Registers
 * ============================================================================
 *
 * The Pi 5 uses the Synopsys DesignWare APB I2C controller, which is
 * fundamentally different from the Broadcom Serial Controller (BSC) used
 * in Pi 1-4.
 *
 * Key differences:
 * - Different register layout and naming
 * - Has separate master and slave functionality
 * - Uses a FIFO for data (32 bytes deep)
 * - Has more sophisticated status and interrupt handling
 * - Supports I2C clock stretching
 *
 * Register offsets (all 32-bit):
 *   0x00 - IC_CON         - Control register
 *   0x04 - IC_TAR         - Target address (master mode)
 *   0x08 - IC_SAR         - Slave address (slave mode)
 *   0x0C - Reserved
 *   0x10 - IC_DATA_CMD    - Data/command register (write: cmd+data, read: cmd)
 *   0x14 - IC_SS_SCL_HCNT - Standard speed SCL high count
 *   0x18 - IC_SS_SCL_LCNT - Standard speed SCL low count
 *   0x1C - IC_FS_SCL_HCNT - Fast speed SCL high count
 *   0x20 - IC_FS_SCL_LCNT - Fast speed SCL low count
 *   0x24 - Reserved
 *   0x28 - IC_INTR_STAT   - Interrupt status (read-only)
 *   0x2C - IC_INTR_MASK   - Interrupt mask
 *   0x30 - IC_RAW_INTR_STAT - Raw interrupt status
 *   0x34 - IC_RX_TL       - RX FIFO threshold
 *   0x38 - IC_TX_TL       - TX FIFO threshold
 *   0x3C - IC_CLR_INTR    - Clear interrupt
 *   0x40 - IC_CLR_RX_UNDER - Clear RX underflow interrupt
 *   0x44 - IC_CLR_RX_OVER  - Clear RX overflow interrupt
 *   0x48 - IC_CLR_TX_OVER  - Clear TX overflow interrupt
 *   0x4C - IC_CLR_RD_REQ   - Clear read request interrupt (slave)
 *   0x50 - IC_CLR_TX_ABRT  - Clear TX abort interrupt
 *   0x54 - IC_CLR_RX_DONE  - Clear RX done interrupt (slave)
 *   0x58 - IC_CLR_ACTIVITY - Clear activity interrupt
 *   0x5C - IC_CLR_STOP_DET - Clear stop detection interrupt
 *   0x60 - IC_CLR_START_DET - Clear start detection interrupt
 *   0x64 - IC_CLR_GEN_CALL  - Clear general call interrupt
 *   0x68 - IC_ENABLE       - Enable/disable controller
 *   0x6C - IC_STATUS       - Status register
 *   0x70 - IC_TXFLR        - TX FIFO level
 *   0x74 - IC_RXFLR        - RX FIFO level
 *   0x78 - IC_SDA_HOLD     - SDA hold time
 *   0x7C - IC_TX_ABRT_SOURCE - TX abort source
 *   0x80 - IC_SLV_DATA_NACK_ONLY - Slave NACK only mode
 *   0x84 - IC_DMA_CR       - DMA control
 *   0x88 - IC_DMA_TDLR     - DMA TX data level
 *   0x8C - IC_DMA_RDLR     - DMA RX data level
 *   0x90 - IC_SDA_SETUP    - SDA setup time
 *   0x94 - IC_ACK_GENERAL_CALL - ACK general call
 *   0x98 - IC_ENABLE_STATUS - Enable status
 *
 * IC_CON (Control Register) bits:
 *   [0]    - MASTER_MODE     - 0=slave, 1=master
 *   [1]    - SPEED           - 00=standard, 01=fast, 10=fast-plus
 *   [3]    - ADDRESSING_MODE - 0=7-bit, 1=10-bit
 *   [4]    - RX_FIFO_FULL_HOLD - 1=hold bus on RX full
 *   [5]    - TX_FIFO_EMPTY_CTRL - TX empty interrupt control
 *   [6]    - STOP_DET_IFMA   - Stop if master active
 *   [7]    - BUS_CLEAR_FEAT  - Bus clear feature enable
 *   [8:10] - SDA_FS_MAX      - Fast speed SDA filter max delay
 *   [11]   - SDA_FS_SCL_LOW  - SDA low in FS mode
 *   [12]   - RX_LSB_LAYOUT   - RX data LSB first
 *   [13]   - TX_LSB_LAYOUT   - TX data LSB first
 *
 * IC_STATUS Register bits (read-only):
 *   [0]    -ACTIVITY         - Controller activity
 *   [1]    -TX_FIFO_EMPTY    - TX FIFO empty
 *   [2]    -TX_FIFO_FULL     - TX FIFO full
 *   [3]    -RX_FIFO_EMPTY    - RX FIFO empty
 *   [4]    -RX_FIFO_FULL     - RX FIFO full
 *   [5]    -SLAVE_ACTIVITY   - Slave activity
 *   [6]    -MASTER_ACTIVITY  - Master activity
 *   [7]    -RFSR             - RX FIFO needs service
 *   [8]    -RFFR             - RX FIFO full and not read
 *   [9]    -TFSR             - TX FIFO needs service
 *   [10]   -TFEU             - TX FIFO empty (underflow)
 *   [11]   -TFFR             - TX FIFO full
 *
 * IC_DATA_CMD register:
 *   [0:7]  - DAT    - Data byte
 *   [8]    - CMD    - 0=read, 1=write
 *   [9]    - STOP   - Generate STOP after this byte
 *
 * IC_TAR / IC_SAR registers:
 *   [0:9]  - ADDR   - I2C address (7 or 10-bit)
 *   [10]   - GC_OR_START - General call or RESTART
 *   [11]   - SPECIAL     - Special case enable
 *   [12]   - ADDR_10BIT_RW - 10-bit address read/write
 */

/* Synopsys DW_apb_i2c register offsets */
#define IC_CON         0x00
#define IC_TAR         0x04
#define IC_SAR         0x08
#define IC_DATA_CMD    0x10
#define IC_SS_SCL_HCNT 0x14
#define IC_SS_SCL_LCNT 0x18
#define IC_FS_SCL_HCNT 0x1C
#define IC_FS_SCL_LCNT 0x20
#define IC_INTR_STAT   0x28
#define IC_INTR_MASK   0x2C
#define IC_RAW_INTR_STAT 0x30
#define IC_RX_TL       0x34
#define IC_TX_TL       0x38
#define IC_CLR_INTR    0x3C
#define IC_CLR_RX_UNDER 0x40
#define IC_CLR_RX_OVER  0x44
#define IC_CLR_TX_OVER  0x48
#define IC_CLR_RD_REQ   0x4C
#define IC_CLR_TX_ABRT  0x50
#define IC_CLR_RX_DONE  0x54
#define IC_CLR_ACTIVITY 0x58
#define IC_CLR_STOP_DET 0x5C
#define IC_CLR_START_DET 0x60
#define IC_CLR_GEN_CALL 0x64
#define IC_ENABLE      0x68
#define IC_STATUS      0x6C
#define IC_TXFLR       0x70
#define IC_RXFLR       0x74
#define IC_SDA_HOLD    0x78
#define IC_TX_ABRT_SOURCE 0x7C
#define IC_SLV_DATA_NACK_ONLY 0x80
#define IC_DMA_CR      0x84
#define IC_DMA_TDLR    0x88
#define IC_DMA_RDLR    0x8C
#define IC_SDA_SETUP   0x90
#define IC_ACK_GENERAL_CALL 0x94
#define IC_ENABLE_STATUS 0x98
#define IC_FS_SPKLEN   0xA0

/* IC_CON bit definitions */
#define IC_CON_MASTER_MODE         (1 << 0)
#define IC_CON_SPEED_STD           (0 << 1)
#define IC_CON_SPEED_FAST          (1 << 1)
#define IC_CON_SPEED_FAST_PLUS     (2 << 1)
#define IC_CON_ADDR_10BIT          (1 << 3)
#define IC_CON_RX_FIFO_FULL_HOLD   (1 << 4)
#define IC_CON_TX_FIFO_EMPTY_CTRL  (1 << 5)

/* IC_ENABLE bit definitions */
#define IC_ENABLE_ENABLE           (1 << 0)
#define IC_ENABLE_ABORT           (1 << 1)

/* IC_STATUS bit definitions */
#define IC_STATUS_ACTIVITY        (1 << 0)
#define IC_STATUS_TX_FIFO_EMPTY   (1 << 1)
#define IC_STATUS_TX_FIFO_FULL    (1 << 2)
#define IC_STATUS_RX_FIFO_EMPTY   (1 << 3)
#define IC_STATUS_RX_FIFO_FULL    (1 << 4)
#define IC_STATUS_SLAVE_ACTIVITY  (1 << 5)
#define IC_STATUS_MASTER_ACTIVITY (1 << 6)

/* IC_DATA_CMD bit definitions */
#define IC_DATA_CMD_DAT_MASK      0xFF
#define IC_DATA_CMD_CMD_WRITE     (0 << 8)
#define IC_DATA_CMD_CMD_READ      (1 << 8)
#define IC_DATA_CMD_STOP          (1 << 9)

/* Interrupt bit masks */
#define IC_INTR_RX_FULL           (1 << 2)
#define IC_INTR_TX_EMPTY          (1 << 4)
#define IC_INTR_STOP_DET          (1 << 9)
#define IC_INTR_RD_REQ            (1 << 6)
#define IC_INTR_TX_ABRT            (1 << 6)

/*
 * ============================================================================
 * Timer Implementation
 * ============================================================================
 *
 * The Pi 5 uses the ARM generic timer (Cortex-A76 has 4 generic timers).
 * The system timer is also available at a different address.
 *
 * For simplicity, we use the approach of reading from the ARM system timer
 * which is available at a known address. The timer increments every 1 us
 * (or at a known frequency based on the ARM timer).
 *
 * On Pi 5, the ARM generic timer can be accessed via:
 * - CNTV_CTL_EL0, CNTV_CVAL_EL0, CNTV_TVAL_EL0 registers
 * - Or via the memory-mapped view at 0xFD590000 (system timer)
 *
 * For compatibility with the existing code pattern, we'll use the system timer
 * approach similar to Pi 1-4 but with the correct Pi 5 addresses.
 *
 * Note: The actual timer implementation may need adjustment based on
 * testing with actual Pi 5 hardware.
 */
#define RPI5_TIMER_BASE      0xFD590000
#define RPI5_TIMER_MAP_SIZE  4096

/* Timer register offsets */
#define RPI5_TIMER_CLO       0x04  /* Counter Least Significant */
#define RPI5_TIMER_CHI       0x08  /* Counter Most Significant */

/*
 * ============================================================================
 * I2C Pin Configuration
 * ============================================================================
 *
 * Default I2C pins on Pi 5:
 *   GPIO 2 = SDA (I2C1)
 *   GPIO 3 = SCL (I2C1)
 *
 * Alternative I2C busses:
 *   I2C0: GPIO 0/1 (reserved for EEPROM), GPIO 28/29 (compute module)
 *   I2C3: GPIO 4/5
 *   I2C4: GPIO 6/7
 *   I2C5: GPIO 12/13 or GPIO 22/23
 *
 * Pin function selection (using ALT0 for I2C):
 *   GPIO 2 -> ALT0 = I2C1 SDA
 *   GPIO 3 -> ALT0 = I2C1 SCL
 */

/* Static register pointers */
static volatile uint32_t *gpio_regs = NULL;
static volatile uint32_t *i2c1_regs = NULL;   /* I2C1 - Master (GPIO 2/3) */
static volatile uint32_t *i2c3_regs = NULL;   /* I2C3 - Slave (GPIO 6/7) */
static volatile uint32_t *timer_regs = NULL;

/* Timer reference for calculating microseconds */
static uint64_t timer_base_us = 0;

/*
 * Map the required hardware regions into memory.
 *
 * We need to map:
 * 1. RP1 GPIO registers (for pin configuration)
 * 2. I2C1 controller registers (for I2C communication)
 * 3. System timer (for microsecond timing)
 *
 * Each region is mapped using mmap() from /dev/mem, which requires
 * root privileges (sudo).
 *
 * Returns: 0 on success, -1 on failure
 */
static int hal_rpi5_map_memory(void) {
  int memfd;

  /* Open /dev/mem for direct hardware access
   * This requires root privileges (sudo)
   */
  memfd = open("/dev/mem", O_RDWR | O_SYNC);
  if (memfd < 0) {
    fprintf(stderr, "hal_rpi5: Failed to open /dev/mem\n");
    return -1;
  }

  /* Map I2C1 controller registers (Master - GPIO 2/3) */
  i2c1_regs = mmap(NULL, RP1_I2C_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   memfd, RP1_BASE + RP1_I2C1_OFFSET);
  if (i2c1_regs == MAP_FAILED) {
    fprintf(stderr, "hal_rpi5: Failed to map I2C1 registers\n");
    close(memfd);
    i2c1_regs = NULL;
    return -1;
  }

  /* Map I2C3 controller registers (Slave - GPIO 6/7) */
  i2c3_regs = mmap(NULL, RP1_I2C_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   memfd, RP1_BASE + RP1_I2C3_OFFSET);
  if (i2c3_regs == MAP_FAILED) {
    fprintf(stderr, "hal_rpi5: Failed to map I2C3 registers\n");
    munmap((void *)i2c1_regs, RP1_I2C_MAP_SIZE);
    i2c1_regs = NULL;
    close(memfd);
    i2c3_regs = NULL;
    return -1;
  }

  /* Map GPIO registers */
  gpio_regs = mmap(NULL, RP1_GPIO_MAP_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, memfd, RP1_BASE + RP1_GPIO_OFFSET);
  if (gpio_regs == MAP_FAILED) {
    fprintf(stderr, "hal_rpi5: Failed to map GPIO registers\n");
    munmap((void *)i2c1_regs, RP1_I2C_MAP_SIZE);
    munmap((void *)i2c3_regs, RP1_I2C_MAP_SIZE);
    i2c1_regs = NULL;
    i2c3_regs = NULL;
    close(memfd);
    gpio_regs = NULL;
    return -1;
  }

  /* Map system timer */
  timer_regs = mmap(NULL, RPI5_TIMER_MAP_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, memfd, RPI5_TIMER_BASE);
  if (timer_regs == MAP_FAILED) {
    fprintf(stderr, "hal_rpi5: Failed to map timer registers\n");
    munmap((void *)i2c1_regs, RP1_I2C_MAP_SIZE);
    munmap((void *)i2c3_regs, RP1_I2C_MAP_SIZE);
    munmap((void *)gpio_regs, RP1_GPIO_MAP_SIZE);
    i2c1_regs = NULL;
    i2c3_regs = NULL;
    gpio_regs = NULL;
    close(memfd);
    timer_regs = NULL;
    return -1;
  }

  close(memfd);
  return 0;
}

/*
 * Unmap all hardware regions.
 * Called during cleanup to release memory mappings.
 */
static void hal_rpi5_unmap_memory(void) {
  if (i2c1_regs != NULL) {
    munmap((void *)i2c1_regs, RP1_I2C_MAP_SIZE);
    i2c1_regs = NULL;
  }
  if (i2c3_regs != NULL) {
    munmap((void *)i2c3_regs, RP1_I2C_MAP_SIZE);
    i2c3_regs = NULL;
  }
  if (gpio_regs != NULL) {
    munmap((void *)gpio_regs, RP1_GPIO_MAP_SIZE);
    gpio_regs = NULL;
  }
  if (timer_regs != NULL) {
    munmap((void *)timer_regs, RPI5_TIMER_MAP_SIZE);
    timer_regs = NULL;
  }
}

/*
 * Write to an I2C controller register.
 *
 * The I2C registers are 32-bit aligned, but offsets are in bytes.
 * We divide by 4 to get the correct register index.
 */
static void hal_rpi5_i2c_write(uint8_t offset, uint32_t value) {
  i2c1_regs[offset / 4] = value;
}

/*
 * Read from an I2C controller register.
 */
static uint32_t hal_rpi5_i2c_read(uint8_t offset) {
  return i2c1_regs[offset / 4];
}

/*
 * Write to I2C3 (slave) controller register.
 */
static void hal_rpi5_i2c3_write(uint8_t offset, uint32_t value) {
  i2c3_regs[offset / 4] = value;
}

/*
 * Read from I2C3 (slave) controller register.
 */
static uint32_t hal_rpi5_i2c3_read(uint8_t offset) {
  return i2c3_regs[offset / 4];
}

/*
 * Configure GPIO pins for I2C1 function.
 *
 * On Pi 5, GPIO 2 and GPIO 3 need to be set to ALT0 function to use
 * the I2C1 controller.
 *
 * GPIO function selection:
 * - Each GPFSEL register controls 10 pins (3 bits each)
 * - GPFSEL0: pins 0-9 (GPIO 2 is bit 6-8 in GPFSEL0)
 * - GPFSEL0: pins 0-9 (GPIO 3 is bit 9-11 in GPFSEL0)
 *
 * To set GPIO 2 to ALT0: clear bits [6:8], set to 010 (ALT0)
 * To set GPIO 3 to ALT0: clear bits [9:11], set to 010 (ALT0)
 */
static void hal_rpi5_config_i2c1_pins(void) {
  uint32_t gpfsel0;

  if (gpio_regs == NULL) {
    return;
  }

  /* Read current GPFSEL0 register */
  gpfsel0 = gpio_regs[RP1_GPIO_FSEL_OFFSET / 4];

  /* Clear bits for GPIO 2 and GPIO 3 (6 bits total: 6-11) */
  gpfsel0 &= ~((0x1F << 6) | (0x1F << 9));

  /* Set GPIO 2 (bits 6-8) and GPIO 3 (bits 9-11) to ALT0 (010) */
  gpfsel0 |= (0x2 << 6) | (0x2 << 9);

  /* Write back GPFSEL0 */
  gpio_regs[RP1_GPIO_FSEL_OFFSET / 4] = gpfsel0;

  /*
   * Enable pull-ups on I2C pins.
   * The I2C specification requires pull-up resistors on SDA and SCL.
   * We use the internal pull-up if external pull-ups aren't available.
   *
   * GPPUD: 0=disable, 1=enable pull-down, 2=enable pull-up
   */
  gpio_regs[RP1_GPIO_PUD_OFFSET / 4] = 0x2;  // Enable pull-up
  usleep(10);  // Wait for pull-up to stabilize
  gpio_regs[RP1_GPIO_PUDCLK_OFFSET / 4] = (1 << 2) | (1 << 3);  // Clock pull-up for GPIO 2,3
  usleep(10);
  gpio_regs[RP1_GPIO_PUD_OFFSET / 4] = 0;  // Disable pull-up control

  /* Clear the clock */
  gpio_regs[RP1_GPIO_PUDCLK_OFFSET / 4] = 0;
}

/*
 * Initialize the I2C1 controller (Master).
 *
 * This sets up the I2C1 controller for master mode:
 * - Master mode for sending messages to the DGT board
 *
 * The DGT board uses I2C address 0x08 for normal communication.
 */
/*
 * Configure I2C timing for 100kHz standard speed.
 *
 * The DW_apb_i2c controller uses an APB (Advanced Peripheral Bus) clock.
 * For standard speed I2C at 100kHz:
 * - High time: minimum 4us
 * - Low time: minimum 4.7us
 *
 * With 125MHz APB clock:
 * - HCNT = 4us * 125MHz = 500
 * - LCNT = 4.7us * 125MHz = 587.5, round to 588
 */
#define I2C_APB_CLOCK_125 125000000
#define I2C_SS_FREQ       100000

static void hal_rpi5_i2c_init(void) {
  uint32_t clock_hcnt, clock_lcnt;
  uint32_t enable_check;

  /* Disable the controller before configuration */
  hal_rpi5_i2c_write(IC_ENABLE, 0);
  fprintf(stderr, "hal_rpi5_i2c_init: disabled controller\n");

  /* Configure I2C controller:
   * - Master mode
   * - Standard speed (100kHz)
   * - 7-bit addressing
   */
  hal_rpi5_i2c_write(IC_CON, IC_CON_MASTER_MODE | IC_CON_SPEED_STD);
  fprintf(stderr, "hal_rpi5_i2c_init: wrote CON=0x%08x\n", IC_CON_MASTER_MODE | IC_CON_SPEED_STD);

  /* Set target address for master mode (DGT board at 0x08) */
  hal_rpi5_i2c_write(IC_TAR, 0x08);
  fprintf(stderr, "hal_rpi5_i2c_init: wrote TAR=0x08\n");

  clock_hcnt = (I2C_APB_CLOCK_125 / I2C_SS_FREQ - 8);
  clock_lcnt = (I2C_APB_CLOCK_125 / I2C_SS_FREQ - 8);
  fprintf(stderr, "hal_rpi5_i2c_init: clock_hcnt=%u, clock_lcnt=%u\n", clock_hcnt, clock_lcnt);

  hal_rpi5_i2c_write(IC_SS_SCL_HCNT, clock_hcnt);
  hal_rpi5_i2c_write(IC_SS_SCL_LCNT, clock_lcnt);

  /*
   * Configure FIFO thresholds:
   * - TX trigger: generate interrupt when TX FIFO has <= 0 empty slots
   * - RX trigger: generate interrupt when RX FIFO has >= 1 byte
   */
  hal_rpi5_i2c_write(IC_TX_TL, 0);
  hal_rpi5_i2c_write(IC_RX_TL, 0);

  /*
   * SDA hold time:
   * The I2C specification requires the data to be held after SCL goes low.
   */
  hal_rpi5_i2c_write(IC_SDA_HOLD, 0x1);

  /*
   * Clear any pending interrupts by reading the interrupt register.
   */
  hal_rpi5_i2c_read(IC_CLR_INTR);
  fprintf(stderr, "hal_rpi5_i2c_init: cleared interrupts\n");

  /* Enable the controller */
  hal_rpi5_i2c_write(IC_ENABLE, IC_ENABLE_ENABLE);
  fprintf(stderr, "hal_rpi5_i2c_init: enabled controller\n");

  /* Check interrupt enable register - the controller may need interrupts enabled */
  /* For master mode, we need to make sure the TX interrupt is working */
  hal_rpi5_i2c_write(IC_INTR_MASK, 0);  /* Disable all interrupts for now */
  fprintf(stderr, "hal_rpi5_i2c_init: masked interrupts\n");

  /* Verify it was enabled */
  enable_check = hal_rpi5_i2c_read(IC_ENABLE);
  fprintf(stderr, "hal_rpi5_i2c_init: ENABLE verify=0x%08x\n", enable_check);

  /* Read back configuration */
  fprintf(stderr, "hal_rpi5_i2c_init: CON readback=0x%08x\n", hal_rpi5_i2c_read(IC_CON));
  fprintf(stderr, "hal_rpi5_i2c_init: TAR readback=0x%08x\n", hal_rpi5_i2c_read(IC_TAR));
  fprintf(stderr, "hal_rpi5_i2c_init: HCNT readback=0x%08x\n", hal_rpi5_i2c_read(IC_SS_SCL_HCNT));
  fprintf(stderr, "hal_rpi5_i2c_init: LCNT readback=0x%08x\n", hal_rpi5_i2c_read(IC_SS_SCL_LCNT));
  fprintf(stderr, "hal_rpi5_i2c_init: INTR_MASK readback=0x%08x\n", hal_rpi5_i2c_read(IC_INTR_MASK));
}

/*
 * Initialize the I2C3 controller (Slave).
 *
 * This sets up the I2C3 controller on GPIO 6/7 for slave mode:
 * - Slave mode for receiving button messages from the DGT board
 * - Listens on address 0x08
 *
 * The DGT board sends button presses to address 0x08.
 */
static void hal_rpi5_i2c3_init(void) {
  uint32_t clock_hcnt, clock_lcnt;

  /* Disable the controller before configuration */
  hal_rpi5_i2c3_write(IC_ENABLE, 0);

  /* Configure I2C controller:
   * - Slave mode (no MASTER_MODE bit)
   * - Standard speed (100kHz)
   * - 7-bit addressing
   */
  hal_rpi5_i2c3_write(IC_CON, IC_CON_SPEED_STD);

  /* Set slave address (for receiving as slave on address 0x08) */
  hal_rpi5_i2c3_write(IC_SAR, 0x08);

  /*
   * Configure I2C timing for 100kHz standard speed.
   */
  clock_hcnt = (I2C_APB_CLOCK_125 / I2C_SS_FREQ - 8);
  clock_lcnt = (I2C_APB_CLOCK_125 / I2C_SS_FREQ - 8);

  hal_rpi5_i2c3_write(IC_SS_SCL_HCNT, clock_hcnt);
  hal_rpi5_i2c3_write(IC_SS_SCL_LCNT, clock_lcnt);

  /*
   * Configure FIFO thresholds:
   * - RX trigger: generate interrupt when RX FIFO has >= 1 byte
   */
  hal_rpi5_i2c3_write(IC_RX_TL, 0);

  /*
   * SDA hold time:
   */
  hal_rpi5_i2c3_write(IC_SDA_HOLD, 0x1);

  /*
   * Clear any pending interrupts.
   */
  hal_rpi5_i2c3_read(IC_CLR_INTR);

  /* Enable the controller */
  hal_rpi5_i2c3_write(IC_ENABLE, IC_ENABLE_ENABLE);
}

/*
 * Configure GPIO pins for I2C3 (Slave).
 *
 * GPIO 6 = SDA (I2C3)
 * GPIO 7 = SCL (I2C3)
 *
 * GPFSEL0: pins 0-9 (GPIO 6 is bits 18-20, GPIO 7 is bits 21-23)
 */
static void hal_rpi5_config_i2c3_pins(void) {
  uint32_t gpfsel0;

  if (gpio_regs == NULL) {
    return;
  }

  /* Read current GPFSEL0 register */
  gpfsel0 = gpio_regs[RP1_GPIO_FSEL_OFFSET / 4];

  /* Clear bits for GPIO 6 and GPIO 7 (6 bits total: 18-23) */
  gpfsel0 &= ~((0x1F << 18) | (0x1F << 21));

  /* Set GPIO 6 (bits 18-20) and GPIO 7 (bits 21-23) to ALT0 (010) */
  gpfsel0 |= (0x2 << 18) | (0x2 << 21);

  /* Write back GPFSEL0 */
  gpio_regs[RP1_GPIO_FSEL_OFFSET / 4] = gpfsel0;

  /*
   * Enable pull-ups on I2C pins.
   */
  gpio_regs[RP1_GPIO_PUD_OFFSET / 4] = 0x2;  // Enable pull-up
  usleep(10);
  gpio_regs[RP1_GPIO_PUDCLK_OFFSET / 4] = (1 << 6) | (1 << 7);  // Clock pull-up for GPIO 6,7
  usleep(10);
  gpio_regs[RP1_GPIO_PUD_OFFSET / 4] = 0;  // Disable pull-up control
  gpio_regs[RP1_GPIO_PUDCLK_OFFSET / 4] = 0;
}

/*
 * Reset the I2C1 controller to a known state.
 *
 * This is called when communication errors occur to recover
 * the I2C bus.
 */
static void hal_rpi5_i2c_reset(void) {
  /* Disable the controller */
  hal_rpi5_i2c_write(IC_ENABLE, 0);

  /* Wait a moment for any pending operations to complete */
  usleep(100);

  /* Clear any pending interrupts */
  hal_rpi5_i2c_read(IC_CLR_INTR);

  /* Re-enable the controller */
  hal_rpi5_i2c_write(IC_ENABLE, IC_ENABLE_ENABLE);
}

/*
 * Send an I2C message to the DGT board.
 *
 * This implements master-mode I2C transmission. The message format
 * is assumed to be:
 *   m[0] = ignored (could be address)
 *   m[1] = destination address (already in IC_TAR)
 *   m[2] = data length
 *   m[3..n] = data bytes
 *
 * Parameters:
 *   message    - The message buffer
 *   msg_length - Length of the message
 *   ack_address - Address to listen for acknowledgment (unused in master)
 *
 * Returns: 0 on success, negative error code
 */
static int hal_rpi5_i2c_send(const uint8_t *message, uint8_t msg_length,
                             uint8_t ack_address) {
  int i;
  uint32_t status;
  uint64_t timeout_us;
  uint8_t data_len;
  const uint8_t *data_ptr;

  fprintf(stderr, "hal_rpi5_i2c_send: called with msg_length=%d\n", msg_length);

  (void)ack_address;  /* Address is already set in IC_TAR */

  if (i2c1_regs == NULL || message == NULL || msg_length < 3) {
    fprintf(stderr, "hal_rpi5_i2c_send: NULL check failed\n");
    return -1;
  }

  /* Extract data length from message (at offset 2) */
  data_len = message[2];
  data_ptr = message + 3;  /* Data starts at offset 3 */
  fprintf(stderr, "hal_rpi5_i2c_send: data_len=%d, first_byte=0x%02x\n", data_len, data_ptr[0]);

  /* Ensure the I2C controller is enabled */
  uint32_t enable_reg = hal_rpi5_i2c_read(IC_ENABLE);
  fprintf(stderr, "hal_rpi5_i2c_send: ENABLE reg=0x%08x\n", enable_reg);
  if ((enable_reg & IC_ENABLE_ENABLE) == 0) {
    fprintf(stderr, "hal_rpi5_i2c_send: enabling controller\n");
    hal_rpi5_i2c_write(IC_ENABLE, IC_ENABLE_ENABLE);
  }

  /* Wait for any master activity to complete */
  timeout_us = hal.get_timer_us() + 10000;
  fprintf(stderr, "hal_rpi5_i2c_send: waiting for master ready\n");
  while (hal_rpi5_i2c_read(IC_STATUS) & IC_STATUS_MASTER_ACTIVITY) {
    if (hal.get_timer_us() > timeout_us) {
      fprintf(stderr, "hal_rpi5_i2c_send: timeout waiting for master ready\n");
      return -6;  /* ERROR_TIMEOUT */
    }
  }
  fprintf(stderr, "hal_rpi5_i2c_send: master ready\n");

  /*
   * Send data bytes to the FIFO.
   *
   * The DW_apb_i2c controller has a 32-byte TX FIFO.
   * We write data to IC_DATA_CMD register.
   *
   * For each byte:
   * - Set DAT[7:0] to the data byte
   * - Set CMD bit to 1 (write)
   * - Set STOP bit if this is the last byte
   */
  for (i = 0; i < data_len; i++) {
    uint32_t cmd;

    /* Check TX FIFO full status */
    timeout_us = hal.get_timer_us() + 10000;
    while (hal_rpi5_i2c_read(IC_STATUS) & IC_STATUS_TX_FIFO_FULL) {
      if (hal.get_timer_us() > timeout_us) {
        fprintf(stderr, "hal_rpi5_i2c_send: TX FIFO full timeout\n");
        hal_rpi5_i2c_reset();
        return -6;  /* ERROR_TIMEOUT */
      }
    }

    /* Write data byte with CMD=write, STOP on last byte */
    cmd = data_ptr[i];
    if (i == data_len - 1) {
      cmd |= IC_DATA_CMD_STOP;
    }
    fprintf(stderr, "hal_rpi5_i2c_send: writing byte %d: 0x%02x\n", i, cmd);
    hal_rpi5_i2c_write(IC_DATA_CMD, cmd);
    fprintf(stderr, "hal_rpi5_i2c_send: wrote, status=0x%08x, TXFLR=%u\n", 
            hal_rpi5_i2c_read(IC_STATUS), hal_rpi5_i2c_read(IC_TXFLR));
  }

  /*
   * Wait for the transmission to complete.
   */
  fprintf(stderr, "hal_rpi5_i2c_send: waiting for tx complete\n");
  timeout_us = hal.get_timer_us() + 10000;
  while (1) {
    status = hal_rpi5_i2c_read(IC_STATUS);
    uint32_t txflr = hal_rpi5_i2c_read(IC_TXFLR);
    uint32_t rawIntr = hal_rpi5_i2c_read(IC_RAW_INTR_STAT);
    fprintf(stderr, "hal_rpi5_i2c_send: status=0x%08x, TXFLR=%u, RAW_INTR=0x%08x\n", status, txflr, rawIntr);

    /* Check for TX abort (NACK, etc.) */
    if (status & 0x100) {  /* Abort flag - this is controller specific */
      fprintf(stderr, "hal_rpi5_i2c_send: TX abort!\n");
      hal_rpi5_i2c_read(IC_TX_ABRT_SOURCE);
      hal_rpi5_i2c_reset();
      return -2;  /* ERROR_NACK or similar */
    }

    /* Check if transmission is complete */
    if ((status & IC_STATUS_MASTER_ACTIVITY) == 0 &&
        (status & IC_STATUS_TX_FIFO_EMPTY)) {
      fprintf(stderr, "hal_rpi5_i2c_send: transmission complete\n");
      break;
    }

    if (hal.get_timer_us() > timeout_us) {
      fprintf(stderr, "hal_rpi5_i2c_send: timeout!\n");
      hal_rpi5_i2c_reset();
      return -6;  /* ERROR_TIMEOUT */
    }
  }

  /* Clear any pending interrupts */
  hal_rpi5_i2c_read(IC_CLR_INTR);

  return 0;
}

/*
 * Check if there is data ready to receive from the I2C slave.
 *
 * Returns: 1 if data is ready, 0 if not
 */
static int hal_rpi5_i2c_receive_ready(void) {
  uint32_t status;

  if (i2c3_regs == NULL) {
    return 0;
  }

  /* Check RX FIFO not empty on I2C3 (slave) */
  status = hal_rpi5_i2c3_read(IC_STATUS);
  return ((status & IC_STATUS_RX_FIFO_EMPTY) == 0) ? 1 : 0;
}

/*
 * Receive data from the I2C slave.
 *
 * This reads from the I2C3 slave controller when the DGT board
 * sends button presses.
 *
 * Parameters:
 *   buffer     - Buffer to receive data
 *   max_length - Maximum number of bytes to receive
 *
 * Returns: Number of bytes received, or negative error code
 */
static int hal_rpi5_i2c_receive(uint8_t *buffer, uint8_t max_length) {
  int i = 0;
  uint64_t timeout_us;
  uint32_t rx_level;

  if (i2c3_regs == NULL || buffer == NULL) {
    return -1;
  }

  /* Ensure controller is enabled */
  if ((hal_rpi5_i2c3_read(IC_ENABLE) & IC_ENABLE_ENABLE) == 0) {
    hal_rpi5_i2c3_write(IC_ENABLE, IC_ENABLE_ENABLE);
  }

  /* Get current RX FIFO level */
  rx_level = hal_rpi5_i2c3_read(IC_RXFLR);

  /* Read available bytes from RX FIFO */
  timeout_us = hal.get_timer_us() + 10000;
  while (i < max_length && i < rx_level) {
    uint32_t data;

    /* Read a data byte from the RX FIFO */
    data = hal_rpi5_i2c3_read(IC_DATA_CMD);
    buffer[i++] = (uint8_t)(data & IC_DATA_CMD_DAT_MASK);

    /* Check if there's more data */
    rx_level = hal_rpi5_i2c3_read(IC_RXFLR);

    /* Timeout protection */
    if (hal.get_timer_us() > timeout_us && rx_level == 0) {
      break;
    }
  }

  return i;
}

/*
 * Set the I2C target address for master mode transmission.
 *
 * This is called before sending messages to change the destination
 * address from the default (DGT board at 0x08).
 */
static void hal_rpi5_i2c_set_destination(uint8_t address) {
  if (i2c1_regs == NULL) {
    return;
  }

  /* Ensure the controller is disabled before changing TAR */
  hal_rpi5_i2c_write(IC_ENABLE, 0);

  /* Set the new target address */
  hal_rpi5_i2c_write(IC_TAR, address);

  /* Re-enable the controller */
  hal_rpi5_i2c_write(IC_ENABLE, IC_ENABLE_ENABLE);
}

/*
 * Set the I2C slave address for receiving messages.
 *
 * This configures the I2C3 controller (slave) to respond to a specific
 * address in slave mode.
 */
static void hal_rpi5_i2c_listen_address(uint8_t address) {
  if (i2c3_regs == NULL) {
    return;
  }

  /* Disable controller before changing SAR */
  hal_rpi5_i2c3_write(IC_ENABLE, 0);

  /* Set the slave address */
  hal_rpi5_i2c3_write(IC_SAR, address);

  /* Re-enable controller */
  hal_rpi5_i2c3_write(IC_ENABLE, IC_ENABLE_ENABLE);
}

/*
 * Get the current timer value in microseconds.
 *
 * The system timer on Pi 5 increments every microsecond.
 * We read the full 64-bit timer value.
 */
#include <time.h>

static uint64_t hal_rpi5_get_timer_us(void) {
  struct timespec ts;
  uint64_t timer_val;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  timer_val = ((uint64_t)ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);

  return timer_val;
}

/*
 * Check the core frequency in MHz.
 *
 * On Pi 5, we can use vcgencmd like Pi 1-4, or read from sysfs.
 * The recommended approach is to use the vcgencmd utility.
 */
static int hal_rpi5_check_core_freq_mhz(void) {
  FILE *fp;
  char line[100];
  int freq_mhz = 250;  /* Default fallback */

  /* Try using vcgencmd first (like Pi 1-4) */
  fp = popen("vcgencmd measure_clock core", "r");
  if (fp != NULL) {
    if (fgets(line, sizeof(line), fp) != NULL) {
      /* Line format: "frequency(45)=250000000" */
      char *equals = strchr(line, '=');
      if (equals != NULL) {
        freq_mhz = atoi(equals + 1) / 1000000;
      }
    }
    pclose(fp);
    return freq_mhz;
  }

  /* Fallback: try reading from sysfs */
  fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
  if (fp != NULL) {
    int freq_khz;
    if (fscanf(fp, "%d", &freq_khz) == 1) {
      freq_mhz = freq_khz / 1000;
    }
    fclose(fp);
  }

  return freq_mhz;
}

/*
 * Initialize the Pi 5 HAL.
 *
 * This is called during hal_init() when the platform is detected as Pi 5.
 *
 * Parameters:
 *   platform - The detected platform version (5 for Pi 5)
 *
 * Returns: 0 on success, negative error code
 */
int hal_rpi5_init(int platform) {
  (void)platform;

  /* Map hardware registers */
  if (hal_rpi5_map_memory() < 0) {
    fprintf(stderr, "hal_rpi5: Failed to map hardware registers\n");
    return -1;
  }

  /* Configure GPIO pins for I2C1 (Master - GPIO 2/3) */
  hal_rpi5_config_i2c1_pins();

  /* Configure GPIO pins for I2C3 (Slave - GPIO 6/7) */
  hal_rpi5_config_i2c3_pins();

  /* Initialize I2C1 controller (Master) */
  hal_rpi5_i2c_init();

  /* Initialize I2C3 controller (Slave) */
  hal_rpi5_i2c3_init();

  /* Initialize timer base */
  timer_base_us = hal_rpi5_get_timer_us();

  return 0;
}

/*
 * Cleanup the Pi 5 HAL.
 *
 * This is called during hal_cleanup() to release resources.
 */
void hal_rpi5_cleanup(void) {
  /* Disable I2C1 controller (Master) */
  if (i2c1_regs != NULL) {
    hal_rpi5_i2c_write(IC_ENABLE, 0);
  }

  /* Disable I2C3 controller (Slave) */
  if (i2c3_regs != NULL) {
    hal_rpi5_i2c3_write(IC_ENABLE, 0);
  }

  /* Unmap hardware registers */
  hal_rpi5_unmap_memory();
}

/*
 * HAL operations structure for Raspberry Pi 5.
 *
 * This connects the HAL interface to the Pi 5 specific implementations.
 */
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

/*
 * Get the platform name (for debugging/testing).
 */
const char *hal_rpi5_get_platform_name(void) { return "Raspberry Pi 5"; }
