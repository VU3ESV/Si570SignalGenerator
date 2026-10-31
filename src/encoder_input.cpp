#include "encoder_input.h"

#include <Arduino.h>
#include <quadrature.h>

#include "config.h"

namespace {

QuadratureDetentDecoder decoder(cfg::kEncTransitionsPerDetent);
volatile int32_t pendingClicks = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
ButtonClassifier button(cfg::kButtonDebounceMs, cfg::kButtonLongMs);

inline uint8_t readAB() {
  return (uint8_t)((digitalRead(cfg::kPinEncA) << 1) | digitalRead(cfg::kPinEncB));
}

void IRAM_ATTR onEncoderEdge() {
  const uint8_t ab = readAB();
  portENTER_CRITICAL_ISR(&mux);
  pendingClicks += decoder.update(ab);
  portEXIT_CRITICAL_ISR(&mux);
}

}  // namespace

void EncoderInput::begin() {
  if (cfg::kPinEncA < 0) return;  // built without an encoder
  // KY-040 boards have pull-ups on CLK/DT but often not on SW; the internal ones cover both.
  pinMode(cfg::kPinEncA, INPUT_PULLUP);
  pinMode(cfg::kPinEncB, INPUT_PULLUP);
  pinMode(cfg::kPinEncSw, INPUT_PULLUP);
  decoder.reset(readAB());
  attachInterrupt(digitalPinToInterrupt(cfg::kPinEncA), onEncoderEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(cfg::kPinEncB), onEncoderEdge, CHANGE);
}

int EncoderInput::takeClicks() {
  if (cfg::kPinEncA < 0) return 0;
  portENTER_CRITICAL(&mux);
  const int32_t clicks = pendingClicks;
  pendingClicks = 0;
  portEXIT_CRITICAL(&mux);
  // With A = CLK and B = DT, clockwise moves 11 -> 01 -> 00 -> 10, which the decoder
  // counts as negative.
  return cfg::kEncReverse ? clicks : -clicks;
}

ButtonClassifier::Event EncoderInput::pollButton(uint32_t nowMs) { return button.update(buttonDown(), nowMs); }

bool EncoderInput::buttonDown() const { return cfg::kPinEncA >= 0 && digitalRead(cfg::kPinEncSw) == LOW; }
