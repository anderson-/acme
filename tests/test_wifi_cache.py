"""Run in nix-shell backends/arduino or backends/idf."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WifiCacheTest(unittest.TestCase):
    def test_backend_cache(self):
        for backend in ('arduino', 'idf'):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                wifi = directory / 'wifi.yaml'
                project = directory / 'project.yaml'
                project.write_text('defines: []\n')
                build = directory / 'build'
                build.mkdir()
                core = directory / 'data/packages/test'
                core.mkdir(parents=True)
                (directory / 'data/arduino-cli.yaml').write_text('directories: {}\n')
                (directory / 'data/package_index.json').touch()
                core.touch()
                libs = directory / 'libs'
                libs.touch()
                harness = directory / 'check.mk'
                harness.write_text(f'''MKDIR := {ROOT}
PROP := {project}
WIFI := {wifi}
SRC := examples/esp-idf/ota
FLASH_SIZE := 4MB
CORE := test
CFG := {directory}/data/arduino-cli.yaml
include {ROOT}/backends/{backend}/backend.mk
''' + ('${STAMP_BUILD}:\n\t@touch $@\n' if backend == 'arduino' else ''))
                stamp = build / ('.stamp-build' if backend == 'arduino' else '.config-stamp')
                def run(*extra, success=True):
                    result = subprocess.run(['make', '-f', str(harness), str(stamp), f'BUILD={build}', f'ADATA={directory}/data', f'STAMP_LIBS={libs}', *extra], text=True, capture_output=True)
                    self.assertEqual(result.returncode == 0, success, result.stderr)
                    return result
                # No file: must not accidentally select ACME's root credentials.
                run()
                initial = stamp.stat().st_mtime_ns
                run()
                self.assertEqual(stamp.stat().st_mtime_ns, initial)
                for content in ['ssid: Test\npsk: first\n', 'ssid: Test\npsk: changed\n', None, 'ssid: Test\npsk: restored\n']:
                    previous = stamp.stat().st_mtime_ns
                    if content is None:
                        wifi.unlink()
                    else:
                        wifi.write_text(content)
                    run()
                    self.assertNotEqual(stamp.stat().st_mtime_ns, previous)
                    current = stamp.stat().st_mtime_ns
                    run()
                    self.assertEqual(stamp.stat().st_mtime_ns, current)
                result = run('DEFINES_LIST=STASSID=project')
                self.assertIn('Wi-Fi defines found in project.yaml', result.stderr)
                wifi.write_text('ssid: [\n')
                self.assertIn('Cannot read', run(success=False).stderr)


if __name__ == '__main__':
    unittest.main()
