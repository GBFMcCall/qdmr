This is a troubleshooting/build log for getting QDMR to talk to the Maverick natively on Linux
(bypassing the Wine/CPS COM-port dead end — see `maverick_wine_troubleshooting.md` in the
`LLM-Markdown-Guides` notes repo).

# BridgeCom Maverick support in QDMR (custom fork) — Status Log
*What was changed, why, and where things currently stand.*

---

## Current Status (as of 2026-08-16)

**USB detection and the raw radio-read protocol work correctly. The codeplug *decode* on top of
that read is wrong and is the active bug to fix next.**

| Layer | Status |
|---|---|
| USB device detection (VID/PID) | ✅ Working |
| Model identification handshake | ✅ Working — radio reports itself as `D890UV` |
| Raw memory read from radio | ✅ Working — full read completes, no transport errors |
| Codeplug decode (turning raw bytes into zones/channels/etc.) | ❌ **Broken** — see below |
| Write to radio | 🚫 Not attempted — blocked on decode being correct first |

### The bug to fix

Tested the custom GUI build (`~/src/qdmr/build/src/qdmr`) against the physical radio, which has a
**known-good, non-default codeplug** written earlier from a real Windows machine running the
AnyTone/BridgeCom CPS (**12 zones, 163 channels** — confirmed from the Windows-side file).
`dmrconf read` / the GUI read complete without crashing, but the decoded result doesn't match:

- **Zone/channel counts are wildly inflated.** QDMR decodes several hundred zones and several
  hundred channels instead of the real 12 / 163 — it's treating far more slots as "in use" than
  actually are.
- **Channel names** all decode as `ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ` (raw `0xFF` fill, normally the "slot unused"
  marker) — even for channels that must be real, since their frequencies are valid, non-placeholder
  numbers.
- **Zone names** all decode as `ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ` except the first two, which decode as blank/empty
  strings instead.
- **Frequencies** parse as valid-looking numbers, but not the real frequencies from the actual
  codeplug.

Net read: the decoder is finding *something* at roughly the right structural shape (it doesn't
crash, and some fields in earlier single-channel spot-checks parsed correctly — see Verification
History below), but it's misreading at least (a) where/how long name strings are, and (b) how the
real in-use zone/channel count is determined, causing it to walk past real data into padding.

**Leading theory:** a field-width or table-stride mismatch between QDMR's real AnyTone D868UVE
memory map and BridgeCom's D890UV variant — i.e. the D890UV firmware likely uses the same general
layout family but with different offsets/sizes somewhere in the zone/channel records. This needs a
byte-level comparison against a known-good reference — e.g. the Windows CPS's own binary/`.rdt`
export of this exact codeplug, diffed against `dmrconf read`'s raw bytes — rather than more
guessing at RadioInfo mappings.

**Do not attempt a `write` to the radio until this is fixed** — writing with a wrong memory-map
assumption is far higher-consequence than reading with one.

---

## Environment

| Item | Value |
|---|---|
| Fork location | `~/src/qdmr` (cloned from `https://github.com/hmatuschek/qdmr.git`) |
| Build dir | `~/src/qdmr/build` (CMake + Qt6, `cmake --build .`) |
| Built binaries | `~/src/qdmr/build/src/qdmr` (GUI, also in XFCE favorites as "QDMR (Maverick fork)"), `~/src/qdmr/build/cli/dmrconf` (CLI — used for all headless testing) |
| Radio cable, as seen by Linux | `0483:5740` — genuine STMicroelectronics CDC-ACM Virtual COM Port → `/dev/ttyACM0` |
| Radio's self-reported identity (over the AnyTone programming protocol) | Model `D890UV`, version `V100` |
| Known-good codeplug shape (from the Windows CPS) | 12 zones, 163 channels |
| Build deps installed | `cmake`, `qt6-base-dev`, `libqt6serialport6-dev`, `qt6-svg-dev`, `qt6-tools-dev(-tools)`, `qt6-positioning-dev`, `qt6-multimedia-dev`, `libusb-1.0-0-dev`, `libyaml-cpp-dev`, `librsvg2-bin` |

