# One package set for every ACME shell, independent of the machine's channels.
# This 26.05 revision supports Linux, Apple Silicon, and Intel macOS.
{ system ? builtins.currentSystem }:
let
  pin = builtins.fromJSON (builtins.readFile ./nixpkgs.json);
  source = builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/${pin.rev}.tar.gz";
    sha256 = pin.sha256;
  };
in
import source {
  inherit system;
  config = {};
  overlays = [];
}
