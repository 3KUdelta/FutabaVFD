/*
  FutabaVFD.cpp - implementation. See FutabaVFD.h for API docs.
*/

#include "FutabaVFD.h"

// SPI command bytes used by the M66004 / Futaba 8-MD-06INKM controller.
// (Same opcodes the original vfd_controls.h used.)
static const uint8_t CMD_DCRAM_BASE   = 0x20; // 0x20 + col -> write DCRAM at col
static const uint8_t CMD_SET_DIGITS   = 0xE0; // followed by N-1
static const uint8_t CMD_BRIGHTNESS   = 0xE4; // followed by 0..255
static const uint8_t CMD_RUN          = 0xEC; // leave standby
static const uint8_t CMD_STANDBY      = 0xED; // enter standby
static const uint8_t CMD_REFRESH      = 0xE8; // commit DCRAM to grid

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------
FutabaVFD::FutabaVFD(uint8_t digits, int8_t pinCS, int8_t pinReset, int spiBus)
: _digits(digits == 16 ? 16 : 8),
  _pinCS(pinCS),
  _pinReset(pinReset),
  _spiBusId(spiBus),
  _spi(nullptr),
  _spiSettings(FUTABA_VFD_DEFAULT_SPI_HZ, LSBFIRST, SPI_MODE3),
  _initialised(false),
  _scrollActive(false), _scrollLoop(false), _scrollStepMs(0),
  _scrollLastMs(0), _scrollPos(0), _scrollLen(0),
  _blinkActive(false), _blinkPhaseOn(true),
  _blinkPeriodMs(0), _blinkLastMs(0)
{
}

FutabaVFD::~FutabaVFD() {
  end();
}

// ---------------------------------------------------------------------------
//  begin / end
// ---------------------------------------------------------------------------
bool FutabaVFD::begin(int8_t sclk, int8_t miso, int8_t mosi,
                      uint32_t spiHz, bool runSelfTest) {
  if (_initialised) return true;

  _spiSettings = SPISettings(spiHz, LSBFIRST, SPI_MODE3);

  pinMode(_pinCS, OUTPUT);
  digitalWrite(_pinCS, HIGH);

  if (_pinReset >= 0) {
    pinMode(_pinReset, OUTPUT);
    digitalWrite(_pinReset, HIGH);   // 8-bit module needs RESET held high
  }

#if defined(ESP32)
  _spi = new SPIClass(_spiBusId);
  _spi->begin(sclk, miso, mosi, _pinCS);
#else
  _spi = &SPI;
  _spi->begin();
#endif

  // Tell the controller how many digits we drive
  beginTxn();
  selectLow();
  tx(CMD_SET_DIGITS);
  tx(_digits == 16 ? 0x0F : 0x07);
  selectHigh();
  endTxn();

  setBrightness(50);
  standby(false);
  clear();
  show();

  _initialised = true;

  // Optional non-essential visual self-test. Off by default because it
  // contains short delay() calls (8 ms per digit).
  if (runSelfTest) {
    for (int i = 9; i >= 0; --i) {
      for (uint8_t col = 0; col < _digits; ++col) {
        writeChar(col, '0' + i);
        delay(8);
      }
    }
    clear();
  }

  return true;
}

void FutabaVFD::end() {
  if (!_initialised) return;
  stopScroll();
  stopBlink();
  clear();
#if defined(ESP32)
  if (_spi) {
    _spi->end();
    delete _spi;
    _spi = nullptr;
  }
#endif
  _initialised = false;
}

// ---------------------------------------------------------------------------
//  Low-level SPI helpers
// ---------------------------------------------------------------------------
void FutabaVFD::beginTxn()   { _spi->beginTransaction(_spiSettings); }
void FutabaVFD::endTxn()     { _spi->endTransaction(); }
void FutabaVFD::tx(uint8_t b){ _spi->transfer(b); }
void FutabaVFD::selectLow()  { digitalWrite(_pinCS, LOW); }
void FutabaVFD::selectHigh() { digitalWrite(_pinCS, HIGH); }

