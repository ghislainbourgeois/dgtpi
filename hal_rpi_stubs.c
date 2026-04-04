#include "hal.h"
#include "rpi.h"
#include "clock_proto.h"
#include "dgtpicom_dgt3000.h"
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

// Global HAL instance - will be populated with current implementations
hal_ops_t hal;

// Extracted from rpi.c - hardware initialization
static int hal_rpi_init_hardware(void) {
    return initHw();
}

// Copy of i2cSend from dgtpicom.c (line 843-1007)
// This is the complex I2C send implementation
static int hal_rpi_i2c_send(const uint8_t *message, uint8_t msg_length, uint8_t ack_address) {
    int i, n;
    uint64_t timeOut;
    char m[256];
    
    // Convert uint8_t to char for compatibility
    for (i = 0; i < msg_length; i++) {
        m[i] = message[i];
    }

    // set length
    *i2cMasterDLEN = m[2]-1;

    // clear buffer
    *i2cMaster = 0x10;

    // fill the buffer
    for (n=1;n<m[2] && *i2cMasterS&0x10;n++) {
        *i2cMasterFIFO=m[n];
    }

    // check 256 times if the bus is free
    timeOut=*timer() + 10000;
    for(i=0;i<256;i++) {
        if ((SCL1IN==0) || (SDA1IN==0)) {
            i=0;
        }
        if ( ((*i2cSlaveFR&0x20)!=0) || ((*i2cSlaveFR&2)==0) ) {
            i=0;
        }
        if (*timer()>timeOut) {
            return ERROR_TIMEOUT;
        }
    }
    pthread_mutex_lock(&receiveMutex);

    // clear ack and hello so we can receive a new ack or hello
    dgtRx.ack[0]=0;
    dgtRx.hello=0;

    // listen to ack adress
    *i2cSlaveSLV = ack_address;

    // start sending
    *i2cMasterS = 0x302;
    *i2cMaster = 0x8080;

    // write the rest of the message
    for (; n<m[2]; n++) {
        timeOut=*timer() + 10000;
        while((*i2cMasterS&0x10)==0) {
            if (*i2cMasterS&2) {
                *i2cSlaveSLV = 0x00;
                break;
            }
            if (*timer()>timeOut) {
                *i2cSlaveSLV = 0x00;
                pthread_mutex_unlock(&receiveMutex);
                return ERROR_TIMEOUT;
            }
        }
        if (*i2cMasterS&2)
            break;
        *i2cMasterFIFO=m[n];
    }

    // wait for done
    timeOut=*timer() + 10000;
    while ((*i2cMasterS&2)==0)
        if (*timer()>timeOut) {
            *i2cSlaveSLV = 0x00;
            pthread_mutex_unlock(&receiveMutex);
            return ERROR_TIMEOUT;
        }

    // succes?
    if ((*i2cMasterS&0x300)==0) {
        pthread_mutex_unlock(&receiveMutex);
        return ERROR_OK;
    }

    *i2cSlaveSLV = 0x00;

    // collision or clock off
    if (*i2cMasterS&0x100) {
        *i2cMasterS=0x100;
    }
    if (*i2cMasterS&0x200) {
        *i2cMasterS=0x200;
        pthread_mutex_unlock(&receiveMutex);
        return ERROR_CST;
    }

    // clear fifo
    *i2cMaster|=0x10;

    if ((SCL1IN==0) || (SDA1IN==0) || ((*i2cSlaveFR&0x20)!=0) || ((*i2cSlaveFR&2)==0)) {
        pthread_mutex_unlock(&receiveMutex);
        return ERROR_LINES;
    }

    pthread_mutex_unlock(&receiveMutex);
    return ERROR_SILENT;
}

static int hal_rpi_i2c_receive_ready(void) {
    return i2cReadyToRead();
}

static int hal_rpi_i2c_receive(uint8_t *buffer, uint8_t max_length) {
    return i2cReceive((char *)buffer);
}

static void hal_rpi_i2c_set_destination(uint8_t address) {
    i2cDestination(address);
}

static void hal_rpi_i2c_listen_address(uint8_t address) {
    i2cListenAddress(address);
}

static void hal_rpi_i2c_reset(void) {
    i2cReset();
}

static uint64_t hal_rpi_get_timer_us(void) {
    return *timer();
}

static int hal_rpi_check_core_freq_mhz(void) {
    return checkCoreFreq();
}

static void hal_rpi_stop_hardware(void) {
    stopHw();
}

// Define HAL instance with all current implementations
hal_ops_t hal = {
    .i2c_send = hal_rpi_i2c_send,
    .i2c_receive_ready = hal_rpi_i2c_receive_ready,
    .i2c_receive = hal_rpi_i2c_receive,
    .i2c_set_destination = hal_rpi_i2c_set_destination,
    .i2c_listen_address = hal_rpi_i2c_listen_address,
    .i2c_reset = hal_rpi_i2c_reset,
    .get_timer_us = hal_rpi_get_timer_us,
    .check_core_freq_mhz = hal_rpi_check_core_freq_mhz,
};

int hal_init(void) {
    return hal_rpi_init_hardware();
}

void hal_cleanup(void) {
    hal_rpi_stop_hardware();
}

// Get platform name
const char* hal_get_platform_name(void) {
    // Use checkPiModel from rpi.c
    extern int checkPiModel(void);
    int model = checkPiModel();

    switch (model) {
        case 4: return "Raspberry Pi 4";
        case 3: return "Raspberry Pi 3";
        case 2: return "Raspberry Pi 2";
        case 1: return "Raspberry Pi 1/B+/Zero";
        default: return "Unknown";
    }
}
