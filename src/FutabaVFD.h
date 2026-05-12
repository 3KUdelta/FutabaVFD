/*
  FutabaVFD.h  –  v3.0
  Non-blocking Arduino library for Futaba 8/16-digit VFD modules
  (e.g. 8-MD-06INKM) on ESP32 via SPI.

  Author: Marc Staehli (hifilabor.ch), 2022-2025.

  Pin defaults (unchanged from original vfd_controls.h):
    MOSI = 23, SCK = 18, CS = 5, RESET = 19 (8-digit only)

  License: MIT
*/

#ifndef FUTABA_VFD_H
#define FUTABA_VFD_H

#include <Arduino.h>
#include <SPI.h>

// ---------------------------------------------------------------------------
//  Build-time configuration  (override before #include if needed)
// ---------------------------------------------------------------------------
#ifndef FUTABA_VFD_DEFAULT_SPI_HZ
  #define FUTABA_VFD_DEFAULT_SPI_HZ 100000UL
#endif
#ifndef FUTABA_VFD_SCROLL_BUF
  #define FUTABA_VFD_SCROLL_BUF 128
#endif
#ifndef FUTABA_VFD_DEFAULT_BUS
  #if defined(VSPI)
    #define FUTABA_VFD_DEFAULT_BUS VSPI
  #elif defined(SPI3_HOST)
    #define FUTABA_VFD_DEFAULT_BUS SPI3_HOST
  #else
    #define FUTABA_VFD_DEFAULT_BUS 0
  #endif
#endif

// ---------------------------------------------------------------------------
//  Direction constant for band()
// ---------------------------------------------------------------------------
enum Direction : int8_t { UP = -1, DOWN = 1 };

// ---------------------------------------------------------------------------
//  FutabaVFD class
// ---------------------------------------------------------------------------
class FutabaVFD {
public:

  // ── Construction ──────────────────────────────────────────────────────────
  // digits  : 8 or 16
  // pinCS   : chip-select pin (active LOW)
  // pinReset: RESET pin for 8-digit module; pass -1 if hard-wired HIGH
  // spiBus  : VSPI / HSPI / SPI3_HOST
  FutabaVFD(uint8_t  digits   = 16,
            int8_t   pinCS    = SS,
            int8_t   pinReset = -1,
            int      spiBus   = FUTABA_VFD_DEFAULT_BUS);
  ~FutabaVFD();

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  // Call SPI.begin() first internally, then configure the controller.
  // spiHz 500000 recommended when using vertical animations.
  // selfTest: counts 9→0 on all digits to verify wiring (uses delay()).
  bool    begin(int8_t   sclk       = -1,
                int8_t   miso       = -1,
                int8_t   mosi       = -1,
                uint32_t spiHz      = FUTABA_VFD_DEFAULT_SPI_HZ,
                bool     selfTest   = false);
  void    end();

  // ── Pump ──────────────────────────────────────────────────────────────────
  // Call from loop() as often as possible.
  // Returns true if the display state changed this call.
  bool    update();

  // ── Direct writes (non-blocking, one SPI transaction each) ────────────────
  void    print(const char*   s);           // write at col 0, pad right
  void    print(const String& s);
  void    writeChar(uint8_t col, char c);   // single character at column
  void    writeString(uint8_t col, const char*   s);  // truncates to width
  void    writeString(uint8_t col, const String& s);
  void    clear();                          // fill display with spaces
  void    show();                           // re-send display-lights-on cmd
  void    setBrightness(uint8_t level);     // 0..255
  void    standby(bool on);                 // power-save; DCRAM preserved
  uint8_t digits() const { return _digits; }

  // ── Horizontal scroll ─────────────────────────────────────────────────────
  // scroll(): classic mode. Text starts left-aligned, pauses for holdMs,
  //           then scrolls left. On each loop restart the hold re-applies.
  void    scroll(const char*   s, uint16_t speedMs = 200, uint16_t holdMs = 500);
  void    scroll(const String& s, uint16_t speedMs = 200, uint16_t holdMs = 500);

  // ticker(): text enters from the right edge and scrolls through
  //           continuously like a news ticker. No initial pause. Loops.
  void    ticker(const char*   s, uint16_t speedMs = 150);
  void    ticker(const String& s, uint16_t speedMs = 150);

  void    stopScroll();
  bool    isScrolling() const { return _scrollActive; }

  // ── Blink ─────────────────────────────────────────────────────────────────
  void    blink(uint16_t periodMs);         // toggle standby every periodMs
  void    stopBlink();
  bool    isBlinking() const { return _blinkActive; }

  // ── Vertical: full display ────────────────────────────────────────────────
  // flipIn(): all characters in `s` drop in from above simultaneously.
  //   Returns false if the text needs more than 8 distinct CGRAM slots
  //   (only possible on 16-digit with >8 distinct chars — use fewer chars
  //   or fall back to scroll()).
  // After completion the result is committed as plain CGROM and CGRAM freed.
  bool    flipIn(const char*   s, uint16_t totalMs = 400);
  bool    flipIn(const String& s, uint16_t totalMs = 400);

  // flipOut(): current display content drops out the bottom.
  //   Uses the internally tracked shadow buffer — works after any write.
  bool    flipOut(uint16_t totalMs = 400);

