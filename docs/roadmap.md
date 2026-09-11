# Next work

## hwmon / sensors

- Expose fractional SBS battery temperature without confusing resolution with accuracy.
- Check ordinary page61's PCH/ME thermal-response cache for additional valid data.
- Validate cell-group format on other packs, including4S, and characterize freshness.
- Establish extended host transport for cell data without debug authentication.
- Replace machine-specific compatibility assumptions with tested EC/platform profiles.

## power_supply / BAT0

Expose identity/history/status through the appropriate battery interface, using
standard attributes where possible. Manufacture date, barcode, possible first-use
date, status and charging requests are already recoverable through ordinary EC
pages. Trace ambiguous vendor fields before naming them. Existing ACPI/thinkpad_acpi
attributes must be checked before duplication. Decide integration with the existing
BAT0 device rather than creating a competing battery or disguising metadata as hwmon.
No extra BAT0 attributes are currently implemented.

## Keyboard investigation (separate feature)

Coreboot h8 already has h8_f1_to_f12_as_primary(), controlling EC09 bits2/3.
The x230 board enables H8_HAS_PRIMARY_FN_KEYS only for X230s. Trace G2HT35WW
consumers before a reversible runtime test on ordinary X230. Observe keyboard
and ACPI hotkey input devices; verify both layers and restore on failure.
Fn/Ctrl swap(CE bit4), sticky Fn(00 bit3), and physical Fn+Esc are distinct paths.
No keyboard remapping is currently implemented by this module.
