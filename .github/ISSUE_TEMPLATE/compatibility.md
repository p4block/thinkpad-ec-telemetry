---
name: ThinkPad compatibility report
about: Working, partly working, or failed model/EC combinations
title: ''
---

## Machine and test

- Model and EC firmware ID:
- Stock BIOS or coreboot (version):
- Repository commit:
- Tested profile and exact module options:
- Temperature reads: works / partial / fails / not tested
- Accelerometer status probe: works / fails / not tested
- Physical tilt, close/reopen, suspend/resume: describe what was actually tested
- Coexistence with ACPI battery reads:

## Compatibility snapshot

Paste `python3 tools/report.py` output. It selects model/firmware and driver
fields, not serials, UUIDs, hostnames, or battery identifiers. Review before posting.

```json
```

## Mapping evidence (if available)

Schematic board code/revision, sheet and diode/component references; firmware
source/table addresses; which EC slots correspond to which locations.
Working raw readings are useful even if their physical locations are unknown.
Do not copy labels from the borrowed profile based only on similar temperatures.

## Failure details

Exact commands/errors and relevant `thinkpad_ec_*` kernel messages, with personal
identifiers removed. Do not upload full firmware/EC dumps or proprietary board files.
