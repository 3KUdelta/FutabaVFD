/*
  FutabaVFD.h - Non-blocking Arduino library for Futaba 8/16-digit
  Vacuum Fluorescent Displays (e.g. 8-MD-06INKM) on ESP32 via SPI.

  Refactor of vfd_controls.h originally by Marc Staehli (2022).
  Architecture: class-based, instance-owned SPI bus, non-blocking scroll
  and blink driven by an update() pump in loop().

  Pin defaults match the original sketch:
    DIN  = 23 (MOSI), CLK = 18 (SCK), CS = 5 (SS), RESET = 19 (8-bit only)

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
    #define FUTABA_VFD_DEFAULT_BUS VSPI
  #else
    #define FUTABA_VFD_DEFAULT_BUS 0
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
  // If loop_ is true the message restarts after the last char leaves the
  // visible area. While scrolling, isScrolling() returns true.
  void printScroll(const String& s, uint16_t stepMs = 250, bool loop_ = true);
  void printScroll(const char* s,   uint16_t stepMs = 250, bool loop_ = true);

  // Stop any running scroll animation. Display content is left as-is.
  void stopScroll();
  bool isScrolling() const { return _scrollActive; }

  // Blink the entire display (toggles standby on/off) every periodMs.
  // Pass 0 to disable.
  void blink(uint16_t periodMs);
  void stopBlink();
  bool isBlinking() const { return _blinkActive; }

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
  void   renderScrollWindow(uint16_t offset);

  // ---- charset translation (UTF-8 -> Futaba codes) --------------------
  // Translates UTF-8 input into the device-specific 8-bit codepage in place
  // of the destination buffer. Returns the resulting byte count.
  size_t translateUtf8(const char* src, uint8_t* dst, size_t dstCap) const;

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
  uint16_t     _scrollStepMs;
  uint32_t     _scrollLastMs;
  uint16_t     _scrollPos;          // current left-edge offset into _scrollBuf
  size_t       _scrollLen;          // valid bytes in _scrollBuf
  uint8_t      _scrollBuf[FUTABA_VFD_SCROLL_BUF];

  // blink
  bool         _blinkActive;
  bool         _blinkPhaseOn;       // true = display visible
  uint16_t     _blinkPeriodMs;
  uint32_t     _blinkLastMs;
};

#endif // FUTABA_VFD_H
