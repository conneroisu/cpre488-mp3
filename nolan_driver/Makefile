KERNEL_PATH = /lib/modules/$(shell uname -r)/build
obj-m = launcher-driver.o

all:
	make -C $(KERNEL_PATH) M=$(shell pwd) modules

clean:
	make -C $(KERNEL_PATH) M=$(shell pwd) clean
