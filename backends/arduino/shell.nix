{ pkgs ? import ../../nix/pkgs.nix {} }:

let
  pythonEnv = pkgs.python311.withPackages (ps: with ps; [
    pyserial
    pyyaml
  ]);
  esptoolPkg = pkgs.python311Packages.esptool or pkgs.esptool;
in
pkgs.mkShell {
  packages = with pkgs; [
    pythonEnv
    esptoolPkg
    jq
    yq-go
    curl
    rsync
    gnumake
    bash
    git
    bash-completion
    arduino-cli
    universal-ctags
  ];

  shellHook = ''
    export ACME_SHELL_BASE=1
    export ACME_SHELL_ARDUINO=1
    export PIP_DISABLE_PIP_VERSION_CHECK=1
    command -v arduino-cli >/dev/null && arduino-cli version || true
    source ${pkgs.bash-completion}/etc/profile.d/bash_completion.sh
  '';
}
