This is a troubleshooting/build log for getting QDMR to talk to the Maverick natively on Linux
(bypassing the Wine/CPS COM-port dead end — see `maverick_wine_troubleshooting.md` in the
`LLM-Markdown-Guides` notes repo).

# BridgeCom Maverick support in QDMR (custom fork) — Status Log
*What was changed, why, and where things currently stand.*

---

## Update (2026-08-16, very late) — BREAKTHROUGH: got a reference codeplug file, cracked the zone table

**Tooling change first:** every `scanaddr` invocation pays for a full enter-program-mode /
leave-program-mode cycle, and the radio needs a multi-second settle time afterward before it'll
respond to a new session — so lots of small invocations back-to-back is slow and occasionally
flaky ("No Maverick interface found" on a too-soon retry). Reworked `scanaddr` to serve many
regions from **one** radio session:
- `--range start:end:stride` is now repeatable — pass as many disjoint regions as needed and
  they're all read in one connect/disconnect cycle.
- `--addr` for one-off single addresses, also repeatable.
- `--save FILE` appends every successful read (address + 16 bytes hex, one per line) to a local
  cache file, so later analysis can grep/script against that file instead of going back to the
  radio.
- `--start/--end/--stride` still work standalone for the simple case.

**Then the user provided something far more valuable than more probing:** a 13.8MB Windows CPS
export, `Bridgecom_Maverick.rdt`, from a personal collection at `~/src/CodePlugs/` (a git repo of
codeplug files for many radios — worth remembering that folder exists for future radio work). This
is **not** a raw flash clone — its header literally starts with the strings `D890UV` / `V100`
(matching the radio's own self-reported model/firmware), but its internal layout is a compact,
padding-free CPS project format, encoded in single-byte Latin1/ASCII (not the live radio's
UTF-16LE), totally different addressing from the live device. So it can't be used as a literal
address-mapped reference — but it's an authoritative, complete, offline-searchable source of
ground truth for everything the user actually programmed, and needed zero radio reads to mine.

**Zone table — fully cracked and 100% validated.** Anchored on the string `"Maverick\0"` (radio
name), followed by a `u16 LE` zone count (`12`, matches), then 12 back-to-back variable-length zone
records:
```
[u8  channel_count]
[channel_count × u16 LE channel index]
[u16 LE selectedChannelA_index][u16 LE selectedChannelB_index]
[NUL-terminated zone name, Latin1]
[u8  trailer — appears to just be the zone's sequence number: 1,2,...,11,0]
```
Parsed all 12 records automatically and they match the user's real zone list **exactly**, in
order, including channel counts:

| # | Name | Channels | Notes |
|---|------|----------|-------|
| 0 | Mounds | 19 | indices `0,1,2,...,8,10,...,18,9` — **byte-for-byte identical** to the zone list already found live at device address `0x02000000`, including the same "9 appended at the end" ordering quirk. This cross-check is what confirms the live `0x02000000` table really is zone 0's member list. |
| 1 | Analog | 21 | |
| 2 | Tulsa So | 20 | |
| 3 | PI | 22 | (stored uppercase; user calls it "Pi") |
| 4 | Tulsa C | 18 | |
| 5 | BikeRide | 7 | |
| 6 | Preston | 2 | |
| 7 | Mannford | 2 | |
| 8 | Claremore | 14 | |
| 9 | Bixby | 17 | |
| 10 | Depew | 16 | |
| 11 | Traveling | 2 | real spelling is "Traveling", not the "Travling" recalled from memory earlier |

**Channel names — found essentially the whole list**, by scanning the channel-table region
(`0x1A0`–`0x4740` in the file) for printable-run strings. ~163 real channel names recovered,
organized by repeater/zone group exactly as expected from the zone names above: `RAB *` (16
channels — Mounds' repeater), `LVT *` (Bartlesville-area repeater, ~20 channels), `Pi/PI *` (~20),
`LVTc *` (~18), plus VFO/APRS/call channels, and per-town sets for the other zones (`Prs`
Preston, `Man` Mannford, `CLR` Claremore, `Bix` Bixby, `Dep` Depew). A few 2-char false-positive
matches (`XR`, `Pk`, `H-`, `tq`) are almost certainly binary field bytes that happened to land in
printable ASCII range, not real names — expected noise from a generic printable-run scan, easy to
filter by cross-checking against the zone lists.

**What this does and doesn't solve:** this file's own internal encoding (Latin1 names, different
per-record layout, no address correlation to the live device) isn't directly usable as the
`D890UVCodeplug` memory map — that still has to come from the live radio, since that's what
`dmrconf read` actually downloads over USB. But it removes essentially all the guesswork about
*what* to look for: we now know the exact zone names, channel names, and zone/channel membership
ground-truth, so the remaining live-radio probing (finding the 2nd channel bank and the other 11
zones' storage on the actual device) can now search for exact known byte patterns instead of
guessing candidate regions blind — which should be much faster than the trial-and-error sweeps
earlier tonight.

**Scripts used for all of the above** (not committed — quick throwaway analysis, easily
reproduced): parse the DfuSe binary dump from `dmrconf read`, and separately grep/parse
`Bridgecom_Maverick.rdt` for the zone-record structure described above. Worth writing a small
proper `contrib/` script if this workflow gets reused.

---

## Update (2026-08-16, later night) — zone/location names: one confirmed hit, rest still elusive

User provided ground truth for cross-checking: the 12 real DMR zone names are **Mounds, Analog,
Tulsa So, Pi, Tulsa C, BikeRide, Preston, Mannford, Claremore, Bixby, Depew, Travling** (`Analog`
and `BikeRide` deliberately break the naming convention — individual repeaters, not
channel-per-mode groups). Separately, there are 4 airband zones: **Sapulpa, Frankfurt, LHR,
Charlotte**. Also: **within a DMR zone, RX and TX frequency are always identical** (no offset) —
useful for telling DMR channel records apart from analog ones once decoding the rest of the
128-byte channel record.

Added a third detector to `scanaddr` — UTF-16LE text (printable byte, `0x00`, repeated) — since
short names like `Pi` or `Bixby` never trigger the plain-ASCII detector (every other byte is
`0x00`, so there's never a run of 8 consecutive printable bytes).

**Confirmed:** `Sapulpa` (exact match, one of the 4 airband zones) found at `0x03888000`, right
next to a record at `0x03880000` decoding as `Civil ` + freq `121.500 MHz` — the international
civil aviation emergency frequency. That pairing suggests this area is more likely an **airband
reference/channel table** (built-in standard frequency + a user airband entry sitting next to it)
than a general zone-name table.

**Not yet confirmed, flagged so it isn't lost:** `Mounds` (`0x03600000`) sits right before a
settings-looking pair — radio name `Maverick` (`0x03680000`) and owner name `Grant`
(`0x03684000`), on a `0x4000` stride distinct from the `0x8000` stride the Sapulpa/Civil pair sits
on. Also found `North...` at `0x03A00000`. None of these fully match a name on the user's list, and
they don't share one consistent record stride with each other, so — unlike the channel table and
the zone-membership list, which are both solid — **the zone *name* table location is still
unresolved.** Widened sweeps immediately around these hits (`0x03000000`–`0x04400000` at both
`0x4000` and `0x8000` strides) found nothing further.

**Where this leaves things:** continuing to find the other 10 zone names, the 3 remaining airband
zones, and the contact/radio-ID tables by blind probing works but has diminishing returns per
sweep. The fastest way to close this out would be a byte-level reference — a Windows CPS raw/`.rdt`
export of the known-good codeplug, if one still exists, would let us grep for exact known strings
instead of guessing candidate address ranges. Worth revisiting that suggestion from the original
Next Steps if the blind-probing pace becomes a bottleneck.

---

## Update (2026-08-16, night) — repo now on GitHub; zone-list and roaming-table found

**Housekeeping:**
- Installed `gh` CLI (via passwordless sudo, same standing change noted in Environment below),
  authenticated as `GBFMcCall`.
- Forked `hmatuschek/qdmr` to `https://github.com/GBFMcCall/qdmr` and pushed both `master`
  (matches upstream + the earlier Maverick commits) and a new `bridgecom-maverick-support` branch
  (all work from here on happens on this branch, so it stays a clean unit to eventually PR against
  `hmatuschek/qdmr`).
- `cli/scanaddr.cc` gained a `--dump` flag (print every 16-byte read, not just flagged hits) to
  make full-record dumps easier.

**More memory-map findings (all read-only, `scanaddr`):**

- **Zone channel-membership list found at `0x02000000`.** A fine dump showed a clean list of
  16-bit little-endian channel indices — `0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16,
  17, 18, 9`, then `0xFFFF` padding/terminator for the rest of the record. 19 entries (note index
  `9` is out of sequence, tacked on at the end — probably meaningful, e.g. added to the zone
  later). This is almost certainly one zone's member-channel list, referencing channels by index
  into the `0x00FC0000` table from the earlier update.
- **Roaming-channel table found at `0x02080000`**, a different record format from both the main
  channel table and the zone list: RX frequency, then a *full TX frequency* (not a TX offset like
  the main channel table), a `01 00` field, then a UTF-16LE name. First record's name decoded as
  `OKWtr` — matching the real `RAB OKWtr` channel — followed by several default/unused-looking
  entries named `Roaming CH1` through `Roaming CH 5`. This maps to the same concept as
  `D878UVCodeplug::Offset::roamingChannels()` in the existing code, just at a different address
  and with a different record layout.
- **Scattered UTF-16LE strings found around `0x03140000`–`0x03A40000`**: `Hello!`, `Mounds`,
  `Maverick`, `Civil ...`, `North...`. Not yet confirmed what table these belong to — `Mounds` is a
  real Oklahoma town name and could plausibly be a zone name (the user's "OK" abbreviation in
  channel names suggests Oklahoma-area organization), but this needs more targeted digging before
  treating it as confirmed. Flagging here so it's not lost.
- **A scripting mistake, logged for the record:** an attempt to survey multiple candidate
  `0x80000`-strided slots hit a decimal/hex mixup in a shell loop (`$((16#$addr + 16#30))`
  produces a decimal string, but `scanaddr --end` parses its argument as hex) and ended up
  launching one large unintended sweep before the 2-minute command timeout stopped it. No harm —
  still entirely read-only — but it means the earlier hypothesis of "12 zones at a clean
  `0x80000` stride starting at `0x02000000`" is **not confirmed**: the slot at `+0x80000`
  (`0x02080000`) turned out to be the roaming table above, not a second zone list, so that
  structural theory needs rework rather than being assumed.

**Where this leaves the picture:** we now have working, verified decodes for one full channel
table (128 of 163 channels) and at least one zone's channel-membership list, plus a plausible
roaming-channel table. Still open: the other ~35 channels, the other 11 zone lists (and their
names), the real contact/radio-ID/group-list/scan-list tables, and the general settings block.

---

## Update (2026-08-16, evening) — BREAKTHROUGH: found the real channel table, read-only

The user confirmed the radio's codeplug is real and fully functional (tested zone/channel
switching, DMR TX to a local repeater/echo, and analog TX) — so the "is the codeplug actually
there" question from earlier today is settled: **it's there, QDMR is just reading the wrong
addresses/layout.** That reframed this from "is there a codeplug" to "map the real memory layout."

Built a small **read-only** diagnostic tool, `cli/scanaddr.cc` (temporary, not installed, not
upstream-worthy as-is — added a `scanaddr` target to `cli/CMakeLists.txt` alongside `dmrconf`, same
build flags). It talks to the radio using the exact same `AnytoneInterface::read()` primitive
`dmrconf` already uses (same `PROGRAM`/`R addr 16`/`END` protocol, verified byte-identical and
reproducible) but isn't restricted to the address list `D868UVCodeplug` knows about — it can sample
or dump any address, and flags reads that decode as a plausible BCD channel frequency or contain a
long run of printable ASCII, so the wide sweeps don't have to be reviewed byte-by-byte. It never
calls `write()`.

**What the sweeps found:**

- **0x00000000–0x00800000 (8MB):** dense, real, non-blank data throughout, but it's firmware
  resource data (font/glyph bitmaps, lookup tables) — not codeplug-shaped. This is *not* where
  channels/zones live.
- **~0x01000000–0x0c000000+:** large stretches of real Unicode (UTF-16LE) text — names, callsigns,
  and country/province names ("Georges", "I4OTX", "Germany", "Guangdong", "Fujian", etc.). This
  is almost certainly AnyTone's built-in worldwide DMR contact/ID database, not the user's own
  codeplug — but it proves real structured data exists far outside the address range
  `D868UVCodeplug` reads, and that it's UTF-16LE, not the Latin1 QDMR's D868UVE map assumes.
- **0x00FC0000: the real channel table.** A fine (`--stride 80`) sweep found 128 back-to-back,
  clearly real channel records, each 128 bytes (`0x80`) — not the 64 bytes (`0x40`) D868UVE uses —
  running from `0x00FC0000` to `0x00FC3FFF` and then going blank immediately after (`0x00FC4000`
  onward is empty, confirmed by fine sweep). Within each record:
  - Bytes `0x00–0x03`: RX frequency, **same BCD8-be encoding as D868UVE** (`rxFrequency()`) —
    decoded real, plausible 2m/70cm ham frequencies throughout (146.520, 446.000, 144.390,
    442.475, 449.000, 444.350 MHz, etc.), not garbage.
  - Bytes `0x04–0x07`: looks like TX offset, same BCD8-be encoding (decoded a clean 5.000 MHz on
    several records — standard 70cm repeater offset).
  - **Name field at relative offset `0x44`** (not `0x23` like D868UVE), **UTF-16LE** (2 bytes/char,
    not single-byte Latin1) — decoded clean, real channel names: `RAB OK`, `RAB OKWtr`, `RAB OKE`,
    `RAB OKTAC`, `RAB OKTlk`, `RAB OK DMR`, `RAB Echo`, `RAB NA`, `RAB Dscnt`, `RAB DMR`,
    `RAB Emcom`, `RAB Ares`, `RAB RedAm`, `RAB Tac1`, `RAB Tac2`, `RAB Tac3`. **These read exactly
    like real user-created repeater/talkgroup channel names** (Emcom, ARES, tactical channels,
    DMR/analog variants of the same repeater) — @user, do these ring a bell / match your actual
    channel list? That would confirm definitively we've found the real table.
  - 128 records × 128 bytes = 0x4000 bytes = exactly `Limit::channelsPerBank() = 128` from the
    existing D868UVE map — the *count* QDMR already assumes per bank is right, only the *record
    size* and *base address* are wrong.
- **Address aliasing found:** `0x00FC0000` and `0x01000000` (exactly `+0x00040000`, the same
  delta as D868UVE's `betweenChannelBanks()`) return **byte-for-byte identical** content — not a
  second bank, an alias/mirror of the first. The same `+0x00040000` delta was independently seen
  earlier today between the small `zoneNames()`/`radioIDs()` records (`0x02540000` /
  `0x02580000`), which also turned out to be identical. This looks like a consistent address-line
  quirk (something bit-18-ish is being folded/ignored) in how this radio's memory decodes reads at
  this protocol level — worth understanding, but not yet fully mapped.
- **Not yet found:** the other ~35 channels (163 total − 128 in this bank), the 12 zones, and the
  contact/radio-ID tables specific to the user's codeplug (as opposed to the built-in worldwide
  database found above). `0x00FC4000` onward (where a second bank would naturally follow) is
  confirmed blank, and the naive "next bank" address (`+0x40000`) is the alias, not real new data,
  so the second bank is somewhere else not yet located.

**Net effect:** the original "field-width/stride mismatch" theory was right in spirit, but the
actual mismatch is much larger than a small offset tweak: real base address `0x00FC0000` (not
`0x00800000`), record size `0x80` (not `0x40`), name field at `+0x44` (not `+0x23`), and **UTF-16LE
encoding** (not Latin1) — a genuinely different generation of the AnyTone codeplug layout, not a
simple BridgeCom relabeling of the D868UVE map. All of this was found read-only, using the same
verified-safe read primitive already in `lib/anytone_interface.cc`.

---

## Update (2026-08-16, later same day) — raw-byte investigation, read-only

Did a fresh **read-only** investigation directly against the physical radio: two independent
`dmrconf read raw.bin` (binary/DfuSe format) dumps, taken minutes apart, came back **byte-for-byte
identical** — so what follows is the radio's genuine, stable, repeatable state, not a USB
transport glitch. (`.bin`/`.dfu` output skips QDMR's decoder entirely and just dumps the raw
per-address memory elements the download step fetched — see `cli/readcodeplug.cc`.) Dumps saved
under `maverick_raw_dumps/` (gitignored, not committed).

Parsed the DfuSe container in Python (element = address + raw bytes, straight from the device) and
checked it against the exact addresses `lib/d868uv_codeplug.hh`/`.cc` uses for the D868UVE map:

- **All "in use" bitmaps are 100% `0xFF`** — `channelBitmap` (`0x024c1500`), `zoneBitmap`
  (`0x024c1300`), `contactBitmap` (`0x02640000`), `radioIDBitmap`, `scanListBitmap`,
  `groupListBitmap` are every single bit set. Since `BitmapElement::isEncoded()` treats bit=1 as
  "in use", and `allocateChannels()`/`createChannels()`/etc. in `d868uv_codeplug.cc` trust these
  bitmaps completely, this **directly explains the wildly-inflated zone/channel counts** — QDMR is
  correctly doing what an all-1s bitmap tells it to do (decode all 4000 channel slots / 250 zone
  slots as populated).
- **The channel-bank region itself is essentially blank.** Scanned all 4000 channel slots at
  `Offset::channelBanks() = 0x00800000` (64 bytes/slot) for a valid BCD-encoded, VHF/UHF-plausible
  RX frequency: **zero** found in banks 0–30 (slots 0–3967). The last 32 slots (bank 31) all show
  one identical placeholder frequency (444.85000 MHz) with null-byte names — reads like a
  factory/CPS default-fill template, not real user channels.
- **Whole-image scan for any sign of the real codeplug came back empty.** Searched the full ~513KB
  dump for (a) any BCD-decodable VHF/UHF frequency sitting next to a readable name string at the
  expected relative offset, and (b) any human-readable ASCII run ≥4 chars anywhere at all. Found
  nothing — the only ASCII-looking runs are fixed-byte fill patterns (`0x55` repeated = "U", a
  repeating 4-byte non-text pattern), not real channel/zone/contact names.
- **Byte-value histogram of the whole dump: 95.8% is `0xFF` (erased-flash marker), 3.3% is `0x00`.**
  Only a handful of small regions have any other content.
- The only genuinely non-blank data found anywhere: 2–3 tiny 32-byte records at
  `Offset::zoneNames() = 0x02540000` and, **byte-for-byte identically**, at
  `Offset::radioIDs() = 0x02580000`. Each contains what looks like an embedded timestamp
  (`ea 07` → `0x07ea` = 2026, followed by month/day/hour/minute-ish bytes reading roughly
  "2026-06-14 07:37/38") but no readable zone or radio-ID name text. Every other record in both
  tables (index 3 onward — i.e. essentially the whole table) is `0xFF`.

**This changes the diagnosis.** The original "field-width/stride mismatch" theory predicts real
data sitting at *shifted* offsets — but a systematic scan of the entire dump turns up no readable
names and no plausible frequencies anywhere outside of one CPS-looking placeholder. The regions
QDMR's D868UVE mapping reads for channels, zones, contacts, and radio IDs are **almost entirely
erased flash on this radio, right now** — not misaligned real data. The two identical-content
"header" records at otherwise-unrelated table addresses (zone names vs. radio IDs) are themselves
odd — possibly address aliasing in the D890UV's memory map, or a shared init-record template CPS
stamps into multiple tables on codeplug creation.

**Open question this raises: is the known-good 12-zone/163-channel codeplug actually on this
physical radio right now?** Two independent read-only sessions agreeing byte-for-byte rules out a
flaky read, but doesn't rule out the radio having been reset, reflashed, or simply being a
different state than assumed since the last confirmed Windows CPS write. This needs to be settled
before spending more time on memory-map archaeology — see Next Steps.

---

## Current Status (as of 2026-08-16, morning)

**USB detection and the raw radio-read protocol work correctly. The codeplug *decode* on top of
that read is wrong and is the active bug to fix next.** *(See the Update above — the picture is
more complicated than this section originally assumed; keeping this section as-is for history.)*

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

*(Superseded: the "is the codeplug even on the radio" question below and the old step 0 are
resolved — user confirmed via live TX test the codeplug is real and working. See the evening
Update above for where things stand instead.)*

1. **Finish mapping the real memory layout**, using `cli/scanaddr.cc` (read-only, safe):
   - Find the second channel bank (remaining ~35 of 163 channels) — not at the naive `+0x40000`
     (that's an alias of bank 0, confirmed identical bytes), and not immediately following bank 0
     in memory (`0x00FC4000`+ confirmed blank). Needs a wider sweep.
   - Find the 12-zone table (zone name + member-channel-index list) and the real contact/radio-ID
     tables for the user's own codeplug, as distinct from the built-in worldwide DMR database
     found around `0x01000000+`.
   - Decode the rest of the 128-byte channel record (mode, power, color code, timeslot, contact
     index, etc.) by comparing against known real settings for a couple of the named channels
     found so far (e.g. ask the user what mode/power/color-code `RAB DMR` or `RAB OK` actually use,
     and match against the raw bytes at that record's offset).
   - Worth understanding the `+0x00040000` address-aliasing quirk found in two independent places
     (channel bank mirror, `zoneNames()`/`radioIDs()` mirror) — may matter for the final address
     map, may just be an artifact of how this firmware decodes reads.
2. **Write a new codeplug class** (e.g. `D890UVCodeplug`, distinct from `D868UVCodeplug` rather
   than mapped onto it) once the layout above is fully characterized, since the differences
   (record size, field offsets, UTF-16LE names) are too large for a same-class offset tweak.
3. **Writing to the radio** — still not attempted, still blocked until the new codeplug class
   round-trips correctly (read, write back unchanged, read again, confirm the real 12/163
   zones/channels survive intact).
4. **Consider upstreaming**, once correct. The `defaultChannel()` fix and Maverick VID/PID +
   identifier support are already generally useful, narrowly-scoped changes independent of the
   layout work above and would likely be welcome as a PR to `hmatuschek/qdmr` on their own —
   BridgeCom is a real commercial reseller of this AnyTone variant, and the bug fix has zero
   downside for existing supported radios. The full `D890UVCodeplug` would be a second, separate
   PR once done.

---

*Generated with Claude (Claude Code) during a live troubleshooting session, started 2026-08-15.*
