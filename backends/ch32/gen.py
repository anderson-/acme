#!/usr/bin/env python3

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from generator import generate, make_list


ACTIONS = ("build", "flash", "monitor")


def variables(project):
    return {
        "MCU": project.get("mcu", "CH32V003"),
        "PREFIX": project.get("prefix", "riscv64-none-elf"),
        "BAUD": project.get("baudrate", 115200),
        "LIB_DIRS": make_list(project.get("lib_dirs")),
        "DEFINES_LIST": make_list(project.get("defines")),
    }


if __name__ == "__main__":
    generate("ch32", variables, ACTIONS)
