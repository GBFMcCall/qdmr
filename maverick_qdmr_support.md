This is a troubleshooting/build log for getting QDMR to talk to the Maverick natively on Linux
(bypassing the Wine/CPS COM-port dead end — see `maverick_wine_troubleshooting.md` in the
`LLM-Markdown-Guides` notes repo).

# BridgeCom Maverick support in QDMR (custom fork) — Status Log
*What was changed, why, and where things currently stand.*

---

## Update (2026-08-16, night, MILESTONE 5) — new-channel test resolves the bank-2 question decisively

User's suggestion, in response to the bank-2 read-reliability scare: rather than keep guessing at
*why* reads were failing, add a genuinely new channel via the Windows CPS and write it to the
radio - both a real independent test of the whole bank-1/bank-2 model, and a natural way to force
a rewrite of that memory region. User created a channel and added it to the Traveling zone.

**Read-only check afterward, and every single prediction held:**
- **Bank 2 is fully readable again** - record 0 decodes as `Bix OKE` (verified the *name* field
  specifically this time, not just frequency, since Bixby's repeater frequency coincidentally
  matches bank 1's first record - false-alarm-proofed this one).
- **The new channel landed exactly where the model predicted**: `0x010C1180`, immediately after
  the previous last real record (`0x010C1100`) - i.e. bank 2 extended by simple sequential fill,
  exactly as documented, with no bitmap or other indexing surprise.
- **The Traveling zone's channel-index list grew from `[161, 162]` to `[161, 162, 163]`** - the
  new channel's index correctly appended, confirming the zone-list encoding side of the CPS's own
  write matches our understanding exactly.
- The new record's own format matches what `D890UVCodeplug` already expects: name at `+0x44`,
  UTF-16LE, same `0x80`-byte size.

One minor discrepancy worth a follow-up: the decoded name reads `Bank2 Test` (10 characters), not
the fuller name mentioned when describing the test - worth double-checking with the user whether
that's exactly what was typed into the CPS, a CPS-side truncation, or a read issue on this end (no
evidence of the latter so far).

**Conclusion: the whole bank-1/bank-2/zone-list model is now about as strongly validated as it can
be without a from-here write** - not just consistent, static reads, but a live before/after test
against a real, independent, user-initiated change, exactly like the zone-name and radio-ID
discoveries earlier. The bank-2 blank-read episode appears to have been resolved by the CPS's own
write to that region (plausibly some kind of session/cache state on the radio that a real
full write cycle resets) rather than anything wrong with the address/format understanding itself.
Given this is worked out via a real write cycle rather than something reproduced and understood
from read side, the earlier caution about not extending write support to bank 2 until the read
issue was understood still applies loosely - what's now known is that a *complete* CPS write
resolves it; still don't know what a *partial* QDMR-side write (touching only bank 2) would do if
this recurs.

