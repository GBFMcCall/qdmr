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
 *  - Channel bank 1: 128 of the radio's real ~163 channels, at device address `0x00FC0000`,
 *    `0x80` bytes/record (double the D868UVE's `0x40`). RX frequency and TX offset/direction are
 *    at the same relative offsets as the D868UVE live format (`+0x00`/`+0x04`/byte `0x08`) and
 *    were cross-checked against known real channel values - high confidence. The channel *name*
 *    field moved to `+0x44` and is UTF-16LE (not Latin1) - also cross-checked directly. Other
 *    per-channel fields (mode, power, color code, timeslot, contact index, admit criterion,
 *    bandwidth) are read using the *same* byte-8-region bit-packing as the D868UVE live format;
 *    this is a reasonable-confidence inference (the repeater-direction bits in that byte decoded
 *    correctly for a known channel during reverse-engineering) but is not independently verified
 *    field-by-field the way frequency/name are.
 *  - Channels 129-163 ("bank 2"): NOT located. A real search effort did not find it - see the doc.
 *  - All 12 zones: channel-membership lists at `0x02000000 + zone_index * 0x200`, fully verified
 *    against a real reference codeplug (exact match, all 12, including edge cases). Zone *names*
 *    were not found anywhere on the live device and are synthesized as "Zone N" here.
 *  - Contacts, radio IDs, scan lists, group lists, general settings (DMR ID, callsign, etc.):
 *    NOT located. Deliberately left empty rather than decoded from the (wrong, D868UVE-inherited)
 *    addresses, which would produce garbage.
 *
 * @warning Do not attempt writes with this class. The encode/write path is entirely inherited,
 * unmodified, from D868UVCodeplug and targets the WRONG (D868UVE) addresses - using it to write
 * to a Maverick would corrupt the radio. This class is decode-path-only until the write side is
 * independently derived and verified. Read-only until further notice.
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

  /** Allocates channel bank 1 (128 channels, unconditionally - no live bitmap is used or
   *  trusted; see class documentation). */
  void allocateChannels();
  bool createChannels(Context &ctx, const ErrorStack &err);
  bool linkChannels(Context &ctx, const ErrorStack &err);

  /** Allocates all 12 zones' channel-membership lists (fixed count, no live bitmap). */
  void allocateZones();
  bool createZones(Context &ctx, const ErrorStack &err);
  bool linkZones(Context &ctx, const ErrorStack &err);

protected:
  /** Limits specific to what's actually been mapped on this radio so far. */
  struct Limit: public D868UVCodeplug::Limit {
    /** Only bank 1 is mapped - the real radio has ~163 channels, this exposes 128. */
    static constexpr unsigned int numChannels() { return 128; }
    /** Fully confirmed against a real reference codeplug. */
    static constexpr unsigned int numZones() { return 12; }
  };

  /** Offsets specific to this radio's real, independently-verified memory map (see class
   *  documentation for what's confirmed vs. inferred). */
  struct Offset: public D868UVCodeplug::Offset {
    /// @cond DO_NOT_DOCUMENT
    static constexpr unsigned int channelBanks() { return 0x00FC0000; }
    static constexpr unsigned int zoneChannels() { return 0x02000000; }
    // betweenZoneChannels() (0x200) matches the inherited D868UVE default - no override needed.
    /// @endcond
  };
};

#endif // D890UVCODEPLUG_HH
