# dgtpi
## DGTPi I2C communication with DGT3000

### Configure your pi:
Make sure the core frequency is locked. To do this you can add the following lines to /boot/config.txt:\
$ core_freq=250\
$ core_freq_min=250\
you can increase these frequencies if needed for 4k resolution on the pi 4

### Pin connections for DGT clock:
The I2C connection requires pull-up resistors (typically 4.7kΩ) on both SDA and SCL lines.

| Raspberry Pi Model | SDA (Data) | SCL (Clock) |
|--------------------|------------|-------------|
| Pi 4               | GPIO10     | GPIO11      |
| Pi 1/2/3           | GPIO2      | GPIO3       |

Note: On Pi 4, the I2C pins use ALT3 function, while on older models they use ALT0.


### How to compile:
to compile use:\
$ make

### The library dgtpicom.so can be used as described in dgtpicom.h

### The application dgtpicom can be used in three ways:
#### To display a message:
$ sudo ./dgtpicom "a message"\
you can add a beep and icons/dots:\
$ sudo ./dgtpicom "a message" 1 31 15

#### To run a clock:
$ sudo ./dgtpicom r 0 10 0 0 10 0\
you can run Left and Right up and down with L,R,l and r

#### to turn off and exit on power button:
$ sudo ./dgtpicom\
lever will pause, off button wil stop te app


