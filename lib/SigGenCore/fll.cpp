#include "fll.h"

#include <math.h>

bool referencePresent(const FllConfig& cfg, const FllSample& s) {
  if (s.refCount <= 0 || s.gateUs <= 0) return false;
  const double refHz = (double)s.refCount * 1e6 / (double)s.gateUs;
  return fabs(refHz / cfg.refHz - 1.0) * 1e6 <= cfg.refTolerancePpm;
}

FllResult fllEvaluate(const FllConfig& cfg, const FllSample& s, uint32_t programmedHz, uint32_t xtalHz) {
  FllResult r{RefStatus::NoRef, 0.0, xtalHz};
  if (!referencePresent(cfg, s)) return r;

  r.status = RefStatus::NoSignal;
  if (s.sigCount <= 0 || programmedHz == 0) return r;

  // Both counters saw the same gate, so the ESP32's clock error cancels out.
  const double measuredHz = (double)cfg.refHz * (double)s.sigCount / (double)s.refCount;
  const double err = (measuredHz / programmedHz - 1.0) * 1e6;
  r.errorPpm = err;
  if (fabs(err) > cfg.signalTolerancePpm) return r;

  // The output scales with the real crystal: real = assumed * measured / programmed.
  const double gain = fabs(err) > cfg.coarsePpm ? 1.0 : cfg.fineGain;
  r.xtalHz = (uint32_t)llround((double)xtalHz * (1.0 + gain * err * 1e-6));
  r.status = fabs(err) <= cfg.lockPpm ? RefStatus::Locked : RefStatus::Tracking;
  return r;
}

const char* refStatusLabel(RefStatus s) {
  switch (s) {
    case RefStatus::Off: return "INT";
    case RefStatus::NoRef: return "NREF";
    case RefStatus::NoSignal: return "NSIG";
    case RefStatus::Tracking: return "TRK";
    case RefStatus::Locked: return "LOCK";
    case RefStatus::Hold: return "HOLD";
    case RefStatus::Calibrating: return "CAL";
  }
  return "?";
}
