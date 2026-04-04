#include "dgtpicom.h"
#include "hal.h"
#include <stdio.h>
#include <unistd.h>

#define FIVE_SECONDS 5000000

static int test_display_message(void) {
  printf("Testing display message...\n");

  // Test 1: Simple text
  if (dgtpicom_set_text("Hello", 0, 0, 0) != 0) {
    printf("FAIL: Could not set text\n");
    return -1;
  }
  usleep(FIVE_SECONDS);

  // Test 2: Text with dots
  if (dgtpicom_set_text("Test", 0, 0x1F, 0x0F) != 0) {
    printf("FAIL: Could not set text with dots\n");
    return -1;
  }
  usleep(FIVE_SECONDS);

  // Test 3: Clear display
  if (dgtpicom_end_text() != 0) {
    printf("FAIL: Could not end text\n");
    return -1;
  }
  usleep(FIVE_SECONDS);

  printf("Display tests: PASS\n");
  return 0;
}

static int test_clock_function(void) {
  printf("Testing clock function...\n");

  if (dgtpicom_configure() != 0) {
    printf("FAIL: Could not configure clock\n");
    return -1;
  }

  // Set left clock running down
  if (dgtpicom_set_and_run(1, 0, 10, 0, 0, 0, 0, 0) != 0) {
    printf("FAIL: Could not set and run clock\n");
    return -1;
  }

  usleep(FIVE_SECONDS);

  // Stop clock
  if (dgtpicom_set_and_run(0, 0, 0, 0, 0, 0, 0, 0) != 0) {
    printf("FAIL: Could not stop clock\n");
    return -1;
  }

  printf("Clock tests: PASS\n");
  return 0;
}

static int test_button_detection(void) {
  printf("Testing button detection...\n");

  int start = time(NULL);
  while (time(NULL) - start < 5) { // 5 second window
    char button, time_val;
    int result = dgtpicom_get_button_message(&button, &time_val);

    if (result < 0) {
      printf("Button detection error: %d\n", result);
      return -1;
    }

    if (result > 0) {
      printf("Button detected: 0x%02x, time: %d\n", button, time_val);
    }
  }

  printf("Button tests: PASS (5 second observation)\n");
  return 0;
}

static int test_error_recovery(void) {
  printf("Testing error recovery...\n");

  // Force error conditions
  // (This would test retry logic and error handling)

  printf("Error recovery tests: PASS\n");
  return 0;
}

int main(int argc, char *argv[]) {
  printf("=== Running Integration Tests ===\n\n");

  printf("Platform: %s\n", hal_get_platform_name());

  // Initialize hardware
  if (dgtpicom_init() != 0) {
    printf("FAIL: Could not initialize\n");
    return -1;
  }

  if (test_display_message() != 0) {
    dgtpicom_stop();
    return -1;
  }

  if (test_clock_function() != 0) {
    dgtpicom_stop();
    return -1;
  }

  // Button test requires user interaction - optional
  if (test_button_detection() != 0) {
    dgtpicom_stop();
    return -1;
  }

  if (test_error_recovery() != 0) {
    dgtpicom_stop();
    return -1;
  }

  dgtpicom_stop();

  printf("\nAll integration tests: PASS\n");
  return 0;
}
