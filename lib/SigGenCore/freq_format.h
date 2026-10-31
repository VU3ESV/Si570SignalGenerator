// Frequency / step text shared by the LCD and OLED renderers.
#pragma once

#include <stddef.h>
#include <stdint.h>

// "145.000 000" - MHz, decimal point, kHz digits, space, Hz digits. Returns the length.
size_t formatFrequency(char* out, size_t size, uint32_t hz);

// Index of the 10^decade digit inside a formatFrequency() string of length len,
// or -1 if the number has no such digit.
int digitIndexForDecade(const char* formatted, size_t len, unsigned decade);

// 1 -> 0, 10 -> 1, 1000 -> 3 ...
unsigned decadeOf(uint32_t step);

// "1Hz", "10kHz", "1MHz" ...
const char* stepLabel(uint32_t stepHz);
