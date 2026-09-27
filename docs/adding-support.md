# Adding a ThinkPad or EC revision

A report such as “raw motion works on T430” is useful without a complete board
map. Report temperatures and acceleration separately: one does not prove the
other, and matching an EC family does not prove its register protocol.

## Report first

Open a **ThinkPad compatibility report** issue and include:

```sh
python3 tools/report.py
```

This reads allowlisted sysfs fields and registered hwmon channels. A hwmon
read can refresh the driver cache through its usual EC transaction. The tool
does not load modules, probe ports directly, or collect serial/UUID fields,
hostnames, battery identifiers, calibration or whole logs. It shows the selected profile,
actual EC ID, verified status and sensor results when the drivers are loaded.
On an unsupported machine it still reports model/BIOS information. State the
repository commit, module arguments and tests performed separately.

Do not describe a successful status probe as working acceleration. Physical
movement, fresh frames and clean close/reopen must be tested too. Likewise,
plausible temperatures do not identify their physical location.

## Trying an existing protocol profile

Automatic matching requires an exact model and readable eight-byte EC firmware
ID in `tp_matches`. Unknown revisions and models are rejected. There is no
model-only fallback and the old `experimental=1` option is removed.

Before borrowing a profile, check firmware/schematic evidence for the existing
EC-ID transport, ACPI ECLK path, page registers or mailbox address/commands.
A borrowed profile is an explicit hypothesis, not a safe generic probe. Loading
hwmon selects/restores an EC page; an accelerometer status probe writes and
acknowledges a mailbox command even though it does not enable sampling.

For example, after establishing a reason to test the X230 protocol:

```sh
# Stop consumers and unload the existing instance before changing options.
sudo insmod ./thinkpad_ec_hwmon.ko profile=x230 allow_unsupported=1
sudo insmod ./thinkpad_ec_accel.ko profile=x230 allow_unsupported=1 probe_only=1
python3 tools/report.py
```

`allow_unsupported=1` requires an existing named profile (`x230` or `t480`). A
misspelled profile, unreadable EC ID or unavailable ACPI lock still fails.
Unverified hwmon exposes raw `EC slot N` labels and no electrical/debug channels.
It reads the candidate profile's page length, not arbitrary EC memory. Electrical
and debug channels are disabled whenever the testing flag is supplied, even on
a recognized machine. Logs and sysfs report `verified=N` for an unlisted pair.

Only after validating the sampling commands, reload the accelerometer with
`probe_only=0` to test acquisition. Use `thinkpad-rotate sample`, move the base
along both axes, stop it, and check `active=N` and `last_error=0`. Tests must
include battery coexistence, close/reopen, and module unload/reload. Report
suspend/resume separately; do not assume it works because sampling works.
Never run two EC accelerometer drivers concurrently or bypass resource claims.

NixOS supports `hardware.thinkpadEc.profile = "x230"` and
`allowUnsupported = true`. In that mode its accelerometer configuration always
uses status-only probing; acquiring on unverified hardware requires the explicit
manual reload above. This avoids persisting unverified acquisition at boot.

## Turn evidence into support

`profiles.h` contains two separate tables:

1. **`tp_profiles`:** protocol/map data: ACPI mutex, temperature page and length,
   exported slots/labels, absent-channel policy, mailbox port and reply quirks.
2. **`tp_matches`:** exact model alias + EC ID + profile name + verified features.
   `TP_HWMON` and `TP_ACCEL` are independent. `TP_CELLS` is a separate grant for
   the fixed G2HT35WW debug implementation, not inherited by a new EC revision.

For another firmware revision with proven identical behavior, add its exact
match row pointing to the existing profile. For another motherboard with a
different map, add a profile and its match rows. Copy only verified capabilities:
leave `x230_electrical` and `g2ht35ww_cells` false unless their implementation is
specifically established. A map-only contribution needs no driver fork or edits
to the shared transaction code.

A temperature channel consists of `{slot, label}`. Labels can remain generic
while mapping is incomplete. Preserve ABI channel ordering for existing
profiles. A schematic identifies diode locations; firmware establishes how
those diodes reach host slots. Record both chains of evidence in `docs/` and
link the report in the compatibility table. Boardview helps confirm placement.

Illustrative match row, **not evidence that T430 is supported**:

```c
{"ThinkPad T430", "<EC ID>", "t430", TP_HWMON},
```

Accelerometer support can be verified first using a known protocol profile;
a later temperature profile can supply the board-specific map. The current
transport implementations use F0..F7 identity, EC81/A0..AF temperature paging,
and the indexed two-axis mailbox. Profiles do not make fundamentally different
protocols work: add a separately reviewed implementation when necessary.
All revisions of a model must share the configured EC-ID transport, or that
bootstrap needs extending too.

## Validate the change

```sh
nix flake check
# Or with a C compiler and Python:
python3 -m unittest discover -s tests
```

The tests iterate the match table, check profile bounds/slot uniqueness, test
unknown/swapped pairs and explicit overrides, and inject page/identity failures.
Extend them for new quirks or protocols. Hardware evidence remains required;
passing CI alone is not a compatibility report. Keep protocol notes, supported
feature claims and the compatibility table consistent with actual testing.
