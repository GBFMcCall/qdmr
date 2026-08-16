// Diagnostic tool: validates D890UVCodeplug's encode path with a proper read-modify-write
// simulation - loads a REAL raw dump (from `dmrconf read foo.dfu` against the physical radio),
// decodes it, re-encodes the SAME Config on top of that already-loaded image (not a blank one -
// this is what makes it a faithful simulation of the real device workflow), and reports any byte
// differences. Entirely offline - never touches the radio itself.
//
// Temporary/diagnostic - not meant to be upstreamed as-is.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <cstdio>

#include "config.hh"
#include "d890uv_codeplug.hh"
#include "errorstack.hh"
#include "logger.hh"
#include "channel.hh"

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addPositionalArgument("groundtruth", "Raw .dfu file from `dmrconf read foo.dfu`.");
  parser.process(app);

  if (parser.positionalArguments().isEmpty()) {
    fprintf(stderr, "Usage: testencode <groundtruth.dfu>\n");
    return -1;
  }
  QString filename = parser.positionalArguments().at(0);

  ErrorStack err;

  // Load the real ground-truth bytes.
  D890UVCodeplug codeplug;
  if (! codeplug.read(filename, err)) {
    fprintf(stderr, "Cannot read '%s': %s\n", filename.toStdString().c_str(),
            err.format().toStdString().c_str());
    return -1;
  }

  // Snapshot the loaded bytes per-element before we touch anything, for comparison.
  QVector<QPair<uint32_t, QByteArray>> before;
  for (int n=0; n<codeplug.image(0).numElements(); n++) {
    uint32_t addr = codeplug.image(0).element(n).address();
    QByteArray snap((const char *)codeplug.data(addr), codeplug.image(0).element(n).data().size());
    before.append({addr, snap});
  }
  fprintf(stderr, "Loaded %d elements from ground truth.\n", before.size());

  // Decode to a Config, same as a normal read would.
  Config config;
  if (! codeplug.decode(&config, err)) {
    fprintf(stderr, "Cannot decode: %s\n", err.format().toStdString().c_str());
    return -1;
  }
  fprintf(stderr, "Decoded: %d channels, %d zones, %d radio IDs.\n",
          config.channelList()->count(), config.zones()->count(), config.radioIDs()->count());
  for (int i=125; i<132 && i<config.channelList()->count(); i++) {
    fprintf(stderr, "  channel[%d] = '%s' (%s)\n", i,
            config.channelList()->channel(i)->name().toStdString().c_str(),
            config.channelList()->channel(i)->metaObject()->className());
  }

  // Re-encode the SAME config back ONTO THE SAME ALREADY-LOADED IMAGE (updateCodeplug=true is
  // the default - this is the key: it skips clear()+allocateUpdated() and instead patches the
  // existing image in place, exactly like a real device read-modify-write would).
  Codeplug::Flags flags;
  if (! codeplug.encode(&config, flags, err)) {
    fprintf(stderr, "Cannot encode: %s\n", err.format().toStdString().c_str());
    return -1;
  }

  fprintf(stderr, "numElements after encode: %d (was %d before)\n",
          codeplug.image(0).numElements(), before.size());
  // Look for duplicate addresses (would indicate allocate-if-not-present logic failed to detect
  // an already-loaded element).
  {
    QSet<uint32_t> seen;
    int dupes = 0;
    for (int n=0; n<codeplug.image(0).numElements(); n++) {
      uint32_t a = codeplug.image(0).element(n).address();
      if (seen.contains(a)) dupes++;
      seen.insert(a);
    }
    fprintf(stderr, "duplicate addresses in element list: %d\n", dupes);
  }

  // Compare.
  int totalBytes=0, diffBytes=0, diffElements=0;
  for (const auto &pair : before) {
    uint32_t addr = pair.first;
    const QByteArray &orig = pair.second;
    const uint8_t *now = codeplug.data(addr);
    bool elementDiffers = false;
    for (int i=0; i<orig.size(); i++) {
      totalBytes++;
      if ((uint8_t)orig[i] != now[i]) {
        diffBytes++;
        if (!elementDiffers) {
          elementDiffers = true;
          diffElements++;
        }
        printf("0x%08x +0x%02x: before=%02x after=%02x\n", addr, i, (uint8_t)orig[i], now[i]);
      }
    }
  }

  fprintf(stderr, "\nDone. %d/%d elements changed by round-trip encode, %d/%d bytes differ.\n",
          diffElements, before.size(), diffBytes, totalBytes);

  return 0;
}
