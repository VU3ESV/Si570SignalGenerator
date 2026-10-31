# Si570 driver (KO7M), ESP32 port

Copy of Jeff Whitlatch KO7M's Si570 Arduino library,
https://github.com/ko7m/Si570 at commit `6ea9f47`, MIT licensed (see
[LICENSE.md](LICENSE.md)). The 1 Hz fixed-point design and the structure are unchanged.
Every modified line is marked `ESP32 port:` in the source.

## Usage

```cpp
Wire.begin(SDA_PIN, SCL_PIN);
Si570 si570(0x55, 10000000);  // address, factory start-up frequency [, 13 for ±7 ppm parts]
if (si570.begin() == SI570_READY)
  si570.setFrequency(14250000);
```

## Bugs fixed

All of these are covered by `test/test_si570`, which runs the driver against a simulated
Si570.

| Problem in upstream | Effect | Fix |
|---|---|---|
| RFREQ "rounding" `rfreq + ((rfreq & 1<<27) << 1)` adds 1.0, not one LSB | About 51% of frequencies are off by up to **2.6%** (26,000 ppm) | Round to nearest LSB |
| `findDivisors()` only checks the 5.67 GHz DCO ceiling | About 16% of 10–160 MHz settings run the DCO **below the 4.85 GHz minimum** | Check both limits; prefer a DCO with room for the ±3500 ppm window |
| `findDivisors()` result ignored | An unreachable frequency was programmed with stale dividers | Return `SI570_ERROR`, write nothing |
| HS_DIV/N1 not set after init | A first step within 3500 ppm of the start-up frequency computed RFREQ = 0 | Use the factory dividers read at init |
| Small-step path didn't re-check the DCO range | Tuning outward from a centre near a DCO limit could leave the range | Fall back to a full reprogram |
| Registers 7–12 hard-coded | ±7 ppm parts (option letter C) use registers **13–18** (datasheet) | `reg_base` constructor argument |
| Debug format string had 3 conversions, 2 arguments | Undefined behaviour on a read error | Fixed |

## ESP32 / API changes

- The constructor no longer calls `Wire.begin()`, `digitalWrite(SDA/SCL, 0)` (an AVR
  pull-up trick) or talks to the chip. Call `Wire.begin(sda, scl)` and then `begin()`.
  This makes a global `Si570` object safe on ESP32.
- `debugSi570()` ran on every frequency change (about 8 serial lines per encoder step).
  It is now only called when compiled with `-DSI570_DEBUG`.
- `setFrequency()` returns `SI570_ERROR` when an I2C write fails. The next call then does
  a full freeze/NewFreq reprogram.
- New: `getFrequency()`, `getXtalFrequency()`, `getFactoryXtalFrequency()` and
  `setXtalFrequency()`. The last one applies a crystal frequency measured against an
  external reference. A small correction uses the glitch-free Freeze-M path. A large
  one, or one made while still on the factory dividers, does a full reprogram. Many
  10 MHz parts ship with a 4.80 GHz DCO, which is out of spec.
- Removed a duplicated `beginTransmission()` in the single-byte read, and the unused
  `debugx()`, `dump()` and `IF_FREQ`.
