#
# Copyright 2020 Andrea Di Iorio
# This file is part of linux_timed_messaging_system.
#
#  linux_timed_messaging_system is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  linux_timed_messaging_system is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with linux_timed_messaging_system.  If not, see <https://www.gnu.org/licenses/>.
#
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
