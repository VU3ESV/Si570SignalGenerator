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

uint32_t applyPpb(uint32_t hz, int32_t ppb) {
  const int64_t delta = ((int64_t)hz * ppb + (ppb >= 0 ? 500000000LL : -500000000LL)) / 1000000000LL;
  return (uint32_t)((int64_t)hz + delta);
}

int32_t correctionFromMeasurement(uint32_t setHz, double measuredHz, int32_t oldPpb) {
  // The output scales with the true crystal: measured / set = true / assumed, and
  // assumed = nominal * (1 + old), so true = nominal * (measured / set) * (1 + old).
  return (int32_t)lround((measuredHz / setHz * (1.0 + oldPpb * 1e-9) - 1.0) * 1e9);
}
