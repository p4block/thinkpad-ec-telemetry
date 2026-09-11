#!/usr/bin/env python3
# SPDX-License-Identifier: WTFPL
"""Offline G2HT35WW EC RAM decoder. No hardware access or writes."""
import argparse
import datetime
import hashlib
import json
import struct
from pathlib import Path


def decode(data):
    if len(data) != 0x4000:
        raise ValueError('Expected exactly 16384 bytes of EC RAM at 0x800000')
    def word(p, signed=False):
        return struct.unpack_from('<h' if signed else '<H', data, p)[0]
    def dword(p):
        return struct.unpack_from('<I', data, p)[0]
    def date(v):
        try:
            return datetime.date(1980+(v >> 9), (v >> 5) & 15, v & 31).isoformat()
        except ValueError:
            return None
    def text(p):
        return data[p:p+16].split(b'\0', 1)[0].decode('ascii', errors='replace')
    batteries = []
    for number in range(2):
        base = 0xc24 + number * 0xfc
        w = lambda off: word(base+off)
        valid = bool(data[base] & 4)
        bat = {'index': number, 'cache_valid_flag': valid,
               'flags_raw': f'{dword(base):08x}', 'raw': data[base:base+0xfc].hex()}
        if not valid:
            batteries.append(bat)
            continue
        # Offsets come from firmware response dispatcher 1C2B8, not guessed
        # battery commands. Values may have different ages in this EC cache.
        bat['metadata'] = {
            'manufacturer': text(base+0x50), 'device_name': text(base+0x60),
            'chemistry': text(base+0x70), 'serial_number': w(0x4e),
            'manufacture_date': date(w(0x4a)), 'cycle_count': w(0x44),
            'specification_info_raw': f'{w(0x4c):04x}',
            'vendor_3f_date_candidate': date(w(0xb2)),
            'vendor_2f_ascii': text(base+0x90),
        }
        status = w(0x42)
        status_names = {15:'overcharged_alarm',14:'terminate_charge_alarm',
                        12:'overtemperature_alarm',11:'terminate_discharge_alarm',
                        9:'remaining_capacity_alarm',8:'remaining_time_alarm',
                        7:'initialized',6:'discharging',5:'fully_charged',4:'fully_discharged'}
        bat['status'] = {'raw': f'{status:04x}', 'error_code': status & 15,
                         'set_flags': [name for bit,name in status_names.items() if status & (1 << bit)],
                         'mode_raw': f'{w(0x28):04x}', 'manufacturer_access_raw': f'{w(0x24):04x}'}
        bat['measurements'] = {
            'voltage_mV': w(0x2c), 'current_mA_positive_charge': word(base+0x2e, True),
            'gauge_average_current_mA': word(base+0x30, True),
            'ec_average_current_mA': word(base+0x18, True),
            'temperature_C': round(w(0x2a)/10-273.15, 2),
            'relative_charge_percent': w(0x34), 'gauge_max_error_percent': w(0x32),
        }
        energy_mode = bool(w(0x28) & 0x8000)
        bat['capacity'] = {'unit': '10 mWh' if energy_mode else 'mAh',
                           'remaining_raw': w(0x26), 'full_raw': w(0x36),
                           'design_raw': w(0x46), 'design_voltage_mV': w(0x48)}
        bat['time_estimates_minutes'] = {
            name: None if w(off) == 0xffff else w(off)
            for name,off in [('runtime_to_empty',0x38),('average_to_empty',0x3a),('average_to_full',0x3c)]}
        bat['requested_charging_settings'] = {'current_mA': w(0x3e), 'voltage_mV': w(0x40)}
        bat['vendor_raw'] = {f'SBS_{cmd:02x}': data[base+off:base+off+n].hex()
                            for cmd,off,n in [(0x23,0x80,16),(0x2f,0x90,16),(0x30,0xa0,16),
                                             (0x35,0xb6,2),(0x37,0xb8,8),(0x38,0xc0,2),
                                             (0x3b,0xb0,2),(0x3e,0xb4,2),(0x3f,0xb2,2)]}
        if bat['metadata']['manufacturer'] == 'SANYO' and bat['metadata']['device_name'] == 'LNV-45N1023':
            groups = [w(0x86),w(0x88),w(0x8a)]
            bat['candidate_cell_groups'] = {
                'confidence': 'strong inference: two captures sum exactly to pack voltage; vendor format not documented',
                'values_in_memory_order_mV': groups, 'spread_mV': max(groups)-min(groups),
                'sum_mV': sum(groups), 'sum_minus_pack_mV': sum(groups)-w(0x2c),
                'source': 'SBS23 manufacturer data bytes 6..11',
                'physical_group_order': 'unknown',
            }
        batteries.append(bat)
    state = dword(0x2f0)
    return {
        'firmware': 'G2HT35WW', 'sha256': hashlib.sha256(data).hexdigest(),
        'caveats': ['Non-atomic EC RAM snapshot; cache age unknown.',
                    '8008D8..8008DF omitted and zero-filled (self-referential debug request buffer).',
                    'Host EC shadow bytes need not equal dynamic read-handler results.',
                    'Charger cached settings are not measurements or proof of current hardware register values.'],
        'batteries': batteries,
        'charger_cache': {'base': '800364', 'fsm_state': word(0x36e),
                          'flags_raw': f'{data[0x368]:02x}',
                          'charge_current_setting_mA': word(0x370),
                          'charge_voltage_setting_mV': word(0x372),
                          'options_raw': f'{word(0x378):04x}'},
        'platform_state': {'flags_raw': f'{state:08x}',
                           'set_bits': [i for i in range(32) if state & (1 << i)],
                           'host_shadow_46_49_raw': data[0x88e:0x892].hex(),
                           'shadow_matches': state == dword(0x88e),
                           'interpretation': 'EC aggregate state published by 139E8; not a per-rail current register'},
    }

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.snapshot.read_bytes()), indent=2))
