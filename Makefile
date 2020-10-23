ccflags-y :=  -Wall -Wextra  -Wno-unused  #-DQUIET #-ggdb -O2
obj-m := timed_msg_sys.o 
timed_msg_sys-y :=  ./src/utils.o  ./src/core.o ./src/timed_messaging_sys.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
install: all
	insmod timed_msg_sys.ko
.PHONY: clean test 
test: 
	$(MAKE) -C test
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