(This session's `D890UVCodeplug::Limit::numChannels()` is still 163 - the new test channel is
channel index 163 (0-based), one past that limit, so it correctly doesn't show up in our own
decode yet. Not a bug; just means this repo's model reflects the *original* known-good codeplug,
not the user's temporary test addition.)

---

## Update (2026-08-16, night) — encode-side validation: real findings, plus an unresolved bank-2 read-reliability issue

**Built a proper round-trip validator** (`cli/testencode.cc`, offline, never touches the radio):
loads a real raw dump (`dmrconf read foo.dfu`), decodes it, re-encodes the *same* `Config` back
onto the *same already-loaded* image (not a blank one - `Codeplug::Flags` defaults to
`updateCodeplug=true`, which is exactly the real device read-modify-write path), then diffs byte
for byte against the original. This is a meaningfully different (and better) test than just
running the file through `dmrconf encode` standalone, which starts from a blank image and would
have hidden exactly the kind of "does this preserve what it doesn't touch" question that matters
for write safety.

**Results, for what's currently implemented:**
- **Zone channel-lists: byte-perfect round-trip**, all 12.
- **Both radio ID entries: byte-perfect round-trip.**
- **Zone names: fixed a real bug found by this test** - the encoder was zeroing the second half of
  each 64-byte slot (should stay untouched/`0xFF`, only the first 32 bytes are the actual name
  field). Fixed in `setZoneName()`.
- **Channels: real, specific gaps**, not a clean round-trip:
  - Contact index (`+0x14`) and scan-list index (`+0x1b`) get reset on every encode - expected,
    since we don't decode either yet, so `Config`'s `Channel` object has nothing there to restore.
    This means **encoding a channel today would silently destroy its contact/scan-list
    assignment** even if nothing about the channel was meant to change.
  - The TX frequency/offset bytes (`+0x04`-`+0x06`) don't round-trip correctly for simplex
    (non-repeater) channels specifically - looks like this radio may store something different
    there for simplex than the D868UVE convention we inherited assumes. Not yet fixed.

**Then, an unrelated and more serious problem surfaced while investigating bank 2's results
specifically**: channel bank 2 (`0x010C0000`) - which decoded correctly earlier this session (all
35 real channels, verified in the YAML output) - is now reading back as **completely blank**
(`0xFF`) on every attempt. Confirmed this is real and specific to that address, not a general
issue or a testing artifact:
- Bank 1 and the zone-list table both read correctly, in the same sessions, right next to the
  failed bank-2 reads.
- Tried: direct single-address read, reading it as part of a continuous sweep from further back
  (in case some kind of sequential "priming" mattered), multiple independent sessions with proper
  settle time, and **a full radio power-cycle** - bank 2 stayed blank through all of it.
- **The data itself is confirmed intact** - user checked the radio's own display directly (Bixby
  and the renamed Test/Depew zone both still show correctly) and successfully transmitted on a
  real channel. This is a read-side/protocol issue, not data loss, and definitely not something
  caused by a write (double- and triple-checked: nothing in this session's encode-validation work
  touches the device at all - `dmrconf encode` and `cli/testencode.cc` both operate purely on
  local files and in-memory objects).

**What this means going forward:** we don't currently understand what made bank 2 accessible
earlier and inaccessible now. Until that's resolved, treat bank 2 read reliability as an open
problem - QDMR's current decode will unpredictably show either the real 35 channels or a
blank/garbage read for them, depending on some still-unknown condition. Two direct consequences:
1. **No write support should be extended to bank 2** until this is understood - a real
   read-modify-write cycle that silently gets a blank read back would write blank data over real
   channels.
2. Worth considering making the decode side detect an all-`0xFF` bank-2 read and handle it
   gracefully (skip those channels / flag it) rather than silently presenting blank channels as if
   they were real - not yet implemented.

---

## Update (2026-08-16, night, MILESTONE 4) — radio ID list decoded

User supplied their real DMR ID (3158993) to search for directly - and it turned out **already
present in data collected during earlier scans**, no new radio reads needed to find it. Its
BCD8-be encoding (`03 15 89 93`) had already been logged twice: at `0x03680000` (paired with the
name "Maverick") and `0x03684000` (paired with the name "Grant") - exactly matching the user's
description of the same ID appearing once as the Master ID and once in the Radio ID List.

