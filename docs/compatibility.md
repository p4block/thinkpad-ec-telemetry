# Compatibility and validation

| Platform / EC | Status |
|---|---|
| X230, product2325DV5, coreboot CBET4000 fosc, EC G2HT35WW | Ordinary sensors and optional cell reads tested on Linux7.2.3 |
| T480, coreboot, EC N24HT37W | Temperature-only profile tested on Linux 7.2.7; [evidence](t480.md) |
| Other X230 firmware configurations | Candidates; reports needed |
| Related Ivy Bridge ThinkPads / same MEC controller | Candidates; firmware protocol and board sensor mapping need checking |

A common EC chip does not establish identical firmware addresses, temperature
routing, units or ACPI locks. Both drivers now require exact model and readable
EC-ID matches from `profiles.h`; automatic X230 model-only fallback is removed.
The ACPI mutex is required in all cases. Verified temperature and acceleration
features are recorded independently per match row.

For unlisted hardware, see [explicit candidate-profile testing](adding-support.md).
An unverified temperature test uses raw slot labels and disables electrical and
debug channels. The old `experimental=1` option no longer exists.

Cell reads require the separate `TP_CELLS` grant, exact G2HT35WW firmware and
the tested SANYO/LNV-45N1023 battery format. They remain opt-in and are disabled
when `allow_unsupported=1` is supplied. Copying a profile or adding a firmware
revision does not automatically grant debug support. The tested pack is
aftermarket; its identity does not establish the origin or age of its cells.

Two captures showed three probable series-group voltages whose sum tracked
pack voltage exactly, including a1mV change. Parallel cells share group voltage.
3S2P and3S3P both have three groups. Lenovo's4-cell Battery44 is nominal14.4V,
consistent with4S1P; three channels must not be generalized to that pack.
[Lenovo specifications](https://support.lenovo.com/au/en/accessories/pd024287-thinkpad-battery-44-4-cell-thinkpad-battery-44-6-cell-and-thinkpad-battery-44-9-cell-overview).
The vendor format and physical group order are still inferred.

Validated before extraction: unload/reload;60 concurrent ordinary-sensor/ACPI
battery batches;24 concurrent cell/current/temperature batches; EC81 restored00;
NixOS activation and boot-entry references. Charging current agreed with ACPI
power divided by pack voltage. Discharging sign is firmware-derived but has
not been validated with a live charge-to-discharge test in this work.

Known limits: EC cache age unknown; sensor-area labels lack controlled stimulus
validation; empirical input watts assume20V and include charging; no measured
CPU/PCH/DIMM rail current. An absent second battery produces N/A, including
read errors in verbose sensors output.

Standalone extraction validation on2026-09-11: unchanged unified driver sources
built successfully against the pinned Linux7.2.3 kernel; build-wrapper regression
checks passed for spaces and missing build trees; NixOS option evaluation generated
the expected modprobe settings. No new hardware validation or expanded model
support is implied by the repository extraction.

Profile refactor: exact model/firmware allowlisting replaces the inherited X230
fallback. Existing verified maps and mailbox behavior are preserved. Test
coverage includes match-table entries, malformed/unknown selections, feature
isolation, EC-ID failures and page restoration. Physical X230 retesting of this
refactor remains outstanding; a build/test pass does not replace it.

On 2026-09-27 the profile-based drivers passed their Nix build/checks and a live
T480 reload: automatic `t480` / `N24HT37W` matching, ten available temperatures,
and 24 concurrent temperature/battery batches over twelve seconds while fresh
accelerometer frames continued. No new model or EC revision is claimed by this
refactor; candidate reports still require the workflow above.
