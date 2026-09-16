{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = with pkgs; [
    gnumake
    bash-completion
  ];

  shellHook = ''
    export ACME_SHELL_BASE=1
    source ${pkgs.bash-completion}/etc/profile.d/bash_completion.sh
  '';
}
