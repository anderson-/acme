#!/usr/bin/env python3
"""Build the repetitive ESP-IDF project files in ACME's cache."""

import argparse
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--flash-size", required=True)
    parser.add_argument("--filesystem", required=True)
    parser.add_argument("--partitions", type=Path, required=True)
    parser.add_argument("--components", default="")
    parser.add_argument("--sdkconfig", default="")
    args = parser.parse_args()

    sources = sorted(
        path for path in args.source.iterdir()
        if path.is_file() and path.suffix.lower() in {".c", ".cc", ".cpp", ".cxx"}
    )
    headers = sorted(
        path for path in args.source.iterdir()
        if path.is_file() and path.suffix.lower() in {".h", ".hh", ".hpp"}
    )
    if not sources:
        raise SystemExit(f"{args.source}: expected at least one C or C++ source file")
    if args.filesystem != "spiffs":
        raise SystemExit("ESP-IDF currently supports filesystem: spiffs")

    if args.output.exists():
        shutil.rmtree(args.output)
    component = args.output / "main"
    component.mkdir(parents=True)
    for path in sources + headers:
        shutil.copy2(path, component / path.name)
    component_manifest = args.source / "idf_component.yml"
    if component_manifest.is_file():
        shutil.copy2(component_manifest, component / component_manifest.name)
    shutil.copy2(args.partitions, args.output / "partitions.csv")

    project_name = "".join(c if c.isalnum() or c == "_" else "_" for c in args.name)
    data = args.source / "data"
    minimal = "idf_build_set_property(MINIMAL_BUILD ON)\n" if args.components or data.is_dir() else ""
    (args.output / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\n"
        "include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n"
        + minimal
        + f"project({project_name})\n",
        encoding="utf-8",
    )

    source_list = " ".join(f'"{path.name}"' for path in sources)
    components = args.components.split()
    if data.is_dir() and "spiffs" not in components:
        components.append("spiffs")
    requires = " REQUIRES " + " ".join(components) if components else ""
    component_cmake = (
        f"idf_component_register(SRCS {source_list} INCLUDE_DIRS \".\"{requires})\n"
    )
    if data.is_dir():
        component_cmake += (
            f'spiffs_create_partition_image(storage "{data.resolve()}" FLASH_IN_PROJECT)\n'
        )
    (component / "CMakeLists.txt").write_text(component_cmake, encoding="utf-8")

    size = args.flash_size.upper()
    extra = [line for line in args.sdkconfig.split() if line]
    defaults = (
        "CONFIG_PARTITION_TABLE_CUSTOM=y\n"
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"\n'
        'CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"\n'
        f"CONFIG_ESPTOOLPY_FLASHSIZE_{size}=y\n"
    )
    if extra:
        # The example's own configuration (project.yaml `sdkconfig:` list): PSRAM,
        # tick rate, CPU frequency, watchdogs, TLS bundle -- whatever the board or the
        # firmware needs beyond the partition table. Later lines win in ESP-IDF's
        # defaults handling, so a project can override the base ones if it wants.
        defaults += "\n" + "\n".join(extra) + "\n"
    (args.output / "sdkconfig.defaults").write_text(defaults, encoding="utf-8")


if __name__ == "__main__":
    main()
