// Minimal Arduino stand-in so lib/Si570 compiles on the host for unit tests.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct FakeSerial {
  bool echo = false;  // set true to see the driver's debug output
  void print(const char* s) { if (echo) fputs(s, stdout); }
  void println(const char* s = "") { if (echo) printf("%s\n", s); }
};

inline FakeSerial Serial;

inline void delay(unsigned long) {}
