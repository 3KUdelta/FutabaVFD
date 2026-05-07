# FutabaVFD

**Driving Futaba's 8 and 16-bit Vacuum Fluorescent Displays with an ESP32**

Non-blocking Arduino library for Futaba 8/16-digit Vacuum Fluorescent Display
modules (e.g. **8-MD-06INKM**) on ESP32, driven over SPI.

Author: Marc Staehli, initial upload April 2022.

This is a refactor of the original `vfd_controls.h` header into a proper
class-based library. Three architectural changes:

1. **Library-shaped.** Implementation lives in `src/FutabaVFD.{h,cpp}` and is
   instantiated as an object: `FutabaVFD vfd(16, 5);`. Multiple displays on
   different SPI buses are supported.
2. **Non-blocking.** Animations (scroll, blink, vertical drop-in/out) are
   state machines that you pump with `vfd.update()` from `loop()`. There are
   no `delay()` calls in any of the animation paths. Direct writes
   (`print`, `writeChar`, `writeString`, `setBrightness`, …) issue exactly
   one SPI transaction and return immediately.
3. **Pixel-accurate vertical animations.** Using the controller's CGRAM, the
   library can drop characters in from above, drop them out the bottom, or
   scroll them as an endless rotating band — all driven by the same
   `update()` pump.

## Features

- Works with 8-digit and 16-digit modules (set per instance, not at compile time)
- Direct API: `print`, `writeString`, `writeChar`, `setBrightness`, `clear`, `standby`
- Non-blocking horizontal scroll (`printScroll`)
- Non-blocking blink (`blink`)
- Pixel-accurate vertical drop-in / drop-out / endless scroll
- UTF-8 input with German umlauts (`äöüÄÖÜ`) and degree sign (`°`)
- Embedded 5×7 font (~500 bytes flash, PROGMEM)
- Multiple display instances on different SPI buses

[![Futaba-VFD-16bit-ESP32](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32/raw/main/pics/VFD_16bit.png)](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32)

This module was bought here: <https://www.aliexpress.com/item/1005001498957894.html>

[![Futaba-VFD-16bit-ESP32](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32/raw/main/pics/Futaba_VFD_16bit.jpg)](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32)

## Installation

1. Download the `.zip` file from [Releases](https://github.com/3KUdelta/Futaba-VFD-16bit_ESP32/releases) (or clone this repo)
2. Unzip
3. Copy the `FutabaVFD` directory into the `libraries` folder of your Arduino environment
4. Restart the Arduino IDE — the examples appear under **File → Examples → FutabaVFD**

## Wiring

| Display | ESP32 pin |
|---------|-----------|
| DIN (MOSI) | 23 |
| CLK (SCK)  | 18 |
| CS (SS)    | 5  |
| RESET (8-digit only) | 19 |

The 8-digit module additionally needs `EN` solder-bridged to +5 V, and
`RESET` held high (pulse low to reset).

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
  vfd.update();   // pump all animation state machines
  // ... your other work ...
}
```

## API

### Lifecycle

| Method | Notes |
|--------|-------|
| `FutabaVFD(digits, cs, reset=-1, bus=VSPI)` | Constructor. `digits` is 8 or 16. |
| `bool begin(sclk, miso, mosi, hz=100000, selfTest=false)` | Init SPI + controller. Self-test off by default to keep startup fast. Bump `hz` to 500000 if you use the vertical animations heavily. |
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
| `printScroll(text, stepMs=250, loop=true)` | Right-to-left horizontal scroll. |
| `stopScroll()` / `isScrolling()` | |
| `blink(periodMs)` | Toggles standby for a soft blink. |
| `stopBlink()` / `isBlinking()` | |
| `printVerticalIn(text, totalMs=500)` | Pixel-accurate drop-in from above. Returns `false` if the 8-glyph CGRAM budget is exceeded — see below. |
| `printVerticalOut(totalMs=500)` | Drop the current text out the bottom. |
| `scrollVertical(text, stepMs=80, dir=-1, loop=true)` | Endless rotating-band effect. `dir=-1` = up, `+1` = down. |
| `stopVertical()` / `isVerticalActive()` | |
| `update()` | **Call from `loop()`.** Returns `true` if anything changed. |

### Vertical animations: the 8-glyph CGRAM budget

The M66004 controller has 8 user-programmable glyph slots. The vertical
animations build pixel-shifted glyphs in those slots every frame. This
means:

- **8-digit module**: every position can be a different character — full flexibility.
- **16-digit module**: at most **8 distinct characters** can be shown at once. Repeated characters share a slot (so `"00:00:00"` works because there are only 3 distinct chars). If your text exceeds the budget, `printVerticalIn` / `scrollVertical` returns `false` and you should fall back to the horizontal `printScroll` for that message.

After `printVerticalIn` finishes its drop-in, the text is committed as
plain CGROM glyphs and CGRAM is freed for the next animation.

## Examples

Four sketches ship with the library:

| Example | Demonstrates |
|---------|-------------|
| `Basic` | Drop-in equivalent of the original sketch — `print`, `writeChar`, brightness |
| `NonBlockingScroll` | Long horizontal scroll while an LED blinks independently |
| `MultiTask` | Phase-based UI: scroll banner → blink readout, no `delay()` anywhere |
| `VerticalScroll` | All three vertical effects: drop-in → hold → drop-out → endless band |

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

MIT, same as the original project.
