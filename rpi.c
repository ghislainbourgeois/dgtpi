#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rpi.h"
#include "dgtpicom_dgt3000.h"

// pointers to BCM2708/9 registers
volatile unsigned *gpio, *gpioset, *gpioclr, *gpioin;
volatile unsigned *i2cSlave, *i2cSlaveRSR, *i2cSlaveSLV, *i2cSlaveCR, *i2cSlaveFR;
volatile unsigned *i2cMaster, *i2cMasterS, *i2cMasterDLEN, *i2cMasterA, *i2cMasterFIFO, *i2cMasterDiv, *i2cMasterDel;
uint32_t *timerh;
uint32_t *timerl;
char piModel;

// read register
static unsigned int dummyRead(volatile unsigned int *addr) {
    return *addr;
}

/* find out which pi
	returns:
	0 = error
	1 = Pi b+
	2 = Pi 2
        3 = Pi 3
        4 = Pi 4
*/
int checkPiModel() {
    FILE *cpuFd ;
    char line [120] ;

    if ((cpuFd = fopen ("/proc/cpuinfo", "r")) == NULL)
        #ifdef debug
        printf("Unable to open /proc/cpuinfo")
        #endif
        ;

    // looking for the revision....
    while (fgets (line, 120, cpuFd) != NULL)
        if (strncmp (line, "Revision", 8) == 0) {
            if ( line[13] == '3' ) {    // BCM2838
                fclose(cpuFd);
                return 4;   // PI 4b
            } else if ( line[13] == '2' ) { // BCM2837
                fclose(cpuFd);
                return 3;   // PI 3b(+)
            } else if ( line[13] == '1' ) { // BCM2836
                fclose(cpuFd);
                return 2;   // PI 2b
            } else {            // BCM2835
                fclose(cpuFd);
                return 1;   // PI a, b, zero (+)
            }
        }
    fclose(cpuFd);
    return 0;
}

// configure IO pins and I2C Master and Slave
void i2cReset() {
    int freq;

    *i2cSlaveCR = 0;
    *i2cMaster = 0x10;
    *i2cMaster = 0x0000;

    // pinmode GPIO2,GPIO3=input (togle via input to reset i2C master(sometimes hangs))
    *gpio &= 0xfffff03f;
    if (piModel==4)
    {
        // pinmode GPIO10,GPIO11=input (togle via input to reset)
        *(gpio+1) &= 0xffffffc0;
    }
    else
    {
        // pinmode GPIO18,GPIO19=input (togle via input to reset)
        *(gpio+1) &= 0xc0ffffff;
    }
    // send something in case master hangs
    *i2cMasterDLEN = 0;
    while((*i2cSlaveFR&2) == 0) {
        dummyRead(i2cSlave);
    }
    usleep(2000);   // not tested! some delay maybe needed
    *i2cSlaveCR = 0x285;
    *i2cMasterS = 0x302;
    *i2cMaster = 0x8010;
    // pinmode GPIO2,GPIO3=ALT0
    *gpio |= 0x900;
    if (piModel==4)
    {
        // pinmode GPIO10,GPIO11=ALT3
        *(gpio+1) |= 0x0000003f;
    }
    else
    {
        // pinmode GPIO18,GPIO19=ALT3
        *(gpio+1) |= 0x3f000000;
    }

    usleep(1000);   // not tested! some delay maybe needed

    #ifdef debug
    if ((SDA1IN==0) || (SCL1IN==0)) {
        printf("I2C Master might be stuck in transfer?\n");
        printf("FIFO=%x\n",*i2cMasterFIFO);
        printf("C   =%x\n",*i2cMaster);
        printf("S   =%x\n",*i2cMasterS);
        printf("DLEN=%x\n",*i2cMasterDLEN);
        printf("A   =%x\n",*i2cMasterA);
        printf("FIFO=%x\n",*i2cMasterFIFO);
        printf("DIV =%x\n",*i2cMasterDiv);
        printf("Del =%x\n",*i2cMasterDel);

        printf("I2C Slave might be stuck in transfer?\n");
        printf("DR  =%x\n",*i2cSlave);
        printf("RSR =%x\n",*i2cSlaveRSR);
        printf("SLV =%x\n",*i2cSlaveSLV);
        printf("CR  =%x\n",*i2cSlaveCR);
        printf("FR  =%x\n",*i2cSlaveFR);

        printf("SDA=%x\n",SDA1IN);
        printf("SCL=%x\n",SCL1IN);
    }
    // pinmode GPIO17,GPIO27,GPIO22=output for debugging
    *(gpio+1) = (*(gpio+1)&0xff1fffff) | 0x00200000;    // GIO17
    *(gpio+2) = (*(gpio+2)&0xff1fffff) | 0x00200000;    // GIO27
    *(gpio+2) = (*(gpio+2)&0xfffffe3f) | 0x00000040;    // GIO22
    #endif

    // set i2c slave control register to break and off
    *i2cSlaveCR = 0x80;
    // set i2c slave control register to enable: receive, i2c, device
    *i2cSlaveCR = 0x205;
    // set i2c slave address 0x00 to listen to broadcasts
    *i2cSlaveSLV = 0x0;
    // reset errors
    *i2cSlaveRSR = 0;

    freq = checkCoreFreq();
    #ifdef debug
    printf("Reset I2C device, core freq = %i MHz\n", freq);
    #endif
    *i2cMasterDiv = 1000*freq/95;
    if ( freq > 300 )
        *i2cMasterDel = 0x600060;
}

