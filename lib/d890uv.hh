/** @defgroup d890uv BridgeCom Maverick (AnyTone AT-D890UV)
 * Device specific classes for the BridgeCom Maverick, sold as a rebadged AnyTone AT-D890UV.
 *
 * @warning Support for this radio is PARTIAL and READ-ONLY. See `D890UVCodeplug` and
 * `maverick_qdmr_support.md` at the repository root for what is and is not mapped/verified.
 *
 * @ingroup anytone */
#ifndef __D890UV_HH__
#define __D890UV_HH__

#include "radio.hh"
#include "anytone_radio.hh"


/** Implements an interface to the BridgeCom Maverick VHF/UHF DMR (Tier I & II) radio.
 *
 * @warning Read-only, partial support - see `D890UVCodeplug` for exactly what is mapped.
 *
 * @ingroup d890uv */
class D890UV: public AnytoneRadio
{
  Q_OBJECT

public:
  /** Do not construct this class directly, rather use @c Radio::detect. */
  explicit D890UV(AnytoneInterface *device=nullptr, QObject *parent=nullptr);

  const RadioLimits &limits() const;

  /** Returns the default radio information. */
  static RadioInfo defaultRadioInfo();

protected:
  /** Holds the limits for this radio.*/
  RadioLimits *_limits;
};

#endif // __D890UV_HH__
