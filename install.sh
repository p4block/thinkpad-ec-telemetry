#!/bin/sh
# SPDX-License-Identifier: WTFPL
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"
if [ "$(id -u)" -ne 0 ]; then
    echo 'Run: sudo ./install.sh [make arguments]' >&2
    exit 1
fi
: "${KDIR:=/lib/modules/$(uname -r)/build}"
export KDIR
release=$(make -s -C "$KDIR" kernelrelease)
if [ "$release" != "$(uname -r)" ]; then
    echo "Kernel tree is $release; running kernel is $(uname -r). Refusing installation." >&2
    exit 1
fi
./build.sh "$@"
install -D -m 644 x230_ec_hwmon.ko "/lib/modules/$release/extra/x230_ec_hwmon.ko"
depmod -a "$release"
if [ -d /sys/module/x230_ec_hwmon ]; then
    modprobe -r x230_ec_hwmon
fi
# Testing another machine must be an explicit choice, not an installer default.
modprobe x230_ec_hwmon experimental="${EXPERIMENTAL:-0}" cell_voltages="${CELL_VOLTAGES:-0}"
if command -v sensors >/dev/null 2>&1; then sensors 'x230_ec-*'; fi
echo 'Installed and loaded. No automatic boot loading was configured.'
