#ifndef D890UVCODEPLUG_HH
#define D890UVCODEPLUG_HH

#include "d868uv_codeplug.hh"

/** Support for the BridgeCom Maverick (AnyTone AT-D890UV variant).
 *
 * @warning This is a PARTIAL, read-only codeplug. Its address map was reverse-engineered against
 * one physical radio by direct USB probing (never guessed), cross-checked against a real Windows
 * CPS export. See `maverick_qdmr_support.md` at the repository root for the full investigation
 * log, including exactly what is and is not independently verified.
 *
 * What's mapped:
 *  - All 163 real channels, in two banks, `0x80` bytes/record (double the D868UVE's `0x40`):
 *    bank 1 holds channel indices 0-127 at device address `0x00FC0000`; bank 2 holds indices
 *    128-162 (the remaining 35) at `0x010C0000` - found by a dense address sweep, not derived
 *    from any formula relative to bank 1 (it is not a clean multiple of `betweenChannelBanks()`
 *    the way D868UVE's own banks are). Both banks confirmed to hold real data ending exactly
 *    where expected (128 records in bank 1, 35 in bank 2 = 163 total, matching the known-good
 *    zone/channel count from the original investigation). RX frequency and TX offset/direction
 *    are at the same relative offsets as the D868UVE live format (`+0x00`/`+0x04`/byte `0x08`)
 *    and were cross-checked against known real channel values - high confidence. The channel
 *    *name* field moved to `+0x44` and is UTF-16LE (not Latin1) - also cross-checked directly.
 *    Other per-channel fields (mode, power, color code, timeslot, contact index, admit criterion,
 *    bandwidth) are read using the *same* byte-8-region bit-packing as the D868UVE live format;
 *    this is a reasonable-confidence inference (the repeater-direction bits in that byte decoded
 *    correctly for a known channel during reverse-engineering) but is not independently verified
 *    field-by-field the way frequency/name are.
 *  - All 12 zones: channel-membership lists at `0x02000000 + zone_index * 0x200`, fully verified
 *    against a real reference codeplug (exact match, all 12, including edge cases). Zone *names*
 *    are a completely separate table at `0x03600000 + zone_index * 0x40` (NUL-terminated
 *    UTF-16LE at offset 0 of each 64-byte slot) - found via a live before/after test (user
 *    renamed zone 10 from "Depew" to "Test" via the Windows CPS and wrote it to the radio; a
 *    read-only search for the new string found it at exactly the predicted address). Definitively
 *    confirmed, not inferred.
 *  - Two radio ID entries at `0x03680000`/`0x03684000` (name at `+0x04`, UTF-16LE, otherwise the
 *    same `D868UVCodeplug::RadioIDElement` format) - found via a live test with the user's real
 *    DMR ID. Whether one is semantically "Master ID" and the other a "Radio ID List" entry isn't
 *    confirmed, but both round-trip byte-perfect.
 *  - Contacts, general settings (callsign, display/audio/etc. preferences): NOT located.
 *    Deliberately left empty rather than decoded from the (wrong, D868UVE-inherited) addresses,
 *    which would produce garbage.
 *  - Scan lists: a real table was found at `0x02100000 + list_index * 0x200` (name at `+0x0E`,
 *    preceded by 4 `u16` fields of unknown meaning) but is not yet wired into this class - see
 *    `maverick_qdmr_support.md`.
 *
 * @warning Encode (write) logic exists for channels, zones (membership + names), and radio IDs -
 * the same addresses as the decode side. Validated with a proper offline round-trip test
 * (`cli/testencode.cc`: load a real raw dump, decode, re-encode the same `Config` back onto the
 * *same already-loaded* image - the actual device read-modify-write path, not a blank-slate
 * encode - then diff byte-for-byte against the original): zone channel-lists and both radio IDs
 * are byte-perfect; ordinary repeater/DMR channels are byte-perfect after fixing two real bugs
 * this test found (contact/scan-list index were being reset instead of preserved; simplex
 * channels' TX field needed to mirror RX, not encode a zero offset). Remaining known gaps are
 * narrow and isolated to special-purpose channels (APRS beacon channels, VFO placeholders) - see
 * `maverick_qdmr_support.md` for specifics. `allocateForEncoding()`/`encodeElements()` touch only
 * the specific addresses this class understands, in keeping with a read-modify-write model where
 * everything else (contacts, scan lists, general settings, and anything else not yet mapped)
 * simply never gets addressed and so can't be corrupted by an encode - growing this class's
 * mapped area over time is meant to stay safe under that same principle. Still: **do not perform
 * an actual write to the radio with this class without explicit, separate confirmation** - a
 * round-trip byte match is strong evidence, not proof, and this hasn't been write-tested against
 * real hardware. Also see the read-reliability note in `maverick_qdmr_support.md` regarding
 * channel bank 2 - it was observed to intermittently read back as blank for reasons not fully
 * understood, which is exactly the failure mode a read-modify-write cycle is vulnerable to.
 *
 * @ingroup anytone */
