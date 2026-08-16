#include "d890uv.hh"

#include "d890uv_codeplug.hh"
#include "d868uv_limits.hh"
#include "anytone_interface.hh"

#include "config.hh"
#include "logger.hh"


D890UV::D890UV(AnytoneInterface *device, QObject *parent)
  : AnytoneRadio("BridgeCom Maverick", device, parent), _limits(nullptr)
{
  _codeplug = new D890UVCodeplug(this);
  _codeplug->clear();

  // Band limits are not yet independently verified for this radio (see D890UVCodeplug docs) -
  // use the common dual-band VHF/UHF range as a reasonable default rather than per-hardware-
  // variant precision.
  _limits = new D868UVLimits({ {Frequency::fromMHz(136.), Frequency::fromMHz(174.)},
                               {Frequency::fromMHz(400.), Frequency::fromMHz(480.)} },
                             { {Frequency::fromMHz(136.), Frequency::fromMHz(174.)},
                               {Frequency::fromMHz(400.), Frequency::fromMHz(480.)} },
                             "unknown", this);
}

const RadioLimits &
D890UV::limits() const {
  return *_limits;
}

RadioInfo
D890UV::defaultRadioInfo() {
  return RadioInfo(
        RadioInfo::D890UV, "d890uv", "BridgeCom Maverick", "BridgeCom",
        {AnytoneMaverickInterface::interfaceInfo()});
}
