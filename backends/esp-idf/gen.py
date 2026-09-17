#!/usr/bin/env python3

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from generator import generate, make_list


ACTIONS = ("build", "flash", "monitor")


def variables(project):
    return {
        "TARGET": project.get("target", "esp32"),
        "IDF_ROOT": project.get("idf_path", Path.home() / "esp" / "esp-idf"),
        "BAUD": project.get("baudrate", 115200),
        "DEFINES_LIST": make_list(project.get("defines")),
    }


if __name__ == "__main__":
    generate("esp-idf", variables, ACTIONS)
