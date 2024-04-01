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
 
#ifndef DGTPICOM_DGT3000_H
#define DGTPICOM_DGT3000_H

#include <stdint.h>
#include <pthread.h>

/* return codes:
 *   -10= no direct access to memory, run as root
 *   -9 = receive failed, software buffer overrun, should not happen
 *   -8 = receive failed, Hardware buffer overrun, load to hi
 *   -7 = receive failed, CRC error, probably noise
 *   -6 = sending failed, hardware timeout, probably hardware fault
 *   -5 = sending failed, lines low, probably collision
 *   -4 = sending failed, clock stretch timeout, probably collision
 *   -3 = sending failed, no response, probably clock off
 *   -2 = no ack received
 *   -1 = negative ack received, 
 *    0 = succes!
 */
 

#define	ERROR_MEM		-10
#define	ERROR_SWB_FULL	-9
#define	ERROR_HWB_FULL	-8
#define	ERROR_CRC		-7
#define	ERROR_TIMEOUT	-6
#define	ERROR_LINES		-5
#define	ERROR_CST		-4
#define	ERROR_SILENT	-3
#define	ERROR_NOACK		-2
#define	ERROR_NACK		-1
#define	ERROR_OK		0
 
//*** helping functions ***//


uint64_t * timer();

/* calculate checksum and put it in the last byte
	*buffer = pointer to buffer */
char crc_calc(char *buffer);

/* print hex values
	bytes = array of bytes
	length is number of bytes to print */
void hexPrint(char bytes[], int length);


//*** Low level I2C communication ***//

/* configure IO pins and I2C Master and Slave
	*/
void i2cReset();

/* get message from I2C receive buffer
	m[] = message buffer of 256 bytes
	timeOut = time to wait for packet in us (0=dont wait)
	returns:
	-6 = CRC Error
	-5 = I2C buffer overrun, at least 16 bytes received succesfully. rest is missing.
	-4 = our buffer overrun (should not happen)
	-3 = timeout
	-2 = I2C Error
	>0 = packet length*/
int i2cReceive(char m[]);

/* send message using I2CMaster
	 message[] = the message to send
	 returns:
	 -7 = message not Acked, probably clock off
	 -3 = message not Acked, probably collision
	 -2 = I2C Error
	 0 = Succes */
int i2cSend(char message[], char ackAdr);



//*** dgt3000 commands ***//

/* send a wake command to the dgt3000
	returns:
	-3 = wake ack error
	-1 = no hello message received
	0 = succes */
int dgt3000Wake();

/* send set central controll command to dgt3000
	returns:
	-3 = sending failed, clock off (or collision)
	-2 = sending failed, I2C error
	-1 = no (positive)ack received, not in CC
	0 = succes */
int dgt3000SetCC();

/* send set mode 25 to dgt3000
	returns:
	-3 = sending failed, clock off (or collision)
	-2 = sending failed, I2C error
	-1 = no (positive)ack received, not in CC
	0 = succes */
int dgt3000Mode25();

/* send set and run command to dgt3000
     */
int dgt3000SetNRun(char srm[]);

/* send end display to dgt3000 to clear te display
	returns:
	-3 = sending failed, clock off (or collision)
	-2 = sending failed, I2C error
	-1 = no (positive)ack received, not in CC
	0 = succes */
int dgt3000EndDisplay();

/* send set display command to dgt3000
	dm = display message to send
	returns:
	-3 = sending failed, clock off (or collision)
	-2 = sending failed, I2C error
	-1 = no (positive)ack received, not in CC
	0 = succes */
int dgt3000Display(char dm[]);

/* check for messages from dgt3000
	returns:
	0 = nothing is received
	1 = something is received
	2 = off button message is received */
void *dgt3000Receive(void *);

/* wait for an Ack message
	adr = adress to listen for ack
	cmd = command to ack
	timeOut = time to wait for ack
	returns:
	-3 = no Ack
	0 = Ack */
int dgt3000GetAck(char adr, char cmd, uint64_t timeOut);

#endif
