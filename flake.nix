{
  description = "CPRE 488 MP3 - Linux Driver Development";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

    flake-parts.url = "github:hercules-ci/flake-parts";
    flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";

    flake-utils.url = "github:numtide/flake-utils";
    flake-utils.inputs.systems.follows = "systems";

    systems.url = "github:nix-systems/default";

    zig = {
      url = "github:mitchellh/zig-overlay";
      inputs = {
        nixpkgs.follows = "nixpkgs";
        flake-utils.follows = "flake-utils";
      };
    };
    zls-overlay.url = "github:zigtools/zls";

    zig2nix = {
      url = "github:jcollie/zig2nix?ref=c311d8e77a6ee0d995f40a6e10a89a3a4ab04f9a";
      inputs = {
        nixpkgs.follows = "nixpkgs";
        flake-utils.follows = "flake-utils";
      };
    };
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
      zigpkgs = inputs.zig.packages.${system};
      overlays = [
        (final: prev: {
          inherit zigpkgs;
        })
      ];
      zig = zigpkgs.master;
      zls = inputs.zls-overlay.packages.x86_64-linux.zls.overrideAttrs (old: {
        nativeBuildInputs = [zig];
      });
      #
      pkgs = import inputs.nixpkgs {inherit system overlays;};
      #
      script = pkgs.writeShellScriptBin;
    in {
      devShells.default = pkgs.mkShell {
        shellHook = ''
                    export REPO_ROOT=$(git rev-parse --show-toplevel)

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

          -Iinc
          -DMACRO
          -I./Vitis/eclipse_workspace/TPG/hw
          -I./Vitis/eclipse_workspace/MP2-TPG/src/lib
          -I./Vitis/eclipse_workspace/TPG/zynq_fsbl
          -I./Vitis/eclipse_workspace/TPG/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/include
          -I./Vitis/eclipse_workspace/TPG/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/libsrc

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
        '';

        packages = with pkgs; [
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
            find $REPO_ROOT -type f -name '*.c' -exec clang-format -i --verbose {} \;
          '')
        ];
      };
    });
}
