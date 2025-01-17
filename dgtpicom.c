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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

#include "dgtpicom.h"
#include "dgtpicom_dgt3000.h"

int ww;
char piModel;

// while loop
void *wl(void *x) {
    int e;
    int i=0;
    while (1)
        if (ww) {
            e=dgtpicom_configure();
            if (e<0)
                printf("%d: Configure failed!\n",i);
            e=dgtpicom_set_text("Hello world",0,0,0);
            if (e<0)
                printf("%d: Display failed!\n",i);
            i++;
            #ifdef debug
            bug.sendTotal++;
            #endif
        } else {
            usleep(10000);
        }
    return 0;
}

int main (int argc, char *argv[]) {
    int e;
    char but,tim;

    // get direct acces to the perhicels
    if (dgtpicom_init()) return ERROR_MEM;

    // configure dgt3000 for mode 25
    e = dgtpicom_configure();
    if (e<0)
        return e;

    // 7 arguments -> setnrun
    if (argc==8) {
        unsigned char leftrun, rightrun=0;
        if (argv[1][0]=='l')
            leftrun=1;
        else if (argv[1][0]=='L')
            leftrun=2;
        else if (argv[1][0]=='r')
            rightrun=1;
        else if (argv[1][0]=='R')
            rightrun=2;

        dgtpicom_set_and_run(leftrun,
                atoi(argv[2]),
                atoi(argv[3]),
                atoi(argv[4]),
                rightrun,
                atoi(argv[5]),
                atoi(argv[6]),
                atoi(argv[7]) );
    } else if (argc>1) {
        unsigned char beep=0, ldots=0, rdots=0;
        dgtpicom_set_and_run(0,0,0,0,0,0,0,0);
        if (argc>2)
            beep=atoi(argv[2]);
        if (argc>4) {
            ldots=atoi(argv[3]);
            rdots=atoi(argv[4]);
        }

        if ( argv[1][0]=='~' ) {
            dgtpicom_set_text("DGT PI",beep,ldots,rdots);
            while(1) {
                usleep(1000000);
                dgtpicom_set_text(" DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("  DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("   DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("    DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("     DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("    DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("   DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("  DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text(" DGT PI",0,ldots,rdots);
                usleep(1000000);
                dgtpicom_set_text("DGT PI",0,ldots,rdots);
            }
        } else if ( argv[1][0]=='*' ){
            while(1) {
                if ( dgtpicom_set_text("  DGT PI  -",beep,ldots,rdots) != ERROR_OK )
                    i2cReset();
                usleep(200000);
                if ( dgtpicom_set_text("  DGT PI  ||",beep,ldots,rdots) != ERROR_OK )
                    i2cReset();
                usleep(200000);
                if ( dgtpicom_set_text("  DGT PI  |",beep,ldots,rdots) != ERROR_OK )
                    i2cReset();
                usleep(200000);
                if ( dgtpicom_set_text("  DGT PI  /",beep,ldots,rdots) != ERROR_OK )
                    i2cReset();
                usleep(200000);
            }
        } else {
            // try three times to end and set de display
            dgtpicom_set_text(argv[1],beep,ldots,rdots);
        }


    } else {
        #ifdef debug
        printf("  %.3f ",(float)*timer()/1000000);
        printf("started\n");
        usleep(10000);
        ww=1;
        pthread_t w;
        pthread_create(&w, NULL, wl, NULL);
        #else
        if ( dgtpicom_off(1) < 0 )
            dgtpicom_off(1);
        #endif
        but=tim=0;
        while(1) {
            if (dgtpicom_get_button_message(&but,&tim)) {
                if (but&0x40) {
                    if (ww)
                        ww=0;
                    else
                        ww=1;
                }
                if (but==0x20) {
                    break;
                }
                printf("%.3f ",(float)*timer()/1000000);
                printf("button=%02x, time=%d\n",but,tim);
            }

            usleep(10000);
        }
    }

    dgtpicom_stop();

    #ifdef debug
    printf("%.3f ",(float)*timer()/1000000);
    printf("After %d messages:\n",bug.sendTotal);
    printf("Send failed: display=%d, endDisplay=%d, changeState=%d, setCC=%d, setNRun=%d\n",
                bug.displaySF, bug.endDisplaySF, bug.changeStateSF, bug.setCCSF, bug.setNRunSF);
    printf("Ack failed : display=%d, endDisplay=%d, changeState=%d, setCC=%d, setNRun=%d\n",
                bug.displayAF, bug.endDisplayAF, bug.changeStateAF, bug.setCCAF, bug.setNRunAF);
    printf("Recieve Errors: timeout=%d, wrongAdr=%d, bufferFull=%d, sizeMismatch=%d, CRCFault=%d\n",
            bug.rxTimeout, bug.rxWrongAdr, bug.rxBufferFull, bug.rxSizeMismatch, bug.rxCRCFault);
    printf("Max recieve buffer size=%d\n",bug.rxMaxBuf);
    #endif

    // succes?
    return ERROR_OK;
}

// Get direct access to BCM2708/9 chip.
int dgtpicom_init() {
    int memfd;
    uint32_t base;
    void *gpio_map, *timer_map, *i2c_slave_map, *i2c_master_map;
    struct sched_param params;

    memset(&dgtRx,0,sizeof(dgtReceive_t));
    #ifdef debug
    memset(&bug,0,sizeof(debug_t));
    #endif

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
    timerh = (u_int32_t *)((char *)timer_map + 4);
    timerl = (u_int32_t *)((char *)timer_map + 8);

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

    // set to I2CMaster destination adress
    *i2cMasterA=8;

    dgtRx.on=1;

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
        e=dgt3000Mode25();
        if (e==ERROR_NACK || e==ERROR_NOACK) {
            // no postive ack, not in cc
            // set central controll
            // try 3 times
            setCCCount++;
            // setCC>3?
            if (setCCCount>3) {
                #ifdef debug
                ERROR_PIN_HI;
                printf("%.3f ",(float)*timer()/1000000);
                printf("sending setCentralControll failed three times\n\n");
                ERROR_PIN_LO;
                #endif
                return e;
            }
            usleep(10000);
            dgt3000SetCC();
        } else if (e==ERROR_TIMEOUT) {
            // timeout, line stay low -> reset i2c
            resetCount++;
            if (resetCount>1) {
                #ifdef debug
                ERROR_PIN_HI;
                printf("%.3f ",(float)*timer()/1000000);
                printf("I2C error, remove jack plug\n\n");
                ERROR_PIN_LO;
                #endif
                return e;
            }
            i2cReset();
            continue;
        } else if (e==ERROR_CST || e==ERROR_LINES) {
            // message not acked, probably collision
            continue;
        } else if (e==ERROR_SILENT) {
            // message not acked, probably clock off -> wake
            wakeCount++;

            // wake#>3? -> error
            if (wakeCount>3) {
                #ifdef debug
                ERROR_PIN_HI;
                printf("%.3f ",(float)*timer()/1000000);
                printf("sending wake command failed three times\n");
                ERROR_PIN_LO;
                #endif
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
int dgtpicom_set_and_run(char lr, char lh, char lm, char ls,
                         char rr, char rh, char rm, char rs) {
    int e;
    int sendCount = 0;

    setnrun[4]=lh;
    setnrun[5]=((lm/10)<<4) | (lm%10);
    setnrun[6]=((ls/10)<<4) | (ls%10);
    setnrun[7]=rh;
    setnrun[8]=((rm/10)<<4) | (rm%10);
    setnrun[9]=((rs/10)<<4) | (rs%10);
    setnrun[10]=lr | (rr<<2);

    crc_calc(setnrun);

    while (1) {
        sendCount++;
        if (sendCount>3) {
            #ifdef debug
            ERROR_PIN_HI;
            printf("%.3f ",(float)*timer()/1000000);
            printf("sending SetNRun failed three times on error%d\n\n",e);
            ERROR_PIN_LO;
            #endif
            return e;
        }

        e=dgt3000SetNRun(setnrun);

        // succes?
        if (e==ERROR_OK)
            return ERROR_OK;
    }
}

// Send set and run command to the dgt3000 with current clock values.
int dgtpicom_run(char lr, char rr) {
    return dgtpicom_set_and_run(
                lr,
                dgtRx.time[0],
                ((dgtRx.time[1]&0xf0)>>4)*10 + (dgtRx.time[1]&0x0f),
                ((dgtRx.time[2]&0xf0)>>4)*10 + (dgtRx.time[2]&0x0f),
                rr,
                dgtRx.time[3],
                ((dgtRx.time[4]&0xf0)>>4)*10 + (dgtRx.time[4]&0x0f),
                ((dgtRx.time[5]&0xf0)>>4)*10 + (dgtRx.time[5]&0x0f));
}

// Set a text message on the DGT3000.
int dgtpicom_set_text(char text[], char beep, char ld, char rd) {
    int i,e;
    int sendCount = 0;

    for (i=0;i<11;i++) {
        if(text[i]==0) break;
        display[i+4]=text[i];
    }

    for (;i<11;i++) {
        display[i+4]=32;
    }

    display[16]=beep;
    display[18]=ld;
    display[19]=rd;

    crc_calc(display);

    while (1) {
        sendCount++;
        if (sendCount>3) {
            #ifdef debug
            ERROR_PIN_HI;
            printf("%.3f ",(float)*timer()/1000000);
            printf("sending clear display failed three times on error%d\n\n",e);
            ERROR_PIN_LO;
            #endif
            return e;
        }

        e=dgt3000EndDisplay();
        // succes?
        if (e==ERROR_OK)
            break;
    }

    sendCount=0;
    while (1) {
        sendCount++;
        if (sendCount>3) {
            #ifdef debug
            ERROR_PIN_HI;
            printf("%.3f ",(float)*timer()/1000000);
            printf("sending display command failed three times on error%d\n\n",e);
            ERROR_PIN_LO;
            #endif
            return e;
        }
        // succes?
        e=dgt3000Display(display);
        if (e==ERROR_OK)
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
        if (sendCount>3) {
            #ifdef debug
            ERROR_PIN_HI;
            printf("%.3f ",(float)*timer()/1000000);
            printf("sending end display failed three times on error%d\n\n",e);
            ERROR_PIN_LO;
            #endif
            return e;
        }

        e=dgt3000EndDisplay();
        // succes?
        if (e==ERROR_OK)
            return ERROR_OK;
    }
}

// Put the last received time message in time[].
void dgtpicom_get_time(char time[]) {
    time[0]=dgtRx.time[0];
    time[1]=((dgtRx.time[1]&0xf0)>>4)*10 + (dgtRx.time[1]&0x0f);
    time[2]=((dgtRx.time[2]&0xf0)>>4)*10 + (dgtRx.time[2]&0x0f);
    time[3]=dgtRx.time[3];
    time[4]=((dgtRx.time[4]&0xf0)>>4)*10 + (dgtRx.time[4]&0x0f);
    time[5]=((dgtRx.time[5]&0xf0)>>4)*10 + (dgtRx.time[5]&0x0f);
}

// Get a button message from the buffer returns number of messages in
// the buffer or recieve error if one occured.
int dgtpicom_get_button_message(char *buttons, char *time) {
    int e=dgtRx.error;
    dgtRx.error=0;
    if (e<0)
        return e;

    //button availible?
    if(dgtRx.buttonStart != dgtRx.buttonEnd) {
        *buttons=dgtRx.buttonPres[dgtRx.buttonStart];
        *time=dgtRx.buttonTime[dgtRx.buttonStart];
        dgtRx.buttonStart=(dgtRx.buttonStart+1)%DGTRX_BUTTON_BUFFER_SIZE;
        return (dgtRx.buttonEnd-dgtRx.buttonStart)%DGTRX_BUTTON_BUFFER_SIZE + 1;
    } else {
        return ERROR_OK;
    }
}

// Return the current button state.
int dgtpicom_get_button_state() {
    return dgtRx.lastButtonState;
}

// Turn off the dgt3000.
int dgtpicom_off(char returnMode) {
    int e;

    mode25[4]=32+returnMode;
    crc_calc(mode25);

    mode25[4]=0;
    crc_calc(mode25);


    // send mode 25 message
    e=i2cSend(mode25,0x00);

    // send succesful?
    if (e<0) {
        #ifdef debug
        bug.changeStateSF++;
        #endif
        return e;
    }

    return ERROR_OK;
}

// Disable the I2C hardware.
void dgtpicom_stop() {
    // stop listening to broadcasts
    *i2cSlaveSLV=16;

    // stop thread
    dgtRx.on=0;

    // wait for thread to finish
    pthread_join(receiveThread, NULL);

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

// send a wake command to the dgt3000
int dgt3000Wake() {
    int e;
    u_int64_t t;

    // send wake
    *i2cMasterA=40;
    e=i2cSend(ping,0x00);
    *i2cMasterA=8;

    // succes? -> error. Wake messages should never get an Ack
    if (e==ERROR_OK) {
        #ifdef debug
        ERROR_PIN_HI;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending wake command failed, received Ack, this should never hapen\n");
        ERROR_PIN_LO;
        #endif
        return ERROR_NACK;
    }

    // Get Hello message (in max 10ms, usualy 5ms)
    t=*timer()+10000;
    while (*timer()<t) {
        if (dgtRx.hello==1)
            return ERROR_OK;
        usleep(100);
    }

    #ifdef debug
    ERROR_PIN_HI;
    printf("%.3f ",(float)*timer()/1000000);
    printf("sending wake command failed, no hello\n");
    ERROR_PIN_LO;
    #endif

    return ERROR_NOACK;
}

// send set central controll command to dgt3000
int dgt3000SetCC() {
    int e;

    // send setCC, error? retry
    e=i2cSend(centralControll,0x10);

    // send succedfull?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.setCCSF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending SetCentralControll command failed, sending failed\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // listen to our own adress and get Reply

    e=dgt3000GetAck(0x10,0x0f,10000);

    // ack received?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.setCCAF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending SetCentralControll command failed, no ack\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // is positive ack?
    if ((dgtRx.ack[1]&8) == 8)
        return ERROR_OK;

    #ifdef debug
    ERROR_PIN_HI;
    printf("%.3f ",(float)*timer()/1000000);
    printf("sending SetCentralControll command failed, negative ack, clock running\n");
    ERROR_PIN_LO;
    #endif

    // nack clock running
    return ERROR_NACK;
}

// send set mode 25 to dgt3000
int dgt3000Mode25() {
    int e;

    mode25[4]=57;
    crc_calc(mode25);

    // send mode 25 message
    e=i2cSend(mode25, 0x10);

    // send succesful?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.changeStateSF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending mode25 command failed, sending failed\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // listen to our own adress an get Reply
    e=dgt3000GetAck(0x10,0x0b,10000);

    // ack received?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.changeStateAF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending mode25 command failed, no ack\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    if (dgtRx.ack[1]==8) return ERROR_OK;

    #ifdef debug
    ERROR_PIN_HI;
    printf("%.3f ",(float)*timer()/1000000);
    printf("sending mode25 command failed, negative ack, not in Central Controll\n");
    ERROR_PIN_LO;
    #endif

    // negetive ack not in CC
    return ERROR_NACK;
}

// send end display to dgt3000 to clear te display
int dgt3000EndDisplay() {
    int e;

    // send end Display
    e=i2cSend(endDisplay,0x10);

    // send succesful?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.endDisplaySF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending end display command failed, sending failed\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // get fast Reply = already empty
    e=dgt3000GetAck(0x10,0x07,1200);

    // display already empty
    if (e==ERROR_OK) {
        if ((dgtRx.ack[1]&0x07) == 0x05) {
            return ERROR_OK;
        } else {
            #ifdef debug
            ERROR_PIN_HI;
            printf("%.3f ",(float)*timer()/1000000);
            printf("sending end display command failed, negative specific ack:%02x\n",dgtRx.ack[1]);
            ERROR_PIN_LO;
            #endif
            return ERROR_NACK;
        }
    }

    //get slow broadcast Reply = display changed
    e=dgt3000GetAck(0x00,0x07,10000);

    // ack received?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.endDisplayAF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending end display command failed, no ack\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // display emptied
    if ((dgtRx.ack[1]&0x07) == 0x00)
        return ERROR_OK;

    #ifdef debug
    ERROR_PIN_HI;
    printf("%.3f ",(float)*timer()/1000000);
    printf("sending end display command failed, negative broadcast ack:%02x\n",dgtRx.ack[1]);
    ERROR_PIN_LO;
    #endif

    return ERROR_NACK;
}

// send display command to dgt3000
int dgt3000Display(char dm[]) {
    int e;

    // send the message
    e=i2cSend(dm,0x00);

    // send succesful?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.displaySF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending display command failed, sending failed\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // get (broadcast) reply
    e=dgt3000GetAck(0x00,0x06,10000);

    // no reply
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.displayAF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending display command failed, no ack\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // nack, already displaying message
    if ((dgtRx.ack[1]&0xf3)==0x23) {
        #ifdef debug
        ERROR_PIN_HI;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending display command failed, display already busy\n");
        ERROR_PIN_LO;
        #endif
        return ERROR_NACK;
    }

    return ERROR_OK;
}

// send set and run command to dgt3000
int dgt3000SetNRun(char srm[]) {
    int e;

    e=i2cSend(srm,0x10);

    // send succesful?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.setNRunSF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending SetNRun command failed, sending failed\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // listen to our own adress an get Reply
    e=dgt3000GetAck(0x10,0x0a,10000);

    // ack received?
    if (e<0) {
        #ifdef debug
        ERROR_PIN_HI;
        bug.setNRunAF++;
        printf("%.3f ",(float)*timer()/1000000);
        printf("sending SetNRun command failed, no ack\n");
        ERROR_PIN_LO;
        #endif
        return e;
    }

    // Positive Ack?
    if (dgtRx.ack[1]==8)
        return ERROR_OK;

    // nack
    #ifdef debug
    ERROR_PIN_HI;
    printf("%.3f ",(float)*timer()/1000000);
    printf("sending SetNRun command failed, not in mode 25\n");
    ERROR_PIN_LO;
    #endif
    return ERROR_NACK;
}

// check for messages from dgt3000
void *dgt3000Receive(void *a) {
    char rm[RECEIVE_BUFFER_LENGTH];
    int e;
    #ifdef debug2
    int i;
    #endif

    #ifdef debug
    RECEIVE_THREAD_RUNNING_PIN_HI;
    #endif

    dgtRx.buttonRepeatTime = 0;

    while (dgtRx.on) {
        pthread_mutex_lock(&receiveMutex);
        if ( (*i2cSlaveFR&0x20) != 0 || (*i2cSlaveFR&2) == 0 ) {

            e=i2cReceive(rm);

            #ifdef debug2
            if (e>0) {
                printf("<- ");
                for (i=0;i<e;i++)
                    printf("%02x ", rm[i]);
            } else if (e<0) {
                printf("<- ");
                for (i=0;i<16;i++)
                    printf("%02x ", rm[i]);
            }
            #endif

            if (e>0) {
                switch (rm[3]) {
                    case 1:     // ack
                        dgtRx.ack[0]=rm[4];
                        dgtRx.ack[1]=rm[5];
                        pthread_cond_signal(&receiveCond);
                        #ifdef debug2
                        printf("= Ack %s\n",packetDescriptor[rm[4]-1]);
                        #endif
                        break;
                    case 2:     // hello
                        dgtRx.hello=1;
                        #ifdef debug2
                        printf("= Hello\n");
                        #endif
                        break;
                    case 4:     // time
                        dgtRx.time[0]=rm[5]&0x0f;
                        dgtRx.time[1]=rm[6];
                        dgtRx.time[2]=rm[7];
                        dgtRx.time[3]=rm[11]&0x0f;
                        dgtRx.time[4]=rm[12];
                        dgtRx.time[5]=rm[13];
                        // store (initial) lever state
                        if ((rm[19]&1) == 1)
                            dgtRx.lastButtonState |= 0x40;
                        else
                            dgtRx.lastButtonState &= 0xbf;
                        #ifdef debug2
                        printf("= Time: %02x:%02x.%02x %02x:%02x.%02x\n",rm[5]&0xf,rm[6],rm[7],rm[11]&0xf,rm[12],rm[13]);
                        #endif
                        if (rm[20]==1) ; // no update
                        break;
                    case 5:     // button
                        // new button pressed
                        if (rm[4]&0x1f) {
                            dgtRx.buttonState |= rm[4]&0x1f;
                            dgtRx.lastButtonState = rm[4];
                            dgtRx.buttonRepeatTime = *timer() + DGTPICOM_KEY_DELAY;
                            dgtRx.buttonCount = 0;

                            // buffer full?
                            if ((dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE == dgtRx.buttonStart) {
                                #ifdef debug
                                printf("%.3f ",(float)*timer()/1000000);
                                printf("Button buffer full, buttons ignored\n");
                                #endif
                            } else {
                                dgtRx.buttonPres[dgtRx.buttonEnd] = dgtRx.buttonState;
                                dgtRx.buttonTime[dgtRx.buttonEnd] = dgtRx.buttonCount;
                                dgtRx.buttonEnd = (dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE;
                            }
                        }
                        // turned off/on
                        if((rm[4]&0x20) != (rm[5]&0x20)) {
                            // buffer full?
                            if ((dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE == dgtRx.buttonStart) {
                                #ifdef debug
                                printf("%.3f ",(float)*timer()/1000000);
                                printf("Button buffer full, on/off ignored\n");
                                #endif
                            } else {
                                dgtRx.buttonPres[dgtRx.buttonEnd] = 0x20 | ((rm[5]&0x20)<<2);
                                dgtRx.buttonTime[dgtRx.buttonEnd] = 0;
                                dgtRx.buttonEnd = (dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE;
                            }
                        }

                        // lever change?
                        if((rm[4]&0x40) != (rm[5]&0x40)) {
                            // buffer full?
                            if ((dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE == dgtRx.buttonStart) {
                                #ifdef debug
                                printf("%.3f ",(float)*timer()/1000000);
                                printf("Button buffer full, lever change ignored\n");
                                #endif
                            } else {
                                dgtRx.buttonPres[dgtRx.buttonEnd] = 0x40 | ((rm[4]&0x40)<<1);
                                dgtRx.buttonTime[dgtRx.buttonEnd] = 0;
                                dgtRx.buttonEnd = (dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE;
                            }
                        }

                        // buttons released
                        if((rm[4]&0x1f) == 0 && dgtRx.buttonState != 0) {
                            dgtRx.buttonRepeatTime = 0;
                            dgtRx.buttonState = 0;
                        }
                        #ifdef debug2
                        printf("= Button: 0x%02x>0x%02x\n",rm[5]&0x7f,rm[4]&0x7f);
                        #endif
                        break;
                        #ifdef debug
                    default:
                        ERROR_PIN_HI;
                        printf("%.3f ",(float)*timer()/1000000);
                        printf("Receive Error: Unknown message from clock\n");
                        ERROR_PIN_LO;
                        #endif
                }
            } else  if (e<0) {
                #ifdef debug2
                printf(" = Error: %d\n",e);
                #endif
                dgtRx.error=e;
            }
        } else {
            if (dgtRx.buttonRepeatTime != 0 && dgtRx.buttonRepeatTime < *timer()) {
                dgtRx.buttonRepeatTime += DGTPICOM_KEY_REPEAT;
                dgtRx.buttonCount++;

                // buffer full?
                if ((dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE == dgtRx.buttonStart) {
                    #ifdef debug
                    printf("%.3f ",(float)*timer()/1000000);
                    printf("Button buffer full, repeated buttons ignored\n");
                    #endif
                } else {
                    dgtRx.buttonPres[dgtRx.buttonEnd] = dgtRx.buttonState;
                    dgtRx.buttonTime[dgtRx.buttonEnd] = dgtRx.buttonCount;
                    dgtRx.buttonEnd = (dgtRx.buttonEnd+1)%DGTRX_BUTTON_BUFFER_SIZE;
                }
            }
            #ifdef debug
            RECEIVE_THREAD_RUNNING_PIN_LO;
            usleep(400);
            RECEIVE_THREAD_RUNNING_PIN_HI;
            #endif

        }
        pthread_mutex_unlock(&receiveMutex);
        usleep(400);
    }
    #ifdef debug
    RECEIVE_THREAD_RUNNING_PIN_LO;
    #endif

    return ERROR_OK;
}

// wait for an Ack message
int dgt3000GetAck(char adr, char cmd, u_int64_t timeOut) {
    struct timespec receiveTimeOut;

    pthread_mutex_lock(&receiveMutex);

    // listen to given adress
    *i2cSlaveSLV=adr;

    // check until timeout
    timeOut+=*timer();
    receiveTimeOut.tv_sec=timeOut/1000000;
    receiveTimeOut.tv_nsec=timeOut%1000000;

    while (*timer()<timeOut) {
        if (dgtRx.ack[0]==cmd) {
            pthread_mutex_unlock(&receiveMutex);
            return ERROR_OK;
        }
        pthread_cond_timedwait(&receiveCond, &receiveMutex, &receiveTimeOut);
    }

    // listen for broadcast again
    *i2cSlaveSLV=0x00;
    pthread_mutex_unlock(&receiveMutex);

    if (dgtRx.ack[0]==cmd)
        return ERROR_OK;
    else
        return ERROR_NOACK;
}

// send message using I2CMaster
int i2cSend(char message[], char ackAdr) {
    int i, n;
    u_int64_t timeOut;

    // set length
    *i2cMasterDLEN = message[2]-1;

    // clear buffer
    *i2cMaster = 0x10;

    #ifdef debug2
    printf("-> %02x ", message[0]);
    #endif

    // fill the buffer
    for (n=1;n<message[2] && *i2cMasterS&0x10;n++) {
        #ifdef debug2
        printf("%02x ", message[n]);
        if(n == message[2]-1)
            printf("= %s\n",packetDescriptor[message[3]-1]);
        #endif
        *i2cMasterFIFO=message[n];
    }

    // check 256 times if the bus is free. At least for 50us because the clock will send waiting messages 50 us after the previeus one.
    timeOut=*timer() + 10000;   // bus should be free in 10ms
    #ifdef debug
    WAIT_FOR_FREE_BUS_PIN_HI;
    #endif
    for(i=0;i<256;i++) {
        // lines low (data is being send, or plug half inserted, or PI I2C peripheral crashed or ...)
        if ((SCL1IN==0) || (SDA1IN==0)) {
            i=0;
        }
        if ( ((*i2cSlaveFR&0x20)!=0) || ((*i2cSlaveFR&2)==0) ) {
            i=0;
        }
        // timeout waiting for bus free, I2C Error (or someone pushes 500 buttons/seccond)
        if (*timer()>timeOut) {
            #ifdef debug
            printf("%.3f ",(float)*timer()/1000000);
            printf("    Send error: Bus free timeout, waited more then 10ms for bus to be free\n");
            if(SCL1IN==0)
                printf("                SCL low. Remove jack?\n");
            if(SDA1IN==0)
                printf("                SDA low. Remove jack?\n");
            if((*i2cSlaveFR&0x20) != 0)
                printf("                I2C Slave receive busy, is the receive thread running?\n");
            if((*i2cSlaveFR&2) == 0)
                printf("                I2C Slave receive fifo not emtpy, is the receive thread running?\n");
            #endif
            return ERROR_TIMEOUT;
        }
    }
    pthread_mutex_lock(&receiveMutex);
    #ifdef debug
    WAIT_FOR_FREE_BUS_PIN_LO;
    #endif

    // clear ack and hello so we can receive a new ack or hello
    dgtRx.ack[0]=0;
    dgtRx.hello=0;

    // dont let the slave listen to 0 (wierd errors)?
    // listen to ack adress
    *i2cSlaveSLV = ackAdr;

    // start sending
    *i2cMasterS = 0x302;
    *i2cMaster = 0x8080;

    // write the rest of the message
    for (; n<message[2]; n++) {
        // wait for space in the buffer
        timeOut=*timer() + 10000;   // should be done in 10ms
        while((*i2cMasterS&0x10)==0) {
            if (*i2cMasterS&2) {
                *i2cSlaveSLV = 0x00;
                #ifdef debug
                printf("%.3f ",(float)*timer()/1000000);
                printf("    Send error: done before complete send\n");
                #endif
                break;
            }
            if (*timer()>timeOut) {
                *i2cSlaveSLV = 0x00;
                #ifdef debug
                printf("%.3f ",(float)*timer()/1000000);
                printf("    Send error: Buffer free timeout, waited more then 10ms for space in the buffer\n");
                #endif
                pthread_mutex_unlock(&receiveMutex);
                return ERROR_TIMEOUT;
            }
        }
        if (*i2cMasterS&2)
            break;
        #ifdef debug2
        printf("%02x ", message[n]);
        if(n == message[2]-1)
            printf("= %s\n",packetDescriptor[message[3]-1]);
        #endif
        *i2cMasterFIFO=message[n];
    }

    // wait for done
    timeOut=*timer() + 10000;   // should be done in 10ms
    while ((*i2cMasterS&2)==0)
        if (*timer()>timeOut) {
            *i2cSlaveSLV = 0x00;
            #ifdef debug
            printf("%.3f ",(float)*timer()/1000000);
            printf("    Send error: done timeout, waited more then 10ms for message to be finished sending\n");
            #endif
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
        // reset error flags
        *i2cMasterS=0x100;
        #ifdef debug
        printf("%.3f ",(float)*timer()/1000000);
        printf("    Send error: byte not Acked\n");
        #endif
    }
    if (*i2cMasterS&0x200) {
        // reset error flags
        *i2cMasterS=0x200;
        #ifdef debug
        printf("%.3f ",(float)*timer()/1000000);
        printf("    Send error: collision, clock stretch timeout\n");
        #endif

        // probably collision
        pthread_mutex_unlock(&receiveMutex);
        return ERROR_CST;
    }

    // clear fifo
    *i2cMaster|=0x10;

    if ((SCL1IN==0) || (SDA1IN==0) || ((*i2cSlaveFR&0x20)!=0) || ((*i2cSlaveFR&2)==0)) {
        #ifdef debug
        printf("%.3f ",(float)*timer()/1000000);
        printf("    Send error: collision, lines busy after send.\n");
        #endif

        // probably collision
        pthread_mutex_unlock(&receiveMutex);
        return ERROR_LINES;
    }

    // probably clock off
    pthread_mutex_unlock(&receiveMutex);
    return ERROR_SILENT;
}

// get message from I2C receive buffer
int i2cReceive(char m[]) {
    // todo implement end of packet check
    int i=1;
    u_int64_t timeOut;

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
            pthread_mutex_unlock(&receiveMutex);
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

// read register
static unsigned int dummyRead(volatile unsigned int *addr) {
    return *addr;
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

// print hex values
void hexPrint(char bytes[], int length) {
    int i;

    for (i=0;i<length;i++)
        printf("%02x ", bytes[i]);
    printf("\n");
}

// calculate checksum and put it in the last byte
char crc_calc(char *buffer) {
    int i;
    char crc_result = 0;
    char length = buffer[2]-1;

    for (i = 0; i < length; i++)
        crc_result = crc_table[ crc_result ^ buffer[i] ]; // new CRC will be the CRC of (old CRC XORed with data byte) - see http://sbs-forum.org/marcom/dc2/20_crc-8_firmware_implementations.pdf

    if (buffer[i]==crc_result)
        return ERROR_OK;
    buffer[i]=crc_result;
    return ERROR_CRC;
}

u_int64_t * timer()
{
    static u_int64_t i;
    i = ((u_int64_t)*timerl << 32) + *timerh;
    return &i;
}

// find out wich pi
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
