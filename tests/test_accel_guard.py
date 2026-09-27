# SPDX-License-Identifier: WTFPL
"""Compile the actual model/firmware guards with mocked DMI values."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class AccelerometerGuard(unittest.TestCase):
    def test_exact_pairs(self):
        source = (Path(__file__).resolve().parents[1]/'thinkpad_ec_accel.c').read_text()
        start=source.index(' bool x230 =')
        model=source[start:source.index(' if (ACPI_FAILURE',start)]
        start=source.index(' if (ret || memcmp(fw,')
        firmware=source[start:source.index(' /* PNP',start)]
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#define DMI_PRODUCT_VERSION 0
#define DMI_PRODUCT_NAME 1
static bool t480;
static const char *dmi;
static bool dmi_match(int field,const char *value){return !strcmp(dmi,value);}
static int accept(const char *fw){int ret=0;
'''+model+firmware+r'''
return 0;}
int main(void){
 const char *models[]={"ThinkPad X230","ThinkPad T480","T480","ThinkPad T480s","ThinkPad X220"};
 const char *firmwares[]={"G2HT35WW","N24HT37W","N24HT99W"};
 for(int m=0;m<5;m++)for(int f=0;f<3;f++){
  dmi=models[m];
  bool expected=(m==0 && f==0)||((m==1||m==2)&&f==1);
  assert((accept(firmwares[f])==0)==expected);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src,exe=Path(tmp)/'guard.c',Path(tmp)/'guard'
            src.write_text(code)
            subprocess.run(['cc',str(src),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True)
