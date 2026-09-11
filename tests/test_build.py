# SPDX-License-Identifier: WTFPL
"""Regression checks for the build wrapper, without invoking a real kernel."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class BuildWrapper(unittest.TestCase):
    def test_spaces_and_output(self):
        with tempfile.TemporaryDirectory(prefix='ec-build-test-') as tmp:
            root = Path(tmp)
            src = root / 'Telegram Desktop' / 'module'
            src.mkdir(parents=True)
            for name in ('build.sh', 'Makefile', 'x230_ec_hwmon.c', 'cell-voltage.h', 'x230_ec_accel.c'):
                shutil.copy2(ROOT / name, src / name)
            kernel = root / 'kernel'
            kernel.mkdir()
            (kernel / 'Makefile').touch()
            bindir = root / 'bin'
            bindir.mkdir()
            make = bindir / 'make'
            make.write_text('''#!/bin/sh
set -eu
for arg do
 case "$arg" in M=*) dest=${arg#M=};; esac
done
case "$dest" in *" "*) exit 9;; esac
test -f "$dest/cell-voltage.h"
printf '%s' "$dest" > "$TEST_BUILD_PATH"
printf test > "$dest/x230_ec_hwmon.ko"
printf accel > "$dest/x230_ec_accel.ko"
''')
            make.chmod(0o755)
            record = root / 'path'
            env = dict(os.environ, KDIR=str(kernel), TEST_BUILD_PATH=str(record),
                       PATH=str(bindir)+':'+os.environ['PATH'])
            subprocess.run([str(src/'build.sh')], env=env, check=True, capture_output=True)
            self.assertEqual((src/'x230_ec_hwmon.ko').read_text(), 'test')
            self.assertEqual((src/'x230_ec_accel.ko').read_text(), 'accel')
            self.assertFalse(Path(record.read_text()).exists())

    def test_missing_kernel(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run([str(ROOT/'build.sh')],
                                    env=dict(os.environ, KDIR=tmp), capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'Missing kernel build tree', result.stderr)
