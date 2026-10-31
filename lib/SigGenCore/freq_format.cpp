#include "freq_format.h"

#include <stdio.h>

size_t formatFrequency(char* out, size_t size, uint32_t hz) {
  const unsigned long mhz = hz / 1000000UL;
  const unsigned long khz = (hz / 1000UL) % 1000UL;
  const unsigned long rest = hz % 1000UL;
  const int n = snprintf(out, size, "%lu.%03lu %03lu", mhz, khz, rest);
  return n < 0 ? 0 : (size_t)n;
}

int digitIndexForDecade(const char* formatted, size_t len, unsigned decade) {
  // Walk left from the last digit, skipping the space and the decimal point.
  const int idx = (int)len - 1 - (int)decade - (decade >= 3 ? 1 : 0) - (decade >= 6 ? 1 : 0);
  if (idx < 0) return -1;
  const char c = formatted[idx];
  return (c >= '0' && c <= '9') ? idx : -1;
}

unsigned decadeOf(uint32_t step) {
  unsigned d = 0;
  while (step >= 10) {
    step /= 10;
    ++d;
  }
  return d;
}

const char* stepLabel(uint32_t stepHz) {
  switch (stepHz) {
    case 1: return "1Hz";
    case 10: return "10Hz";
    case 100: return "100Hz";
    case 1000: return "1kHz";
    case 10000: return "10kHz";
    case 100000: return "100kHz";
    case 1000000: return "1MHz";
    case 10000000: return "10MHz";
    case 100000000: return "100MHz";
    default: return "?";
  }
}
