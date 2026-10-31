// Rotary encoder (interrupt driven) and its push switch (polled).
#pragma once

#include <button.h>
#include <stdint.h>

class EncoderInput {
 public:
  void begin();
  int takeClicks();  // clicks since the last call, + = clockwise
  ButtonClassifier::Event pollButton(uint32_t nowMs);
  bool buttonDown() const;  // raw level, for the hold-at-power-on reset
};
