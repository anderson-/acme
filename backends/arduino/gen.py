#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import tempfile

import yaml


ACTIONS = ("build", "flash", "monitor")
DATA_ACTIONS = ("serve", "fs", "ota", "ota-fs", "flash-fs")


def make_value(value):
    return str(value).replace("$", "$$").replace("#", r"\#")


def make_list(value):
    if value is None:
        return ""
    if not isinstance(value, list):
        raise ValueError("expected a YAML list")
    return " ".join(str(item) for item in value)


def render(output, project_file, name, source):
    with project_file.open(encoding="utf-8") as stream:
        project = yaml.safe_load(stream) or {}

    if not isinstance(project, dict):
        raise ValueError(f"{project_file}: expected a YAML mapping")

    platform = str(project.get("platform") or "arduino")
    if platform != "arduino":
        raise ValueError(
            f"{project_file}: Arduino generator cannot handle {platform!r}"
        )

    board = str(project.get("board") or "")
    target_name = f"{platform}-{name}"
    actions = list(ACTIONS)
    if (source / "data").is_dir():
        actions.extend(DATA_ACTIONS)

    variables = {
        "SRC": source,
        "PROP": project_file,
        "SKETCH": source.name,
        "FQBN": board,
        "CORE": board.split(":", 1)[0] if board else "",
        "BAUD": project.get("baudrate", 115200),
        "DEPENDENCIES": make_list(project.get("dependencies")),
        "LIB_DIRS": make_list(project.get("lib_dirs")),
        "INJECT": make_list(project.get("inject")),
        "DEFINES_LIST": make_list(project.get("defines")),
        "FS": project.get("filesystem", "spiffs"),
    }

    lines = [
        f"# Generated from {project_file}; do not edit.",
        "",
        f"ifeq ($(ACME_PROJECT_TARGETS),{make_value(output)})",
    ]
    lines.extend(
        f"{variable} := {make_value(value)}" for variable, value in variables.items()
    )
    lines.extend(
        [
            "include $(MKDIR)/backends/arduino/backend.mk",
            "endif",
            "",
            "ifeq ($(ACME_PROJECT_TARGETS),)",
        ]
    )

    for action in actions:
        target = f"{action}-{target_name}"
        command = (
            f"$(MAKE) {action} "
            f"ACME_PROJECT_TARGETS={make_value(output)}"
        )
        lines.extend(
            [
                f".PHONY: {target}",
                f"{target}:",
                "ifdef ACME_SHELL_ARDUINO",
                f"\t@{command}",
                "else",
                "\t@nix-shell $(MKDIR)/backends/arduino "
                f"--command '{command}; return'",
                "endif",
                "",
            ]
        )

    lines.extend(["endif", ""])
    return "\n".join(lines)


def atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent, prefix=f".{path.name}.", text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("project_file", type=Path)
    parser.add_argument("name")
    parser.add_argument("source", type=Path)
    args = parser.parse_args()

    atomic_write(
        args.output,
        render(args.output, args.project_file, args.name, args.source),
    )


if __name__ == "__main__":
    main()
