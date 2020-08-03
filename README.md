# dgtpi
## DGTPi I2C communication with DGT3000

### Configure your pi:
Make sure the core frequency is locked. To do this you can add the following lines to /boot/config.txt:\
$ core_freq=250\
$ core_freq_min=250\
you can increase these frequencies if needed for 4k resolution on the pi 4


### How to compile:
to compile use:\
$ make

to compile with debug info use\
$ make debug

to compile with lots of debug info use\
$ make debug2

### The library dgtpicom.so can be used as described in dgtpicom.h

### The application dgtpicom can be used in three ways:
#### To display a message:
$ sudo ./dgtpicom "a message"\
you can add a beep and icons/dots:\
$ sudo ./dgtpicom "a message" 1 31 15

#### To run a clock:
$ sudo ./dgtpicom r 0 10 0 0 10 0\
you can run Left and Right up and down with L,R,l and r

#### to turn of and exit on power button (or run some tests in debug):
$ sudo ./dgtpicom\
lever will pause, off button wil stop te app


