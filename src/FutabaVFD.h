/*
  FutabaVFD.h - Non-blocking Arduino library for Futaba 8/16-digit
  Vacuum Fluorescent Displays (e.g. 8-MD-06INKM) on ESP32 via SPI.

  Refactor of vfd_controls.h originally by Marc Staehli (2022).
  Architecture: class-based, instance-owned SPI bus, non-blocking scroll
  and blink driven by an update() pump in loop().

  Pin defaults match the original sketch:
    DIN  = 23 (MOSI), CLK = 18 (SCK), CS = 5 (SS), RESET = 19 (8-bit only)

  v2.0.1 - 2025
    - Fixed: _initialised guard prevented setBrightness/standby/clear/show
      from executing during begin(), leaving the display dark after power-cycle.
    - Fixed: SPI bus default fallback now uses SPI3_HOST on ESP32 Arduino
      Core 3.x where the VSPI macro is no longer defined.

  License: MIT
*/

#ifndef FUTABA_VFD_H
#define FUTABA_VFD_H

#include <Arduino.h>
#include <SPI.h>

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------

// Default SPI clock for the display. The original driver used 100 kHz which
// is conservative; the M66004 / 6206 controllers happily go higher.
#ifndef FUTABA_VFD_DEFAULT_SPI_HZ
  #define FUTABA_VFD_DEFAULT_SPI_HZ 100000UL
#endif

// Maximum supported width. The hardware tops out at 16 digits. The internal
// scroll buffer is sized to hold any reasonable user message.
#ifndef FUTABA_VFD_SCROLL_BUF
  #define FUTABA_VFD_SCROLL_BUF 128
#endif

// SPI bus selector for ESP32. VSPI is the default of the original code.
// On ESP32-S2/S3/C3 the macro is named differently; we accept any int.
#ifndef FUTABA_VFD_DEFAULT_BUS
  #if defined(VSPI)
    #define FUTABA_VFD_DEFAULT_BUS VSPI              // Core 2.x / Core 3.x classic ESP32
  #elif defined(SPI3_HOST)
    #define FUTABA_VFD_DEFAULT_BUS SPI3_HOST          // Core 3.x without VSPI macro (S2/S3/C3)
  #else
    #define FUTABA_VFD_DEFAULT_BUS 0                   // non-ESP32 fallback
  #endif
#endif

class FutabaVFD {
public:
  // ---------------------------------------------------------------------
  //  Construction
  // ---------------------------------------------------------------------
  // digits   : 8 or 16
  // pinCS    : chip-select (SS) pin
  // pinReset : RESET pin (only required by 8-digit module). Pass -1 if unused.
  // spiBus   : VSPI / HSPI selector (ESP32 only). Defaults to VSPI.
  FutabaVFD(uint8_t digits = 16,
            int8_t  pinCS    = SS,
            int8_t  pinReset = -1,
            int     spiBus   = FUTABA_VFD_DEFAULT_BUS);

  ~FutabaVFD();

  // ---------------------------------------------------------------------
  //  Lifecycle
  // ---------------------------------------------------------------------
  // Initialises SPI, configures the controller, optionally runs a quick
  // visual self-test of all digits.
  // sclk/miso/mosi default to -1 = use board defaults of the chosen bus.
  // The self-test is OFF by default to keep begin() non-blocking.
  bool begin(int8_t sclk        = -1,
             int8_t miso        = -1,
             int8_t mosi        = -1,
             uint32_t spiHz     = FUTABA_VFD_DEFAULT_SPI_HZ,
             bool runSelfTest   = false);

  void end();

  // ---------------------------------------------------------------------
  //  Pump – call from loop() as often as possible.
  //  Returns true if internal state changed (e.g. a scroll step occurred).
  // ---------------------------------------------------------------------
  bool update();