class D890UVCodeplug : public D868UVCodeplug
{
  Q_OBJECT

public:
  /** Channel element for the BridgeCom Maverick.
   *
   * Memory layout of the encoded channel element (size 0x0080 bytes, double the D868UVE's
   * 0x0040): RX frequency, TX offset, and the byte-0x08 mode/power/bandwidth/repeater-direction
   * bit-packing are at the same relative offsets as `D868UVCodeplug::ChannelElement` (inherited
   * unmodified); only the size and the name field (moved to +0x44, UTF-16LE) differ. */
  class ChannelElement: public D868UVCodeplug::ChannelElement
  {
  public:
    /** Constructor. */
    explicit ChannelElement(uint8_t *ptr);

    /** Returns the size of the element. */
    static constexpr unsigned int size() { return 0x0080; }

    /** Returns the name of the channel (UTF-16LE, unlike the D868UVE's Latin1). */
    QString name() const;
    /** Sets the name of the channel. */
    void setName(const QString &name);

  protected:
    /** Internal used offsets within the channel element. */
    struct Offset: public D868UVCodeplug::ChannelElement::Offset {
      /// @cond DO_NOT_DOCUMENT
      static constexpr unsigned int name() { return 0x0044; }
      /// @endcond
    };
  };

  /** Radio ID element for the BridgeCom Maverick.
   *
   * Found via a live test: user provided their real DMR ID (3158993); a read-only search for its
   * BCD8-be encoding found it at exactly two addresses, `0x03680000` (name "Maverick", the radio's
   * own name) and `0x03684000` (name "Grant", the user's own name) - matching the user's
   * description of the same ID appearing once as the Master ID and once in the Radio ID List.
   * Which of the two is semantically "master" vs. "list entry" isn't independently confirmed;
   * both are exposed as radio ID list entries here since that's the only shape QDMR's `Config`
   * model has for this data, and both round-trip through the *same* record format:
   * `[u32 BCD8-be number][UTF-16LE name, NUL-terminated]`, i.e. `D868UVCodeplug::RadioIDElement`
   * with the name field moved from `+0x05` to `+0x04` and switched from Latin1 to UTF-16LE -
   * exactly the same kind of change as `ChannelElement` and the zone names above. */
  class RadioIDElement: public D868UVCodeplug::RadioIDElement
  {
  public:
    /** Constructor. */
    explicit RadioIDElement(uint8_t *ptr);

    /** Returns the name of the radio ID (UTF-16LE, unlike the D868UVE's Latin1). */
    QString name() const;
    /** Sets the name of the radio ID. */
    void setName(const QString &name);

  protected:
    /** Internal used offsets within the radio ID element. */
    struct Offset: public D868UVCodeplug::RadioIDElement::Offset {
      /// @cond DO_NOT_DOCUMENT
      static constexpr unsigned int name() { return 0x0004; }
      /// @endcond
    };
  };

public:
  /** Constructor. */
  explicit D890UVCodeplug(QObject *parent=nullptr);

  void clear();

protected:
  bool allocateBitmaps();
  void setBitmaps(Context &ctx);

  void allocateForDecoding();
  bool createElements(Context &ctx, const ErrorStack &err);
  bool linkElements(Context &ctx, const ErrorStack &err);