// Get access to required hardware and initialize it
int initHw() {
    int memfd;
    uint32_t base;
    void *gpio_map, *timer_map, *i2c_slave_map, *i2c_master_map;

    piModel = checkPiModel();
    if (piModel==4)
        base=0xfe000000;
    else if (piModel==1)
        base=0x20000000;
    else
        base=0x3f000000;

    memfd = open("/dev/mem",O_RDWR|O_SYNC);
    if(memfd < 0) {
        #ifdef debug
        printf("/dev/mem open error, run as root\n");
        #endif
        return ERROR_MEM;
    }

    gpio_map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, GPIO_BASE+base);
    timer_map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, TIMER_BASE+base);
    i2c_slave_map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, I2C_SLAVE_BASE+base);
    i2c_master_map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, I2C_MASTER_BASE+base);

    close(memfd);

    if( gpio_map == MAP_FAILED || timer_map == MAP_FAILED || i2c_slave_map == MAP_FAILED || i2c_master_map == MAP_FAILED) {
        #ifdef debug
        printf("Map failed\n");
        #endif
        return ERROR_MEM;
    }

    // GPIO pointers
    gpio =  (volatile unsigned *)gpio_map;
    gpioset = gpio + 7;     // set bit register offset 28
    gpioclr = gpio + 10;    // clr bit register
    gpioin = gpio + 13;     // read all bits register

    // timer pointer
    timerh = (uint32_t *)((char *)timer_map + 4);
    timerl = (uint32_t *)((char *)timer_map + 8);

    // i2c slave pointers
    i2cSlave = (volatile unsigned *)i2c_slave_map;
    i2cSlaveRSR = i2cSlave + 1;
    i2cSlaveSLV = i2cSlave + 2;
    i2cSlaveCR = i2cSlave + 3;
    i2cSlaveFR = i2cSlave + 4;

    // i2c master pointers
    i2cMaster = (volatile unsigned *)i2c_master_map;
    i2cMasterS = i2cMaster + 1;
    i2cMasterDLEN = i2cMaster + 2;
    i2cMasterA = i2cMaster + 3;
    i2cMasterFIFO = i2cMaster + 4;
    i2cMasterDiv = i2cMaster + 5;
    i2cMasterDel = i2cMaster + 6;

    // check wiring
    // configured as an output? probably in use for something else
    if ((*gpio & 0x1c0) == 0x40) {
        #ifdef debug
        printf("Error, GPIO02 configured as output, in use? We asume not a DGTPI\n");
        #endif
        return ERROR_LINES;
    }
    if ((*gpio & 0xe00) == 0x200) {
        #ifdef debug
        printf("Error, GPIO03 configured as output, in use? We asume not a DGTPI\n");
        #endif
        return ERROR_LINES;
    }
    if  (piModel==4)
    {
        if ((*(gpio+1) & 0x07) == 0x01) {
            #ifdef debug
            printf("Error, GPIO10 configured as output, in use? We asume not a DGTPI\n");
            #endif
            return ERROR_LINES;
        }
        if ((*(gpio+1) & 0x38) == 0x08) {
            #ifdef debug
            printf("Error, GPIO11 configured as output, in use? We asume not a DGTPI\n");
            #endif
            return ERROR_LINES;
        }
    }
    else
    {
        if ((*(gpio+1) & 0x07000000) == 0x01000000) {
            #ifdef debug
            printf("Error, GPIO18 configured as output, in use? We asume not a DGTPI\n");
            #endif
            return ERROR_LINES;
        }
        if ((*(gpio+1) & 0x38000000) == 0x08000000) {
            #ifdef debug
            printf("Error, GPIO19 configured as output, in use? We asume not a DGTPI\n");
            #endif
            return ERROR_LINES;
        }
    }
    // pinmode GPIO2,GPIO3=input
    *gpio &= 0xfffff03f;
    if  (piModel==4)
    {
        // pinmode GPIO10,GPIO11=input
        *(gpio+1) &= 0xffffffc0;
    }
    else
    {
        // pinmode GPIO18,GPIO19=input
        *(gpio+1) &= 0xc0ffffff;
    }
    usleep(1);
    // all pins hi through pullup?
    if  (piModel==4)
    {
        if ((*gpioin & 0x0c0c)!=0x0c0c) {
            #ifdef debug
            printf("Error, pin(s) low, shortcircuit, or no connection?\n");
            #endif
            return ERROR_LINES;
        }
    }
    else
    {
        if ((*gpioin & 0xc000c)!=0xc000c) {
            #ifdef debug
            printf("Error, pin(s) low, shortcircuit, or no connection?\n");
            #endif
            return ERROR_LINES;
        }
    }

    i2cReset();
    i2cDestination(0x08);
    return ERROR_OK;
}

