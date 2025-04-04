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

        # Create linux directory if it doesn't exist
        mkdir -p $REPO_ROOT/linux

        # Link the Nix-provided kernel source to the expected path
        if [ ! -d "$REPO_ROOT/linux/linux-xlnx" ]; then
          ln -sfn ${pkgs.linux.dev} "$REPO_ROOT/linux/linux-xlnx"
          echo "Created symlink from Nix kernel source to $REPO_ROOT/linux/linux-xlnx"
        fi

        # Set cross-compile environment variable for ARM target
        export CROSS_COMPILE=armv7l-unknown-linux-gnueabihf-
        export ARCH=arm

        # Create symlinks for cross compiler tools if they don't exist
        if ! command -v arm-linux-gnueabihf-gcc &> /dev/null; then
          mkdir -p $HOME/.local/bin
          for tool in gcc ld ar as objdump objcopy strip; do
            ln -sf "$(which armv7l-unknown-linux-gnueabihf-$tool)" "$HOME/.local/bin/arm-linux-gnueabihf-$tool" 2>/dev/null || true
          done
          export PATH="$HOME/.local/bin:$PATH"
        fi

        # Set kernel source directory environment variable
        export NIX_KERNEL_DIR="${pkgs.linux.dev}"

        # Generate .ccls file with dynamic kernel paths
        KERNEL_SOURCE_DIR=$(echo ${pkgs.linux.dev}/lib/modules/*/source)
        cat > $REPO_ROOT/.ccls << EOF
          clang
          %c -std=c11
          %cpp -std=c++2a
          %h %hpp --include=./Vitis/eclipse_workspace/TPG/hw/*.h
          %h %hpp --include=./Vitis/eclipse_workspace/MP2-TPG/src/lib/**/*.h
          %h %hpp --include=./Vitis/eclipse_workspace/TPG/zynq_fsbl/*.h
          %h %hpp --include=./Vitis/eclipse_workspace/TPG/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/include/*.h
          %h %hpp --include=./Vitis/eclipse_workspace/TPG/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/libsrc/**/**/*.h
          %h %hpp --include=$KERNEL_SOURCE_DIR/include/**/*.h
          %h %hpp --include=$KERNEL_SOURCE_DIR/include/linux/*.h
          %h %hpp --include=$KERNEL_SOURCE_DIR/include/uapi/**/*.h
          %h %hpp --include=$KERNEL_SOURCE_DIR/arch/arm/include/**/*.h
          %h %hpp --include=${opencvWithGUI}/include

          -Iinc
          -DMACRO
          -I./Vitis/eclipse_workspace/TPG/hw
          -I./Vitis/eclipse_workspace/MP2-TPG/src/lib
          -I./Vitis/eclipse_workspace/TPG/zynq_fsbl
          -I./Vitis/eclipse_workspace/TPG/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/include
          -I./Vitis/eclipse_workspace/TPG/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/libsrc
          -I${opencvWithGUI}/include

          # Linux kernel headers for driver development
          -I$KERNEL_SOURCE_DIR/include
          -I$KERNEL_SOURCE_DIR/include/uapi
          -I$KERNEL_SOURCE_DIR/include/linux
          -I$KERNEL_SOURCE_DIR/include/asm-generic
          -I$KERNEL_SOURCE_DIR/arch/arm/include
          -I$KERNEL_SOURCE_DIR/arch/arm/include/generated
          -D__KERNEL__
          -DMODULE
          EOF

                    echo "Kernel source available at: $NIX_KERNEL_DIR"
                    echo "Generated .ccls file with kernel include paths"

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

      # Script to create a headless OpenCV example that doesn't require GUI support
      createHeadlessOpenCVScript = script "create-headless-opencv" ''
                export REPO_ROOT=$(git rev-parse --show-toplevel)

                cat > $REPO_ROOT/headless_detector.cpp << 'EOF'
        #include <opencv2/opencv.hpp>
        #include <iostream>
        #include <vector>
        #include <chrono>
        #include <thread>
        #include <fstream>

        // Target parameters
        #define TARGET_ACTUAL_DIAMETER_CM 19.0f
        #define CAMERA_FOCAL_LENGTH_MM 3.6f
        #define CAMERA_SENSOR_WIDTH_MM 4.8f

        // Simulated launcher commands
        #define CMD_UP    0x01
        #define CMD_DOWN  0x02
        #define CMD_LEFT  0x04
        #define CMD_RIGHT 0x08
        #define CMD_FIRE  0x10
        #define CMD_STOP  0x20

        // Detection result struct
        struct DetectionResult {
            cv::Point2f center;
            float radius;
            float distance;
            float confidence;
            int64_t timestamp;
        };

        // Main detection function - doesn't use any GUI components
        DetectionResult detectTarget(const cv::Mat& frame,
                                     const cv::Scalar& hsvLower,
                                     const cv::Scalar& hsvUpper,
                                     const cv::Scalar& hsvLower2,
                                     const cv::Scalar& hsvUpper2,
                                     float focalLengthPixels) {

            DetectionResult result;
            result.center = cv::Point2f(frame.cols / 2, frame.rows / 2);
            result.radius = 0;
            result.distance = 0;
            result.confidence = 0;
            result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // Convert to HSV color space
            cv::Mat hsvFrame;
            cv::cvtColor(frame, hsvFrame, cv::COLOR_BGR2HSV);

            // Threshold the image using both ranges
            cv::Mat mask1, mask2, mask;
            cv::inRange(hsvFrame, hsvLower, hsvUpper, mask1);
            cv::inRange(hsvFrame, hsvLower2, hsvUpper2, mask2);
            cv::bitwise_or(mask1, mask2, mask);

            // Apply morphological operations
            cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
            cv::morphologyEx(mask, mask, cv::MORPH_OPEN, element);
            cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, element);

            // Find contours
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;
            cv::findContours(mask, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            // Find the best contour
            float bestConfidence = 0;
            for (size_t i = 0; i < contours.size(); i++) {
                // Filter small contours
                double area = cv::contourArea(contours[i]);
                if (area < 100) continue;

                // Fit circle
                cv::Point2f center;
                float radius;
                cv::minEnclosingCircle(contours[i], center, radius);

                // Calculate circularity
                double perimeter = cv::arcLength(contours[i], true);
                double circularity = 4 * M_PI * area / (perimeter * perimeter);

                // Filter based on circularity
                if (circularity < 0.7) continue;

                // Estimate distance
                float distance = TARGET_ACTUAL_DIAMETER_CM * focalLengthPixels / (2 * radius);

                // Calculate confidence
                float confidence = circularity * std::min(1.0f, radius / 30.0f);

                if (confidence > bestConfidence) {
                    bestConfidence = confidence;
                    result.center = center;
                    result.radius = radius;
                    result.distance = distance;
                    result.confidence = confidence;
                }
            }

            return result;
        }

        // Save results to a CSV file for analysis
        void saveResultToFile(const DetectionResult& result, const std::string& filename) {
            bool fileExists = std::ifstream(filename).good();

            std::ofstream file(filename, std::ios::app);
            if (!file.is_open()) {
                std::cerr << "Failed to open output file" << std::endl;
                return;
            }

            // Write header if new file
            if (!fileExists) {
                file << "timestamp,x,y,radius,distance,confidence\n";
            }

            // Write data
            file << result.timestamp << ","
                << result.center.x << ","
                << result.center.y << ","
                << result.radius << ","
                << result.distance << ","
                << result.confidence << "\n";

            file.close();
        }

        // Main function
        int main(int argc, char** argv) {
            // Parse command line arguments
            std::string outputFile = "detection_results.csv";
            if (argc > 1) {
                outputFile = argv[1];
            }

            // Open camera
            cv::VideoCapture cap(0);
            if (!cap.isOpened()) {
                std::cerr << "Failed to open camera" << std::endl;
                return -1;
            }

            // Set camera properties
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

            // Calculate focal length in pixels
            int frameWidth = cap.get(cv::CAP_PROP_FRAME_WIDTH);
            int frameHeight = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
            float sensorWidthPixels = frameWidth;
            float focalLengthMm = CAMERA_FOCAL_LENGTH_MM;
            float sensorWidthMm = CAMERA_SENSOR_WIDTH_MM;
            float focalLengthPixels = (focalLengthMm / sensorWidthMm) * sensorWidthPixels;

            // Set color thresholds for red color (default)
            cv::Scalar hsvLower(0, 100, 100);
            cv::Scalar hsvUpper(10, 255, 255);
            cv::Scalar hsvLower2(160, 100, 100);
            cv::Scalar hsvUpper2(179, 255, 255);

            std::cout << "Starting headless target detection..." << std::endl;
            std::cout << "Results will be saved to " << outputFile << std::endl;
            std::cout << "Press Ctrl+C to stop" << std::endl;

            // Main loop
            while (true) {
                // Read frame
                cv::Mat frame;
                if (!cap.read(frame)) {
                    std::cerr << "Failed to read frame" << std::endl;
                    break;
                }

                // Detect target
                DetectionResult result = detectTarget(frame, hsvLower, hsvUpper,
                                                     hsvLower2, hsvUpper2,
                                                     focalLengthPixels);

                // Log results
                if (result.confidence > 0.5) {
                    std::cout << "Target detected! Distance: " << result.distance
                              << " cm, Confidence: " << result.confidence << std::endl;

                    // Save to file
                    saveResultToFile(result, outputFile);
                } else {
                    std::cout << "No target detected" << std::endl;
                }

                // Wait a bit to avoid too many detections
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            return 0;
        }
        EOF

                cat > $REPO_ROOT/build_headless.sh << 'EOF'
        #!/bin/bash
        export REPO_ROOT=$(git rev-parse --show-toplevel)
        cd $REPO_ROOT

        $CXX -std=c++14 headless_detector.cpp -o headless_detector \
          $(pkg-config --cflags --libs opencv4)

        echo "Headless OpenCV detector built successfully."
        echo "Run with: ./headless_detector [output_file.csv]"
        EOF

                chmod +x $REPO_ROOT/build_headless.sh
                echo "Headless OpenCV detector created at $REPO_ROOT/headless_detector.cpp"
                echo "Build script created at $REPO_ROOT/build_headless.sh"
      '';
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
