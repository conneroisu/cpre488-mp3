# cpre488-mp3

- [x] Explain how this Make process was configured to appropriately use a cross-compiler targeting the ARM architecture.
- [ ] Explanatory annotation of the boot messages that print as PetaLinux starts up. 
- [ ] Include the kernel messages that result from the plugging in the of the usb missle launcher.
- [ ] Describe changes made to the usb driver `usb-skeletion.c`
- [ ] Describe the operation of the `launcher_fire.c` file.
- [ ] Describe algorithm used to detect the target.

## `launcher_fire.c` Makefile

This Makefile is configured to build both a Linux kernel module (`launcher_driver.ko`) and a user-space program (`launcher_fire`) using a cross-compiler for ARM architecture. Let me explain the key components:

### Cross-Compilation Setup

The Makefile uses the `CROSS_COMPILE` environment variable to specify the cross-compiler toolchain:

```makefile
CC := $(CROSS_COMPILE)gcc
```

This is the core mechanism that enables ARM architecture targeting. The `CROSS_COMPILE` variable is expected to be set in the environment before running the make command (e.g., `CROSS_COMPILE=arm-linux-gnueabihf-` or similar ARM toolchain prefix). When expanded, it creates commands like `arm-linux-gnueabihf-gcc` that invoke the cross-compiler instead of the host system's native compiler.

### Kernel Module Building

For the kernel module (`launcher_driver.ko`):

1. `obj-m += launcher_driver.o` tells the kernel build system which object files to build into modules.
2. `KDIR := ../linux/linux-xlnx/` points to the Xilinx Linux kernel source directory (often used for Zynq ARM platforms).
3. The main kernel build is triggered with:
   ```makefile
   $(MAKE) -C $(KDIR) M=${shell pwd} modules
   ```
   This invokes the kernel's build system, which will use the cross-compiler settings defined in the kernel's configuration.

### User-Space Program Building

For the user application (`launcher_fire`):

1. The application is built directly using the cross-compiler via:
   ```makefile
   $(BIN): $(SOURCES)
       $(CC) $@.c -o $@
   ```
   The `$(CC)` variable expands to the cross-compiler as defined earlier.

### Clean Target

The clean target is thorough, removing both the kernel module files and the user-space binary:

```makefile
clean:
	-$(MAKE) -C $(KDIR) M=${shell pwd} clean || true
	-rm $(BIN) || true
	-rm *.o *.ko *.mod.{c,o} modules.order Module.symvers || true
```

The `launcher_fire.c` code is a user-space application that communicates with the kernel module through a device node (`/dev/launcher0`), sending commands to control what seems to be a physical launcher device (likely a USB missile launcher or similar gadget).

This configuration works for ARM because:
1. It uses the ARM cross-compiler toolchain via `CROSS_COMPILE`
2. It builds against an ARM-targeted kernel source tree (Xilinx's Linux kernel)
3. The resulting binaries will be compatible with ARM systems, specifically a Xilinx Zynq platform.
