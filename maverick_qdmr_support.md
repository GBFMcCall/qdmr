This is a troubleshooting/build log for getting QDMR to talk to the Maverick natively on Linux
(bypassing the Wine/CPS COM-port dead end — see `maverick_wine_troubleshooting.md`).

# BridgeCom Maverick support in QDMR (custom fork) — Status Log
*What was changed, why, and what's confirmed working so far.*

---

## Summary

QDMR (`hmatuschek/qdmr`) doesn't officially support the BridgeCom Maverick. It's a rebadged
AnyTone AT-D890UV (see `general_radio_context.md`), and QDMR's AnyTone driver only recognized two
USB VID/PID pairs for AnyTone-branded programming cables, neither of which matches the Maverick's
cable. Cloned the upstream repo to `~/src/qdmr`, patched it to recognize the Maverick's real USB
ID and its device-identification string, and found/fixed a real (non-Maverick-specific) decoding
bug along the way. **`dmrconf detect` and `dmrconf read` both now work cleanly against the
physical radio.** Writing to the radio has not been attempted yet.

---

## Environment

| Item | Value |
|---|---|
| Fork location | `~/src/qdmr` (cloned from `https://github.com/hmatuschek/qdmr.git`) |
| Build dir | `~/src/qdmr/build` (CMake + Qt6, `cmake --build .`) |
| Built binaries | `~/src/qdmr/build/src/qdmr` (GUI), `~/src/qdmr/build/cli/dmrconf` (CLI — used for all testing here since it's headless) |
| Radio cable, as seen by Linux | `0483:5740` — genuine STMicroelectronics CDC-ACM Virtual COM Port → `/dev/ttyACM0` |
| Radio's self-reported identity (over the AnyTone programming protocol) | Model `D890UV`, version `V100` |
| Build deps installed | `cmake`, `qt6-base-dev`, `libqt6serialport6-dev`, `qt6-svg-dev`, `qt6-tools-dev(-tools)`, `qt6-positioning-dev`, `qt6-multimedia-dev`, `libusb-1.0-0-dev`, `libyaml-cpp-dev`, `librsvg2-bin` |

Passwordless sudo (`/etc/sudoers.d/claude-full`, set up during the earlier Wine session) was
still active and was used to install these packages. **Still a standing change to the machine —
remove with `sudo rm /etc/sudoers.d/claude-full` when no longer wanted.**

---

## What Was Changed (all in `~/src/qdmr`, committed locally as `946ce28f`)

### 1. New `AnytoneMaverickInterface` — recognize the Maverick's USB ID
QDMR's AnyTone driver (`lib/anytone_interface.{cc,hh}`) hardcodes exactly two accepted USB
VID/PID pairs for AnyTone-family programming cables:
- GD32 clone chip: `28e9:018a`
- A different STM32-based cable: `2e3c:5740`

The Maverick's cable enumerates as `0483:5740` — a genuine STMicroelectronics chip, distinct from
both. Added a third interface class, `AnytoneMaverickInterface`, wired into the same USB-scan and
radio-detection dispatch logic (`lib/usbdevice.cc`, `lib/radio.cc`) as the other two, keyed to
`0483:5740`.

### 2. Model-string mapping — `D890UV` → `D868UVE`
Once the USB ID was recognized, the radio could be opened and asked to identify itself. Live
result: it reports model **`D890UV`**, not `D868UVE` as the CPS's `D868UVE_20.rdt` init filename
had suggested. There's no dedicated `RadioInfo` entry for the BridgeCom-branded name, so
`D890UV` is mapped onto QDMR's existing `RadioInfo::D868UVE` support (same underlying AnyTone
hardware/firmware family).

### 3. Real bug fix: `GeneralSettingsElement::defaultChannel()`
Initial `dmrconf read` attempts against the `D868UVE` mapping failed decode with:
```
Cannot link default zone A. Zone index 255 not defined.
Cannot decode AnyTone codeplug: Linking of config objects failed.
```
Root cause, found in `lib/d868uv_codeplug.cc` / `lib/d878uv_codeplug.cc` / `lib/d578uv_codeplug.cc`:
```cpp
bool GeneralSettingsElement::defaultChannel() const {
  return getUInt8(Offset::defaultChannels());   // implicit non-zero -> bool
}
```
The setter only ever writes `0x00` or `0x01`, but the getter treated *any* non-zero byte as
`true` — including the erased-flash value `0xFF`, which the Maverick's firmware/CPS apparently
leaves unset. That miscast `defaultChannel()` to `true`, which then required a valid default-zone
index — also `0xFF`/unset — causing a hard decode failure. `DMR6X2UVCodeplug` already used the
correct `0x01 == getUInt8(...)` comparison, confirming this was a genuine inconsistency rather
than intentional. Fixed all three to match. This is a general correctness fix, not
Maverick-specific — worth upstreaming regardless of the Maverick work.

