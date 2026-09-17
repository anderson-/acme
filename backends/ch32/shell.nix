{ pkgs ? import <nixpkgs> {} }:

let
  riscv = pkgs.pkgsCross.riscv64-embedded;
in
pkgs.mkShell {
  packages = with pkgs; [
    bash
    git
    gnumake
    pkg-config
    libusb1
    python311
    python311Packages.pyyaml
    riscv.stdenv.cc
  ];

  shellHook = ''
    export ACME_SHELL_BASE=1
    export ACME_SHELL_CH32=1
  '';
}
