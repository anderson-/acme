"""Exercise streaming and failure handling with native WiFi/flash test doubles."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("g++"), "native g++ is required for firmware protocol tests")
class FirmwareOTATest(unittest.TestCase):
    def test_both_platforms_stream_and_do_not_activate_incomplete_images(self):
        with tempfile.TemporaryDirectory() as temporary:
            for platform in ("ESP32", "ESP8266"):
                with self.subTest(platform=platform):
                    output = str(Path(temporary) / platform)
                    result = subprocess.run([
                        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-D" + platform, "-I" + str(ROOT / "tests/firmware_stubs"),
                        "-I" + str(ROOT / "libraries/AcmeOTA/src"),
                        str(ROOT / "tests/firmware_ota.cpp"), "-o", output,
                    ], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([output], check=True)


if __name__ == "__main__":
    unittest.main()
