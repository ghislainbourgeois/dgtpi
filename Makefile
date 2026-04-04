CC=gcc

CFLAGS=-pthread -Wall -pedantic-errors

all:
	$(CC) $(CFLAGS) -o dgtpicom dgtpicom.c hal_rpi234.c hal_rpi5.c hal_detect.c clock_proto.c main.c
	$(CC) $(CFLAGS) -shared -fPIC -o dgtpicom.so dgtpicom.c hal_rpi234.c hal_rpi5.c hal_detect.c clock_proto.c
	
test-protocol:
	$(CC) $(CFLAGS) -o test_protocol test_protocol.c -I.
	./test_protocol

test-integration:
	$(CC) $(CFLAGS) -o dgtpicom_test dgtpicom.c hal_rpi234.c hal_rpi5.c hal_detect.c clock_proto.c test_integration.c
	sudo ./dgtpicom_test

test: test-protocol test-integration

clean:
	rm -f *.o test_protocol dgtpicom_test dgtpicom dgtpicom.so
