#!/usr/bin/env bash
# Validate actual shell creation and imports on the current host.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Checking ACME host environments on $(uname -sm)"
nix-shell --pure "$root/shell.nix" --run \
  'python3 -c "import yaml; import sys; print(sys.version)" && make --version'
nix-shell --pure "$root/backends/arduino/shell.nix" --run \
  'python3 -c "import serial, yaml" && arduino-cli version && ctags --version && yq --version'
nix-shell --pure "$root/backends/idf/shell.nix" --run \
  'python3 -c "import serial, yaml" && cmake --version && ninja --version && yq --version'
nix-shell --pure "$root/backends/ch32/shell.nix" --run \
  'python3 -c "import serial, yaml" && git --version && pkg-config --version'
echo "All four ACME shells opened successfully."