Passwordless sudo (`/etc/sudoers.d/claude-full`, set up during the earlier Wine session) was
still active and was used to install these packages. **Still a standing change to the machine —
remove with `sudo rm /etc/sudoers.d/claude-full` when no longer wanted.**

---

## What Was Changed So Far (all in `~/src/qdmr`; commits `946ce28f`, `77fec75e`)

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
hardware/firmware family). **This gets far enough to read without crashing, but per Current Status
above, the mapping is not a fully faithful match — some offsets differ.**

### 3. Real bug fix: `GeneralSettingsElement::defaultChannel()`
Initial `dmrconf read` attempts against the `D868UVE` mapping failed decode outright with:
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
`true` — including the erased-flash value `0xFF`, which this radio's firmware/CPS apparently
leaves unset. That miscast `defaultChannel()` to `true`, which then required a valid default-zone
index — also `0xFF`/unset — causing a hard decode failure. `DMR6X2UVCodeplug` already used the
correct `0x01 == getUInt8(...)` comparison, confirming this was a genuine inconsistency rather
than intentional. Fixed all three to match. This is a general correctness fix, not
Maverick-specific — worth upstreaming regardless of the Maverick work. **This fix is what got the
decode from "hard crash" to "completes but wrong" — it's necessary but not sufficient.**

---

## Verification History (chronological)

1. **`dmrconf detect`** — clean success:
   ```
   $ ~/src/qdmr/build/cli/dmrconf -V detect
   ...
   Found radio 'D890UV', version 'V100'.
   Found: Anytone AT-D868UV
   ```
2. **First `dmrconf read`** (before the `defaultChannel()` fix) — hard decode failure, see bug #3
   above.
3. **`dmrconf read` after the fix** — completed with exit code 0, produced a ~2.8MB YAML file.
   Spot-checking individual entries showed structurally correct, sensible data (valid frequencies,
   color codes, admit criteria, power levels) for a handful of slots, and mass "blank" slots
   (`0xFF` fill, placeholder `1666.66665 MHz`) elsewhere. **At the time this was misread as "the
   radio is just factory-default because the Wine writes never worked."** That theory is now
   known to be wrong (see Current Status) — the mass-blank appearance was actually the decoder
   misreading real data as unused padding.
4. **GUI test against the radio's real, Windows-written codeplug** (12 zones / 163 channels
   known-good) — this is the test that surfaced the actual bug described in Current Status above:
   wrong counts, `0xFF` names, wrong frequencies.

---

## Next Steps

1. **Fix the codeplug decode** (Current Status above) — wrong channel/zone name offsets and wrong
   in-use counts, most likely a memory-map/offset/stride mismatch between real AnyTone D868UVE and
   BridgeCom's D890UV. Suggested approach: get a byte-level reference (Windows CPS binary export or
   raw `dmrconf read` dump) and diff field-by-field against the `D868UVE` codeplug class
   (`lib/d868uv_codeplug.cc`) to find where the layout actually diverges. **This is the task
   starting next.**
2. **Writing to the radio** — do not attempt until #1 is verified correct (ideally by round-tripping:
   read, write back unchanged, read again, and confirm the real 12/163 zones/channels survive
   intact).
3. **Consider upstreaming**, once correct. Both the `defaultChannel()` fix and Maverick VID/PID +
   identifier support are generally useful, narrowly-scoped changes that would likely be welcome
   as a PR to `hmatuschek/qdmr` — BridgeCom is a real commercial reseller of this AnyTone variant,
   and the bug fix has zero downside for existing supported radios.

---

*Generated with Claude (Claude Code) during a live troubleshooting session, started 2026-08-15.*
