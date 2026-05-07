# FutabaVFD
**Driving Futaba's 8 and 16bit Vacuum Fluorescent Display with an ESP32**

Non-blocking Arduino library for Futaba 8/16-digit Vacuum Fluorescent Display
modules (e.g. **8-MD-06INKM**) on ESP32, driven over SPI.

Author: Marc Staehli, initial upload April 2022

This is a refactor of the original `vfd_controls.h` header into a proper
class-based library. Two architectural changes:

1. **Library-shaped.** Implementation lives in `src/FutabaVFD.{h,cpp}` and is
   instantiated as an object: `FutabaVFD vfd(16, 5);`. Multiple displays on
   different SPI buses are supported.
2. **Non-blocking.** Animations (scroll, blink) are state machines that you
   pump with `vfd.update()` from `loop()`. There are no `delay()` calls in
   any of the animation paths. Direct writes (`print`, `writeChar`,
   `writeString`, `setBrightness`, …) issue exactly one SPI transaction and
   return immediately.

Features:
- Works with 8 digits and with 16 digits (must be set in vfd_controls.h)
- Simple functions to control the display
- Includes Umlaute (äöüÄÖÜ) and the degree sign (°)
- vfd_controls.h includes all functions (works similar as a library)

[![Futaba-VFD-16bit-ESP32](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32/blob/main/pics/VFD_16bit.png)](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32)

this one was bought here: https://www.aliexpress.com/item/1005001498957894.html


[![Futaba-VFD-16bit-ESP32](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32/blob/main/pics/Futaba_VFD_16bit.jpg)](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32)

## Wiring

Same as the original sketch:

| Display | ESP32 pin |
|---------|-----------|
| DIN (MOSI) | 23 |
| CLK (SCK)  | 18 |
| CS  (SS)   | 5  |
| RESET (8-digit only) | 19 |

The 8-digit module additionally needs `EN` solder-bridged to +5 V and `RESET`
held high (pulse low to reset).

## Quick start

```cpp
#include <FutabaVFD.h>

FutabaVFD vfd(16, /*CS*/5, /*RESET*/-1);

void setup() {
  vfd.begin(/*SCLK*/18, /*MISO*/-1, /*MOSI*/23);
  vfd.setBrightness(120);
  vfd.printScroll("hifilabor.ch  -  Futaba VFD demo  -  ", 200, true);
}

void loop() {
  vfd.update();   // pump scroll/blink state machines
  // ... your other work ...
}
```

## API

### Lifecycle

| Method | Notes |
|--------|-------|
| `FutabaVFD(digits, cs, reset=-1, bus=VSPI)` | Constructor. `digits` is 8 or 16. |
| `bool begin(sclk, miso, mosi, hz=100000, selfTest=false)` | Init SPI + controller. Self-test off by default to keep startup fast. |
| `void end()` | Release SPI bus, clear display. |

### Direct writes (non-blocking, one SPI transaction)

| Method | |
|--------|---|
| `setBrightness(0..255)` | |
| `clear()` / `show()` | |
| `standby(bool)` | |
| `writeChar(col, byte)` | |
| `writeString(col, text)` | Truncated to visible width. |
| `print(text)` | Convenience: writes at column 0 with right-padding. |

UTF-8 input is translated to the device codepage internally. Supported
specials: `Ä ä Ö ö Ü ü °`.

### Animations (driven by `update()`)

| Method | |
|--------|---|
| `printScroll(text, stepMs=250, loop=true)` | Right-to-left scroll. |
| `stopScroll()` / `isScrolling()` | |
| `blink(periodMs)` | Toggles standby for a soft blink. |
| `stopBlink()` / `isBlinking()` | |
| `update()` | **Call from `loop()`.** Returns true if anything changed. |

## Migration from `vfd_controls.h`

| Old (header) | New (library) |
|--------------|---------------|
| `VFD_init();` | `vfd.begin(...)` |
| `VFD_brightness(n);` | `vfd.setBrightness(n);` |
| `VFD_clearScreen();` | `vfd.clear();` |
| `VFD_standby(b);` | `vfd.standby(b);` |
| `VFD_WriteASCII(x, c);` | `vfd.writeChar(x, c);` |
| `VFD_WriteStr(x, s);` *(blocking scroll!)* | `vfd.writeString(x, s)` *(no scroll, truncates)* **or** `vfd.printScroll(s, ms)` *(non-blocking scroll)* |

Note that the old `VFD_WriteStr()` had two behaviours rolled into one: pad
short strings, scroll long strings with `delay(100)` between steps. The new
API splits these because they have very different runtime characteristics.

## License

MIT, same as the upstream project.
