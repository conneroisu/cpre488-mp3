# cpre488-mp3

- [x] Explain how this Make process was configured to appropriately use a cross-compiler targeting the ARM architecture.
- [x] Explanatory annotation of the boot messages that print as PetaLinux starts up. 
- [ ] Include the kernel messages that result from the plugging in the of the usb missle launcher.
- [ ] Describe changes made to the usb driver `usb-skeletion.c`
- [ ] Describe the operation of the `launcher_fire.c` file.
- [ ] Describe algorithm used to detect the target.


# In your writeup, briefly explain how this Make process was configured to appropriately use a cross-compiler targeting the ARM architecture.

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

## Boot Process Analysis

### U-Boot Initialization (Bootloader Phase)
```
U-Boot 2020.01 (Mar 14 2025 - 15:19:07 +0000)

CPU:   Zynq 7z020
Silicon: v3.1
Model: Zynq Zed Development Board
DRAM:  ECC disabled 512 MiB
Flash: 0 Bytes
NAND:  0 MiB
MMC:   mmc@e0100000: 0
```
This section shows U-Boot 2020.01 bootloader starting. It identifies the hardware as a Zynq 7z020 CPU on a Zed Development Board with 512MB of RAM (with ECC disabled). It's detecting storage devices, including an MMC (SD card) interface.

```
Loading Environment from SPI Flash... SF: Detected s25fl256s1 with page size 256 Bytes, erase size 64 KiB, total 32 MiB
*** Warning - bad CRC, using default environment
```
U-Boot is attempting to load environment variables from SPI Flash memory. It finds an s25fl256s1 flash chip (32MB total) but encounters a CRC error, so it falls back to default settings.

```
In:    serial@e0001000
Out:   serial@e0001000
Err:   serial@e0001000
Net:   
ZYNQ GEM: e000b000, mdio bus e000b000, phyaddr 0, interface rgmii-id
```
Sets up the console I/O through a serial port (UART) and initializes the Gigabit Ethernet MAC (GEM).

### Boot Image Loading
```
Hit any key to stop autoboot:  2  1  0 
switch to partitions #0, OK
mmc0 is current device
Scanning mmc 0:1...
Found U-Boot script /boot.scr
2010 bytes read in 33 ms (58.6 KiB/s)
## Executing script at 03000000
11543076 bytes read in 656 ms (16.8 MiB/s)
```
U-Boot is performing autoboot countdown.

After no interruption, it scans the first partition of the SD card, finds a boot script, and executes it.

This script then loads the kernel and initial ramdisk.
```
## Loading kernel from FIT Image at 10000000 ...
   Using 'conf@system-top.dtb' configuration
   Verifying Hash Integrity ... OK
   Trying 'kernel@1' kernel subimage
     Description:  Linux kernel
     Type:         Kernel Image
     Compression:  uncompressed
     Data Start:   0x100000e8
     Data Size:    4325680 Bytes = 4.1 MiB
     Architecture: ARM
     OS:           Linux
     Load Address: 0x00200000
     Entry Point:  0x00200000
     Hash algo:    sha256
     Hash value:   16a76e92c611898f8057d865ef087705fef1aceff96e78675bc68784fd25ac76
   Verifying Hash Integrity ... sha256+ OK
```
U-Boot is loading the Linux kernel from a FIT (Flattened Image Tree) image.

It verifies the hash integrity of the kernel (4.1 MiB in size) to ensure it hasn't been corrupted.

```
## Loading ramdisk from FIT Image at 10000000 ...
   [Details about the ramdisk loading]
## Loading fdt from FIT Image at 10000000 ...
   [Details about the device tree loading]
```
Next, it loads the initial RAM disk (6.9 MiB) and the Flattened Device Tree (FDT) file that describes the hardware to the kernel.

### Linux Kernel Startup
```
Starting kernel ...

Booting Linux on physical CPU 0x0
Linux version 5.4.0-xilinx-v2020.1 (oe-user@oe-host) (gcc version 9.2.0 (GCC)) #1 SMP PREEMPT Fri Mar 14 15:18:45 UTC 2025
CPU: ARMv7 Processor [413fc090] revision 0 (ARMv7), cr=18c5387d
```
The kernel begins executing. This shows Linux 5.4.0 specifically built for Xilinx hardware. It's running on an ARMv7 processor.

```
Memory policy: Data cache writealloc
cma: Reserved 16 MiB at 0x1f000000
percpu: Embedded 15 pages/cpu s31948 r8192 d21300 u61440
Built 1 zonelists, mobility grouping on.  Total pages: 129920
Kernel command line: console=ttyPS0,115200 earlycon root=/dev/ram0 rw
```
The kernel is setting up memory management policies and showing the command line parameters that were passed to it. It will use a serial console and boot from an initial RAM disk.

```
Memory: 484528K/524288K available (6144K kernel code, 217K rwdata, 1840K rodata, 1024K init, 131K bss, 23376K reserved, 16384K cma-reserved, 0K highmem)
```
Memory summary: out of 512MB (524288K) total RAM, about 484MB is available for use after accounting for kernel code, data, and reserved regions.

