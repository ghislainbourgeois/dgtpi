#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// pointers to BCM2708/9 registers
volatile unsigned *gpio, *gpioset, *gpioclr, *gpioin;
volatile unsigned *i2cSlave, *i2cSlaveRSR, *i2cSlaveSLV, *i2cSlaveCR, *i2cSlaveFR;
volatile unsigned *i2cMaster, *i2cMasterS, *i2cMasterDLEN, *i2cMasterA, *i2cMasterFIFO, *i2cMasterDiv, *i2cMasterDel;
uint32_t *timerh;
uint32_t *timerl;

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

uint64_t * timer()
{
    static uint64_t i;
    i = ((uint64_t)*timerl << 32) + *timerh;
    return &i;
}
