#include "d890uv_codeplug.hh"
#include "config.hh"
#include "logger.hh"
#include <QtEndian>


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
  // does NOT call the inherited allocateContacts()/allocateScanLists()/allocateGeneralSettings()/
  // etc - those target D868UVE addresses that read back as blank or unrelated data on this radio
  // (see maverick_qdmr_support.md).
  this->allocateChannels();
  this->allocateZones();
  this->allocateRadioIDs();
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
D890UVCodeplug::createElements(Context &ctx, const ErrorStack &err) {
  if (! this->setRadioID(ctx, err))
    return false;
  if (! this->createChannels(ctx, err))
    return false;
  if (! this->createZones(ctx, err))
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
