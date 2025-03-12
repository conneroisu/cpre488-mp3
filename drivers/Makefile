# Simple Makefile to build launcher_driver.ko and launcher_control

obj-m += launcher_driver.o
KDIR := ../linux/linux-xlnx/
PWD := $(shell pwd)

CC := $(CROSS_COMPILE)gcc
BIN = launcher_fire
SOURCES = $(BIN:=.c)


all: $(BIN)
	$(MAKE) -C $(KDIR) M=${shell pwd} modules


$(BIN): $(SOURCES)
	$(CC) $@.c -o $@

clean:
	-$(MAKE) -C $(KDIR) M=${shell pwd} clean || true
	-rm $(BIN) || true
	-rm *.o *.ko *.mod.{c,o} modules.order Module.symvers || true

