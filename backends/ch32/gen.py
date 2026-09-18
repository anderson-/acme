#!/usr/bin/env python3

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from generator import generate, make_list


ACTIONS = ("build", "flash", "monitor", "list-usb", "forget-usb")
DEFAULT_CH32FUN_REF = "6670407ae29d06fb6155ca1e0f7a5058918d05d8"


def variables(project):
    reference = str(project.get("version", project.get("ch32fun_ref", DEFAULT_CH32FUN_REF)))
    reference_key = "".join(c if c.isalnum() or c in ".-_" else "-" for c in reference)
    return {
        "MCU": project.get("mcu", "CH32V003"),
        "PREFIX_OVERRIDE": project.get("prefix", ""),
        "TOOLCHAIN_VERSION": project.get("toolchain", "15.2.0-1"),
        "CH32FUN_REF": reference,
        "CH32FUN_REF_KEY": reference_key,
        "PROGRAMMER": project.get("programmer", "esp32"),
        "BAUD": project.get("baudrate", 115200),
        "LIB_DIRS": make_list(project.get("lib_dirs")),
        "DEFINES_LIST": make_list(project.get("defines")),
    }


if __name__ == "__main__":
    generate("ch32", variables, ACTIONS)
