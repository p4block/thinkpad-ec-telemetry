# Implemented EC interfaces

Evidence comes from disassembly of Lenovo G2HT35WW EC firmware and the X230
schematic. Numbers below are hexadecimal unless units are given. Firmware
images and memory captures are intentionally outside this source repository.
The historical investigation remains in the parent workspace.

## Ordinary page interface

Acquire ACPI `\_SB.PCI0.LPCB.EC.ECLK`, save selector81, select and verify a page,
read A0–AF as needed, restore/verify selector, release lock. A Linux mutex also
serializes this module's readers. Cache both successes and errors for10seconds.

Page60 indices0,2,3,4,5,6,7,8,9,10,11,12 supply signed Celsius;80 means absent.
Firmware1DBF8 indexes cached records at801EA8+8*index. The normal publisher13A00
copies only index0 into legacy EC78, explaining the missing ordinary channels.
Board labels: remote1 WLAN,Q133; remote2 CPU VRM,Q135; remote3 RAM top,Q137;
remote4 WWAN,Q132; remote5 RAM bottom,Q134; remote6 VTT,Q136.

PCH temperature comes through an EC SMBus cache, wire address96/7-bit4B,
command40, with PCH-only and multibyte modes. No inference about functional ME
power management follows from this temperature being available.

EC CC/CD is a firmware-averaged charger IOUT monitor in millivolts. High/low/high
reads guard carry changes. ADC8 connects to charger BQ24760 IOUT through the
board's ISYS_EC net. Estimated watts=mV/10 at20V is empirical, not an exact
manufacturer transfer function. No extra transaction is needed for power when
monitor voltage is already cached.

Battery0: page00 AA/AB voltage supplies availability; page00 A8/A9 signed EC
average current; page01 A6/A7 signed instantaneous current, milliamps. Query
getters1B424/1B3F8 read battery cache+18/+2E respectively. Firmware1C2B8 maps
SBS0A into+2E. EC average differs from the gauge's separate SBS0B average.

## Optional fixed-address debug reads

`cell-voltage.h` reads only a fixed list of addresses; no arbitrary address
parameter or target write operation is exposed. Battery cache base800C24;
manufacturer/name at+50/+60, voltage+2C, ManufacturerData+80. Candidate group
words are at ManufacturerData+6,+8,+A. A verified G2HT35WW build ID, valid flags, known battery identity, plausible
voltage range and sum-to-pack check gate outputs.

Command90=95 invokes read32;91–93 target address;94–97 response in big-endian
byte order. Request shadows saved/restored, command left idle. Timeouts avoid
restoring a potentially in-flight buffer. ECLK protects the request transaction.

Firmware2B88 derives key slot1 from public image data: source base140C8 plus
(BE16 at022E &7FFF), take eight nonzero bytes. Authentication uses EC3E–45 and
command3D=C1. A successful match sets a volatile permission flag. This is not
a BIOS password or per-user secret. Other firmware keys/layouts are untested.
Successful authorization remains enabled; the module does not revoke it on
unload. Ordinary sensors never invoke this mechanism.

Debug requests and page selection necessarily write request/selector registers.
The driver does not change charging settings, fan policy, firmware or target RAM.
Other EC tools that ignore ECLK can still interfere.

## References

- [Linux hwmon API](https://www.kernel.org/doc/html/latest/hwmon/hwmon-kernel-api.html)
- [Smart Battery Data Specification](https://sbs-forum.org/specs/sbdat110.pdf)
- [Lenovo EC research](https://github.com/hamishcoleman/thinkpad-ec)

The offline decoder identifies vendor fields separately from standardized SBS
fields. Cached charger settings are not measurements or guaranteed current IC
register values. Snapshots are non-atomic and can disturb idle-power measurements.
