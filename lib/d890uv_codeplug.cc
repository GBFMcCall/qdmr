#include "d890uv_codeplug.hh"
#include "config.hh"
#include "logger.hh"
#include <QtEndian>
#include <cstring>


/* ******************************************************************************************** *
 * Implementation of D890UVCodeplug::ChannelElement
 * ******************************************************************************************** */
D890UVCodeplug::ChannelElement::ChannelElement(uint8_t *ptr)
  : D868UVCodeplug::ChannelElement(ptr, ChannelElement::size())
{
  // pass...
}

QString
D890UVCodeplug::ChannelElement::name() const {
  return readUnicode(Offset::name(), Limit::nameLength(), 0x0000);
}

void
D890UVCodeplug::ChannelElement::setName(const QString &name) {
  writeUnicode(Offset::name(), name, Limit::nameLength(), 0x0000);
}


/* ******************************************************************************************** *
 * Implementation of D890UVCodeplug::RadioIDElement
 * ******************************************************************************************** */
D890UVCodeplug::RadioIDElement::RadioIDElement(uint8_t *ptr)
  : D868UVCodeplug::RadioIDElement(ptr)
{
  // pass...
}

QString
D890UVCodeplug::RadioIDElement::name() const {
  return readUnicode(Offset::name(), Limit::nameLength(), 0x0000);
}

void
D890UVCodeplug::RadioIDElement::setName(const QString &name) {
  writeUnicode(Offset::name(), name, Limit::nameLength(), 0x0000);
}


/* ******************************************************************************************** *
 * Implementation of D890UVCodeplug::ScanListElement
 * ******************************************************************************************** */
D890UVCodeplug::ScanListElement::ScanListElement(uint8_t *ptr)
  : Element(ptr, ScanListElement::size())
{
  // pass...
}

QString
D890UVCodeplug::ScanListElement::name() const {
  // Name length not independently verified beyond what's been observed in real data (up to 10
  // chars, e.g. "Tulsa Cntr") - 16 matches the convention used everywhere else on this radio.
  return readUnicode(Offset::name(), 16, 0x0000);
}


/* ******************************************************************************************** *
 * Implementation of D890UVCodeplug
 * ******************************************************************************************** */
D890UVCodeplug::D890UVCodeplug(QObject *parent)
  : D868UVCodeplug(parent)
{
  // pass...
}

void
D890UVCodeplug::clear() {
  while (this->numImages())
    remImage(0);
  addImage("BridgeCom Maverick Codeplug");
  this->allocateBitmaps();
}

bool
D890UVCodeplug::allocateBitmaps() {
  // No live bitmap tables are used or trusted for this radio (see class documentation) - channel
  // and zone allocation below is unconditional over a fixed, known-good count instead.
  return true;
}

void
D890UVCodeplug::setBitmaps(Context &ctx) {
  Q_UNUSED(ctx)
  // Encode/write path is not implemented for this radio - see class documentation. Intentionally
  // a no-op; must not be reached from the read-only workflow.
}

void
D890UVCodeplug::allocateForDecoding() {
  // Only allocate what has actually been independently verified on the live device. Deliberately
  // does NOT call the inherited allocateContacts()/allocateGeneralSettings()/etc - those target
  // D868UVE addresses that read back as blank or unrelated data on this radio (see
  // maverick_qdmr_support.md).
  this->allocateChannels();
  this->allocateZones();
  this->allocateRadioIDs();
  this->allocateScanLists();
}

void
D890UVCodeplug::allocateForEncoding() {
  // Same addresses as allocateForDecoding() - see class documentation on the read-modify-write
  // safety model this depends on.
  this->allocateChannels();
  this->allocateZones();
  this->allocateRadioIDs();
}

void
D890UVCodeplug::allocateUpdated() {
  // See header: must allocate exactly the same (safe, verified) addresses as
  // allocateForEncoding(), never the inherited D868UVE-specific settings blocks.
  this->allocateForEncoding();
}

bool
D890UVCodeplug::encodeElements(const Flags &flags, Context &ctx, const ErrorStack &err) {
  if (! this->encodeRadioID(flags, ctx, err))
    return false;
  if (! this->encodeChannels(flags, ctx, err))
    return false;
  if (! this->encodeZones(flags, ctx, err))
    return false;
  return true;
}

uint32_t
D890UVCodeplug::radioIdAddress(uint16_t i) {
  // Two independently-placed slots, not a confirmed array - see RadioIDElement documentation.
  static const uint32_t addr[2] = {0x03680000, 0x03684000};
  return addr[i];
}