  // ---------------------------------------------------------------------
  //  Direct (non-blocking, no animation) commands
  //  These do NOT call delay(). They send one SPI transaction and return.
  // ---------------------------------------------------------------------
  void setBrightness(uint8_t level);   // 0..255
  void clear();
  void show();                         // refresh / commit DCRAM
  void standby(bool on);

  // Write one ASCII char at column x (0-based). Cancels any active scroll.
  void writeChar(uint8_t x, uint8_t c);

  // Write a string starting at column x. If the string is longer than the
  // visible width it is *truncated* (no scroll). Use printScroll() to scroll.
  // Cancels any active scroll.
  void writeString(uint8_t x, const String& s);
  void writeString(uint8_t x, const char* s);

  // Convenience: clear + writeString(0, s)
  void print(const String& s);
  void print(const char* s);

  // ---------------------------------------------------------------------
  //  Non-blocking animations
  //  These start an animation; subsequent update() calls advance it.
  // ---------------------------------------------------------------------

  // Scroll a long string from right to left at the given step interval (ms).
  // holdMs is the initial pause before scrolling starts (default 500ms),
  // giving the reader time to see the beginning of the message.
  // If enterRight is true, the text enters from the right edge of the
  // display (like a news ticker) — holdMs is ignored in this mode.
  // If loop_ is true the message restarts after the last char leaves the
  // visible area. While scrolling, isScrolling() returns true.
  void printScroll(const String& s, uint16_t stepMs = 250, bool loop_ = true,
                   uint16_t holdMs = 500, bool enterRight = false);
  void printScroll(const char* s,   uint16_t stepMs = 250, bool loop_ = true,
                   uint16_t holdMs = 500, bool enterRight = false);

  // Stop any running scroll animation. Display content is left as-is.
  void stopScroll();
  bool isScrolling() const { return _scrollActive; }

  // Blink the entire display (toggles standby on/off) every periodMs.
  // Pass 0 to disable.
  void blink(uint16_t periodMs);
  void stopBlink();
  bool isBlinking() const { return _blinkActive; }

  // ---------------------------------------------------------------------
  //  Pixel-accurate VERTICAL animations (uses CGRAM)
  //
  //  These methods build per-frame glyphs in CGRAM slots 0..7 to create
  //  the illusion of pixels falling/rising. They share the 8-slot CGRAM
  //  budget, so on the 16-digit module the message can show at most 8
  //  *distinct* characters at once. Repeated characters share a slot.
  //  If more than 8 distinct chars are required, the call returns false
  //  and nothing is animated.
  //
  //  Direction of motion (in/out/scroll) is independent of horizontal
  //  scroll and runs through the same update() pump.
  //  Cancels any active horizontal scroll.
  // ---------------------------------------------------------------------

  // Drop characters in from above. Each column starts entirely above
  // the visible area and slides down to its final position.
  // totalMs is the wall-clock time the whole animation takes.
  // After it finishes, the result is committed as normal CGROM glyphs
  // and CGRAM is freed.
  bool printVerticalIn (const char* s, uint16_t totalMs = 500);
  bool printVerticalIn (const String& s, uint16_t totalMs = 500);

  // Drop the currently-displayed text out the bottom. After this finishes
  // the display is blank.
  bool printVerticalOut(uint16_t totalMs = 500);

  // Endless vertical scroll: a stream of characters wraps continuously
  // upward (or downward). stepMs = ms per pixel-row of motion.
  // direction: -1 = up, +1 = down.
  // Subject to the 8-distinct-glyphs-per-frame rule.
  bool scrollVertical(const char* s, uint16_t stepMs = 80,
                      int8_t direction = -1, bool loop_ = true);
  bool scrollVertical(const String& s, uint16_t stepMs = 80,
                      int8_t direction = -1, bool loop_ = true);

  void stopVertical();
  bool isVerticalActive() const { return _vMode != V_IDLE; }

