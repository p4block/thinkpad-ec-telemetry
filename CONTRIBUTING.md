# Contributing

Keep ordinary EC telemetry separate from optional debug access. No generic EC
write interface, unsolicited charger/policy changes or unbounded busy waits.
Preserve ACPI ECLK serialization and restoration on error. New channels should
return unavailable rather than plausible-looking zeros for absent/invalid data.

For compatibility reports include machine model, BIOS/EC version, kernel version,
which options were enabled, sensors output and relevant kernel errors. Redact
serials, UUIDs, battery barcodes and other identifiers before posting public reports.
Do not upload complete EC dumps by default. Negative results are useful.

Build with ./build.sh against the intended kernel; `nix build` checks the pinned
reference build. Run `python3 -m unittest discover -s tests`. Tests exercise build
packaging without hardware; they cannot establish EC protocol compatibility.
Hardware validation must check coexistence with ACPI battery reads, error handling,
page restoration and unload/reload. A shared EC chip alone does not establish a
compatible register map.

Current source retains the original module name for existing installations.
Changes to experimental formats should be documented in docs/protocol.md and the
compatibility table. Never present charging requests as measured current/voltage.
