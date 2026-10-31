// Debounced push button with short / long press detection.
#pragma once

#include <stdint.h>

class ButtonClassifier {
 public:
  enum class Event : uint8_t { None, Short, Long };

  ButtonClassifier(uint32_t debounceMs, uint32_t longMs) : debounceMs_(debounceMs), longMs_(longMs) {}

  // Feed the raw "is pressed" level regularly. Long fires once while still held, so the
  // user gets feedback without letting go; releasing after a long press is not a short.
  Event update(bool pressedRaw, uint32_t nowMs) {
    if (pressedRaw != raw_) {
      raw_ = pressedRaw;
      rawSinceMs_ = nowMs;
    }
    if (raw_ != stable_ && nowMs - rawSinceMs_ >= debounceMs_) {
      stable_ = raw_;
      if (stable_) {
        pressedAtMs_ = nowMs;
        longFired_ = false;
      } else if (!longFired_) {
        return Event::Short;
      }
    }
    if (stable_ && !longFired_ && nowMs - pressedAtMs_ >= longMs_) {
      longFired_ = true;
      return Event::Long;
    }
    return Event::None;
  }

  bool pressed() const { return stable_; }

 private:
  uint32_t debounceMs_;
  uint32_t longMs_;
  bool raw_ = false;
  bool stable_ = false;
  bool longFired_ = false;
  uint32_t rawSinceMs_ = 0;
  uint32_t pressedAtMs_ = 0;
};