  // ---------------------------------------------------------------------
  //  Introspection
  // ---------------------------------------------------------------------
  uint8_t digits() const { return _digits; }

private:
  // ---- low-level SPI helpers ------------------------------------------
  void   beginTxn();
  void   endTxn();
  void   tx(uint8_t b);
  void   selectLow();
  void   selectHigh();

  // ---- scrolling state machine ----------------------------------------
  void   advanceScroll();
  void   renderScrollWindow(int16_t offset);

  // ---- charset translation (UTF-8 -> Futaba codes) --------------------
  // Translates UTF-8 input into the device-specific 8-bit codepage in place
  // of the destination buffer. Returns the resulting byte count.
  size_t translateUtf8(const char* src, uint8_t* dst, size_t dstCap) const;

  // ---- vertical animation engine --------------------------------------
  enum VMode : uint8_t { V_IDLE = 0, V_IN, V_OUT, V_SCROLL };

  // Set up CGRAM slot allocation for one frame. `codes` are the device
  // codepoints visible in this frame (length = numVisible). On success
  // returns true and fills outSlot[i] with the slot index 0..7 to use
  // for codes[i]. Frees-and-rebuilds the slot table from scratch.
  // Returns false if more than 8 distinct codes are needed.
  bool   buildSlotTable(const uint8_t* codes, uint8_t numVisible,
                        uint8_t outSlot[]);

  // Upload one 5-byte glyph into the given CGRAM slot.
  void   writeCgramSlot(uint8_t slot, const uint8_t glyph[5]);

  // Render one vertical-animation frame. `pixelOffset` is the per-pixel
  // shift in [-7..+7]; positive = text shifted UP (used during OUT and
  // upward SCROLL), negative = text shifted DOWN (used during IN and
  // downward SCROLL). For SCROLL, the offset wraps.
  void   renderVerticalFrame(int8_t pixelOffset);

  // Commit current text as plain CGROM codes (after IN finishes), so
  // CGRAM slots are free again for other animations.
  void   commitVerticalAsCgrom();

  // ---- members --------------------------------------------------------
  uint8_t      _digits;
  int8_t       _pinCS;
  int8_t       _pinReset;
  int          _spiBusId;
  SPIClass*    _spi;
  SPISettings  _spiSettings;
  bool         _initialised;

  // scroll
  bool         _scrollActive;
  bool         _scrollLoop;
  bool         _scrollHolding;       // true = in initial hold phase before scrolling
  bool         _scrollEnterRight;    // true = text enters from right edge (ticker style)
  uint16_t     _scrollStepMs;
  uint16_t     _scrollHoldMs;        // initial pause before first scroll step
  uint32_t     _scrollLastMs;
  int16_t      _scrollPos;          // current left-edge offset into _scrollBuf (negative = text still off-screen right)
  size_t       _scrollLen;          // valid bytes in _scrollBuf
  uint8_t      _scrollBuf[FUTABA_VFD_SCROLL_BUF];

  // blink
  bool         _blinkActive;
  bool         _blinkPhaseOn;       // true = display visible
  uint16_t     _blinkPeriodMs;
  uint32_t     _blinkLastMs;

  // vertical animation
  VMode        _vMode;
  uint16_t     _vStepMs;            // ms per pixel-row step
  uint32_t     _vLastMs;
  int16_t      _vStep;              // current frame index (counts pixel rows)
  int16_t      _vTotalSteps;        // total steps for IN/OUT (8 = 7 pixels + settle)
  int8_t       _vDirection;         // for SCROLL: -1 up, +1 down
  bool         _vLoop;              // for SCROLL
  uint8_t      _vCodes[16];         // device codepoints currently being animated
  uint8_t      _vCodesLen;          // visible length (clipped to _digits)
  uint8_t      _vSlotMap[16];       // slot index per visible position
  uint8_t      _vSlotCodes[8];      // which code lives in each CGRAM slot (0xFF = unused)
};

#endif // FUTABA_VFD_H
