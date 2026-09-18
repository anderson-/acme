{ pkgs ? import <nixpkgs> {} }:

let
  pythonEnv = pkgs.python311.withPackages (ps: with ps; [
    pyserial
    pyyaml
    zeroconf
  ]);
in
pkgs.mkShell {
  packages = with pkgs; [
    pythonEnv
    bash
    ccache
    cmake
    curl
    dfu-util
    git
    gnumake
    ninja
    pkg-config
    libusb1
    yq-go
  ];

  shellHook = ''
    export ACME_SHELL_BASE=1
    export ACME_SHELL_IDF=1
    export PIP_DISABLE_PIP_VERSION_CHECK=1
  '';
}
