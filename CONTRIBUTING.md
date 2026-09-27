# Contributing

Start with [adding support](docs/adding-support.md). Working raw readings, failed
probes and partially mapped models are all useful reports. Use the compatibility
issue template and `python3 tools/report.py`; no disassembly is required to
submit a report. Contributors can map the remaining sensors in a later PR.


Keep ordinary EC telemetry separate from optional debug access. No generic EC
write interface, unsolicited charger/policy changes or unbounded busy waits.
Preserve ACPI ECLK serialization and restoration on error. New channels should
return unavailable rather than plausible-looking zeros for absent/invalid data.

For compatibility reports include machine model, BIOS/EC version, kernel version,
which options were enabled, sensors output and relevant kernel errors. Redact
serials, UUIDs, battery barcodes and other identifiers before posting public reports.
Do not upload complete EC dumps by default. Negative results are useful.

Build with matching kernel headers; `nix build` checks the pinned
reference build. Run `python3 -m unittest discover -s tests`. Tests exercise profile selection and EC transactions without hardware; they
cannot establish physical compatibility.
Hardware validation must check coexistence with ACPI battery reads, error handling,
page restoration and unload/reload. A shared EC chip alone does not establish a
compatible register map.

Support requires verified model/firmware evidence; do not widen guards based on EC family.
Changes to experimental formats should be documented in docs/protocol.md and the
compatibility table. Never present charging requests as measured current/voltage.
