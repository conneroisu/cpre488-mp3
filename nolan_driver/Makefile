KERNEL_PATH = /lib/modules/$(shell uname -r)/build
obj-m = launcher-driver.o
GCC_FLAGS = -g

launcher-fire-buttons : control-interface.o launcher-fire-buttons.o
	gcc $(GCC_FLAGS) -o launcher-fire-buttons control-interface.o launcher-fire-buttons.o
	
control-interface.o : controls/control-interface.c
	gcc $(GCC_FLAGS) -c controls/control-interface.c

launcher-fire-buttons.o : launcher-fire-buttons.c
	gcc $(GCC_FLAGS) -c launcher-fire-buttons.c
	
mmap_test.o : mmap_test.c
	gcc $(GCC_FLAGS) -c mmap_test.c

mmap_test : mmap_test.o control-interface.o
	gcc $(GCC_FLAGS) -o mmap_test mmap_test.o control-interface.o

all: launcher-fire-buttons mmap_test
	make -C $(KERNEL_PATH) M=$(shell pwd) modules

clean:
	make -C $(KERNEL_PATH) M=$(shell pwd) clean
	rm launcher-fire-buttons
	rm mmap_test
