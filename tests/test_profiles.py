# SPDX-License-Identifier: WTFPL
"""Compile the real profile selection and EC-ID reader with mocked Linux APIs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class Profiles(unittest.TestCase):
    def test_profiles_matching_overrides_and_identity_errors(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
typedef unsigned char u8;
#define module_param(...)
#define MODULE_PARM_DESC(...)
#define KBUILD_MODNAME "test"
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
#define DMI_PRODUCT_NAME 0
#define DMI_PRODUCT_VERSION 1
#define ACPI_FAILURE(x) (x)
static const char *name, *version, *fw;
static int lock_error, read_error, locks, reads;
static const char *dmi_get_system_info(int field) { return field ? version : name; }
static int acpi_acquire_mutex(void *p,const char*s,int t) {
 if(lock_error)return lock_error; locks++; return 0;
}
static void acpi_release_mutex(void*p,const char*s) { locks--; }
static int ec_read(int addr,u8 *v) {
 assert(locks==1 && addr>=0xf0 && addr<=0xf7); reads++;
 if(read_error)return read_error; *v=fw[addr-0xf0];return 0;
}
#include "ec-profile.h"
static void reset(void) {
 name="20L6"; version="ThinkPad T480"; fw="N24HT37W";
 profile=NULL;allow_unsupported=false;verified=false;ec_profile=NULL;
 lock_error=read_error=locks=reads=0;
 memset(ec_firmware,0,sizeof(ec_firmware));
}
int main(void) {
 // Profile shape invariants: mistakes must fail CI before touching hardware.
 for(unsigned i=0;i<sizeof(tp_profiles)/sizeof(tp_profiles[0]);i++) {
  const struct tp_ec_profile *p=&tp_profiles[i];
  assert(p->temp_bytes>0 && p->temp_bytes<=TP_MAX_TEMPS);
  assert(p->temp_count>0 && p->temp_count<=TP_MAX_TEMPS);
  assert(p->ec_lock && p->hwmon_name && p->mailbox_port);
  unsigned slots=0;
  for(int j=0;j<p->temp_count;j++) {
   unsigned slot=p->temps[j].slot;
   assert(slot<p->temp_bytes && p->temps[j].label);
   assert(!(slots&(1u<<slot)));slots|=1u<<slot;
  }
 }
 // Every allowlisted feature works through the actual identity reader.
 for(unsigned i=0;i<sizeof(tp_matches)/sizeof(tp_matches[0]);i++) {
  const struct tp_ec_match *m=&tp_matches[i];
  assert(strlen(m->firmware)==8 && tp_profile_named(m->profile));
  for(unsigned j=0;j<i;j++) {
   const struct tp_ec_match *previous=&tp_matches[j];
   if(!strcmp(m->model,previous->model)) {
    assert(!strcmp(tp_profile_named(m->profile)->ec_lock,
                   tp_profile_named(previous->profile)->ec_lock));
    if(!strcmp(m->firmware,previous->firmware) && (m->features&previous->features))
     assert(!strcmp(m->profile,previous->profile));
   }
  }
  for(unsigned feature=TP_HWMON;feature<=TP_CELLS;feature<<=1) {
   reset();version=m->model;fw=m->firmware;
   int ret=select_ec_profile(feature);
   assert((ret==0)==!!(m->features&feature));
   if(!ret)assert(verified && !strcmp(selected_profile,m->profile));
   assert(!locks);
  }
 }
 reset();assert(!select_ec_profile(TP_HWMON) && verified && reads==8);
 assert(!tp_feature_verified(TP_CELLS));
 reset();version="ThinkPad X230";fw="G2HT35WW";
 assert(!select_ec_profile(TP_ACCEL) && tp_feature_verified(TP_CELLS));
 reset();version="ThinkPad UNLISTED";fw="G2HT35WW";
 assert(select_ec_profile(TP_HWMON)==-ENODEV && reads==0);
 reset();fw="N24HT99W";
 assert(select_ec_profile(TP_ACCEL)==-ENODEV);
 reset();fw="G2HT35WW";
 assert(select_ec_profile(TP_ACCEL)==-ENODEV); // swapped model/firmware
 reset();allow_unsupported=true;
 assert(select_ec_profile(TP_HWMON)==-EINVAL && reads==0);
 reset();profile="typo";allow_unsupported=true;
 assert(select_ec_profile(TP_HWMON)==-ENODEV && reads==0);
 reset();version="ThinkPad UNLISTED";profile="x230";
 assert(select_ec_profile(TP_HWMON)==-ENODEV);
 allow_unsupported=true;
 assert(!select_ec_profile(TP_HWMON) && !verified);
 assert(!tp_feature_verified(TP_CELLS));
 reset();profile="x230";allow_unsupported=true;
 assert(!select_ec_profile(TP_ACCEL) && !verified); // no false T480 verification
 reset();fw="        ";profile="t480";allow_unsupported=true;
 assert(select_ec_profile(TP_HWMON)==-ENODEV && !locks);
 reset();read_error=-EIO;profile="t480";allow_unsupported=true;
 assert(select_ec_profile(TP_ACCEL)==-EIO && !locks);
 reset();lock_error=1;
 assert(select_ec_profile(TP_HWMON)==-EBUSY && !locks && reads==0);
 reset();name=version=NULL;
 assert(select_ec_profile(TP_HWMON)==-ENODEV);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp)/'profiles.c', Path(tmp)/'profiles'
            src.write_text(code)
            subprocess.run(['cc','-std=c11','-I',str(ROOT),str(src),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True)