### Hardware Detection and Initialization
```
rcu: Preemptible hierarchical RCU implementation.
[...]
smp: Bringing up secondary CPUs ...
CPU1: thread -1, cpu 1, socket 0, mpidr 80000001
CPU1: Spectre v2: using BPIALL workaround
smp: Brought up 1 node, 2 CPUs
```
The kernel is initializing the RCU (Read-Copy-Update) subsystem and bringing up multiple CPU cores.

It's a dual-core system with Spectre vulnerability mitigations.

```
devtmpfs: initialized
VFP support v0.3: implementor 41 architecture 3 part 30 variant 9 rev 4
[...]
SCSI subsystem initialized
usbcore: registered new interface driver usbfs
usbcore: registered new interface driver hub
usbcore: registered new device driver usb
```
Initialization of various subsystems: device manager, floating-point support, SCSI, USB, etc.

### File Systems and Network Setup
```
FPGA manager framework
Advanced Linux Sound Architecture Driver Initialized.
[...]
tcp_listen_portaddr_hash hash table entries: 512 (order: 0, 6144 bytes, linear)
[...]
Trying to unpack rootfs image as initramfs...
Freeing initrd memory: 7028K
```
Setting up FPGA management, sound drivers, TCP/IP networking stacks, and unpacking the initial root filesystem from RAM.

```
jffs2: version 2.2. (NAND) (SUMMARY)  © 2001-2006 Red Hat, Inc.
io scheduler mq-deadline registered
io scheduler kyber registered
[...]
spi-nor spi0.0: found s25fl256s1, expected n25q128a11
spi-nor spi0.0: s25fl256s1 (32768 Kbytes)
8 fixed-partitions partitions found on MTD device spi0.0
```
Setting up various filesystems, I/O schedulers, and detecting flash memory partitions. The system found a different SPI flash chip than expected but continues with it.

### Device Detection and Driver Loading
```
Marvell 88E1510 e000b000.ethernet-ffffffff:00: attached PHY driver [Marvell 88E1510] (mii_bus:phy_addr=e000b000.ethernet-ffffffff:00, irq=POLL)
macb e000b000.ethernet eth0: Cadence GEM rev 0x00020118 at 0xe000b000 irq 26 (00:0a:35:00:1e:53)
[...]
usb usb1: New USB device found, idVendor=1d6b, idProduct=0002, bcdDevice= 5.04
usb usb1: New USB device strings: Mfr=3, Product=2, SerialNumber=1
usb usb1: Product: EHCI Host Controller
```
Detection and initialization of Ethernet and USB controllers with their respective drivers.

```
mmc0: SDHCI controller on e0100000.mmc [e0100000.mmc] using ADMA
[...]
mmc0: new high speed SDHC card at address 59b4
mmcblk0: mmc0:59b4 USD   14.7 GiB 
mmcblk0: p1
```
SD card controller initialization. It detects a 14.7GB SDHC card with one partition.

### Transition to Userspace
```
Freeing unused kernel memory: 1024K
Run /init as init process

INIT: version 2.88 booting
```
The kernel has completed initialization and is now starting the first userspace process (/init), which is using SysVinit (version 2.88).

```
Starting udev
udevd[73]: starting version 3.2.8
[...]
FAT-fs (mmcblk0p1): Volume was not properly unmounted. Some data may be corrupt. Please run fsck.
```
Starting the device manager (udev) and mounting filesystems. A warning appears about the FAT filesystem on the SD card.

```
Configuring packages on first boot....
 (This may take several minutes. Please do not power off the machine.)
[...]
INIT: Entering runlevel: 5
```
Running first-boot configurations and entering runlevel 5 (graphical multi-user mode).

### Network and Service Configuration
```
Configuring network interfaces... udhcpc: started, v1.31.0
udhcpc: sending discover
[...]
udhcpc: no lease, forking to background
```
Attempting to configure network via DHCP, but it doesn't receive a lease (no DHCP server responding).

```
Starting Dropbear SSH server: 
[...]
Public key portion is:
ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDFi2F+hJ58qyEF5ZI0VNshOIuSYHRUfMaMcRfvd7yR/ilXnshWpyT49fqkJ7ZiofJ2LtHc3i8+98yDtk3WWk9FFOiVFgum9rEiRh+lVimeRX1zv0AA+GZiwQYmzFxxyPJgRxisuWOgZJ7VR8zZwdd/mizMBczpsTKv22QSx2ymgJUQQBnnr2fkeDZEhK34mh1m+c/n+B0uLIvjBiy9SJeL38CVWsTzN0bmL26o2DKjwYTU+j//QWUC02r1kodxS4d9cr0GZyg91/xtPHqk5+jVgbtTe2iapT0d+YFZFI/x4HkJSj7fp25qnGpc3hNczqUobnLy9KL0F4bpfOjwIcGt root@avnet-digilent-zedboard-2020_1
```
Starting the SSH server (Dropbear) and generating SSH host keys.

```
Starting internet superserver: inetd.
Starting syslogd/klogd: done
Starting tcf-agent: OK
```
Starting various system services: internet services daemon, system logging, and TCF (Target Communication Framework) agent.

