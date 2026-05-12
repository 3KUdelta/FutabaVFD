# FutabaVFD

Arduino library for Futaba 8-digit and 16-digit VFD modules (M66004 controller, e.g. 8-MD-06INKM) on ESP32 via SPI.

Supports non-blocking horizontal scroll, split-flap single-digit animation, full-display flipIn/flipOut, and continuous vertical band — all driven from `loop()` without `delay()`.

**Author:** Marc Staehli 
**License:** MIT  
**Version:** 3.0 (2026)

---

## Hardware

| Signal | ESP32 pin | Notes |
|--------|-----------|-------|
| MOSI   | 23        | |
| SCK    | 18        | |
| CS     | 5         | active LOW |
| MISO   | 19        | 16-digit only |
| RESET  | 19        | 8-digit only (same physical pin as MISO — different display) |

Both modules use the same `begin()` call:
```cpp
vfd.begin(/*SCLK*/ 18, /*MISO*/ 19, /*MOSI*/ 23, /*spiHz*/ 500000);
```
500 kHz SPI is recommended when using vertical animations.

---

## Installation

Copy into your Arduino libraries folder:
```
FutabaVFD/
  FutabaVFD.h
  FutabaVFD.cpp
  FutabaVFD_Font5x7.h
  examples/
    APITest/APITest.ino
    ClockDemo/ClockDemo.ino
    FontTest/FontTest.ino
```

---

## Quick start

```cpp
#include <FutabaVFD.h>

FutabaVFD vfd(16, /*CS*/ 5, /*RESET*/ -1);   // 16-digit
// FutabaVFD vfd(8, /*CS*/ 5, /*RESET*/ 19); // 8-digit

void setup() {
  vfd.begin(18, 19, 23, 500000);
  vfd.setBrightness(120);
  vfd.print("Hello!");
}

void loop() {
  vfd.update();   // must be called from loop()
}
```

---

## API reference

### Lifecycle

```cpp
FutabaVFD(uint8_t digits = 16, int8_t pinCS = SS,
           int8_t pinReset = -1, int spiBus = VSPI);

bool begin(int8_t sclk, int8_t miso, int8_t mosi,
           uint32_t spiHz = 100000, bool selfTest = false);
void end();
bool update();   // call every loop() iteration; returns true if display changed
```

`selfTest` counts 9→0 on all digits to verify wiring. Uses `delay()` internally — enable only during development.

---

### Direct writes

```cpp
void    print(const char* s);                    // write at col 0, right-pad
void    writeChar(uint8_t col, char c);           // single character at column
void    writeString(uint8_t col, const char* s);  // write from col, truncate
void    clear();                                  // fill display with spaces
void    show();                                   // re-send display-lights-on
void    setBrightness(uint8_t level);             // 0 (dim) .. 255 (full)
void    standby(bool on);                         // power-save; DCRAM preserved
uint8_t digits() const;                           // returns 8 or 16
```

UTF-8 German umlauts (Ä Ö Ü ä ö ü) and degree sign (°) are translated automatically.

---

### Horizontal scroll

```cpp
// Classic: text starts left-aligned, holds for holdMs, then scrolls left. Loops.
void scroll(const char* s, uint16_t speedMs = 200, uint16_t holdMs = 500);

// Ticker: enters from right edge, scrolls through continuously. Loops.
void ticker(const char* s, uint16_t speedMs = 150);

void stopScroll();
bool isScrolling() const;
```

---

### Blink

```cpp
void blink(uint16_t periodMs);   // toggle display on/off every periodMs
void stopBlink();
bool isBlinking() const;
```

---

### Vertical: full display

```cpp
// All characters in s drop in from above simultaneously.
// Returns false if s has more than 8 distinct characters (CGRAM limit).
bool flipIn(const char* s, uint16_t totalMs = 400);

// Current display slides out downward. Reads from internal shadow buffer.
bool flipOut(uint16_t totalMs = 400);
```

After `flipIn` completes, the result is committed as plain CGROM and CGRAM is freed.

---

### Vertical: single digit — split-flap

```cpp
// Old character at col slides down; new one slides in from above.
// All other columns are completely unaffected.
// Always uses exactly 1 CGRAM slot — never exceeds the 8-slot budget.
void flip(uint8_t col, char newChar, uint16_t totalMs = 250);
```

---

### Vertical: endless rotating band

```cpp
bool band(const char* s, uint16_t speedMs = 80, Direction dir = UP);
// dir: UP or DOWN

void stopVertical();
bool isAnimating() const;   // true if scroll or any vertical animation is active
```

---

### Font

Two built-in 5×7 pixel fonts are stored in flash (`PROGMEM`), one per display type. Selected automatically from the `digits` constructor parameter.

| Array | Display |
|-------|---------|
| `kAscii16[95][5]` | 16-digit |
| `kAscii8[95][5]`  | 8-digit  |

Coverage: ASCII 0x20–0x7E + Ä Ö Ü ä ö ü °. Both tables compile in at ~1900 bytes total.

---

### CGRAM budget

The M66004 has 8 CGRAM slots. `flipIn` and `band` need one slot per **distinct** character in the string.

| Situation | Slots used |
|-----------|-----------|
| `flip()` single digit | always 1 — always safe |
| `flipIn("12:34:56")` | 6 (1 2 3 4 5 6) — ok |
| `flipIn("AAABBBCCC")` | 3 (A B C) — ok |
| `flipIn("abcdefghi")` | 9 — **fails**, returns false |

If `flipIn` returns `false`, reduce distinct characters or use `scroll()`.

---

## Examples

### APITest

Sequential test of every API call. Select display type at the top of the file with a single `#define`.

### ClockDemo

`hh:mm:ss` clock using `flip()` for each digit change — only digits that actually change are animated. Works on both 8-digit and 16-digit displays. Select at top with `#define`.

### FontTest

Cycles through all 62 printable characters (0–9, A–Z, a–z) using `flipIn` and `flipOut`. Select display at top with `#define`.

---

## Notes

- `update()` must be called from `loop()` as often as possible. Never use `delay()` in your main loop when animation is active.
- The shadow buffer tracks DCRAM content internally. All direct writes update the shadow automatically, so `flipOut` and `flip` always know the current display state without DCRAM readback (the M66004 is write-only).
- `flip()` is always safe regardless of how many distinct characters are on the display.
