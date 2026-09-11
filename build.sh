#!/bin/sh
# SPDX-License-Identifier: WTFPL
set -eu
src_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
: "${KDIR:=/lib/modules/$(uname -r)/build}"
if [ ! -f "$KDIR/Makefile" ]; then
    echo "Missing kernel build tree: $KDIR" >&2
    echo 'Set KDIR to the configured build/output tree of your running kernel.' >&2
    exit 1
fi
# Kbuild splits M= on whitespace even when the shell argument is quoted.
# Build only our sources in a private directory whose path has no spaces.
build_dir=$(mktemp -d /tmp/x230-ec-build.XXXXXXXX)
trap 'rm -rf -- "$build_dir"' EXIT HUP INT TERM
cp "$src_dir/Makefile" "$src_dir/x230_ec_hwmon.c" "$src_dir/cell-voltage.h" "$src_dir/x230_ec_accel.c" "$build_dir/"
make -C "$KDIR" M="$build_dir" "$@" modules
cp "$build_dir/x230_ec_hwmon.ko" "$build_dir/x230_ec_accel.ko" "$src_dir/"
echo "Built: $src_dir/x230_ec_hwmon.ko"
