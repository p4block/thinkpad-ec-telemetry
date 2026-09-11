# SPDX-License-Identifier: WTFPL
"""Run the actual compatibility predicate with mocked EC/ACPI/DMI reads."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FirmwareGuard(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cc'), 'C compiler required')
    def test_firmware_and_model_policy(self):
        source = (ROOT/'x230_ec_hwmon.c').read_text()
        start = source.index('static int check_ec_firmware(void)')
        end = source.index('#include "cell-voltage.h"', start)
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
typedef unsigned char u8;
#define EC_LOCK "test"
#define ACPI_FAILURE(x) (x)
#define DMI_PRODUCT_VERSION 1
#define DMI_PRODUCT_NAME 2
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
static bool experimental, known_ec_layout, model, lock_fail, read_fail;
static const char *fw;
static int acpi_acquire_mutex(void *p, const char *s, int t) { return lock_fail; }
static void acpi_release_mutex(void *p, const char *s) {}
static int ec_read(int addr, u8 *v) {
 assert(addr >= 0xf0 && addr <= 0xf7);
 if(read_fail) return -EIO;
 *v=fw[addr-0xf0]; return 0;
}
static bool dmi_match(int field, const char *value) {
 assert(!strcmp(value,"ThinkPad X230")); return model;
}
'''+source[start:end]+r'''
int main(void) {
 fw="G2HT35WW";
 assert(check_ec_firmware()==0 && known_ec_layout); /* no DMI required */
 fw="G2HT99WW"; model=true;
 assert(check_ec_firmware()==-ENODEV && !known_ec_layout);
 experimental=true;
 assert(check_ec_firmware()==0 && !known_ec_layout);
 experimental=false; fw="        ";
 assert(check_ec_firmware()==0 && !known_ec_layout);
 model=false;
 assert(check_ec_firmware()==-ENODEV);
 read_fail=true; model=true;
 assert(check_ec_firmware()==0 && !known_ec_layout);
 experimental=true; lock_fail=true;
 assert(check_ec_firmware()==-EBUSY);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'guard.c';exe=Path(tmp)/'guard'
            src.write_text(harness)
            subprocess.run(['cc',str(src),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True)
