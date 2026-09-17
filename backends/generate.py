#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

import yaml


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: generate.py OUTPUT PROJECT NAME SOURCE")

    project_file = Path(sys.argv[2])
    with project_file.open(encoding="utf-8") as stream:
        project = yaml.safe_load(stream) or {}
    if not isinstance(project, dict):
        raise ValueError(f"{project_file}: expected a YAML mapping")

    platform = str(project.get("platform") or "arduino")
    generator = Path(__file__).parent / platform / "gen.py"
    if not generator.is_file():
        raise ValueError(f"{project_file}: unsupported platform {platform!r}")

    subprocess.run([sys.executable, generator, *sys.argv[1:]], check=True)


if __name__ == "__main__":
    main()
