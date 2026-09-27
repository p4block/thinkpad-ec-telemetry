# SPDX-License-Identifier: WTFPL
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class Visibility(unittest.TestCase):
    def test_unverified_profiles_do_not_expose_electrical_or_debug(self):
        source=(ROOT/'thinkpad_ec_hwmon.c').read_text()
        helpers=source[source.index('static int temp_count(void)'):source.index('/* Caller serializes')]
        visible=source[source.index('static umode_t visible('):source.index('/* Dedicated firmware')]
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef unsigned int u32;
typedef unsigned short umode_t;
#define BIT(n) (1ul<<(n))
#include "profiles.h"
static const struct tp_ec_profile *ec_profile;
static bool verified,allow_unsupported,cell_voltages,known_ec_layout;
static unsigned long present;
enum hwmon_sensor_types {hwmon_temp,hwmon_curr,hwmon_power,hwmon_in};
enum {hwmon_temp_input=1,hwmon_temp_label,hwmon_curr_input,hwmon_curr_label,
      hwmon_power_input,hwmon_power_label,hwmon_in_input,hwmon_in_label};
'''+helpers+visible+r'''
int main(void) {
 ec_profile=tp_profile_named("x230");verified=true;cell_voltages=true;known_ec_layout=true;
 assert(visible(NULL,hwmon_in,hwmon_in_input,0)==0444);
 assert(visible(NULL,hwmon_in,hwmon_in_input,1)==0444);
 assert(temp_count()==12 && temp_slot(1)==2);
 known_ec_layout=false;
 assert(!visible(NULL,hwmon_in,hwmon_in_input,1));
 allow_unsupported=true;
 assert(!visible(NULL,hwmon_in,hwmon_in_input,0));
 assert(!visible(NULL,hwmon_curr,hwmon_curr_input,0));
 assert(!visible(NULL,hwmon_power,hwmon_power_input,0));
 verified=false;allow_unsupported=false;
 assert(!visible(NULL,hwmon_in,hwmon_in_input,0));
 assert(temp_count()==13 && temp_slot(1)==1);
 assert(visible(NULL,hwmon_temp,hwmon_temp_input,12)==0444);
 assert(!visible(NULL,hwmon_temp,hwmon_temp_input,13));
 ec_profile=tp_profile_named("t480");verified=true;present=BIT(2);
 assert(visible(NULL,hwmon_temp,hwmon_temp_input,2)==0444);
 assert(!visible(NULL,hwmon_temp,hwmon_temp_input,3));
 assert(!visible(NULL,hwmon_in,hwmon_in_input,0));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src,exe=Path(tmp)/'visible.c',Path(tmp)/'visible'
            src.write_text(code)
            subprocess.run(['cc','-I',str(ROOT),str(src),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True)