void stopHw() {
    // disable i2cSlave device
    *i2cSlaveCR=0;

    // pinmode GPIO2,GPIO3=input
    *gpio &= 0xfffff03f;
    if (piModel==4)
    {
        // pinmode GPIO10,GPIO11=input
        *(gpio+1) &= 0xffffffc0;
    }
    else
    {
        // pinmode GPIO18,GPIO19=input
        *(gpio+1) &= 0xc0ffffff;
    }
}

void i2cDestination(char addr) {
    *i2cMasterA = addr;
}

void i2cListenAddress(char addr) {
    *i2cSlaveSLV = addr;
}

int i2cReadyToRead() {
    if ( (*i2cSlaveFR&0x20) != 0 || (*i2cSlaveFR&2) == 0 ) {
        return 1;
    }
    return 0;

}

// get message from I2C receive buffer
int i2cReceive(char m[]) {
    // todo implement end of packet check
    int i=1;
    uint64_t timeOut;

    m[0]=*i2cSlaveSLV*2;

    // a message should be finished receiving in 10ms
    timeOut=*timer()+10000;

    #ifdef debug
    if (bug.rxMaxBuf<(*i2cSlaveFR&0xf800)>>11)
        bug.rxMaxBuf=(*i2cSlaveFR&0xf800)>>11;
    #endif

    // while I2CSlave is receiving or byte availible
    while( ((*i2cSlaveFR&0x20) != 0) || ((*i2cSlaveFR&2) == 0) ) {

        // timeout
        if (timeOut<*timer()) {
            #ifdef debug
            ERROR_PIN_HI;
            printf("%.3f ",(float)*timer()/1000000);
            printf("    Receive error: Timeout, hardware stays in receive mode for more then 10ms\n");
            bug.rxTimeout++;
            hexPrint(m,i);
            ERROR_PIN_LO;
            #endif
            return ERROR_TIMEOUT;
        }

        // when a byte is availible, store it
        if((*i2cSlaveFR&2) == 0) {
            m[i]=*i2cSlave & 0xff;
            i++;
            // complete packet
            if (i>2 && i>=m[2])
                break;
            if (i >= RECEIVE_BUFFER_LENGTH) {
                #ifdef debug
                ERROR_PIN_HI;
                bug.rxWrongAdr++;
                printf("%.3f ",(float)*timer()/1000000);
                printf("    Receive error: Buffer overrun, size to large for the supplied buffer %d bytes.\n",i);
                hexPrint(m,i);
                ERROR_PIN_LO;
                #endif
                return ERROR_SWB_FULL;
            }
        } else {
        // no byte availible receiving a new one will take 70us
            #ifdef debug
            RECEIVE_THREAD_RUNNING_PIN_LO;
            #endif
            usleep(10);
            #ifdef debug
            RECEIVE_THREAD_RUNNING_PIN_HI;
            #endif
        }
    }

    // listen for broadcast again
    *i2cSlaveSLV=0x00;

    m[i]=-1;

    // nothing?
    if (i==1)
        return ERROR_OK;

    // dgt3000 sends to 0 bytes after some packets
    if (i==3 && m[1]==0 && m[2]==0)
        return ERROR_OK;

    // not from clock?
    if (m[1]!=16) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.rxWrongAdr++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("    Receive error: Wrong adress, Received message not from clock (16) but from %d.\n",m[1]);
        hexPrint(m,i);
        ERROR_PIN_LO;
        #endif
        return ERROR_NACK;
    }

    // errors?
    if (*i2cSlaveRSR&1 || i<5 || i!=m[2] )  {
        #ifdef debug
        ERROR_PIN_HI;
        printf("%.3f ",(float)*timer()/1000000);
        if(*i2cSlaveRSR&1) {
            printf("    Receive error: Hardware buffer full.\n");
            bug.rxBufferFull++;
        } else {
            if (i<5)
                printf("    Receive Error: Packet to small, %d bytes.\n",i);
            else
                printf("    Receive Error: Size mismatch, packet length is %d bytes but received %d bytes.\n",m[2],i);
            bug.rxSizeMismatch++;
        }
        hexPrint(m,i);
        ERROR_PIN_LO;
        #endif
        *i2cSlaveRSR=0;
        return ERROR_HWB_FULL;
    }

    if (crc_calc(m)) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.rxCRCFault++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("    Receive error: CRC Error\n");
        hexPrint(m,i);
        ERROR_PIN_LO;
        #endif
        return ERROR_CRC;
    }

    return i;
}

int checkCoreFreq() {
    FILE *fp;
    char line[100];

    /* Open the command for reading. */
    fp = popen("vcgencmd measure_clock core", "r");
    if (fp == NULL) {
        #ifdef debug
        printf("Failed to measure core clock\n" );
        #endif
        return 250;
    }

    /* Read the output a line at a time - output it. */
    fgets(line, sizeof(line), fp);

    /* close */
    pclose(fp);

    return atoi(line+13)/1000000;
}

uint64_t * timer()
{
    static uint64_t i;
    i = ((uint64_t)*timerl << 32) + *timerh;
    return &i;
}
