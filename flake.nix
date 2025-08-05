{
  description = "CPRE488 MP3 USB Launcher";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    flake-utils.inputs.systems.follows = "systems";
    systems.url = "github:nix-systems/default";
    treefmt-nix.url = "github:numtide/treefmt-nix";
    treefmt-nix.inputs.nixpkgs.follows = "nixpkgs";
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
  outputs = inputs @ {
    flake-utils,
    treefmt-nix,
    ...
  }:
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

      # treefmt configuration
      treefmtEval = treefmt-nix.lib.evalModule pkgs {
        projectRootFile = "flake.nix";
        programs = {
          alejandra.enable = true; # Nix formatter
          clang-format.enable = true; # C/C++ formatter
        };
      };
    in {
      formatter = treefmtEval.config.build.wrapper;

      devShells.default = pkgs.mkShell {
        shellHook = ''
          export REPO_ROOT=$(git rev-parse --show-toplevel)
          export CC=${pkgs.gcc}/bin/gcc
          export CXX=${pkgs.gcc}/bin/g++
          export CROSS_COMPILE=${armToolchain.gcc}/bin/arm-linux-gnueabihf-
          export PKG_CONFIG_PATH=${opencvWithGUI}/lib/pkgconfig:$PKG_CONFIG_PATH
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
        ];
      };
    });
}