void
D890UVCodeplug::allocateRadioIDs() {
  for (uint16_t i=0; i<Limit::numRadioIDs(); i++) {
    uint32_t addr = radioIdAddress(i);
    if (! isAllocated(addr, 0))
      image(0).addElement(addr, RadioIDElement::size());
  }
}

bool
D890UVCodeplug::setRadioID(Context &ctx, const ErrorStack &err) {
  Q_UNUSED(err)
  for (uint16_t i=0; i<Limit::numRadioIDs(); i++) {
    RadioIDElement id(data(radioIdAddress(i)));
    if (DMRRadioID *rid = id.toRadioID()) {
      ctx.config()->radioIDs()->add(rid); ctx.add(rid, i);
    }
  }
  return true;
}

bool
D890UVCodeplug::encodeRadioID(const Flags &flags, Context &ctx, const ErrorStack &err) {
  Q_UNUSED(flags); Q_UNUSED(err)
  unsigned int n = ctx.count<DMRRadioID>();
  if (n > Limit::numRadioIDs())
    n = Limit::numRadioIDs();
  for (unsigned int i=0; i<n; i++) {
    RadioIDElement(data(radioIdAddress(i))).fromRadioID(ctx.get<DMRRadioID>(i));
  }
  return true;
}

bool
D890UVCodeplug::createElements(Context &ctx, const ErrorStack &err) {
  if (! this->setRadioID(ctx, err))
    return false;
  if (! this->createChannels(ctx, err))
    return false;
  if (! this->createZones(ctx, err))
    return false;
  if (! this->createScanLists(ctx, err))
    return false;
  return true;
}

bool
D890UVCodeplug::linkElements(Context &ctx, const ErrorStack &err) {
  // Zones reference channels by index, so channels must be linked first.
  if (! this->linkChannels(ctx, err))
    return false;
  if (! this->linkZones(ctx, err))
    return false;
  return true;
}


uint32_t
D890UVCodeplug::channelAddress(uint16_t i) {
  if (i < Limit::channelsInBank1())
    return Offset::channelBanks() + i*ChannelElement::size();
  return Offset::channelBank2() + (i-Limit::channelsInBank1())*ChannelElement::size();
}

void
D890UVCodeplug::allocateChannels() {
  for (uint16_t i=0; i<Limit::numChannels(); i++) {
    uint32_t addr = channelAddress(i);
    if (! isAllocated(addr, 0))
      image(0).addElement(addr, ChannelElement::size());
  }
}

bool
D890UVCodeplug::createChannels(Context &ctx, const ErrorStack &err) {
  Q_UNUSED(err)
  for (uint16_t i=0; i<Limit::numChannels(); i++) {
    ChannelElement ch(data(channelAddress(i)));
    if (Channel *obj = ch.toChannelObj(ctx)) {
      ctx.config()->channelList()->add(obj); ctx.add(obj, i);
    }
  }
  return true;
}

bool
D890UVCodeplug::linkChannels(Context &ctx, const ErrorStack &err) {
  Q_UNUSED(err)
  for (uint16_t i=0; i<Limit::numChannels(); i++) {
    ChannelElement ch(data(channelAddress(i)));
    if (ctx.has<Channel>(i))
      ch.linkChannelObj(ctx.get<Channel>(i), ctx);
  }
  return true;
}

bool
D890UVCodeplug::encodeChannels(const Flags &flags, Context &ctx, const ErrorStack &err) {
  Q_UNUSED(flags); Q_UNUSED(err)
  unsigned int n = ctx.count<Channel>();
  if (n > Limit::numChannels())
    n = Limit::numChannels();
  for (unsigned int i=0; i<n; i++) {
    ChannelElement ch(data(channelAddress(i)));

    // Contacts and scan lists aren't decoded yet (see class documentation), so the Channel object
    // being encoded carries no real information for these two fields. Round-trip validation
    // showed the inherited fromChannelObj() was silently resetting real assignments to "none" -
    // preserve whatever was already on the radio instead.
    unsigned int savedContactIndex = ch.contactIndex();
    unsigned int savedScanListIndex = ch.scanListIndex();

    if (! ch.fromChannelObj(ctx.get<Channel>(i), ctx))
      return false;

    ch.setContactIndex(savedContactIndex);
    ch.setScanListIndex(savedScanListIndex);

    // The inherited fromChannelObj() computes the TX-offset field as an actual delta from RX,
    // i.e. 0 for simplex channels. Round-trip validation showed this radio instead stores a full
    // copy of the RX frequency there for simplex channels (not a zero offset) - restore that.
    if (AnytoneCodeplug::ChannelElement::RepeaterMode::Simplex == ch.repeaterMode())
      ch.setTXOffset(ch.rxFrequency());
  }
  return true;
}