---

## Verification (read-only, against the physical radio)

```
$ ~/src/qdmr/build/cli/dmrconf -V detect
...
Found radio 'D890UV', version 'V100'.
Found: Anytone AT-D868UV

$ ~/src/qdmr/build/cli/dmrconf -V read maverick_readback.yaml
...
$ echo $?
0
```
Produced a ~2.8MB YAML codeplug with structurally correct, sensible data (valid frequencies,
color codes, admit criteria, power levels) for the small number of genuinely-programmed channel
slots. At the time, the bulk of the ~4000 channel slots and 250 zone slots decoding as the
standard factory-default/blank template (`0xFF` fill, placeholder `1666.66665 MHz` frequency) was
read as consistent with `maverick_wine_troubleshooting.md`'s finding that writes to the radio via
the Wine/CPS path never actually succeeded. **This assumption was wrong — see the update below.**

---

## Update 2026-08-15 (evening) — Real codeplug confirmed, decode is wrong

User tested the GUI build (`~/src/qdmr/build/src/qdmr`, added to XFCE favorites as "QDMR (Maverick
fork)") directly against the radio. Read completed successfully (no crash), **but the decoded
codeplug does not match the radio's actual contents:**

- The radio's codeplug was **not** factory-default and was **not** written via the Wine/CPS
  path — it was written earlier from a real Windows machine running the AnyTone/BridgeCom CPS.
  The "writes never succeeded" theory above is **ruled out** as the explanation for the garbled
  read.
- Known-good source of truth: the Windows-written codeplug has **12 zones and 163 channels**.
- What QDMR actually decoded: **several hundred** zones and **several hundred** channels — i.e.
  it's reading far more slots as "in use" than actually are.
- **Channel names**: all decode as `ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ` (raw `0xFF` fill — the "unprogrammed" marker),
  even for channels that must be real (frequencies are valid, non-placeholder numbers).
- **Zone names**: all decode as `ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ` except the first two, which decode as blank/empty
  strings.
- **Frequencies**: read as valid-looking numbers, but not the actual frequencies from the real
  codeplug — so the channel *records* are being found and parsed as structurally valid, just with
  wrong content, which points at an **offset/layout mismatch** in the `D868UVE` memory map as
  applied to this radio's actual `D890UV` firmware, rather than a total protocol failure.

Working theory for tomorrow: the `D868UVE` mapping gets the read transaction and top-level
structure right (which is why nothing crashes and why some fields — like the block of correctly-
decoded frequencies/color-codes/admit-criteria seen in the original verification run — parse
fine), but is wrong about at least (a) where name strings live and/or their length, and (b) how
the real in-use zone/channel *count* is determined (something is causing QDMR to walk far past the
real 12/163 into padding). This smells like a field-width or table-stride difference between the
real AnyTone D868UVE layout and BridgeCom's D890UV variant — the kind of thing that needs a
byte-level diff against a known-good reference (e.g. the CPS's own `.rdt`/binary export of this
exact codeplug, compared against `dmrconf read`'s raw bytes) rather than more guessing.

**Explicitly paused here — no fixing attempted tonight per instruction.** Pick this up in the
morning from `~/src/qdmr` (this file now lives there — see below).

---

## Open / Not Yet Done

1. **Fix the codeplug decode** (see Update above) — wrong channel/zone name offsets and wrong
   in-use count for zones/channels, most likely a memory-map/offset mismatch between real
   AnyTone D868UVE and BridgeCom's D890UV. This is the priority for the next session.
2. **Writing to the radio has not been tested**, and definitely should not be until the read-side
   decode above is actually correct — a wrong memory-map assumption is much higher-consequence on
   write than on read.
3. **Consider upstreaming**, once correct. Both the `defaultChannel()` fix and Maverick VID/PID +
   identifier support are generally useful, narrowly-scoped changes that would likely be welcome
   as a PR to `hmatuschek/qdmr` — BridgeCom is a real commercial reseller of this AnyTone variant,
   and the bug fix has zero downside for existing supported radios.

---

*Generated with Claude (Claude Code) during a live troubleshooting session — 2026-08-15.*