// ---------------------------------------------------------------------------
//  Direct commands
// ---------------------------------------------------------------------------
void FutabaVFD::setBrightness(uint8_t level) {
  if (!_initialised) return;
  beginTxn(); selectLow();
  tx(CMD_BRIGHTNESS);
  tx(level);
  selectHigh(); endTxn();
}

void FutabaVFD::clear() {
  if (!_initialised) return;
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE);                   // address column 0
  for (uint8_t i = 0; i < _digits; ++i) tx(0x20);  // space
  tx(CMD_REFRESH);
  selectHigh(); endTxn();
}

void FutabaVFD::show() {
  if (!_initialised) return;
  beginTxn(); selectLow();
  tx(CMD_REFRESH);
  selectHigh(); endTxn();
}

void FutabaVFD::standby(bool on) {
  if (!_initialised) return;
  beginTxn(); selectLow();
  tx(on ? CMD_STANDBY : CMD_RUN);
  selectHigh(); endTxn();
}

void FutabaVFD::writeChar(uint8_t x, uint8_t c) {
  if (!_initialised) return;
  if (x >= _digits) return;
  _scrollActive = false;
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE + x);
  tx(c);
  selectHigh(); endTxn();
}

void FutabaVFD::writeString(uint8_t x, const char* s) {
  if (!_initialised || s == nullptr) return;
  if (x >= _digits) return;
  _scrollActive = false;

  uint8_t buf[FUTABA_VFD_SCROLL_BUF];
  size_t  n = translateUtf8(s, buf, sizeof(buf));

  uint8_t visible = _digits - x;
  if (n > visible) n = visible;        // truncate; use printScroll() to scroll

  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE + x);
  for (size_t i = 0; i < n; ++i)            tx(buf[i]);
  for (size_t i = n; i < visible; ++i)      tx(0x20);   // pad with blanks
  selectHigh(); endTxn();
}

void FutabaVFD::writeString(uint8_t x, const String& s) {
  writeString(x, s.c_str());
}

void FutabaVFD::print(const char* s)        { writeString(0, s); }
void FutabaVFD::print(const String& s)      { writeString(0, s.c_str()); }

// ---------------------------------------------------------------------------
//  Non-blocking scroll
// ---------------------------------------------------------------------------
void FutabaVFD::printScroll(const char* s, uint16_t stepMs, bool loop_) {
  if (!_initialised || s == nullptr) return;

  // Translate UTF-8 source into our scroll buffer once. Subsequent update()
  // calls just slice a window out of it - no allocation per step.
  _scrollLen    = translateUtf8(s, _scrollBuf, sizeof(_scrollBuf));
  _scrollPos    = 0;
  _scrollStepMs = (stepMs == 0) ? 1 : stepMs;
  _scrollLoop   = loop_;
  _scrollLastMs = millis();
  _scrollActive = (_scrollLen > 0);

  // Render initial frame so the user sees something immediately.
  renderScrollWindow(0);

  // If the message fits entirely on the display, no scrolling needed.
  if (_scrollLen <= _digits) _scrollActive = false;
}

void FutabaVFD::printScroll(const String& s, uint16_t stepMs, bool loop_) {
  printScroll(s.c_str(), stepMs, loop_);
}

void FutabaVFD::stopScroll() {
  _scrollActive = false;
}

void FutabaVFD::renderScrollWindow(uint16_t offset) {
  // Send one frame: _digits chars taken from _scrollBuf starting at offset.
  // Out-of-range positions are padded with spaces.
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE);
  for (uint8_t i = 0; i < _digits; ++i) {
    size_t idx = (size_t)offset + i;
    tx(idx < _scrollLen ? _scrollBuf[idx] : 0x20);
  }
  selectHigh(); endTxn();
}

void FutabaVFD::advanceScroll() {
  // Move window one step to the right (i.e. text moves left).
  ++_scrollPos;

  // Have we scrolled past the end? End-of-message is when the last char of
  // the message has just left the visible area.
  if ((size_t)_scrollPos + _digits > _scrollLen + _digits) {
    if (_scrollLoop) {
      _scrollPos = 0;
    } else {
      _scrollActive = false;
      return;
    }
  }

  renderScrollWindow(_scrollPos);
}

