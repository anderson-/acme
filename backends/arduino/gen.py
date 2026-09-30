#!/usr/bin/env python3

from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from generator import generate, make_list


ACTIONS = (
    "build", "flash", "monitor", "ota", "scan", "list-usb",
    "forget-usb", "forget-ota",
)
DATA_ACTIONS = ("serve", "fs", "ota-fs", "flash-fs")


def variables(project):
    board = str(project.get("board") or "")
    return {
        "FQBN": board,
        "CORE": board.split(":", 1)[0] if board else "",
        "OTA_PORT": project.get("ota_port", 80),
        "OTA_PATH": project.get("ota_path", "/update"),
        "OTA_FS_PATH": project.get("ota_fs_path", "/update-fs"),
        "BAUD": project.get("baudrate", 115200),
        "DEPENDENCIES": make_list(project.get("dependencies")),
        "LIB_DIRS": make_list(project.get("lib_dirs")),
        "INJECT": make_list(project.get("inject")),
        "DEFINES_LIST": make_list(project.get("defines")),
        "FS": project.get("filesystem", "spiffs"),
    }


if __name__ == "__main__":
    generate("arduino", variables, ACTIONS, DATA_ACTIONS)
