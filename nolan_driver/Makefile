KERNEL_PATH = /lib/modules/$(shell uname -r)/build
obj-m = launcher-driver.o

launcher-fire-buttons : control-interface.o launcher-fire-buttons.o
	gcc -o launcher-fire-buttons control-interface.o launcher-fire-buttons.o
	
control-interface.o : controls/control-interface.c
	gcc -c controls/control-interface.c

launcher-fire-buttons.o : launcher-fire-buttons.c
	gcc -c launcher-fire-buttons.c
	
all: launcher-fire-buttons
	make -C $(KERNEL_PATH) M=$(shell pwd) modules

clean:
	make -C $(KERNEL_PATH) M=$(shell pwd) clean
	rm launcher-fire-buttons
