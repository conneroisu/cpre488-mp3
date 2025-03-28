{
  description = "CPRE488 MP3 USB Launcher";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    flake-utils.inputs.systems.follows = "systems";
    systems.url = "github:nix-systems/default";
  };
  nixConfig = {
    extra-substituters = ''
      https://cache.nixos.org
      https://nix-community.cachix.org
      https://devenv.cachix.org
    '';
    extra-trusted-public-keys = ''
      cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=
      nix-community.cachix.org-1:mB9FSh9qf2dCimDSUo8Zy7bkq5CX+/rkCWyvRCYg3Fs=
      devenv.cachix.org-1:w1cLUi8dv3hnoSPGAuibQv+f9TZLr6cv/Hm9XgU50cw=
    '';
    extra-experimental-features = "nix-command flakes";
  };
  outputs = inputs @ {flake-utils, ...}:
    flake-utils.lib.eachSystem [
      "x86_64-linux"
      "i686-linux"
      "x86_64-darwin"
      "aarch64-linux"
      "aarch64-darwin"
    ] (system: let
      #
      pkgs = import inputs.nixpkgs {
        inherit system;
        config = {
          allowUnfree = true; # Required for some Xilinx tools
        };
      };
      #
      script = pkgs.writeShellScriptBin;

      # Cross-compilation toolchain for ARM (ZedBoard)
      armToolchain = pkgs.pkgsCross.armv7l-hf-multiplatform.buildPackages;

      # Helper script for SSH to ZedBoard
      zedboardSshScript = script "zedboard-ssh" ''
        if [ -z "$1" ]; then
          IP=192.168.1.10  # Default IP, adjust as needed
        else
          IP=$1
        fi
        ssh root@''${IP} $2
      '';

      # Helper script to copy files to ZedBoard
      zedboardCopyScript = script "zedboard-copy" ''
        if [ -z "$2" ]; then
          echo "Usage: zedboard-copy <file> <destination>"
          exit 1
        fi

        if [ -z "$3" ]; then
          IP=192.168.1.10  # Default IP, adjust as needed
        else
          IP=$3
        fi

        scp $1 root@''${IP}:$2
      '';

      # Helper script to build the launcher camera app
      buildLauncherScript = script "build-launcher" ''
        export REPO_ROOT=$(git rev-parse --show-toplevel)
        cd $REPO_ROOT

        # Default to normal C version unless specified
        VERSION="c"
        OUTPUT="launcher_fire_camera"

        if [ "$1" = "opencv" ]; then
          VERSION="opencv"
          OUTPUT="launcher_fire_camera_opencv"

          # Build with OpenCV support
          echo "Building OpenCV version..."
          $CXX -std=c++14 $CXXFLAGS \
            launcher_fire_camera_opencv.cpp \
            -o $OUTPUT \
            $(pkg-config --cflags --libs opencv4) \
            -lusb-1.0
        else
          # Build basic C version
          echo "Building basic C version..."
          $CC -std=c11 $CFLAGS \
            launcher_fire_camera.c \
            -o $OUTPUT \
            -lusb-1.0
        fi

        echo "Build complete: $OUTPUT"
      '';

      # Helper script to compile and load the kernel module
      buildKernelModuleScript = script "build-kernel-module" ''
        export REPO_ROOT=$(git rev-parse --show-toplevel)
        cd $REPO_ROOT/drivers

        # Check if KERNEL_SRC is set
        if [ -z "$KERNEL_SRC" ]; then
          echo "Error: KERNEL_SRC environment variable not set"
          echo "Please set it to the path of your Petalinux kernel source"
          exit 1
        fi

        echo "Building kernel module..."
        make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

        echo "Kernel module built. Use zedboard-copy to transfer to the ZedBoard."
      '';

      # Helper script to create BOOT.BIN for Petalinux
      createBootBinScript = script "create-bootbin" ''
        export REPO_ROOT=$(git rev-parse --show-toplevel)
        cd $REPO_ROOT

        # Check if PETALINUX_DIR is set
        if [ -z "$PETALINUX_DIR" ]; then
          echo "Error: PETALINUX_DIR environment variable not set"
          echo "Please set it to the path of your Petalinux project directory"
          exit 1
        fi

        echo "Creating BOOT.BIN..."
        cd $PETALINUX_DIR
        petalinux-package --boot --fsbl $1 --fpga $2 --u-boot

        echo "BOOT.BIN created in $PETALINUX_DIR/images/linux/"
      '';

      # Script to setup shell environment
      setupEnvScript = script "setup-env" ''
                export REPO_ROOT=$(git rev-parse --show-toplevel)

                # Create project directory structure if it doesn't exist
                mkdir -p $REPO_ROOT/drivers
                mkdir -p $REPO_ROOT/bin
                mkdir -p $REPO_ROOT/include
                mkdir -p $REPO_ROOT/build

                # Copy template headers and source files if they don't exist
                if [ ! -f "$REPO_ROOT/include/launcher_commands.h" ]; then
                  cat > $REPO_ROOT/include/launcher_commands.h << 'EOF'
        // Launcher command definitions
        #ifndef LAUNCHER_COMMANDS_H
        #define LAUNCHER_COMMANDS_H

        #define LAUNCHER_VENDOR_ID 0x2123
        #define LAUNCHER_PRODUCT_ID 0x1010

        #define LAUNCHER_CTRL_CMD_PREFIX 0x02

        // Launcher commands
        #define LAUNCHER_STOP 0x00
        #define LAUNCHER_UP 0x01
        #define LAUNCHER_DOWN 0x02
        #define LAUNCHER_LEFT 0x04
        #define LAUNCHER_RIGHT 0x08
        #define LAUNCHER_FIRE 0x10

        #endif // LAUNCHER_COMMANDS_H
        EOF
                fi

                echo "Environment setup complete."
                echo "Use 'build-launcher' to build the camera application."
                echo "Use 'build-kernel-module' to build the kernel module (requires KERNEL_SRC to be set)."
                echo "Use 'create-bootbin' to create BOOT.BIN (requires PETALINUX_DIR to be set)."
      '';

      # Script to create a Makefile for the project
      createMakefileScript = script "create-makefile" ''
                export REPO_ROOT=$(git rev-parse --show-toplevel)

                cat > $REPO_ROOT/Makefile << 'EOF'
        CC = $(CROSS_COMPILE)gcc
        CXX = $(CROSS_COMPILE)g++
        CFLAGS = -Wall -Wextra -I./include
        CXXFLAGS = -Wall -Wextra -I./include -std=c++14

        TARGET_C = launcher_fire_camera
        TARGET_CPP = launcher_fire_camera_opencv
        TARGET_BUTTONS = launcher_fire_buttons

        OPENCV_LIBS = $(shell pkg-config --libs opencv4)
        OPENCV_CFLAGS = $(shell pkg-config --cflags opencv4)

        .PHONY: all clean opencv buttons

        all: $(TARGET_C)

        opencv: $(TARGET_CPP)

        buttons: $(TARGET_BUTTONS)

        $(TARGET_C): launcher_fire_camera.c
        	$(CC) $(CFLAGS) -o $@ $< -lusb-1.0

        $(TARGET_CPP): launcher_fire_camera_opencv.cpp
        	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -o $@ $< $(OPENCV_LIBS) -lusb-1.0

        $(TARGET_BUTTONS): launcher_fire_buttons.c
        	$(CC) $(CFLAGS) -o $@ $< -lusb-1.0

        clean:
        	rm -f $(TARGET_C) $(TARGET_CPP) $(TARGET_BUTTONS)
        EOF

                # Create a Makefile for the kernel module
                mkdir -p $REPO_ROOT/drivers
                cat > $REPO_ROOT/drivers/Makefile << 'EOF'
        obj-m := launcher_driver.o

        KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

        all:
        	make -C $(KERNEL_SRC) M=$(PWD) modules

        clean:
        	make -C $(KERNEL_SRC) M=$(PWD) clean
        EOF

                echo "Makefiles created."
      '';

      # Script to generate the LED control shell script
      createLEDScriptScript = script "create-led-script" ''
                export REPO_ROOT=$(git rev-parse --show-toplevel)

                cat > $REPO_ROOT/LEDfun.sh << 'EOF'
        #!/bin/bash
        # LEDfun.sh - Script to control LEDs based on button and switch states

        # Define memory addresses for LEDs, buttons, and switches
        # These addresses may need to be adjusted based on your implementation
        LED_ADDR=0x41200000
        BTN_ADDR=0x41220000
        SW_ADDR=0x41240000

        # Function to read device memory
        read_mem() {
            devmem $1
        }

        # Function to write to device memory
        write_mem() {
            devmem $1 32 $2
        }

        echo "Starting LED control script..."

        while true; do
            # Read button and switch states
            BUTTONS=$(read_mem $BTN_ADDR)
            SWITCHES=$(read_mem $SW_ADDR)

            # Count number of buttons pressed (Check how many bits are set)
            BUTTON_COUNT=$(printf "%d" $BUTTONS | tr -cd '1' | wc -c)

            # Check button count and set LEDs accordingly
            if [ $BUTTON_COUNT -eq 0 ]; then
                # No buttons pressed, LEDs = switches
                write_mem $LED_ADDR $SWITCHES
            elif [ $BUTTON_COUNT -eq 1 ]; then
                # 1 button pressed, LEDs = inverse of switches
                # Calculate inverse by XORing with all 1s (0xFFFFFFFF)
                INVERSE=$((SWITCHES ^ 0xFFFFFFFF))
                write_mem $LED_ADDR $INVERSE
            else
                # More than 1 button pressed, all LEDs on
                write_mem $LED_ADDR 0xFFFFFFFF
            fi

            # Short delay to avoid hammering the system
            sleep 0.1
        done
        EOF

                chmod +x $REPO_ROOT/LEDfun.sh
                echo "LED control script created at $REPO_ROOT/LEDfun.sh"
      '';
    in {
      devShells.default = pkgs.mkShell {
        shellHook = ''
          export REPO_ROOT=$(git rev-parse --show-toplevel)
          export CC=${pkgs.gcc}/bin/gcc
          export CXX=${pkgs.gcc}/bin/g++
          export CROSS_COMPILE=${armToolchain.gcc}/bin/arm-linux-gnueabihf-
          export PKG_CONFIG_PATH=${pkgs.opencv4}/lib/pkgconfig:$PKG_CONFIG_PATH

          # Display welcome message
          echo "=== CPRE488 MP3 USB Launcher Development Environment ==="
          echo "Available commands:"
          echo "  setup-env         - Set up the project structure"
          echo "  create-makefile   - Create Makefiles for the project"
          echo "  create-led-script - Create the LED control script"
          echo "  build-launcher    - Build the launcher camera application"
          echo "  build-launcher opencv - Build the OpenCV version"
          echo "  build-kernel-module - Build the kernel module (set KERNEL_SRC first)"
          echo "  create-bootbin    - Create BOOT.BIN (set PETALINUX_DIR first)"
          echo "  zedboard-ssh      - SSH to ZedBoard"
          echo "  zedboard-copy     - Copy files to ZedBoard"
          echo "  format            - Format C/C++ code"
          echo "  dx                - Edit flake.nix"
          echo "=================================================="
        '';

        buildInputs = [
          # Standard development tools
          pkgs.git
          pkgs.gnumake
          pkgs.cmake
          pkgs.pkg-config

          # C/C++ toolchain
          pkgs.gcc
          pkgs.gdb
          pkgs.binutils

          # Cross-compilation tools for ARM
          armToolchain.gcc
          armToolchain.binutils

          # OpenCV and image processing
          pkgs.opencv4

          # USB development
          pkgs.libusb1
          pkgs.usbutils

          # Embedded systems tools
          pkgs.minicom
          pkgs.screen
          pkgs.picocom
          pkgs.socat

          # SD card and filesystem tools
          pkgs.parted
          pkgs.dosfstools
          pkgs.mtools

          # Network tools
          pkgs.inetutils
          pkgs.openssh

          # Code Quality and Documentation
          pkgs.alejandra
          pkgs.nixd
          pkgs.clang-tools
          pkgs.ccls
          pkgs.doxygen
          pkgs.graphviz
        ];

        packages = [
          # Helper scripts for the environment
          setupEnvScript
          createMakefileScript
          createLEDScriptScript
          buildLauncherScript
          buildKernelModuleScript
          createBootBinScript
          zedboardSshScript
          zedboardCopyScript

          # Original scripts
          (script "dx" ''
            $EDITOR $REPO_ROOT/flake.nix
          '')
          (script "format" ''
            export REPO_ROOT=$(git rev-parse --show-toplevel) # needed
            # format all files with clang-format
            find $REPO_ROOT -type f -name '*.c' -o -name '*.cpp' -o -name '*.h' -exec clang-format -i --verbose {} \;
          '')
        ];
      };
    });
}
