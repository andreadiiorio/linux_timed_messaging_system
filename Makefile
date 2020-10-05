ccflags-y := -O2 -Wall -Wno-unused-label 
obj-m := timed_msg_sys.o 
timed_msg_sys-y :=  ./src/utils.o  ./src/core.o ./src/timed_messaging_sys.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