void
D890UVCodeplug::allocateZones() {
  for (uint16_t i=0; i<Limit::numZones(); i++) {
    uint32_t addr = Offset::zoneChannels() + i*Offset::betweenZoneChannels();
    if (! isAllocated(addr, 0))
      image(0).addElement(addr, Size::zoneChannels());
    uint32_t nameAddr = Offset::zoneNames() + i*Offset::betweenZoneNames();
    if (! isAllocated(nameAddr, 0))
      image(0).addElement(nameAddr, Offset::betweenZoneNames());
  }
}

QString
D890UVCodeplug::zoneName(uint16_t i) {
  // NUL-terminated UTF-16LE at offset 0 of each 0x40-byte slot - see class documentation.
  uint16_t *ptr = (uint16_t *)data(Offset::zoneNames() + i*Offset::betweenZoneNames());
  QString name;
  for (unsigned int j=0; (j<Offset::betweenZoneNames()/2) && (0 != ptr[j]); j++)
    name.append(QChar(qFromLittleEndian(ptr[j])));
  return name;
}

void
D890UVCodeplug::setZoneName(uint16_t i, const QString &name) {
  // The name field is only the first Limit::zoneNameLength() (16) UTF-16 chars (32 bytes) of the
  // 0x40-byte slot - confirmed by round-trip validation: the remaining 32 bytes are unused
  // (0xFF-filled on a real read) and must be left untouched, not zeroed.
  uint16_t *ptr = (uint16_t *)data(Offset::zoneNames() + i*Offset::betweenZoneNames());
  unsigned int maxlen = Limit::zoneNameLength();
  for (unsigned int j=0; j<maxlen; j++)
    ptr[j] = qToLittleEndian((uint16_t)((j < (unsigned int)name.length()) ? name.at(j).unicode() : 0));
}

bool
D890UVCodeplug::createZones(Context &ctx, const ErrorStack &err) {
  Q_UNUSED(err)
  for (uint16_t i=0; i<Limit::numZones(); i++) {
    QString name = zoneName(i);
    if (name.isEmpty())
      name = QString("Zone %1").arg(i+1);
    Zone *zone = new Zone(name);
    ctx.config()->zones()->add(zone); ctx.add(zone, i);
  }
  return true;
}

bool
D890UVCodeplug::linkZones(Context &ctx, const ErrorStack &err) {
  Q_UNUSED(err)
  for (uint16_t i=0; i<Limit::numZones(); i++) {
    if (! ctx.has<Zone>(i))
      continue;
    Zone *zone = ctx.get<Zone>(i);
    uint16_t *channels = (uint16_t *)data(Offset::zoneChannels() + i*Offset::betweenZoneChannels());
    for (uint16_t j=0; j<(Size::zoneChannels()/2); j++, channels++) {
      if (0xffff == qFromLittleEndian(*channels))
        continue;
      uint16_t cidx = qFromLittleEndian(*channels);
      if (! ctx.has<Channel>(cidx))
        continue;
      zone->A()->add(ctx.get<Channel>(cidx));
    }
  }
  return true;
}

bool
D890UVCodeplug::encodeZones(const Flags &flags, Context &ctx, const ErrorStack &err) {
  Q_UNUSED(flags); Q_UNUSED(err)
  unsigned int n = ctx.count<Zone>();
  if (n > Limit::numZones())
    n = Limit::numZones();
  for (unsigned int i=0; i<n; i++) {
    Zone *zone = ctx.get<Zone>(i);
    setZoneName(i, zone->name());
    uint16_t *channels = (uint16_t *)data(Offset::zoneChannels() + i*Offset::betweenZoneChannels());
    memset(channels, 0xff, Size::zoneChannels());
    unsigned int maxChannels = Size::zoneChannels()/2;
    for (int j=0; (j<zone->A()->count()) && ((unsigned int)j<maxChannels); j++) {
      channels[j] = qToLittleEndian((uint16_t)ctx.index(zone->A()->get(j)->as<Channel>()));
    }
  }
  return true;
}


void
D890UVCodeplug::allocateScanLists() {
  for (uint16_t i=0; i<Limit::numScanLists(); i++) {
    uint32_t addr = Offset::scanLists() + i*ScanListElement::size();
    if (! isAllocated(addr, 0))
      image(0).addElement(addr, ScanListElement::size());
  }
}

bool
D890UVCodeplug::createScanLists(Context &ctx, const ErrorStack &err) {
  Q_UNUSED(err)
  for (uint16_t i=0; i<Limit::numScanLists(); i++) {
    ScanListElement sl(data(Offset::scanLists() + i*ScanListElement::size()));
    ScanList *obj = new ScanList(sl.name());
    ctx.config()->scanlists()->add(obj); ctx.add(obj, i);
  }
  return true;
}
