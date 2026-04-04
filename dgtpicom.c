/* functions to communicate to a DGT3000 using I2C
 * version 0.8
 *
 * Copyright (C) 2015 DGT
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "clock_proto.h"
#include "dgtpicom.h"
#include "dgtpicom_dgt3000.h"
#include "hal.h"

// Initialize communication with the hardware
int dgtpicom_init() {
  struct sched_param params;

  memset(&dgtRx, 0, sizeof(dgtReceive_t));

  hal_init();

  dgtRx.on = 1;

  pthread_create(&receiveThread, NULL, dgt3000Receive, NULL);

  // give thread max priority
  params.sched_priority = sched_get_priority_max(SCHED_FIFO);
  pthread_setschedparam(receiveThread, SCHED_FIFO, &params);

  return ERROR_OK;
}

// Configure the dgt3000.
int dgtpicom_configure() {
  int e;
  int wakeCount = 0;
  int setCCCount = 0;
  int resetCount = 0;

  // get the clock into the right state
  while (1) {
    // set to mode 25 and run
    e = dgt3000Mode25();
    if (e == ERROR_NACK || e == ERROR_NOACK) {
      // no postive ack, not in cc
      // set central controll
      // try 3 times
      setCCCount++;
      // setCC>3?
      if (setCCCount > 3) {
        return e;
      }
      usleep(10000);
      dgt3000SetCC();
    } else if (e == ERROR_TIMEOUT) {
      // timeout, line stay low -> reset i2c
      resetCount++;
      if (resetCount > 1) {
        return e;
      }
      hal.i2c_reset();
      continue;
    } else if (e == ERROR_CST || e == ERROR_LINES) {
      // message not acked, probably collision
      continue;
    } else if (e == ERROR_SILENT) {
      // message not acked, probably clock off -> wake
      wakeCount++;

      // wake#>3? -> error
      if (wakeCount > 3) {
        return e;
      }
      dgt3000Wake();
      continue;
    } else {
      // succes!
      break;
    }
  }
  return ERROR_OK;
}

// send set and run command to dgt3000
int dgtpicom_set_and_run(char lr, char lh, char lm, char ls, char rr, char rh,
                         char rm, char rs) {
  int e;
  int sendCount = 0;

  setnrun[4] = lh;
  setnrun[5] = ((lm / 10) << 4) | (lm % 10);
  setnrun[6] = ((ls / 10) << 4) | (ls % 10);
  setnrun[7] = rh;
  setnrun[8] = ((rm / 10) << 4) | (rm % 10);
  setnrun[9] = ((rs / 10) << 4) | (rs % 10);
  setnrun[10] = lr | (rr << 2);

  crc_calc(setnrun);

  while (1) {
    sendCount++;
    if (sendCount > 3) {
      return e;
    }

    e = dgt3000SetNRun(setnrun);

    // succes?
    if (e == ERROR_OK)
      return ERROR_OK;
  }
}

// Send set and run command to the dgt3000 with current clock values.
int dgtpicom_run(char lr, char rr) {
  return dgtpicom_set_and_run(
      lr, dgtRx.time[0],
      ((dgtRx.time[1] & 0xf0) >> 4) * 10 + (dgtRx.time[1] & 0x0f),
      ((dgtRx.time[2] & 0xf0) >> 4) * 10 + (dgtRx.time[2] & 0x0f), rr,
      dgtRx.time[3],
      ((dgtRx.time[4] & 0xf0) >> 4) * 10 + (dgtRx.time[4] & 0x0f),
      ((dgtRx.time[5] & 0xf0) >> 4) * 10 + (dgtRx.time[5] & 0x0f));
}

// Set a text message on the DGT3000.
int dgtpicom_set_text(char text[], char beep, char ld, char rd) {
  int i, e;
  int sendCount = 0;

  for (i = 0; i < 11; i++) {
    if (text[i] == 0)
      break;
    display[i + 4] = text[i];
  }

  for (; i < 11; i++) {
    display[i + 4] = 32;
  }

  display[16] = beep;
  display[18] = ld;
  display[19] = rd;

  crc_calc(display);

  while (1) {
    sendCount++;
    if (sendCount > 3) {
      return e;
    }

    e = dgt3000EndDisplay();
    // succes?
    if (e == ERROR_OK)
      break;
  }

  sendCount = 0;
  while (1) {
    sendCount++;
    if (sendCount > 3) {
      return e;
    }
    // succes?
    e = dgt3000Display(display);
    if (e == ERROR_OK)
      break;
  }
  return ERROR_OK;
}

// End a text message on the DGT3000 an return to clock mode.
int dgtpicom_end_text() {
  int e;
  int sendCount = 0;

  while (1) {
    sendCount++;
    if (sendCount > 3) {
      return e;
    }

    e = dgt3000EndDisplay();
    // succes?
    if (e == ERROR_OK)
      return ERROR_OK;
  }
}

// Put the last received time message in time[].
void dgtpicom_get_time(char time[]) {
  time[0] = dgtRx.time[0];
  time[1] = ((dgtRx.time[1] & 0xf0) >> 4) * 10 + (dgtRx.time[1] & 0x0f);
  time[2] = ((dgtRx.time[2] & 0xf0) >> 4) * 10 + (dgtRx.time[2] & 0x0f);
  time[3] = dgtRx.time[3];
  time[4] = ((dgtRx.time[4] & 0xf0) >> 4) * 10 + (dgtRx.time[4] & 0x0f);
  time[5] = ((dgtRx.time[5] & 0xf0) >> 4) * 10 + (dgtRx.time[5] & 0x0f);
}

// Get a button message from the buffer returns number of messages in
// the buffer or recieve error if one occured.
int dgtpicom_get_button_message(char *buttons, char *time) {
  int e = dgtRx.error;
  dgtRx.error = 0;
  if (e < 0)
    return e;

  // button availible?
  if (dgtRx.buttonStart != dgtRx.buttonEnd) {
    *buttons = dgtRx.buttonPres[dgtRx.buttonStart];
    *time = dgtRx.buttonTime[dgtRx.buttonStart];
    dgtRx.buttonStart = (dgtRx.buttonStart + 1) % DGTRX_BUTTON_BUFFER_SIZE;
    return (dgtRx.buttonEnd - dgtRx.buttonStart) % DGTRX_BUTTON_BUFFER_SIZE + 1;
  } else {
    return ERROR_OK;
  }
}

// Return the current button state.
int dgtpicom_get_button_state() { return dgtRx.lastButtonState; }

// Turn off the dgt3000.
int dgtpicom_off(char returnMode) {
  int e;

  mode25[4] = 32 + returnMode;
  crc_calc(mode25);

  mode25[4] = 0;
  crc_calc(mode25);

  // send mode 25 message
  e = hal.i2c_send((const uint8_t *)mode25, 6, 0x00);

  // send succesful?
  if (e < 0) {
    return e;
  }

  return ERROR_OK;
}

// Disable the I2C hardware.
void dgtpicom_stop() {
  // stop listening to broadcasts
  hal.i2c_listen_address(0x10);

  // stop thread
  dgtRx.on = 0;

  // wait for thread to finish
  pthread_join(receiveThread, NULL);

  hal_cleanup();
}

// send a wake command to the dgt3000
int dgt3000Wake() {
  int e;
  uint64_t t;

  // send wake
  hal.i2c_set_destination(40);
  e = hal.i2c_send((const uint8_t *)ping, 5, 0x00);
  hal.i2c_set_destination(8);

  // succes? -> error. Wake messages should never get an Ack
  if (e == ERROR_OK) {
    return ERROR_NACK;
  }

  // Get Hello message (in max 10ms, usualy 5ms)
  t = *timer() + 10000;
  while (*timer() < t) {
    if (dgtRx.hello == 1)
      return ERROR_OK;
    usleep(100);
  }

  return ERROR_NOACK;
}

// send set central controll command to dgt3000
int dgt3000SetCC() {
  int e;

  // send setCC, error? retry
  e = hal.i2c_send((const uint8_t *)centralControll, 5, 0x10);

  // send succedfull?
  if (e < 0) {
    return e;
  }

  // listen to our own adress and get Reply

  e = dgt3000GetAck(0x10, 0x0f, 10000);

  // ack received?
  if (e < 0) {
    return e;
  }

  // is positive ack?
  if ((dgtRx.ack[1] & 8) == 8)
    return ERROR_OK;

  // nack clock running
  return ERROR_NACK;
}

// send set mode 25 to dgt3000
int dgt3000Mode25() {
  int e;

  mode25[4] = 57;
  crc_calc(mode25);

  // send mode 25 message
  e = hal.i2c_send((const uint8_t *)mode25, 6, 0x10);

  // send succesful?
  if (e < 0) {
    return e;
  }

  // listen to our own adress an get Reply
  e = dgt3000GetAck(0x10, 0x0b, 10000);

  // ack received?
  if (e < 0) {
    return e;
  }

  if (dgtRx.ack[1] == 8)
    return ERROR_OK;

  // negetive ack not in CC
  return ERROR_NACK;
}

// send end display to dgt3000 to clear te display
int dgt3000EndDisplay() {
  int e;

  // send end Display
  e = hal.i2c_send((const uint8_t *)endDisplay, 5, 0x10);

  // send succesful?
  if (e < 0) {
    return e;
  }

  // get fast Reply = already empty
  e = dgt3000GetAck(0x10, 0x07, 1200);

  // display already empty
  if (e == ERROR_OK) {
    if ((dgtRx.ack[1] & 0x07) == 0x05) {
      return ERROR_OK;
    } else {
      return ERROR_NACK;
    }
  }

  // get slow broadcast Reply = display changed
  e = dgt3000GetAck(0x00, 0x07, 10000);

  // ack received?
  if (e < 0) {
    return e;
  }

  // display emptied
  if ((dgtRx.ack[1] & 0x07) == 0x00)
    return ERROR_OK;

  return ERROR_NACK;
}

// send display command to dgt3000
int dgt3000Display(char dm[]) {
  int e;

  // send the message
  e = hal.i2c_send((const uint8_t *)dm, dm[2] + 1, 0x00);

  // send succesful?
  if (e < 0) {
    return e;
  }

  // get (broadcast) reply
  e = dgt3000GetAck(0x00, 0x06, 10000);

  // no reply
  if (e < 0) {
    return e;
  }

  // nack, already displaying message
  if ((dgtRx.ack[1] & 0xf3) == 0x23) {
    return ERROR_NACK;
  }

  return ERROR_OK;
}

// send set and run command to dgt3000
int dgt3000SetNRun(char srm[]) {
  int e;

  e = hal.i2c_send((const uint8_t *)srm, srm[2] + 1, 0x10);

  // send succesful?
  if (e < 0) {
    return e;
  }

  // listen to our own adress an get Reply
  e = dgt3000GetAck(0x10, 0x0a, 10000);

  // ack received?
  if (e < 0) {
    return e;
  }

  // Positive Ack?
  if (dgtRx.ack[1] == 8)
    return ERROR_OK;

  // nack
  return ERROR_NACK;
}

// check for messages from dgt3000
void *dgt3000Receive(void *a) {
  char rm[RECEIVE_BUFFER_LENGTH];
  int e;

  dgtRx.buttonRepeatTime = 0;

  while (dgtRx.on) {
    pthread_mutex_lock(&receiveMutex);
    if (hal.i2c_receive_ready()) {

      e = hal.i2c_receive((uint8_t *)rm, (uint8_t)RECEIVE_BUFFER_LENGTH);

      if (e > 0) {
        switch (rm[3]) {
        case 1: // ack
          dgtRx.ack[0] = rm[4];
          dgtRx.ack[1] = rm[5];
          pthread_cond_signal(&receiveCond);
          break;
        case 2: // hello
          dgtRx.hello = 1;
          break;
        case 4: // time
          dgtRx.time[0] = rm[5] & 0x0f;
          dgtRx.time[1] = rm[6];
          dgtRx.time[2] = rm[7];
          dgtRx.time[3] = rm[11] & 0x0f;
          dgtRx.time[4] = rm[12];
          dgtRx.time[5] = rm[13];
          // store (initial) lever state
          if ((rm[19] & 1) == 1)
            dgtRx.lastButtonState |= 0x40;
          else
            dgtRx.lastButtonState &= 0xbf;
          if (rm[20] == 1)
            ; // no update
          break;
        case 5: // button
          // new button pressed
          if (rm[4] & 0x1f) {
            dgtRx.buttonState |= rm[4] & 0x1f;
            dgtRx.lastButtonState = rm[4];
            dgtRx.buttonRepeatTime = *timer() + DGTPICOM_KEY_DELAY;
            dgtRx.buttonCount = 0;

            // buffer full?
            if ((dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE ==
                dgtRx.buttonStart) {
            } else {
              dgtRx.buttonPres[dgtRx.buttonEnd] = dgtRx.buttonState;
              dgtRx.buttonTime[dgtRx.buttonEnd] = dgtRx.buttonCount;
              dgtRx.buttonEnd =
                  (dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE;
            }
          }
          // turned off/on
          if ((rm[4] & 0x20) != (rm[5] & 0x20)) {
            // buffer full?
            if ((dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE ==
                dgtRx.buttonStart) {
            } else {
              dgtRx.buttonPres[dgtRx.buttonEnd] = 0x20 | ((rm[5] & 0x20) << 2);
              dgtRx.buttonTime[dgtRx.buttonEnd] = 0;
              dgtRx.buttonEnd =
                  (dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE;
            }
          }

          // lever change?
          if ((rm[4] & 0x40) != (rm[5] & 0x40)) {
            // buffer full?
            if ((dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE ==
                dgtRx.buttonStart) {
            } else {
              dgtRx.buttonPres[dgtRx.buttonEnd] = 0x40 | ((rm[4] & 0x40) << 1);
              dgtRx.buttonTime[dgtRx.buttonEnd] = 0;
              dgtRx.buttonEnd =
                  (dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE;
            }
          }

          // buttons released
          if ((rm[4] & 0x1f) == 0 && dgtRx.buttonState != 0) {
            dgtRx.buttonRepeatTime = 0;
            dgtRx.buttonState = 0;
          }
          break;
        }
      } else if (e < 0) {
        dgtRx.error = e;
      }
    } else {
      if (dgtRx.buttonRepeatTime != 0 && dgtRx.buttonRepeatTime < *timer()) {
        dgtRx.buttonRepeatTime += DGTPICOM_KEY_REPEAT;
        dgtRx.buttonCount++;

        // buffer full?
        if ((dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE ==
            dgtRx.buttonStart) {
        } else {
          dgtRx.buttonPres[dgtRx.buttonEnd] = dgtRx.buttonState;
          dgtRx.buttonTime[dgtRx.buttonEnd] = dgtRx.buttonCount;
          dgtRx.buttonEnd = (dgtRx.buttonEnd + 1) % DGTRX_BUTTON_BUFFER_SIZE;
        }
      }
    }
    pthread_mutex_unlock(&receiveMutex);
    usleep(400);
  }

  return ERROR_OK;
}

// wait for an Ack message
int dgt3000GetAck(char adr, char cmd, uint64_t timeOut) {
  struct timespec receiveTimeOut;

  pthread_mutex_lock(&receiveMutex);

  // listen to given adress
  hal.i2c_listen_address(adr);

  // check until timeout
  timeOut += *timer();
  receiveTimeOut.tv_sec = timeOut / 1000000;
  receiveTimeOut.tv_nsec = timeOut % 1000000;

  while (*timer() < timeOut) {
    if (dgtRx.ack[0] == cmd) {
      pthread_mutex_unlock(&receiveMutex);
      return ERROR_OK;
    }
    pthread_cond_timedwait(&receiveCond, &receiveMutex, &receiveTimeOut);
  }

  // listen for broadcast again
  hal.i2c_listen_address(0x00);

  pthread_mutex_unlock(&receiveMutex);

  if (dgtRx.ack[0] == cmd)
    return ERROR_OK;
  else
    return ERROR_NOACK;
}

// send message using I2CMaster
// send message using I2CMaster (wrapper for hal)
int i2cSend(char message[], char ackAdr) {
  return hal.i2c_send((const uint8_t *)message, message[2] + 1, ackAdr);
}

// print hex values
void hexPrint(char bytes[], int length) {
  int i;

  for (i = 0; i < length; i++)
    printf("%02x ", bytes[i]);
  printf("\n");
}

// calculate checksum and put it in the last byte
char crc_calc(char *buffer) {
  int i;
  char crc_result = 0;
  char length = buffer[2] - 1;

  for (i = 0; i < length; i++)
    crc_result = crc_table
        [crc_result ^
         buffer
             [i]]; // new CRC will be the CRC of (old CRC XORed with data byte)
                   // - see
                   // http://sbs-forum.org/marcom/dc2/20_crc-8_firmware_implementations.pdf

  if (buffer[i] == crc_result)
    return ERROR_OK;
  buffer[i] = crc_result;
  return ERROR_CRC;
}
