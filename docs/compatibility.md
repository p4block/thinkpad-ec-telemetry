# Compatibility and validation

| Platform / EC | Status |
|---|---|
| X230, product2325DV5, coreboot CBET4000 fosc, EC G2HT35WW | Ordinary sensors and optional cell reads tested on Linux7.2.3 |
| Other X230 firmware configurations | Candidates; reports needed |
| Related Ivy Bridge ThinkPads / same MEC controller | Candidates; firmware protocol and board sensor mapping need checking |

A common EC chip makes reuse plausible, but does not establish identical
firmware addresses, temperature routing, channel units or ACPI locks.
The driver reads the EC build ID from registers F0–F7 under ECLK. G2HT35WW
is accepted regardless of BIOS vendor/version or machine-specific DMI strings.
If that ID is unreadable or invalid, an exact ThinkPad X230 model match permits
ordinary sensors. A different readable firmware ID requires `experimental=1`.
The ACPI ECLK lock is required in all cases.

Cell reads require a positively identified G2HT35WW EC plus the tested
SANYO/LNV-45N1023 battery format. Neither the X230 model fallback nor
`experimental=1` bypasses this fixed-RAM-layout requirement. Cell readings
remain disabled by default. The tested pack is aftermarket; its identity need
not establish the origin or age of its cells.

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