  /** Encode-path counterpart of @c allocateForDecoding() - allocates the exact same addresses
   *  (channels, zones, radio IDs). Deliberately does not touch anything else - see class
   *  documentation on the read-modify-write safety model. */
  void allocateForEncoding();
  /** `AnytoneCodeplug::encode()` calls this instead of @c allocateForEncoding() when building a
   *  codeplug from scratch (`!flags.updateCodeplug()`) - inherited D868UVCodeplug behavior
   *  allocates a long list of D868UVE-specific settings blocks at the wrong addresses for this
   *  radio. Overridden to do exactly what @c allocateForEncoding() does, so this class can't
   *  accidentally touch unverified addresses no matter which entry point is used. */
  void allocateUpdated();
  /** Encode-path counterpart of @c createElements()/@c linkElements() combined. */
  bool encodeElements(const Flags &flags, Context &ctx, const ErrorStack &err);

  /** Allocates the two known radio-ID-list slots (see @c RadioIDElement documentation). */
  void allocateRadioIDs();
  bool setRadioID(Context &ctx, const ErrorStack &err);
  /** Encode-path counterpart of @c setRadioID(). */
  bool encodeRadioID(const Flags &flags, Context &ctx, const ErrorStack &err);
  /** Returns the device address of the i-th (0 or 1) known radio ID slot. */
  static uint32_t radioIdAddress(uint16_t i);

  /** Allocates both channel banks (163 channels total, unconditionally - no live bitmap is used
   *  or trusted; see class documentation). */
  void allocateChannels();
  bool createChannels(Context &ctx, const ErrorStack &err);
  bool linkChannels(Context &ctx, const ErrorStack &err);
  /** Encode-path counterpart of @c createChannels()/@c linkChannels(). Bounded to
   *  @c Limit::numChannels() (163) - a config with more channels than that would mean creating a
   *  channel beyond bank 2's currently-known real content, which needs a deliberate capacity
   *  increase, not an automatic one; extra channels are silently not written rather than writing
   *  to unverified addresses. */
  bool encodeChannels(const Flags &flags, Context &ctx, const ErrorStack &err);

  /** Returns the device address of the i-th channel (0-based), accounting for the bank 1/bank 2
   *  split (see class documentation). */
  static uint32_t channelAddress(uint16_t i);

  /** Allocates all 12 zones' channel-membership lists and name fields (fixed count, no live
   *  bitmap). */
  void allocateZones();
  bool createZones(Context &ctx, const ErrorStack &err);
  bool linkZones(Context &ctx, const ErrorStack &err);
  /** Encode-path counterpart of @c createZones()/@c linkZones() combined - writes both the
   *  channel-membership list and the zone name. Bounded to @c Limit::numZones() (12); a config
   *  with more zones than that is not written, for the same reason as @c encodeChannels(). */
  bool encodeZones(const Flags &flags, Context &ctx, const ErrorStack &err);

  /** Returns the real zone name read from the device, or an empty string if unset. */
  QString zoneName(uint16_t i);
  /** Writes the zone name to the device. */
  void setZoneName(uint16_t i, const QString &name);

protected:
  /** Limits specific to what's actually been mapped on this radio so far. */
  struct Limit: public D868UVCodeplug::Limit {
    /** Both banks mapped - the full, real, confirmed channel count. */
    static constexpr unsigned int numChannels() { return 163; }
    /** Number of channels in bank 1, at @c Offset::channelBanks(). Channels at or beyond this
     *  index live in bank 2, at @c Offset::channelBank2(). */
    static constexpr unsigned int channelsInBank1() { return 128; }
    /** Fully confirmed against a real reference codeplug. */
    static constexpr unsigned int numZones() { return 12; }
    /** Only two radio-ID-list slots are known (see @c RadioIDElement documentation) - the real
     *  capacity of this table (if it's an array at all, rather than two independently-placed
     *  settings fields) is not known. */
    static constexpr unsigned int numRadioIDs() { return 2; }
  };

  /** Offsets specific to this radio's real, independently-verified memory map (see class
   *  documentation for what's confirmed vs. inferred). */
  struct Offset: public D868UVCodeplug::Offset {
    /// @cond DO_NOT_DOCUMENT
    static constexpr unsigned int channelBanks() { return 0x00FC0000; }
    static constexpr unsigned int channelBank2() { return 0x010C0000; }
    static constexpr unsigned int zoneChannels() { return 0x02000000; }
    // betweenZoneChannels() (0x200) matches the inherited D868UVE default - no override needed.
    static constexpr unsigned int zoneNames() { return 0x03600000; }
    static constexpr unsigned int betweenZoneNames() { return 0x0040; }
    /// @endcond
  };
};

#endif // D890UVCODEPLUG_HH