// ---------------------------------------------------------------------------
//  Non-blocking blink
// ---------------------------------------------------------------------------
void FutabaVFD::blink(uint16_t periodMs) {
  if (periodMs == 0) { stopBlink(); return; }
  _blinkPeriodMs = periodMs;
  _blinkActive   = true;
  _blinkPhaseOn  = true;
  _blinkLastMs   = millis();
  standby(false);
}

void FutabaVFD::stopBlink() {
  if (!_blinkActive) return;
  _blinkActive = false;
  standby(false);   // make sure we leave the display visible
}

// ---------------------------------------------------------------------------
//  update() - the heart of the non-blocking design
// ---------------------------------------------------------------------------
bool FutabaVFD::update() {
  if (!_initialised) return false;
  bool changed = false;
  uint32_t now = millis();

  // Scroll
  if (_scrollActive && (uint32_t)(now - _scrollLastMs) >= _scrollStepMs) {
    _scrollLastMs = now;
    advanceScroll();
    changed = true;
  }

  // Blink
  if (_blinkActive && (uint32_t)(now - _blinkLastMs) >= _blinkPeriodMs) {
    _blinkLastMs   = now;
    _blinkPhaseOn  = !_blinkPhaseOn;
    standby(!_blinkPhaseOn);    // standby=true hides the display
    changed = true;
  }

  return changed;
}

// ---------------------------------------------------------------------------
//  UTF-8 -> Futaba codepage translation
//  Mirrors the original translateSpecialChars() but works on byte buffers
//  so we can avoid the String class on the hot path.
// ---------------------------------------------------------------------------
size_t FutabaVFD::translateUtf8(const char* src, uint8_t* dst, size_t dstCap) const {
  if (!src || !dst || dstCap == 0) return 0;
  size_t out = 0;
  const bool is16 = (_digits == 16);

  for (size_t i = 0; src[i] != '\0' && out < dstCap; ) {
    uint8_t b = (uint8_t)src[i];

    // ASCII fast path
    if (b < 0x80) {
      dst[out++] = b;
      ++i;
      continue;
    }

    // 2-byte UTF-8 sequence?
    uint8_t b2 = (uint8_t)src[i + 1];

    if (b == 0xC3 && b2 != 0) {            // Latin Extended (umlauts live here)
      uint8_t mapped = 0;
      if (is16) {
        switch (b2) {
          case 0x84: mapped = 0xC4; break; // Ä
          case 0xA4: mapped = 0xE4; break; // ä
          case 0x96: mapped = 0xD6; break; // Ö
          case 0xB6: mapped = 0xF6; break; // ö
          case 0x9C: mapped = 0xDC; break; // Ü
          case 0xBC: mapped = 0xFC; break; // ü
        }
      } else {
        switch (b2) {
          case 0x84: mapped = 0x8E; break; // Ä  (CP437-ish on the 8-digit unit)
          case 0xA4: mapped = 0x84; break; // ä
          case 0x96: mapped = 0x99; break; // Ö
          case 0xB6: mapped = 0x94; break; // ö
          case 0x9C: mapped = 0x9A; break; // Ü
          case 0xBC: mapped = 0x81; break; // ü
        }
      }
      if (mapped) { dst[out++] = mapped; i += 2; continue; }
      // Unknown 0xC3xx: emit '?'
      dst[out++] = '?'; i += 2; continue;
    }

    if (b == 0xC2 && b2 == 0xB0) {         // ° (degree sign)
      dst[out++] = is16 ? 0xB0 : 0xEF;
      i += 2; continue;
    }

    // Anything else multibyte we don't handle: emit '?' and skip the
    // continuation byte(s).
    dst[out++] = '?';
    if      ((b & 0xE0) == 0xC0) i += 2;   // 2-byte
    else if ((b & 0xF0) == 0xE0) i += 3;   // 3-byte
    else if ((b & 0xF8) == 0xF0) i += 4;   // 4-byte
    else                          i += 1;
  }

  return out;
}
