{ pkgs ? import <nixpkgs> {} }:

let
  pythonEnv = pkgs.python311.withPackages (ps: with ps; [
    pyyaml
  ]);
in
pkgs.mkShell {
  packages = with pkgs; [
    pythonEnv
    gnumake
    bash-completion
  ];

  shellHook = ''
    export ACME_SHELL_BASE=1
    source ${pkgs.bash-completion}/etc/profile.d/bash_completion.sh
  '';
}
