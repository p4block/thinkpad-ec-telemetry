# Accelerometer and Sway rotation

Optional `x230_ec_accel` module and `thinkpad-rotate` user program. Tested on a
coreboot ThinkPad X230 with EC firmware **G2HT35WW**. The module requires both
that model and EC ID; there is no override for unsupported firmware.

This replaces the original experiment that repeatedly loaded a diagnostic module
and read its kernel log. No debug key is used. The hardware interface is an
ordinary EC mailbox discovered by tracing firmware and testing the real board.

## What it provides

- A Linux input accelerometer: **ThinkPad EC accelerometer**, ABS_X and ABS_Y.
- Raw unsigned firmware counts, not calibrated g or m/s². No invented Z axis.
  The first two polls after starting/resuming are discarded to flush startup data.
- Sampling every 500 ms while an input consumer holds the device open; no
  periodic driver work or sensor acquisition when the last consumer closes it.
- Power-off before suspend and restart after resume if still open.
- Bounded mailbox transactions, ACPI ECLK serialization, exclusive I/O-port
  ownership, and shutdown attempts on failures. The driver refuses an already
  active acquisition or self-test. Do not run the old diagnostic concurrently:
  it predates resource claiming and can bypass that protection.
- An unprivileged Rust Sway helper with one-second orientation dwell, a flat dead
  zone, original-transform restoration on exit, and stale-data detection.
- The helper blocks on input events, with a four-second stale-data deadline and
  immediate signalfd wakeup for stop requests; it has no independent polling timer.
  One thread, direct evdev/Sway IPC, no interpreter or async runtime.
- X/Y are retained from evdev axis-change events. Axis ioctls are only used for
  startup and recovery after dropped events, not for every report.
- The output-only sample command skips payload save/clear/restore, as verified
  in G2HT35WW firmware and Lenovo APS. Control commands preserve payload;
  ECLK locking, completion/acknowledgement and timeout ownership remain enforced.

The sensor is in the **keyboard base**, not the screen. Changing the hinge angle
is not measurable by this sensor. The current profile was measured on one
laptop: right edge down selects 270°, left selects 90°, front selects normal,
rear selects 180°. These directions worked in the physical Sway prototype.
The driver delivers raw data; only the optional helper applies this profile.

## NixOS

Import `nixos-module.nix`, or just `accelerometer.nix` if using another hwmon
package. Enable:

```nix
hardware.thinkpad-ec-telemetry.accelerometer = {
  enable = true;
  swayRotation = true;
};
```

This loads the module at boot and installs device permissions plus a **manually
started** user service. It does not enable continuous rotation at login:

```sh
systemctl --user start thinkpad-rotate
systemctl --user stop thinkpad-rotate
journalctl --user -u thinkpad-rotate
```

Or run `thinkpad-rotate` directly from a Sway terminal; Ctrl-C stops it. Use
`thinkpad-rotate --dry-run` to log transforms without applying them. Do not run
both copies: a per-user lock rejects a second instance. The service deliberately
does not restart itself after sensor/IPC failure; fix the cause, then start it.
The driver can remain loaded when rotation is stopped.

Device access is granted to the active local session with udev `uaccess`, not
by adding the user to the input group (which would expose other input devices).
The helper uses Sway's Unix socket directly, without root or subprocesses.
Only the active internal LVDS/eDP output is selected automatically; `--output`
can select another explicitly. Neither Sway configuration nor key mappings change.

## Other distributions

`./build.sh` builds both modules. To test just the accelerometer:

```sh
sudo insmod ./x230_ec_accel.ko
```

Install the udev rules from `udev/`, reload rules, and trigger the new input
device. Build the helper with Rust/Cargo, then run it as your desktop user:

```sh
cargo build --release --locked --manifest-path tools/thinkpad-rotate/Cargo.toml
tools/thinkpad-rotate/target/release/thinkpad-rotate
```

The binary needs no Python installation. Its Cargo lockfile pins the `libc` and
`serde_json` dependency trees. Nix users can also build just the helper with
`nix build .#thinkpad-rotate`. Installation to `/lib/modules/$(uname -r)/extra/`
and `depmod -a` can make the module available to modprobe. The existing
`install.sh` still installs only the hwmon module; accelerometer enablement is
separate and opt-in.

Run the helper's tests with
`cargo test --locked --manifest-path tools/thinkpad-rotate/Cargo.toml`.

## Calibration and diagnostics

Default center: X=572, Y=571. Positive excursions: 131/125; negative: 203/211.
The asymmetry is unresolved, so these are empirical pose thresholds, not
physical acceleration calibration. `--profile file.json` accepts replacement
`center`, `positive`, and `negative` two-element arrays for another machine.
A profile is a userspace choice and does not change raw kernel readings.

Read driver status without starting sampling:

