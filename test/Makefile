CC=gcc
CFLAGS=-Wall -Wextra -Wno-sign-compare -ggdb #-O2

all: dbg ddriver_test simple_test ioctl

ddriver_test: ddriver_test.c
	$(CC) $(CFLAGS) -pthread $< -o $@.o
dbg: dbg.c
	$(CC) $(CFLAGS) $< -o $@.o
ioctl: dbg.c
	$(CC) $(CFLAGS) -DTEST_ALL_IOCTL $< -o $@.o
simple_test: simple_test.c
	$(CC) $(CFLAGS) $< -o $@.o
clean:
	rm -i *.o 
