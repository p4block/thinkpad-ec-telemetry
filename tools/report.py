#!/usr/bin/env python3
# SPDX-License-Identifier: WTFPL
"""Print a shareable compatibility snapshot. No direct EC access or configuration changes."""
import json
from pathlib import Path
import platform


def read(path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def collect(sys=Path('/sys')):
    # Deliberate allowlists: no hostname, serials, UUIDs, battery IDs, or logs.
    result = {'kernel': platform.release(), 'identity': {}, 'modules': {}, 'temperatures': []}
    for field in ('sys_vendor', 'product_name', 'product_version', 'board_name',
                  'bios_vendor', 'bios_version', 'ec_firmware_release'):
        value = read(sys / 'class/dmi/id' / field)
        if value is not None:
            result['identity'][field] = value
    for name in ('thinkpad_ec_hwmon', 'thinkpad_ec_accel'):
        params = sys / 'module' / name / 'parameters'
        values = {'loaded': params.is_dir()}
        for field in ('selected_profile', 'firmware', 'verified', 'allow_unsupported',
                      'probe_only', 'active', 'last_error', 'samples'):
            value = read(params / field)
            if value is not None:
                values[field] = value
        result['modules'][name] = values
    for hwmon in sorted((sys / 'devices/platform/thinkpad_ec_hwmon/hwmon').glob('hwmon*')):
        for label in sorted(hwmon.glob('temp*_label')):
            stem = label.name.removesuffix('_label')
            value = read(hwmon / (stem + '_input'))
            result['temperatures'].append({
                'channel': stem, 'label': read(label),
                'millidegrees_c': int(value) if value is not None else None,
            })
    return result


if __name__ == '__main__':
    print(json.dumps(collect(), indent=2))