Added `D890UVCodeplug::RadioIDElement` (same idea as the channel/zone-name overrides: reuses
`D868UVCodeplug::RadioIDElement`'s `number()` - BCD8-be at offset 0, unchanged - but moves the
name field from `+0x05` to `+0x04` and switches it from Latin1 to UTF-16LE). Both known slots are
read as radio-ID-list entries (whether one is semantically "the Master ID" and the other "a list
entry" isn't independently confirmed, but both round-trip through the same record shape, and
`Config`'s model only has one kind of radio-ID-list entry to put them in anyway).

Verified against the live radio: `dmrconf read` now shows both entries correctly
(`id1: Maverick/3158993`, `id2: Grant/3158993`) with no new errors introduced; channels' own
`radioId` field still shows as unset/default, which is expected (none of them override the
default ID).

**Status now:** channels (163), zones (12, with real names), and radio IDs (2) all read
correctly. Remaining for write-readiness: general settings (deferred - not required to create a
channel), and independently verifying enough of the channel/zone *encode* path to write safely
without disturbing anything unmapped (contacts, scan lists, general settings, etc.) - contacts
explicitly deprioritized per user direction (tens of thousands of entries, not worth mapping now).

---

## Update (2026-08-16, night) — next phase: working toward write-readiness

User direction for this phase: skip contacts entirely for now (tens of thousands of entries in
the built-in DB, not worth the effort at this stage). Focus instead on radio ID, general settings,
and anything else needed to eventually create a new channel and write it to the radio. Channel
membership in zones is already done (see MILESTONE 3 below) - not additional work needed there.

**Channel bank 2 has room to grow.** Checked from the end of its 35 real channels (`0x010C1180`)
out to `0x010C4000` (matching bank 1's block size) - entirely blank. Both banks use simple
sequential fill with no bitmap gating which slots are "in use" (consistent with everything found
so far - this radio doesn't seem to rely on the bitmap-driven allocation D868UVE uses at all).
Good sign for channel creation: a new channel likely just needs to go in the next open slot,
without first needing to solve a capacity or indexing puzzle.

**Asked the user for their radio's real DMR ID** to search for directly, the same way the zone-name
table was found via a known exact value - much faster than blind sweeping. Radio-ID search paused
pending that.

---

## Update (2026-08-16, night, MILESTONE 3) — zone names found via a live before/after test; complete

Blind sweeping (previous entry) never found the zone-name table because it doesn't follow the
`0x200`-byte record convention every other table here uses. Found it instead by direct experiment:
**user renamed zone 10 from "Depew" to "Test" via the Windows CPS and wrote it to the physical
radio**, then a read-only search on this side for the new string `Test` (UTF-16LE) found it
immediately at `0x03600280`.

Combined with the two names already known from that address neighborhood (`Mounds` at
`0x03600000`, `Claremore` at `0x03600200`), the real stride became obvious:
`0x03600200 - 0x03600000 = 0x200`, matching zone-index delta 8 (`Mounds`=0, `Claremore`=8) →
`0x200 / 8 = 0x40`. **Zone names live at `0x03600000 + zone_index * 0x40`**, NUL-terminated
UTF-16LE starting at offset 0 of each 64-byte slot. This also retroactively explains the earlier
"only 2 of 12 found" result: a `0x200`-stride sweep of this table only ever lands on every 8th
real slot (`0x200` happens to be an exact multiple of the real `0x40` stride) - not a partial
table, a fully aliased sampling artifact.

Read all 12 slots directly and confirmed every name, in order: `Mounds`, `Analog`, `Tulsa So`,
`PI`, `Tulsa C`, `BikeRide`, `Preston`, `Mannford`, `Claremore`, `Bixby`, `Test` (was `Depew` -
the user's deliberate test edit, not yet reverted on the radio), `Traveling`.

**`D890UVCodeplug` updated**: `Offset::zoneNames()`/`betweenZoneNames()` added, `createZones()` now
reads the real name via a small inline UTF-16LE decode (`zoneName(i)`), falling back to the old
`Zone N` placeholder only if a slot is empty. Verified against the live radio - `dmrconf read`
produces all 12 zones with their real names and full, correct channel membership.

**Status now: all 163 channels and all 12 zones (names + membership) read correctly from the live
radio.** Remaining gaps: contacts, radio ID(s), general settings, and the scan-list table found
along the way (documented above, not yet wired into the codeplug class).

---

## Update (2026-08-16, night) — zone names: continued search, still not found

Continued the search using the `0x200`-byte alignment convention established by every real table
found so far (zone channel-lists, roaming channels, scan lists all start their records on `0x200`
boundaries) - swept at exactly that stride so table starts can't be missed by phase, unlike the
bank-2 near-miss earlier. Covered, in three batched (multi-range, single-session) sweeps:
- `0x02180000`-`0x03600000` (the main gap between the known table cluster and the
  Mounds/Maverick/Grant area)
- `0x010C2000`-`0x02000000` (between the end of channel bank 2 and the zone-list table)
- `0x03700000`-`0x04000000` (after the settings cluster)

**No new zone names found** - only re-encountered already-known content (the canned SMS/greeting
messages near `0x03140000`, `Sapulpa`/airband stuff near `0x03880000`, `North`/"Hotspot Setup"
near `0x03a00000`). Also checked for single-byte ASCII encoding (in case zone names, unlike
channel/scan-list names, use Latin1 instead of UTF-16LE) - none found either.

**Where this leaves it:** the zone-name table isn't in any of the "obvious" gaps between what's
already mapped, and doesn't follow the same array/stride convention as the channel-list or
scan-list tables closely enough to have been caught by an alignment-matched sweep. It may be
scattered as individually-placed fields (the way radio/owner name are, at `0x03680000`/`0x684000`,
0x4000 apart with no discoverable stride) rather than stored as a clean array - which would make it
much more expensive to find via blind sweeping. Continuing to search blindly means more radio
reads with no guaranteed payoff; worth deciding with the user whether to keep going that way, try
a different technique (e.g. a full dense capture of a much larger region, or comparing before/after
a deliberate CPS-side edit to one zone name - if a Windows/Wine path is ever revisited), or move on
to contacts/radio-ID/general-settings and leave zone names as `Zone N` placeholders for now.

---

## Update (2026-08-16, night) — found the scan-list table (not zone names - corrected by user)

While hunting for zone names, found a real, distinct table at `0x02100000` (`0x200`-byte stride,
mirrored at `0x02140000` per the usual aliasing pattern) holding 7 named records: `Analog`, `Pi`,
`RAB`, `Tulsa So`, `Tulsa Cntr`, `Claremore`, `Bixby`. Initially misread this as a partial/uncertain
zone-name table, since 6 of the 7 strings match real zone names. **User corrected this: these are
the radio's 7 Scan Lists, not zones** - `RAB` in particular is not one of the 12 zones, which is
what first flagged something was off. Worth capturing properly since scan lists are a real DMR
codeplug feature QDMR should eventually support for this radio too, independent of the zone-name
question:

- Record format (confirmed structure, semantics of the 4 leading `u16` values not yet decoded):
  ```
  [u16][u16=0xffff][u16=0xffff][u16][u16][u16][u16][name, UTF-16LE, NUL-terminated @ +0x0E]
  ```
- 6 of 7 records share an identical 4-value prefix (`20,30,31,31`) with only `Analog`'s differing
  (`15,25,29,29`) - looks like default/boilerplate values from list creation, not meaningfully
  distinct per-list data; needs more investigation to decode (likely priority-channel or
  channel-count/range fields, analogous to `D868UVCodeplug`'s `ScanListElement`).
- Table capacity is presumably larger than 7 (D868UVE allows up to 250 scan lists) - only checked
  indices 0-6 (`0x02100000`-`0x02100c00`); didn't verify where the real capacity/bitmap-equivalent
  ends.
- A second, structurally different table at `0x03600000` (name at `+0x00` directly, no prefix
  fields) holds exactly 2 entries: `Mounds` and `Claremore`. `Claremore` is explained (it's also a
  real scan-list name, plausibly naming both a zone and its corresponding scan list after the same
  site). `Mounds` is *not* one of the 7 scan lists, so this table's purpose is still unclear -
  possibly Roaming Zones (a separate AnyTone feature from Roaming Channels, found earlier at
  `0x02080000`) or something else entirely. Not yet resolved either way.

**Zone names are still not found.** Corrected the search plan to stop treating either table above
as a candidate. See below for where the search goes next.

---

## Update (2026-08-16, night, MILESTONE 2) — bank 2 found: all 163 channels, all 12 zones complete

User confirmed the GUI read (fresh relaunch, direct Read, no errors) matches the CLI output
byte-for-byte and loaded all channels correctly; zones showed as `Zone 1`-`Zone 12` placeholders
as expected. Also saved a QDMR-exported copy of the read (`Bridgecom_Maverick_qdmr.yaml`) alongside
the reference file - confirmed identical to the CLI's own output, good cross-check that GUI and
CLI paths agree.

**Found channel bank 2.** Revisited the address search with two fixes to the earlier approach:
1. `scanaddr` sweeps were using a stride that's a clean multiple of the 128-byte channel record
   size (`0x1000`, `0x400`) - if bank 2 wasn't phase-aligned the same way as bank 1, every sample
   could land on the same "dead" byte offset within each record and systematically miss it
   regardless of how fine the stride looked. Re-swept with an odd stride (`0x333`) to shift phase.
2. That immediately surfaced real content (`WT5EOC`, `Dep OK`, `LVTc`, `Bix`, `Channel...`) in
   `0x01000000`-`0x02000000` - a region earlier written off entirely as "just a mirror of bank 1"
   because only the first record had been checked there.

A dense follow-up dump found the full picture: `0x01000000`-`0x01004000` genuinely is a complete,
exact mirror of bank 1 (consistent with the address-aliasing quirk noted throughout this log) -
but **`0x010C0000` is a second, real, distinct table** picking up exactly where bank 1's channel
indices leave off: 35 more records, same `0x80`-byte format, running `Bix OKE` → ... → `Dep OK
Wtr1` → `Channel VFO B/A` → `EU APRS TransmiT/Receive`, then blank. `128 + 35 = 163` - the exact
count from the very first Windows-CPS-confirmed codeplug at the top of this log. Bank 2's base
(`0x010C0000`) isn't a clean multiple of `betweenChannelBanks()` or any other formula relative to
bank 1 found so far - just a real, separate address, located and used as-is.

**`D890UVCodeplug` updated accordingly** (`channelAddress(i)` now routes to bank 1 or bank 2
depending on index; `Limit::numChannels()` is now the real `163`). Re-verified against the live
radio: **all 163 channels decode correctly, and all 12 zones now show their full, correct channel
membership** - Bixby (17), Depew (16), and Traveling (2), previously truncated/empty, are complete.

**Updated status:**
- ✅ All 163 channels (name, frequency, TX offset/direction, inferred mode/power/etc.)
- ✅ All 12 zones, full correct membership
- ❌ Zone names (still placeholders - real table not found)
- ❌ Contacts, radio ID(s), scan lists, group lists, general settings

---

## Update (2026-08-16, night, MILESTONE) — first real, working read from the live radio

**`dmrconf read` now produces a correct, real decode of this radio for the first time.** New
files `lib/d890uv_codeplug.{hh,cc}` (the actual memory-map fix) and `lib/d890uv.{hh,cc}` (radio
wrapper, mirrors `d868uv.{hh,cc}`), plus a `RadioInfo::D890UV` entry and updated dispatch so the
radio identifies as "BridgeCom Maverick" instead of being silently mapped onto D868UVE. Verified:

```
$ dmrconf -V read out.yaml
```
succeeds (exit 0) against the physical radio and produces a YAML config with:
- **128 real channels**, correct names and frequencies, e.g. `RAB OKWtr` at 444.85/449.85 MHz -
  matching the reference file exactly, in the same order (`RAB *` → `2 M Call`/`70 CM Call` →
  `APRS *` → `Suprlnk *` → callsign channels → `LVT *` → `Pi/PI *` → `LVTc *` → `Prs/Man/CLR/Bix *`).
- **Channel *mode* (DMR vs analog) decodes correctly** without ever being independently verified
  field-by-field - inherited the D868UVE live bit-layout unmodified (see `D890UVCodeplug`
  doc-comment) and it happens to be right: the RAB group decodes `dmr:`, and it correctly flips to
  `fm:` exactly at `2 M Call` / `70 CM Call` - real analog National Simplex calling frequencies.
  Strong independent confirmation the byte-0x08 region carries over unchanged from D868UVE.
- **All 12 zones**, correct channel membership, gracefully incomplete where it should be: zones
  that reference channel indices ≥128 (Bixby, Depew, Traveling) correctly show only their
  in-range channels (or empty) rather than crashing or inventing data, since bank 2 isn't mapped.
  Zone *names* show as placeholder `Zone 1`..`Zone 12` (real names not yet located - see below).
- **Contacts, radio IDs, scan lists, group lists, general settings**: empty/default, as intended -
  deliberately not decoded from unverified addresses rather than showing garbage.

**Implementation approach:** `D890UVCodeplug` subclasses `D868UVCodeplug` (inherits its full,
already-implemented `GeneralSettingsElement`/etc. interface for free - reimplementing that from
`AnytoneCodeplug` directly would mean reimplementing hundreds of pure-virtual methods) and
overrides only:
- `allocateBitmaps()` → no-op (no live bitmap table is trusted; see history above for why - they
  read back as 100% "in use").
- `allocateForDecoding()` / `createElements()` / `linkElements()` → call *only* the channel and
  zone methods below, skipping every other D868UVE subsystem entirely (rather than inheriting
  D868UVE's bitmap-driven contact/radio-ID/scan-list/etc. handling, which would read garbage from
  wrong addresses on this radio).
- `allocateChannels()`/`createChannels()`/`linkChannels()` → unconditional loop over 128 fixed
  channels at `0x00FC0000`, `0x80` bytes/record (no bitmap check).
- `allocateZones()`/`createZones()`/`linkZones()` → unconditional loop over 12 fixed zones at
  `0x02000000 + i*0x200`; channel indices ≥ `Limit::numChannels()` are silently skipped via the
  existing `ctx.has<Channel>()` check, which is what produces the graceful "empty until bank 2 is
  found" zone behavior above for free.
- `ChannelElement` → same base class as D868UVE (inherits `rxFrequency()`/`txOffset()`/mode/
  power/colorCode/timeslot bit-decoding unmodified - offsets `0x00`/`0x04`/`0x08` etc. are shared),
  only overrides `size()` (`0x80`) and `name()`/`setName()` (UTF-16LE @ `+0x44` via
  `readUnicode`/`writeUnicode`, instead of Latin1 @ `+0x23`).
- `setBitmaps()` → no-op (encode path, unused - read-only, see warning below).

**Known limitations, all documented in the new files' doc-comments so anyone continuing this has
it in context, not just here:**
1. Channel bank 2 (channels 129-163) not located - those channels don't appear at all; zones that
   reference them show fewer channels than real, or empty.
2. Zone names are placeholders (`Zone N`) - the real zone-name table hasn't been found on the live
   device (checked the rest of each zone's `0x200` slot specifically - not there).
3. Contacts, radio ID(s), scan lists, group lists, and general settings (DMR ID, callsign,
   display/audio/etc. preferences) are not read at all - the YAML's `settings:` section shows
   `Config`'s built-in defaults, not anything from the radio.
4. Per-channel fields beyond name/frequency (color code, timeslot, admit, power, bandwidth,
   contact index) use the *inherited* D868UVE bit-layout at `+0x08` etc., which is a
   reasonable-confidence inference (validated indirectly via the DMR/analog mode split matching
   real-world channel naming) but not independently, field-by-field verified the way frequency and
   name are. Log noise on read (`Cannot resolve contact index N for channel X`) is expected and
   harmless - it's `linkChannelObj()` (inherited, unmodified) trying to resolve digital contact
   references against the empty contact list from point 3; it doesn't stop the read from
   succeeding.

**Still absolutely read-only.** `setBitmaps()`/the encode path are no-ops/inherited-but-unused;
`D890UVCodeplug`'s doc-comment states explicitly not to attempt writes with it - the encode path
targets D868UVE addresses, unmodified, and would corrupt the radio if actually invoked.

---

## Update (2026-08-16, very very late) — all 12 zones fully mapped and validated on the live radio

**Goal for this pass** (per user): read zones, channels, and radio settings correctly at minimum;
eventual goal is full read/write for channels/zones (add, edit, change frequencies) without
touching anything QDMR doesn't understand (e.g. airband) — **still read-only until further
notice.**

**Zones: DONE.** Used the reference file's exact channel-index lists per zone as known values to
search for on the live radio, rather than guessing. Result: **the live zone-list table is at
`0x02000000`, one record per zone, at a fixed `0x200`-byte stride** —
`zone_addr = 0x02000000 + zone_index × 0x200`. Confirmed by directly reading all 12 slots and
diffing against the file's data — **every single one matches exactly**:

| Zone | Live address | First few indices (live) | Matches file? |
|---|---|---|---|
| 0 Mounds | `0x02000000` | `0,1,2,3,4,5,6,7...` | ✅ |
| 1 Analog | `0x02000200` | `19,20,21,22,23,24,25,26` | ✅ |
| 2 Tulsa So | `0x02000400` | `40,41,42,43,44,45,46,47` | ✅ |
| 3 PI | `0x02000600` | `60,61,62,63,64,66,67,68` | ✅ (note the 65-skip, matches file) |
| 4 Tulsa C | `0x02000800` | `82,83,84,85,86,87,88,89` | ✅ |
| 5 BikeRide | `0x02000a00` | `101,102,103,104,105,106,107` | ✅ |
| 6 Preston | `0x02000c00` | `108,109` | ✅ |
| 7 Mannford | `0x02000e00` | `110,111` | ✅ |
| 8 Claremore | `0x02001000` | `112,113,...,119` (first 8 of 14) | ✅ |
| 9 Bixby | `0x02001200` | `126,127,128,129,130,131,132,133` | ✅ — crosses into channel index 128+ |
| 10 Depew | `0x02001400` | `143,144,...,150` (first 8 of 16) | ✅ |
| 11 Traveling | `0x02001600` | `161,162` | ✅ |

Each slot: list of `u16 LE` channel indices, `0xFFFF`-terminated/padded, matching the format
already established from zone 0. This is a complete, high-confidence result — ready to implement
in `D890UVCodeplug`.

**Channels: bank 1 confirmed complete (128 of ~163), bank 2 still not located despite a real
search effort.** Cross-referencing the reference file (which gives exact frequency + name + BCD
encoding for every real channel — see below) against zone 9 "Bixby"'s live index list
(`126,127,128,129,...`) confirms **channel index 128 exists and is referenced**, but its physical
storage location was not found this session. Things tried and ruled out:
- Immediately after bank 1 (`0x00FC4000` onward): blank (confirmed multiple times).
- Immediately before bank 1 (`0x00F80000`–`0x00FC0000`): blank.
- The "obvious" `+0x40000` bank-stride guess (`0x01000000`) and several further multiples
  (`0x01040000`, `0x01080000`, `0x010C0000`): all **exact mirrors of bank 1**, not new data — this
  region seems to alias broadly, not just at one paired offset.
- Smaller nearby offsets (`0x00FC8000`, `0x00FD0000`, `0x00FE0000`, `0x00FF0000`): blank.
- Right after the zone table (`0x02001800`–`0x02002000`): blank.
- A `0x100`-stride guess for zone spacing (superseded by the correct `0x200` finding above) and a
  wide, coarse (`0x1000`-stride) sweep of `0x00000000`–`0x08000000`: no second bank found; that
  region is dominated by firmware/font data and the built-in worldwide contact database, both
  already characterized as unrelated.

**Radio settings: partially found.** Confirmed a settings-string area with one field per
`0x4000`-byte slot: radio name `"Maverick"` at `0x03680000` and owner name `"Grant"` at
`0x03684000` (each record starts with an identical 4-byte prefix `03 15 89 93`, presumably some
kind of field-type tag). **Not yet found: the DMR radio ID number itself** — the reference file's
own ID field looks like placeholder/demo data (`12345678`, callsign `KC1KCE`, name `WELCOME`), not
directly useful for finding the real one live. Also still open: contacts (the file has a real
contact list — `"Contact1"` etc. visible right after the zone table around file offset `0x4b50` —
not yet cross-referenced against the live device at all).

**Reference-file frequency encoding, decoded and validated** (useful context for anyone continuing
this): channel records in the `.rdt` file store RX frequency as a **plain `u32 LE`, in units of
10Hz** (not BCD like the live device) at a fixed 46-byte offset before the name string, followed 4
bytes later by TX offset in the same units, with a direction byte in between (`0`=simplex,
`1`=+, `2`=-). Cross-decoded all ~165 real channels this way (frequencies, names, and TX
direction/offset) purely offline — zero radio reads needed for that part. Full list not
reproduced here in full (mostly real callsigns/personal channel names) but is fully reproducible
from `Bridgecom_Maverick.rdt` with the method above.

**Where this leaves the D890UVCodeplug work:** zones are done and channel bank 1 (128/163 real
channels) is fully understood (base `0x00FC0000`, `0x80`-byte records, RX BCD @ `+0x00`, TX offset
BCD @ `+0x04`, UTF-16LE name @ `+0x44`). That's enough to implement a real, working partial decode
right now. Bank 2 (channels 128–162), the DMR ID, and contacts remain open — worth deciding whether
to keep hunting those before writing code, or implement what's solid now and treat the rest as
follow-up.

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
