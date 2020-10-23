CC=gcc
CFLAGS=-Wall -Wextra -Wno-sign-compare -ggdb -O2 #-DQUIET

objs = ddriver_test.o dbg.o ioctl.o simple_test.o time_test_gettimeoftheday.o time_test_tsc.o 
tsc_khz=$(shell sh tsc_khz_get.sh ) #NEED RUN AS ROOT!!!

all: $(objs)
.PHONY: all

ddriver_test.o: ddriver_test.c
	$(CC) $(CFLAGS) -pthread $< -o $@
dbg.o: dbg.c
	$(CC) $(CFLAGS) $< -o $@
ioctl.o: dbg.c
	$(CC) $(CFLAGS) -DTEST_ALL_IOCTL $< -o $@
simple_test.o: simple_test.c
	$(CC) $(CFLAGS) $< -o $@
time_test_gettimeoftheday.o: time_test.c
	$(CC) $(CFLAGS) -pthread -DGETTIMEOFDAY -DTSC_KHZ=$(tsc_khz)  $< -o $@
time_test_tsc.o: time_test.c
	$(CC) $(CFLAGS) -pthread -DTSC_KHZ=$(tsc_khz) $< -o $@
clean:
	rm -i *.o 
