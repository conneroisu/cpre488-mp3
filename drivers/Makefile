# Makefile to build launcher_driver.ko and launcher_fire

# USB Launcher driver module
obj-m += launcher_driver.o
launcher_driver-objs := usb-skeleton.o

# Set kernel directory path - accepts command line arg, NIX_KERNEL_DIR, or fallback
PWD := $(shell pwd)

# Priority order for kernel directory:
# 1. Command line KDIR
# 2. Find kernel dir using nix-build
# 3. Fallback path
ifndef KDIR
    KDIR := $(shell nix-build -E '(import <nixpkgs> {}).linux.dev' --no-out-link 2>/dev/null)
    ifeq ($(KDIR),)
        KDIR := ../linux/linux-xlnx/
    else
        KDIR := $(KDIR)/lib/modules/*/source
    endif
endif

# Cross-compilation settings - set by flake.nix
CC := $(CROSS_COMPILE)gcc
BIN = launcher_fire
SOURCES = $(BIN:=.c)

# Default target
all: $(BIN) module

# Build the kernel module
module:
	@echo "Building kernel module with KDIR=$(KDIR)"
	@echo "ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)"
	
	# Use standard kernel build when available
	@if [ -f "$(KDIR)/Makefile" ] && [ -d "$(KDIR)/include/linux" ]; then \
		echo "Using kernel build system"; \
		$(MAKE) -C $(KDIR) M=$(PWD) modules; \
	else \
		echo "WARNING: Full kernel build system not available."; \
		echo "Skipping module compilation - IDE will use headers for code analysis only."; \
		touch usb-skeleton.o launcher_driver.ko; \
	fi

# Build the user-space launcher_fire utility
$(BIN): $(SOURCES)
	@echo "Building $(BIN) utility"
	@which $(CC) || { echo "ERROR: Cross-compiler $(CC) not found in PATH"; exit 1; }
	$(CC) $@.c -o $@

# Clean target
clean:
	@echo "Cleaning..."
	-$(MAKE) -C $(KDIR) M=$(PWD) clean || true
	-rm $(BIN) || true
	-rm *.o *.ko *.mod.{c,o} modules.order Module.symvers || true