```sh
cat /sys/module/x230_ec_accel/parameters/{active,last_error,samples}
```

`active` records successfully started acquisition, not a measured power rail.
`samples` counts delivered input reports. `last_error` is a negative errno or 0.
There is no per-sample printk. Each report includes MSC_TIMESTAMP as a freshness
heartbeat in host-receipt microseconds, not an EC sample timestamp.

If mailbox completion times out, its buffers are left untouched. A later cleanup
attempt only acknowledges the timed-out command issued by this driver. Failed
cleanup is logged rather than silently claiming the sensor powered off.
Suspend preparation is rejected if shutdown cannot be confirmed.
The protocol's idle status does not expose standalone sensor-power ownership;
coexistence with an unknown client that only powers the sensor is not validated.

## iio-sensor-proxy / GNOME

Not implemented yet. Stock iio-sensor-proxy's input backend requires ABS_Z and
assumes 256 counts/g. The supplied late udev rule excludes this raw two-axis
device from that backend. A real two-axis consumer/API integration and calibrated
axis handling remain future work. Sway works directly through evdev today.

## Validation limits

The hardware protocol and pose profile have been tested on one X230. Broader
model support, exhaustive mailbox fault injection, and a precise isolated power
measurement are outstanding. Treat this as an experimental driver with proper lifecycle
management, not as an upstream-qualified general ThinkPad driver.

### Validation on 2026-09-11

- Both modules built against Linux 7.2.3; full NixOS configuration built and
  activated. The Rust helper has eight tests covering pose classification,
  debounce, profiles, output selection, IPC framing, poll/signals, and dry-run
  restoration, plus retained-axis/overflow handling. Four C/build tests cover
  the actual mailbox code (including busy/timeout/recovery), build wrapper and
  firmware guard.
- I/O ownership appears as `1610-1611 : x230_ec_accel` nested inside the existing
  PNP motherboard reservation; no reservation was removed.
- Active-session ACL grants hal access only to the new accelerometer event node.
  The systemd user service runs as hal and uses no sudo/root helper.
- Opening the device starts acquisition; closing the last reader stops it with
  error status 0. With the service stopped, the sample count remained 176 across
  a two-second observation, and acquisition was inactive.
- Real S3 via a 15-second RTC wake test succeeded. The service survived, acquisition
  resumed, samples continued, and driver error status remained 0.
- Service stop/restart succeeded; the output was normal afterward. The user
  confirmed physical rotation works with both the prototype and the installed
  implementation. The service is left running.

### Initial power comparison

Kernel profiling subsequently found ~815 µs per callback excluding scheduled-out
time, dominated by the mailbox. The output-only sample fast path reduced this to
~325 µs (60%) in repeated baseline/optimized comparisons at the same poll rate.
Live rotation/cleanup and RTC-backed S3 passed with the optimized module.
Function-graph timings include tracing overhead and do not measure battery watts.

The Rust replacement was also tested live with synthetic calibration profiles:
90/180/270-degree transforms and restoration passed, along with dry-run,
duplicate rejection, wrong-device rejection, and acquisition shutdown on exit.
Its default profile preserves the user's previously verified physical poses.
In a 20-second stationary observation it consumed 2.63 ms of helper CPU time,
with 39 reports and 39 timeslices. RSS was 2364 KiB versus 22516 KiB for the
previous Python process. These are resource measurements, not measured watts.

An OFF/ON/OFF/ON battery test (60 s per measured phase, 15 s settling) averaged
6.645 W off versus 6.728 W on: an observed +0.083 W. RAPL package delta was +0.065 W.
A spike in the final ON phase and disagreement between pairs prevent treating
these as precise isolated overhead values. The daemon keeps the sensor/EC
acquisition running while enabled; power-on-open does not make autorotation free.

Firmware already flags >=5-count motion, but the traced path sets EC3 status
bit6 and later clears it; it does not generate a host interrupt. The matching
MEC family documentation explicitly distinguishes these status flags from SCI/SMI
signalling. A true motion-triggered host interface remains research work.

### Lenovo Windows APS comparison

Static analysis of Lenovo APS 1.82.0.20 (`n1msk20w.exe`, `apsx64.sys`) found a
backend using the same 0x1610/0x1611 mailbox and 0x161c status port. Its enabled
worker polls with a nominal 20 ms timeout. After establishing a quiet state, it
can check motion-status flags and reuse cached axes instead of reading the full
mailbox. Mailbox completion is also polled. This is evidence about the matching
backend, not a Windows runtime trace or a claim about every APS version.

That shortcut reduces transactions but retains periodic wakeups. Our 500 ms
interval is already much slower; checking only a short-lived motion flag at
that rate could miss movement. Adopting the shortcut would require a full-read
fallback or another persistent change indicator. No such optimization is enabled
in this driver yet.
