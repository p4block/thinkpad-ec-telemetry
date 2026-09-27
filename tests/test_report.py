# SPDX-License-Identifier: WTFPL
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('report', ROOT/'tools/report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

class Report(unittest.TestCase):
    def test_allowlist_and_missing_devices(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            dmi=root/'class/dmi/id'; dmi.mkdir(parents=True)
            (dmi/'product_version').write_text('ThinkPad T430')
            for field in ('product_serial','product_uuid','board_serial'):
                (dmi/field).write_text('DO-NOT-PUBLISH')
            params=root/'module/thinkpad_ec_hwmon/parameters'; params.mkdir(parents=True)
            (params/'selected_profile').write_text('x230')
            (params/'verified').write_text('N')
            hwmon=root/'devices/platform/thinkpad_ec_hwmon/hwmon/hwmon0';hwmon.mkdir(parents=True)
            (hwmon/'temp1_label').write_text('EC slot 0')
            result=report.collect(root)
            self.assertNotIn('DO-NOT-PUBLISH',json.dumps(result))
            self.assertEqual(result['identity'],{'product_version':'ThinkPad T430'})
            self.assertFalse(result['modules']['thinkpad_ec_accel']['loaded'])
            self.assertIsNone(result['temperatures'][0]['millidegrees_c'])
