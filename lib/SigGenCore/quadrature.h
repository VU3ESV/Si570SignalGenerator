// Rotary encoder decoding. No tables and no Arduino calls, so it is safe in an ISR.
#pragma once

#include <stdint.h>

// Gray-code transition between two AB states (A = bit 1, B = bit 0):
// +1 / -1 for a valid step, 0 for no change or an impossible double step.
inline int8_t quadratureDelta(uint8_t prevAB, uint8_t currAB) {
  const uint8_t s = (uint8_t)(((prevAB & 3u) << 2) | (currAB & 3u));
  // 00->01->11->10->00 is one direction, the reverse is the other.
  const uint16_t kUp = (1u << 0b0001) | (1u << 0b0111) | (1u << 0b1110) | (1u << 0b1000);
  const uint16_t kDown = (1u << 0b0010) | (1u << 0b1011) | (1u << 0b1101) | (1u << 0b0100);
  return (int8_t)(((kUp >> s) & 1u) - ((kDown >> s) & 1u));
}

// Turns AB samples into detent clicks. Contact bounce produces +1/-1 pairs that cancel,
// and the count re-synchronises every time the encoder rests in a detent position, so
// a missed edge can never shift the click point permanently.
class QuadratureDetentDecoder {
 public:
  // 4 = full-step encoders (one detent per Gray cycle, e.g. KY-040), 2 = half-step.
  explicit QuadratureDetentDecoder(uint8_t transitionsPerDetent = 4)
      : halfStep_(transitionsPerDetent == 2) {}

  void reset(uint8_t ab) {
    prev_ = ab & 3u;
    acc_ = 0;
  }

  // Returns +1 / -1 when a detent click completes, otherwise 0.
  int8_t update(uint8_t ab) {
    ab &= 3u;
    acc_ += quadratureDelta(prev_, ab);
    prev_ = ab;
    const bool detent = (ab == 3u) || (halfStep_ && ab == 0u);
    if (!detent) return 0;
    const int8_t click = acc_ >= 2 ? 1 : (acc_ <= -2 ? -1 : 0);
    acc_ = 0;
    return click;
  }

 private:
  bool halfStep_;
  uint8_t prev_ = 3;
  int8_t acc_ = 0;
};
