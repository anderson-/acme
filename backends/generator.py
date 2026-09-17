#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import tempfile

import yaml


def make_value(value):
    return str(value).replace("$", "$$").replace("#", r"\#")


def make_list(value):
    if value is None:
        return ""
    if not isinstance(value, list):
        raise ValueError("expected a YAML list")
    return " ".join(str(item) for item in value)


def render(output, project_file, name, source, platform, variables, actions):
    target_name = f"{platform}-{name}"
    shell_flag = f"ACME_SHELL_{platform.upper().replace('-', '_')}"
    command = f"$(MAKE) {{action}} ACME_PROJECT_TARGETS={make_value(output)}"

    lines = [
        f"# Generated from {project_file}; do not edit.",
        "",
        f"ifeq ($(ACME_PROJECT_TARGETS),{make_value(output)})",
        f"SRC := {make_value(source)}",
        f"PROP := {make_value(project_file)}",
        f"SKETCH := {make_value(source.name)}",
    ]
    lines.extend(
        f"{variable} := {make_value(value)}" for variable, value in variables.items()
    )
    lines.extend(
        [
            f"include $(MKDIR)/backends/{platform}/backend.mk",
            "endif",
            "",
            "ifeq ($(ACME_PROJECT_TARGETS),)",
        ]
    )

    for action in actions:
        target = f"{action}-{target_name}"
        lines.extend(
            [
                f".PHONY: {target}",
                f"{target}:",
                f"ifdef {shell_flag}",
                f"\t@{command.format(action=action)}",
                "else",
                f"\t@nix-shell $(MKDIR)/backends/{platform} "
                f"--run '{command.format(action=action)}'",
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


def generate(platform, variables, actions, data_actions=()):
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("project_file", type=Path)
    parser.add_argument("name")
    parser.add_argument("source", type=Path)
    args = parser.parse_args()

    with args.project_file.open(encoding="utf-8") as stream:
        project = yaml.safe_load(stream) or {}
    if not isinstance(project, dict):
        raise ValueError(f"{args.project_file}: expected a YAML mapping")

    selected_platform = str(project.get("platform") or "arduino")
    if selected_platform != platform:
        raise ValueError(
            f"{args.project_file}: {platform} generator cannot handle "
            f"{selected_platform!r}"
        )

    selected_actions = list(actions)
    if (args.source / "data").is_dir():
        selected_actions.extend(data_actions)

    atomic_write(
        args.output,
        render(
            args.output,
            args.project_file,
            args.name,
            args.source,
            platform,
            variables(project),
            selected_actions,
        ),
    )
