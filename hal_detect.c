#include "hal.h"
#include "rpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern hal_ops_t hal_rpi5_ops;
extern hal_ops_t hal_rpi_stubs_ops;
extern void hal_rpi_stop_hardware(void);
extern int hal_rpi_init_hardware(void);

int hal_rpi5_init(void);
void hal_rpi5_cleanup(void);

static int current_platform = 0;
static void (*platform_cleanup)(void) = NULL;

hal_ops_t hal;

static int detect_pi_version(void) {
  FILE *fp = fopen("/proc/cpuinfo", "r");
  if (!fp)
    return 0;

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "Revision", 8) == 0) {
      char *rev_str = strchr(line, ':');
      if (!rev_str)
        break;

      rev_str += 2;

      char *end;
      long rev = strtol(rev_str, &end, 16);

      int new_format = (rev >> 23) & 0x1;

      if (!new_format) {
        fclose(fp);
        return 0;
      }

      int model = (rev >> 4) & 0xff;

      if (model == 0x17) {
        fclose(fp);
        return 5;
      }
      if (model == 0x11) {
        fclose(fp);
        return 4;
      }
      if (model == 0x08 || model == 0x0d || model == 0x0e) {
        fclose(fp);
        return 3;
      }
      if (model == 0x04) {
        fclose(fp);
        return 2;
      }

      fclose(fp);
      return 0;
    }
  }

  fclose(fp);
  return 0;
}

int hal_init(void) {
  current_platform = detect_pi_version();

  switch (current_platform) {
  case 5:
    hal = hal_rpi5_ops;
    platform_cleanup = hal_rpi5_cleanup;
    return hal_rpi5_init();
  case 4:
  case 3:
  case 2:
    hal = hal_rpi_stubs_ops;
    setPiModel(current_platform);
    platform_cleanup = hal_rpi_stop_hardware;
    return hal_rpi_init_hardware();
  default:
    return -1;
  }
}

void hal_cleanup(void) {
  if (platform_cleanup)
    platform_cleanup();
}

const char *hal_get_platform_name(void) {
  if (current_platform == 0) {
    current_platform = detect_pi_version();
  }
  switch (current_platform) {
  case 5:
    return "Raspberry Pi 5";
  case 4:
    return "Raspberry Pi 4";
  case 3:
    return "Raspberry Pi 3";
  case 2:
    return "Raspberry Pi 2";
  default:
    return "Unknown";
  }
}
