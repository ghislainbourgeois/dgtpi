#include "hal.h"
#include "hal_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int current_platform = 0;

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
    break;
  case 4:
  case 3:
  case 2:
    hal = hal_rpi_stubs_ops;
    break;
  default:
    return -1;
  }

  if (hal.init) {
    return hal.init(current_platform);
  }
  return 0;
}

void hal_cleanup(void) {
  if (hal.cleanup) {
    hal.cleanup();
  }
}

const char *hal_get_platform_name(void) {
  if (hal.name) {
    return hal.name;
  }
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
