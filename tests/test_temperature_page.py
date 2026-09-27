# SPDX-License-Identifier: WTFPL
"""Exercise the actual page transaction, including injected EC failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class TemperaturePage(unittest.TestCase):
    def test_restore_and_cache_commit(self):
        source = (Path(__file__).resolve().parents[1] / 'x230_ec_hwmon.c').read_text()
        function = source[source.index('static int snapshot(void)'):source.index('static umode_t visible(')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
typedef unsigned char u8;
#define EC_LOCK "test"
#define ACPI_FAILURE(x) (x)
#define pr_err_ratelimited(...) ((void)0)
static bool t480_layout;
static u8 temps[14], page;
static int fail_at, operation, locks, reads;
static int acpi_acquire_mutex(void *p, const char *s, int t) { locks++; return 0; }
static void acpi_release_mutex(void *p, const char *s) { locks--; }
static int ec_read(int addr, u8 *v) {
 if (++operation == fail_at) return -EIO;
 if (addr == 0x81) { *v=page; return 0; }
 assert(page==0x60 && addr>=0xa0 && addr<=0xad);
 reads++; *v=addr-0xa0+30; return 0;
}
static int ec_write(int addr, u8 v) {
 assert(addr==0x81);
 if (++operation == fail_at) return -EIO;
 page=v; return 0;
}
''' + function + r'''
static void reset(int fail) {
 page=7; fail_at=fail; operation=reads=locks=0;
 memset(temps,0xa5,sizeof(temps));
}
int main(void) {
 for(int board=0;board<2;board++) {
  t480_layout=board; int count=board?14:13;
  reset(0); assert(!snapshot() && page==7 && !locks && reads==count);
  for(int i=0;i<count;i++) assert(temps[i]==30+i);
  // Saved-page read, selection, verification and every data read may fail.
  for(int fail=1;fail<=3+count;fail++) {
   reset(fail); assert(snapshot()==-EIO && page==7 && !locks);
   for(int i=0;i<14;i++) assert(temps[i]==0xa5);
  }
  // A failed restore must be surfaced and must not commit the cache.
  reset(4+count); assert(snapshot()==-EIO && !locks);
  for(int i=0;i<14;i++) assert(temps[i]==0xa5);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp)/'page.c', Path(tmp)/'page'
            src.write_text(harness)
            subprocess.run(['cc', str(src), '-o', str(exe)], check=True, capture_output=True)
            subprocess.run([str(exe)], check=True)
