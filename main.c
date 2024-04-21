#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dgtpicom.h"
#include "dgtpicom_dgt3000.h"
#include "debug.h"
#include "rpi.h"

int ww;

#ifdef debug
extern debug_t bug;
#endif

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
