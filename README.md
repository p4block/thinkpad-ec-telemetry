# thinkpad-ec-telemetry

Linux kernel module exposing extra motherboard thermal sensors, battery current, charger telemetry, and cell-group voltages via standard `hwmon`. No full kernel rebuild, patched `lm_sensors`, or EC firmware flashes are required.

This project originated from debugging idle power consumption on an X230 by analyzing Embedded Controller (EC) firmware routines alongside motherboard schematics.

**This work was mainly performed by GPT6** with some guidance on what to look for in the Intel docs and motherboard schematics.

Special thanks to [thinkpad-ec](https://github.com/hamishcoleman/thinkpad-ec).

## Output Example

Reading from a coreboot X230 running on battery with the optional cell-voltage feature enabled:
```text
$ sensors 'x230_ec-*'
x230_ec-isa-0000
Adapter: ISA adapter
Charger IOUT monitor:              0.00 V
Battery0 group A (experimental):   4.07 V
Battery0 group B (experimental):   4.06 V
Battery0 group C (experimental):   4.07 V
CPU (EC):                          +41.0°C
CPU VRM area:                      +30.0°C
WLAN area:                         +29.0°C
Battery 0 source 2:                +25.0°C
Battery 1 source 2:                    N/A
Battery 0 source 1:                +25.0°C
Battery 1 source 1:                    N/A
Memory top:                        +33.0°C
WWAN area:                         +25.0°C
Memory bottom:                     +36.0°C
PCH:                               +48.0°C
VTT regulator area:                +30.0°C
DC input estimate (20V):           0.00 W
Battery0 current (+charge):      -487.00 mA
Battery0 average (+charge):      -537.00 mA
```

The adapter is disconnected in this example, so zero charger readings do not mean zero laptop power consumption. Negative battery current means discharge; positive means charging. `N/A` marks the absent second battery.

## Features

* **Accelerometer (optional):** Raw X/Y Linux input device and opt-in Sway rotation via a small Rust daemon, with power-on-open and power-off-on-close. Tested on X230/G2HT35WW; no debug key. See [setup and limitations](docs/accelerometer.md).
* **Thermal telemetry:** PCH temperature, CPU VRM/WLAN/memory/WWAN/VTT regulator area temperatures, and two temperature sources per battery.
* **Battery current:** Instantaneous and EC-averaged readings with directional sign.
* **Charger input:** IOUT signal monitoring and DC input wattage estimates based on empirical calibration at 20 V, including battery charging.
* **Cell-group voltages (experimental):** Series-group monitoring on the tested battery format; parallel cells share a group reading.

Standard thermal, current, and charger telemetry operate without special EC keys and are candidates for upstreaming into `thinkpad-acpi`. Cell-group readings use a dedicated debug interface, are disabled by default, and remain locked to tested hardware configurations.

**The magic debug key:** Cell-group voltage reads require unlocking the EC debug interface with an eight-byte key derived from the public `G2HT35WW` firmware. The module includes this key and supplies it automatically when the optional feature is enabled and authorization is needed. It is not your BIOS password or a device-unique secret for the tested firmware. Other firmware versions may differ. Ordinary temperature, current, and charger readings do not use it. See the [protocol notes](docs/protocol.md) for details.

## Quickstart

### Prerequisites (Debian/Ubuntu)
```sh
sudo apt install build-essential linux-headers-$(uname -r) lm-sensors
```

### Build and Load
```sh
git clone https://github.com/p4block/thinkpad-ec-telemetry.git
cd thinkpad-ec-telemetry
./build.sh
sudo insmod ./x230_ec_hwmon.ko experimental=1
sensors 'x230_ec-*'
```

The driver checks the EC firmware ID directly: `G2HT35WW` is accepted without a specific BIOS version. If the ID is unavailable, it falls back to an X230 model check for ordinary sensors. Setting `experimental=1` enables ordinary sensor testing on other EC versions; it does not bypass the cell-voltage firmware check. Hardware validation is currently performed on a coreboot X230 running firmware `G2HT35WW`. See the [compatibility guide](docs/compatibility.md) for details on submitting hardware telemetry reports.

Unload an existing module before loading a replacement:
```sh
sudo modprobe -r x230_ec_hwmon
```

### Custom Kernel Trees or Installation

To target a separate kernel tree:
```sh
KDIR=/path/to/kernel-output ./build.sh LLVM=1
```

To install directly to system module storage:
```sh
sudo env EXPERIMENTAL=1 ./install.sh
```

Manual installation does not configure loading at boot.

## NixOS Configuration

Import `nixosModules.default` (or `nixos-module.nix` from a local checkout) into your system configuration:
```nix
hardware.thinkpad-ec-telemetry = {
  enable = true;
  experimental = true; # Allow unvalidated hardware testing
  # cellVoltages = true; # Enables cell-group monitoring (strict compatibility checks apply)
};
```

## Technical Details

* **ACPI Safety:** Page and debug transactions acquire the ACPI EC mutex and restore shared state. Debug timeouts leave potentially in-flight request buffers untouched.
* **Caching:** Hwmon uses a 10-second demand-driven cache. The optional accelerometer polls only while an input consumer has it open.
* **Scope:** Exposes read-only measurements; internally writes page selectors and debug requests. The optional accelerometer also controls sensor power and acquisition. Does not modify fan tables, adjust charging thresholds, or write to firmware flash memory.
* **Artifacts:** Repository contains the driver, offline snapshot decoder, and [protocol documentation](docs/protocol.md). Proprietary firmware binaries are neither included nor required.

## Roadmap

* Expansion of battery thermal channels and additional motherboard sensor locations.
* Machine profiles for non-X230 Ivy Bridge ThinkPads.
* Fn/media-key state introspection (e.g., Fn-lock behavior).

Refer to the [roadmap](docs/roadmap.md) and [contributing guide](CONTRIBUTING.md) for details.

## License

[WTFPL v2](LICENSE).

The kernel module declares `MODULE_LICENSE("GPL")` for symbol compatibility with the Linux kernel; individual source file licensing is maintained in their respective SPDX headers.
