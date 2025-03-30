KERNEL_PATH = /lib/modules/$(shell uname -r)/build
obj-m = launcher-driver.o

all:
	make -C $(KERNEL_PATH) M=$(shell pwd) modules
	gcc launcher-fire-buttons.c -o launcher-fire-buttons

clean:
	make -C $(KERNEL_PATH) M=$(shell pwd) clean
	rm launcher-fire-buttons