  // ── Vertical: single digit — split-flap effect ───────────────────────────
  // flip(): the old character at `col` slides downward while the new
  //   character slides in from above, both visible simultaneously.
  //   All other columns remain completely unchanged (no flicker).
  //   Always uses exactly 2 CGRAM slots; never exceeds the budget.
  void    flip(uint8_t col, char newChar, uint16_t totalMs = 250);

  // ── Vertical: endless rotating band ──────────────────────────────────────
  // band(): characters rotate continuously upward (UP) or downward (DOWN).
  //   Returns false if >8 distinct chars.
  bool    band(const char*   s, uint16_t speedMs = 80, Direction dir = UP);
  bool    band(const String& s, uint16_t speedMs = 80, Direction dir = UP);

  // ── Vertical: stop / status ───────────────────────────────────────────────
  void    stopVertical();
  bool    isAnimating() const;              // true if scroll OR vertical active

private:
  // ── SPI helpers ───────────────────────────────────────────────────────────
  void    beginTxn();
  void    endTxn();
  void    tx(uint8_t b);
  void    selectLow();
  void    selectHigh();

  // ── Internal write (updates shadow buffer) ────────────────────────────────
  void    dcramWrite(uint8_t col, uint8_t code);   // single position
  void    dcramWriteAll(const uint8_t* codes, uint8_t n);  // n positions from col 0

  // ── Horizontal scroll internals ───────────────────────────────────────────
  void    advanceScroll();
  void    renderScrollWindow(int16_t offset);

  // ── UTF-8 → device codepage ───────────────────────────────────────────────
  size_t  translateUtf8(const char* src, uint8_t* dst, size_t cap) const;
  uint8_t translateChar(char c) const;     // single-char convenience

  // ── CGRAM slot management ─────────────────────────────────────────────────
  bool    buildSlotTable(const uint8_t* codes, uint8_t n, uint8_t outSlot[]);
  void    writeCgramSlot(uint8_t slot, const uint8_t glyph[5]);
  void    uploadAllSlots(const uint8_t slotData[8][5]);

  // ── Vertical rendering ────────────────────────────────────────────────────
  // renderFullFrame: used by flipIn / flipOut / band.
  //   Writes all digit positions. pixelOffset: >0 = shifted UP, <0 = DOWN.
  void    renderFullFrame(int8_t pixelOffset);

  // renderSplitFrame: used by flip().
  //   Writes only _vFlipCol. step 0 = full old, step 7 = full new.
  //   Intermediate steps show old sliding down / new coming from above.
  void    renderSplitFrame(uint8_t step);

  // After flipIn finishes: commit new codes to DCRAM as plain CGROM.
  void    commitAsCgrom();
  // After flip() finishes: commit new char to DCRAM and shadow.
  void    commitCharAsCgrom();

  // ── Glyph shift helpers ───────────────────────────────────────────────────
  // shiftGlyph: shift all columns of a glyph by `offset` pixel rows.
  //   +offset = UP (top rows disappear), -offset = DOWN.
  static void shiftGlyph(const uint8_t in[5], uint8_t out[5], int8_t offset);

  // splitGlyph: combine old and new glyph for split-flap effect.
  //   step 0 = full old, step 7 = full new.
  static void splitGlyph(const uint8_t oldG[5], const uint8_t newG[5],
                          uint8_t step, uint8_t out[5]);

  // ── Members ───────────────────────────────────────────────────────────────
  uint8_t     _digits;
  int8_t      _pinCS;
  int8_t      _pinReset;
  int         _spiBusId;
  SPIClass*   _spi;
  SPISettings _spiSettings;
  bool        _initialised;

  // Shadow buffer — mirrors DCRAM content for split-flap old-value lookup.
  uint8_t     _shadow[16];

  // Scroll
  bool        _scrollActive;
  bool        _scrollLoop;
  bool        _scrollHolding;
  bool        _scrollEnterRight;
  uint16_t    _scrollStepMs;
  uint16_t    _scrollHoldMs;
  uint32_t    _scrollLastMs;
  int16_t     _scrollPos;
  size_t      _scrollLen;
  uint8_t     _scrollBuf[FUTABA_VFD_SCROLL_BUF];

  // Blink
  bool        _blinkActive;
  bool        _blinkPhaseOn;
  uint16_t    _blinkPeriodMs;
  uint32_t    _blinkLastMs;

  // Vertical
  enum VMode : uint8_t { V_IDLE, V_FLIP_IN, V_FLIP_OUT, V_FLIP_CHAR, V_BAND };
  VMode       _vMode;
  uint16_t    _vStepMs;
  uint32_t    _vLastMs;
  uint8_t     _vStep;
  uint8_t     _vTotalSteps;
  Direction   _vDir;
  bool        _vLoop;

  // Full-display vertical (flipIn / flipOut / band)
  uint8_t     _vCodes[16];       // new codes for all positions
  uint8_t     _vCodesLen;
  uint8_t     _vSlotMap[16];     // CGRAM slot index per display position
  uint8_t     _vSlotCodes[8];    // which code lives in each slot

  // Single-digit vertical (flip)
  uint8_t     _vFlipCol;
  uint8_t     _vFlipOldCode;
  uint8_t     _vFlipNewCode;
};

#endif // FUTABA_VFD_H
