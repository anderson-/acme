{ pkgs ? import <nixpkgs> {} }:

let
  pythonEnv = pkgs.python311.withPackages (ps: with ps; [
    pip
    virtualenv
    pyserial
    pyyaml
    zeroconf
    websockets
    yq
  ]);
  esptoolPkg = pkgs.python311Packages.esptool or pkgs.esptool;
in
pkgs.mkShell {
  packages = with pkgs; [
    pythonEnv
    esptoolPkg
    jq
    curl
    rsync
    gnumake
    bash
    bash-completion
    arduino-cli
  ];

  shellHook = ''
    export PIP_DISABLE_PIP_VERSION_CHECK=1
    command -v arduino-cli >/dev/null && arduino-cli version || true
    source ${pkgs.bash-completion}/etc/profile.d/bash_completion.sh
  '';
}
