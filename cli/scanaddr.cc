// Diagnostic, READ-ONLY address-space scanner for the BridgeCom Maverick / AnyTone AT-D890UV.
//
// Purpose: find where on this radio's actual memory map the real codeplug (zones, channels,
// contacts) lives, since QDMR's D868UVE-derived address map (lib/d868uv_codeplug.hh) reads back
// almost entirely 0xFF at the addresses it expects to hold that data, even though the radio
// itself is confirmed to have a fully working, non-default codeplug loaded.
//
// This tool NEVER calls AnytoneInterface::write(). It only ever calls read(). Temporary/diagnostic
// - not meant to be upstreamed as-is.
//
// Supports multiple --range/--addr arguments per invocation, all served from a single
// enter-program-mode/close session, since each invocation's connect/disconnect cycle costs the
// radio a several-second settle time. Also supports --save to append every successful read to a
// local cache file (address + 16 bytes hex, one per line) for later offline analysis (grep/python)
// without touching the radio again.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>
#include <cstdio>
#include <vector>

#include "usbdevice.hh"
#include "anytone_interface.hh"
#include "errorstack.hh"
#include "logger.hh"

struct Range { uint32_t start, end, stride; };

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOptions({
    {"start", "Start address (hex, no 0x prefix)", "start"},
    {"end", "End address (hex, no 0x prefix)", "end"},
    {"stride", "Stride between sampled 16-byte reads, in bytes (hex)", "stride", "1000"},
    {"range", "Address range 'start:end:stride' (hex, no 0x prefix). Repeatable - all ranges "
              "are served in one radio session.", "range"},
    {"addr", "A single address to sample (hex, no 0x prefix). Repeatable.", "addr"},
    {"dump", "Print every 16-byte read, not just non-uniform/strong hits."},
    {"save", "Append every successful read (address + 16 bytes hex) to this file, "
              "for later offline analysis without touching the radio again.", "file"},
  });
  parser.process(app);

  std::vector<Range> ranges;
  std::vector<uint32_t> singles;
  bool ok = false;

  if (parser.isSet("start") || parser.isSet("end")) {
    uint32_t start = parser.value("start").isEmpty() ? 0 : parser.value("start").toUInt(&ok, 16);
    uint32_t end = parser.value("end").isEmpty() ? start+0x1000000 : parser.value("end").toUInt(&ok, 16);
    uint32_t stride = parser.value("stride").toUInt(&ok, 16);
    if (stride < 16) stride = 16;
    ranges.push_back({start, end, stride});
  }
  for (const QString &r : parser.values("range")) {
    QStringList parts = r.split(':');
    if (parts.size() != 3) {
      fprintf(stderr, "Bad --range '%s', expected start:end:stride\n", r.toStdString().c_str());
      return -1;
    }
    uint32_t start = parts[0].toUInt(&ok, 16);
    uint32_t end = parts[1].toUInt(&ok, 16);
    uint32_t stride = parts[2].toUInt(&ok, 16);
    if (stride < 16) stride = 16;
    ranges.push_back({start, end, stride});
  }
  for (const QString &a : parser.values("addr")) {
    singles.push_back(a.toUInt(&ok, 16));
  }

  if (ranges.empty() && singles.empty()) {
    fprintf(stderr, "Nothing to do - pass --start/--end, --range, and/or --addr.\n");
    return -1;
  }

  bool dumpAll = parser.isSet("dump");
  QFile saveFile;
  QTextStream saveStream;
  if (parser.isSet("save")) {
    saveFile.setFileName(parser.value("save"));
    if (! saveFile.open(QIODevice::Append | QIODevice::Text)) {
      fprintf(stderr, "Cannot open --save file '%s'\n", parser.value("save").toStdString().c_str());
      return -1;
    }
    saveStream.setDevice(&saveFile);
  }

  ErrorStack err;
  QList<USBDeviceDescriptor> ifaces = AnytoneMaverickInterface::detect(false);
  if (ifaces.isEmpty()) {
    fprintf(stderr, "No Maverick interface found.\n");
    return -1;
  }

  AnytoneMaverickInterface iface(ifaces.first(), err);
  if (! iface.isOpen()) {
    fprintf(stderr, "Cannot open interface: %s\n", err.format().toStdString().c_str());
    return -1;
  }

  uint32_t totalSamples = 0;
  for (auto &r : ranges) totalSamples += (r.end - r.start) / r.stride;
  totalSamples += singles.size();
  fprintf(stderr, "One session: %zu range(s), %zu single addr(s), ~%u samples total...\n",
          ranges.size(), singles.size(), totalSamples);

  // BCD8-be decode of the first 4 bytes, same as AnytoneCodeplug::ChannelElement::rxFrequency().
  auto bcd8 = [](const uint8_t *b) -> long {
    uint32_t val = (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
    long digits[8] = {(long)(val&0xf), (long)((val>>4)&0xf), (long)((val>>8)&0xf), (long)((val>>12)&0xf),
                       (long)((val>>16)&0xf), (long)((val>>20)&0xf), (long)((val>>24)&0xf), (long)((val>>28)&0xf)};
    for (int i=0;i<8;i++) if (digits[i]>9) return -1;
    long mult[8] = {1,10,100,1000,10000,100000,1000000,10000000};
    long v=0; for (int i=0;i<8;i++) v += digits[i]*mult[i];
    return v*10;
  };

  uint32_t nread=0, nerr=0, nhit=0, nstrong=0;

  auto processAddr = [&](uint32_t addr) {
    uint8_t buf[16];
    ErrorStack rerr;
    if (! iface.read(0, addr, buf, 16, rerr)) {
      nerr++;
      return;
    }
    nread++;

    if (saveStream.device()) {
      saveStream << QString("%1:").arg(addr, 8, 16, QChar('0'));
      for (int i=0; i<16; i++) saveStream << QString("%1").arg(buf[i], 2, 16, QChar('0'));
      saveStream << "\n";
    }

    bool allSame = true;
    for (int i=1; i<16; i++) if (buf[i] != buf[0]) { allSame = false; break; }
    if (allSame && !dumpAll) return;

    nhit++;

    if (dumpAll) {
      printf("0x%08x:", addr);
      for (int i=0; i<16; i++) printf(" %02x", buf[i]);
      printf("  |");
      for (int i=0; i<16; i++) printf("%c", (buf[i]>=32 && buf[i]<127) ? (char)buf[i] : '.');
      printf("|\n");
      fflush(stdout);
      return;
    }

    // Strong signal 1: bytes 0..3 decode as a plausible VHF/UHF frequency (same encoding as
    // AnytoneCodeplug::ChannelElement::rxFrequency()).
    long freq = bcd8(buf);
    bool freqHit = (freq >= 30000000 && freq <= 174000000) || (freq >= 400000000 && freq <= 520000000);

    // Strong signal 2: a long run (>=8) of printable, non-space ASCII - candidate name text.
    int maxrun=0, run=0;
    for (int i=0;i<16;i++) {
      if (buf[i]>32 && buf[i]<127) { run++; if (run>maxrun) maxrun=run; }
      else run=0;
    }
    bool asciiHit = maxrun >= 8;

    // Strong signal 3: UTF-16LE text - printable byte, then 0x00, repeated. Short zone/channel
    // names (e.g. "Pi", "Bixby") never trigger asciiHit above since every other byte is 0x00;
    // this catches them. Checked at both even and odd start offsets in case the 16-byte window
    // doesn't land on a char boundary.
    int maxu16 = 0;
    for (int startOff=0; startOff<2; startOff++) {
      int urun=0, ubest=0;
      for (int i=startOff; i+1<16; i+=2) {
        if (buf[i]>32 && buf[i]<127 && buf[i+1]==0) { urun++; if (urun>ubest) ubest=urun; }
        else urun=0;
      }
      if (ubest>maxu16) maxu16=ubest;
    }
    bool utf16Hit = maxu16 >= 3;

    if (freqHit || asciiHit || utf16Hit) {
      nstrong++;
      const char *tag = freqHit ? "[FREQ]" : (utf16Hit ? "[UTF16]" : "[ASCII]");
      printf("%s 0x%08x:", tag, addr);
      for (int i=0; i<16; i++) printf(" %02x", buf[i]);
      printf("  |");
      for (int i=0; i<16; i++) printf("%c", (buf[i]>=32 && buf[i]<127) ? (char)buf[i] : '.');
      printf("|");
      if (freqHit) printf("  freq=%.5f MHz", freq/1e6);
      printf("\n");
      fflush(stdout);
    }
  };

  for (auto &r : ranges)
    for (uint32_t addr = r.start; addr < r.end; addr += r.stride)
      processAddr(addr);
  for (uint32_t addr : singles)
    processAddr(addr);

  fprintf(stderr, "Done. %u reads ok, %u errors, %u non-uniform hits, %u strong (freq/ascii/utf16) hits.\n",
          nread, nerr, nhit, nstrong);

  // Cleanly leave program mode / close, same as normal successful read path.
  iface.close();

  return 0;
}
