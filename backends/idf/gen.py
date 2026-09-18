#!/usr/bin/env python3

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from generator import generate, make_list


ACTIONS = (
    "build", "flash", "monitor", "ota", "scan", "list-usb",
    "forget-usb", "forget-ota",
)
DATA_ACTIONS = ("serve", "fs", "flash-fs", "ota-fs")


def variables(project):
    version = str(project.get("version", project.get("idf_version", "v6.0.3")))
    version_key = "".join(c if c.isalnum() or c in ".-_" else "-" for c in version)
    return {
        "TARGET": project.get("target", "esp32"),
        "IDF_VERSION": version,
        "IDF_VERSION_KEY": version_key,
        "BAUD": project.get("baudrate", 115200),
        "FLASH_SIZE": project.get("flash_size", "4MB"),
        "FS": project.get("filesystem", "spiffs"),
        "OTA_PORT": project.get("ota_port", 80),
        "OTA_PATH": project.get("ota_path", "/update"),
        "OTA_FS_PATH": project.get("ota_fs_path", "/update-fs"),
        "COMPONENTS_LIST": make_list(project.get("components")),
        "DEFINES_LIST": make_list(project.get("defines")),
        "SDKCONFIG_LIST": make_list(project.get("sdkconfig")),
    }


if __name__ == "__main__":
    generate("idf", variables, ACTIONS, DATA_ACTIONS)
