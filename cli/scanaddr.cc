// Diagnostic, READ-ONLY address-space scanner for the BridgeCom Maverick / AnyTone AT-D890UV.
//
// Purpose: find where on this radio's actual memory map the real codeplug (zones, channels,
// contacts) lives, since QDMR's D868UVE-derived address map (lib/d868uv_codeplug.hh) reads back
// almost entirely 0xFF at the addresses it expects to hold that data, even though the radio
// itself is confirmed to have a fully working, non-default codeplug loaded.
//
// This tool NEVER calls AnytoneInterface::write(). It only ever calls read(). Temporary/diagnostic
// - not meant to be upstreamed as-is.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <cstdio>
#include <vector>

#include "usbdevice.hh"
#include "anytone_interface.hh"
#include "errorstack.hh"
#include "logger.hh"

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOptions({
    {"start", "Start address (hex, no 0x prefix)", "start", "0"},
    {"end", "End address (hex, no 0x prefix)", "end", "1000000"},
    {"stride", "Stride between sampled 16-byte reads, in bytes (hex)", "stride", "1000"},
    {"dump", "Print every 16-byte read, not just non-uniform/strong hits."},
  });
  parser.process(app);

  bool ok = false;
  uint32_t start = parser.value("start").toUInt(&ok, 16);
  uint32_t end = parser.value("end").toUInt(&ok, 16);
  uint32_t stride = parser.value("stride").toUInt(&ok, 16);
  if (stride < 16) stride = 16;
  bool dumpAll = parser.isSet("dump");

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

  fprintf(stderr, "Scanning 0x%08x .. 0x%08x, stride 0x%x (%u samples)...\n",
          start, end, stride, (end-start)/stride);

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
  for (uint32_t addr = start; addr < end; addr += stride) {
    uint8_t buf[16];
    ErrorStack rerr;
    if (! iface.read(0, addr, buf, 16, rerr)) {
      nerr++;
      continue;
    }
    nread++;
    // Skip boring fill patterns: all-same-byte.
    bool allSame = true;
    for (int i=1; i<16; i++) if (buf[i] != buf[0]) { allSame = false; break; }
    if (allSame && !dumpAll) continue;

    nhit++;

    if (dumpAll) {
      printf("0x%08x:", addr);
      for (int i=0; i<16; i++) printf(" %02x", buf[i]);
      printf("  |");
      for (int i=0; i<16; i++) printf("%c", (buf[i]>=32 && buf[i]<127) ? (char)buf[i] : '.');
      printf("|\n");
      fflush(stdout);
      continue;
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
  }

  fprintf(stderr, "Done. %u reads ok, %u errors, %u non-uniform hits, %u strong (freq/ascii) hits.\n",
          nread, nerr, nhit, nstrong);

  // Cleanly leave program mode / close, same as normal successful read path.
  iface.close();

  return 0;
}
