<!--
CPRE 488 MP3 - Digital Camera Pipeline
Authors: Conner Ohnesorge, Nolan Eastburn, Owen Parker, Jason Xie
Copyright (c) 2025
-->

# PetaLinux Quick Reference Guide

## Table of Contents

- [Environment Setup](#environment-setup)
- [Configuring, Compiling, and Packaging Linux](#configuring-compiling-and-packaging-linux)
- [Linux Device Driver Development](#linux-device-driver-development)
- [User Application Development](#user-application-development)
- [Installing Third-Party Tools](#installing-third-party-tools)
- [Troubleshooting](#troubleshooting)

## Environment Setup

Before starting any PetaLinux development, you need to set up the environment:

```bash
# Must be done each time you open a Terminal
source /remote/petalinux/settings.sh
```

## Configuring, Compiling, and Packaging Linux

*Reference: PetaLinux Reference Guide (UG1144), Chapters 2 (last two pages), 3, 4, and 5*

### 1. Create Development Directory

```bash
# Create and enter a directory for Linux development
mkdir mp3-linux-development
cd mp3-linux-development
```

### 2. Create PetaLinux Project

```bash
# Create a project based on the Avnet Zedboard BSP
petalinux-create -t project -s /remote/petalinux/BSPs/avnet-digilent-zedboard-v2020.1-final.bsp

# Enter the created project directory
cd avnet-digilent-zedboard-2020.1

# You should read the README file in this directory
```

### 3. Configure Project with Hardware Platform

```bash
# Configure project with a specific VIVADO hardware platform (XSA file)
# Example assuming XSA is in ../../xsa-file/ directory:
petalinux-config --get-hw-description ../../xsa-file/
```

> **Important Notes**:
>
> - When the configuration menu appears, update the Yocto Settings → TEMPDIR to a subdirectory of the /tmp drive: `/tmp/<unique-project-label>/tmp`
> - This prevents errors about TEMPDIR being located on an NFS
> - Ensure the directory name is unique and doesn't already exist in /tmp

### 4. Configure Linux Kernel

```bash
# Configure the Linux kernel (~15 minutes)
# Enable required features like "USB announce new devices"
petalinux-config -c kernel
```

### 5. Build Linux Components

```bash
# Build the Linux Kernel (~15 minutes)
# This also produces resources required for device driver compilation
petalinux-build -c kernel

# Build the entire project (~5 minutes)
# This produces all files needed for booting Linux
petalinux-build
```
boot.bin boot.scr image.ub

### 6. Package for Boot

```bash
# Package the compiled Linux kernel into a bootable BOOT.BIN
# Example with bitfile in ../../xsa-file/
petalinux-package --boot --force --fpga ../../xsa-file/sw_btn_led_wrapper.bit --fsbl --u-boot
```

### 7. Copy Boot Files

```bash
# Copy the files required to boot Linux from the Zedboard's SD card
# Generated files are located in the following directory:
# <project root>/images/linux/

# You will need to copy them to your SD card files folder (e.g., mp3-sdcard-files)
```

> **Note**: See the MP-3.pdf directions for booting Linux on the Zedboard from an SD card.

## Linux Device Driver Development

*Reference: PetaLinux Reference Guide (UG1144), Chapter 8*

### 1. Create Driver Template

```bash
# Create a template for your device driver (run from project top directory)
# Note: --enable is omitted as driver will be loaded dynamically
petalinux-create -t modules --name launcher-driver
```

### 2. Develop Driver Code

```bash
# Navigate to the driver template file location
cd project-spec/meta-user/recipes-modules/launcher-driver/files/

# Edit launcher-driver.c with your driver code
# Also read the README file in the parent directory
```

### 3. Compile Driver

```bash
# Compile your driver
# Note: If you skipped building the Linux kernel earlier, it will be compiled first
petalinux-build -c launcher-driver
```

### 4. Locate and Deploy Compiled Driver

```bash
# The compiled .ko file will be located at:
# build/tmp/sysroots-components/zedboard_zynq7/launcher-driver/lib/modules/5.4.0-xilinx-v2020.1/extra/

# Copy launcher-driver.ko to your SD card files directory (e.g., mp3-sdcard-files)
```

## User Application Development

*Reference: PetaLinux Reference Guide (UG1144), Chapter 8*

### Option 1: Integration with Root Filesystem

```bash
# Create an application template
# Using --enable adds it to the root filesystem automatically
petalinux-create -t apps --name myapp --enable

# Navigate to the app template directory
cd project-spec/meta-user/recipes-apps/myapp/files

# Update the template C file with your code

# Compile your application
petalinux-build -c myapp

# Build the rootfile system
petalinux-build -c rootfs

# Copy the updated image.ub to your SD card
# From: <project-root>/images/linux/image.ub
```

### Option 2: Standalone Executable Deployment

```bash
# Create an application template without enabling
petalinux-create -t apps --name myapp

# Navigate to the app template directory
cd project-spec/meta-user/recipes-apps/myapp/files

# Update the template C file with your code

# Compile your application
petalinux-build -c myapp -x compile

# Locate the executable at:
# <proj-top>/build/tmp/work/cortexa9t2hf-neon-xilinx-linux-gnueabi/myapp/1.0-r0/myapp

# Copy the executable to the SD card or use RX to transfer it while Linux is running
```

## Installing Third-Party Tools

To add third-party packages such as OpenCV:

1. Download the package from the Xilinx PetaLinux website
2. Configure the root filesystem:

```bash
petalinux-config -c rootfs
```

3. Select and enable the package from the menu

## Troubleshooting

### Common Issues and Solutions

#### Yocto TEMPDIR Error

- **Problem**: Error about TEMPDIR on NFS
- **Solution**: Set Yocto TEMPDIR to a subdirectory in /tmp during `petalinux-config`

#### Build Failures

- **Problem**: Compilation errors during build
- **Solution**:
  - Check log files in `<project>/build/tmp/work/...`
  - Verify hardware description matches your design
  - Ensure proper environment setup with `source /remote/petalinux/settings.sh`

#### Driver Loading Issues

- **Problem**: Driver fails to load with `insmod`
- **Solution**:
  - Check kernel log with `dmesg` for specific errors
  - Verify kernel version matches with `uname -r` vs. driver path
  - Check permissions on the .ko file

#### SD Card Boot Problems

- **Problem**: Linux fails to boot from SD card
- **Solution**:
  - Verify BOOT.BIN, image.ub and boot.scr are correctly placed
  - Check SD card format (FAT32 for boot partition)
  - Verify jumper settings on Zedboard for SD boot mode

---

*This quick reference is based on PetaLinux 2020.1 documentation. For detailed instructions, refer to the PetaLinux Tools Reference Guide (UG1144).*
