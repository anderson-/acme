{ pkgs ? import ../../nix/pkgs.nix {} }:

let
  pythonEnv = pkgs.python311.withPackages (ps: with ps; [
    pyserial
    pyyaml
  ]);
in
pkgs.mkShell {
  packages = with pkgs; [
    bash
    git
    gnumake
    pkg-config
    libusb1
    pythonEnv
  ];

  shellHook = ''
    export ACME_SHELL_BASE=1
    export ACME_SHELL_CH32=1
  '';
}
