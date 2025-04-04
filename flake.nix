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

      # Custom OpenCV with GUI support
      opencvWithGUI = pkgs.opencv4.override {
        enableGtk3 = true; # Enable GTK3 support for GUI
        enableGStreamer = true; # Enable GStreamer support
        enableFfmpeg = true; # Enable FFmpeg support
        enableTbb = true; # Thread Building Blocks for performance
        enableCuda = false; # Disable CUDA to avoid dependencies
      };
    in {
      devShells.default = pkgs.mkShell {
        shellHook = ''
          export REPO_ROOT=$(git rev-parse --show-toplevel)
          export CC=${pkgs.gcc}/bin/gcc
          export CXX=${pkgs.gcc}/bin/g++
          export CROSS_COMPILE=${armToolchain.gcc}/bin/arm-linux-gnueabihf-
          export PKG_CONFIG_PATH=${opencvWithGUI}/lib/pkgconfig:$PKG_CONFIG_PATH

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
          echo "  create-headless-opencv - Create headless OpenCV app (no GUI needed)"
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

          # OpenCV with GUI support
          opencvWithGUI

          # GUI dependencies
          pkgs.gtk3
          pkgs.glib
          pkgs.gst_all_1.gstreamer
          pkgs.gst_all_1.gst-libav
          pkgs.ccls
          pkgs.clang-tools

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

        packages = with pkgs; [
          # Helper scripts for the environment
          setupEnvScript
          createMakefileScript
          createLEDScriptScript
          buildLauncherScript
          buildKernelModuleScript
          createBootBinScript
          zedboardSshScript
          zedboardCopyScript
          createHeadlessOpenCVScript

          # Original scripts
          (script "dx" ''
            $EDITOR $REPO_ROOT/flake.nix
          '')

          # Development tools
          alejandra
          nixd
          zig
          zls
          clang-tools
          ccls
          gnumake

          # Cross-compilation toolchain
          pkgsCross.armv7l-hf-multiplatform.buildPackages.gcc
          pkgsCross.armv7l-hf-multiplatform.buildPackages.binutils

          # Kernel development tools
          linux.dev
          linuxHeaders
          bc # Required for kernel builds

          # Scripts
          (script "dx" ''
            $EDITOR $REPO_ROOT/flake.nix
          '')
          (script "build-driver" ''
            # Ensure we have the latest environment variables
            cd $REPO_ROOT && direnv reload
            cd $REPO_ROOT/drivers && make
          '')
          (script "check-driver" ''
            # Check usb_free_urb declaration in ./drivers/launcher_driver.c
            cd $REPO_ROOT/drivers

            echo "Checking usb_free_urb declaration in launcher_driver.c..."
            if grep -q "usb_free_urb" launcher_driver.c; then
              if ! grep -q "extern void usb_free_urb" launcher_driver.c; then
                # Add the declaration if missing
                echo "Adding usb_free_urb declaration to launcher_driver.c"
                sed -i '/static void launcher_draw_down(struct usb_launcher \*dev);/a\\n/* Forward declarations for kernel APIs */\\nextern void usb_free_urb(struct urb *urb);' launcher_driver.c
              else
                echo "usb_free_urb is already declared in launcher_driver.c"
              fi
            fi

            # Simple sanity check for source file
            if gcc -fsyntax-only -c launcher_driver.c 2>&1 | grep -q "usb_free_urb"; then
              echo "❌ Driver code has an issue with usb_free_urb"
            else
              echo "✅ Driver code looks good - usb_free_urb declaration added"
            fi

            echo "NOTE: For full kernel module compilation, you'll need to build on the target system."
          '')
          (script "clean-driver" ''
            cd $REPO_ROOT/drivers && make clean
          '')
          (script "format" ''
            export REPO_ROOT=$(git rev-parse --show-toplevel) # needed
            # format all files with clang-format
            find $REPO_ROOT -type f -name '*.c' -o -name '*.cpp' -o -name '*.h' -exec clang-format -i --verbose {} \;
          '')
          (script "run-headless-opencv" ''
            export REPO_ROOT=$(git rev-parse --show-toplevel)
            if [ ! -f "$REPO_ROOT/headless_detector" ]; then
              echo "Building headless detector first..."
              $REPO_ROOT/build_headless.sh
            fi
            echo "Running headless OpenCV detector..."
            $REPO_ROOT/headless_detector "$@"
          '')
        ];
      };
    });
}
