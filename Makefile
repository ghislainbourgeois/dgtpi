

CC=gcc

CFLAGS=-pthread -Wall -pedantic-errors

all:
	$(CC) $(CFLAGS) -o dgtpicom dgtpicom.c rpi.c main.c
	$(CC) $(CFLAGS) -shared -fPIC -o dgtpicom.so dgtpicom.c rpi.c
	
debug:
	$(CC) $(CFLAGS) -Ddebug -o dgtpicom dgtpicom.c rpi.c main.c
	$(CC) $(CFLAGS) -Ddebug -shared -fPIC -o dgtpicom.so dgtpicom.c rpi.c

debug2:
	$(CC) $(CFLAGS) -Ddebug -Ddebug2 -o dgtpicom dgtpicom.c rpi.c main.c
	$(CC) $(CFLAGS) -Ddebug -Ddebug2 -shared -fPIC -o dgtpicom.so dgtpicom.c rpi.c
