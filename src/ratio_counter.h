// Counts two signals (reference and Si570 sample) over exactly the same window using
// two ESP32 pulse-counter units. Both units use one GPIO as their gate (level) input, so
// a single gpio_set_level() opens or closes both on the same clock edge - the ESP32's
// own clock error therefore cancels out of the count ratio.
#pragma once

#include <driver/pulse_cnt.h>
#include <stdint.h>

class RatioCounter {
 public:
  struct Sample {
    int64_t refCount;
    int64_t sigCount;
    int64_t gateUs;  // by the ESP32 clock, only good to a few ppm
  };

  bool begin(int refPin, int sigPin, int gatePin);
  void openGate();
  void closeGate();
  Sample read() const;

 private:
  pcnt_unit_handle_t ref_ = nullptr;
  pcnt_unit_handle_t sig_ = nullptr;
  int gatePin_ = -1;
  int refBase_ = 0;
  int sigBase_ = 0;
  int64_t openedUs_ = 0;
  int64_t closedUs_ = 0;
};
