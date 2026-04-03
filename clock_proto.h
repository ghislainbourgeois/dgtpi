#ifndef CLOCK_PROTO_H
#define CLOCK_PROTO_H

#include <pthread.h>


#define DGTRX_BUTTON_BUFFER_SIZE 16
typedef struct {
	char on;
	char ack[2];
	char hello;
	char buttonPres[DGTRX_BUTTON_BUFFER_SIZE];
	char buttonTime[DGTRX_BUTTON_BUFFER_SIZE];
	int buttonStart;
	int buttonEnd;
	long long int buttonRepeatTime;
	char buttonCount;
	char buttonState;
	char lastButtonState;
	char time[6];
	int error;
} dgtReceive_t;

extern dgtReceive_t dgtRx;

extern pthread_t receiveThread;
extern pthread_mutex_t receiveMutex;
extern pthread_cond_t receiveCond;

extern char startMode;

// I2C message descriptors
extern char ping[];
extern char centralControll[];
extern char mode25[];
extern char endDisplay[];
extern char noAutoMessage[];
extern char display[];
extern char setnrun[];

extern const char* packetDescriptor[];

// pre-calculated CRC ATM-8 (x^8 + x^2 + x^1 + x^0)
extern const char crc_table[256];

#endif