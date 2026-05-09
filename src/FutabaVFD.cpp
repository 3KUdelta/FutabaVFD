/*
  FutabaVFD.cpp - implementation. See FutabaVFD.h for API docs.

  v2.0.1 - 2025
    - Fixed: _initialised is now set BEFORE setBrightness/standby/clear/show
      in begin(). Previously these calls were guarded by if(!_initialised)
      and silently returned, leaving the display dark after a cold power-cycle.
    - Re-assert pinMode(CS, OUTPUT) after SPI.begin() for ESP32 Core 3.x
      compatibility.
*/

#include "FutabaVFD.h"

// SPI command bytes used by the M66004 / Futaba 8-MD-06INKM controller.
// (Same opcodes the original vfd_controls.h used.)
static const uint8_t CMD_DCRAM_BASE   = 0x20; // 0x20 + col -> write DCRAM at col
static const uint8_t CMD_SET_DIGITS   = 0xE0; // followed by N-1
static const uint8_t CMD_BRIGHTNESS   = 0xE4; // followed by 0..255
static const uint8_t CMD_RUN          = 0xEC; // leave standby
static const uint8_t CMD_STANDBY      = 0xED; // enter standby
static const uint8_t CMD_REFRESH      = 0xE8; // commit DCRAM to grid / display light ON

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
  _scrollActive(false), _scrollLoop(false), _scrollHolding(false),
  _scrollEnterRight(false),
  _scrollStepMs(0), _scrollHoldMs(0),
  _scrollLastMs(0), _scrollPos(0), _scrollLen(0),
  _blinkActive(false), _blinkPhaseOn(true),
  _blinkPeriodMs(0), _blinkLastMs(0),
  _vMode(V_IDLE), _vStepMs(0), _vLastMs(0),
  _vStep(0), _vTotalSteps(0),
  _vDirection(-1), _vLoop(false),
  _vCodesLen(0)
{
  for (uint8_t i = 0; i < 8; ++i) _vSlotCodes[i] = 0xFF;
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
  // Re-assert CS pin mode after SPI.begin() — on some ESP32 Arduino Core
  // versions, SPI.begin() can reconfigure the SS pin.
  pinMode(_pinCS, OUTPUT);
  digitalWrite(_pinCS, HIGH);
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

  // FIX v2.0.1: Set _initialised = true BEFORE calling setBrightness(),
  // standby(), clear(), show(). These methods all have an early-return
  // guard "if (!_initialised) return;" which previously caused them to
  // silently skip during begin(), leaving the display dark after a cold
  // power-cycle. On a software reset (e.g. after flashing) the display
  // retained its previous state, masking this bug.
  _initialised = true;

  setBrightness(50);
  standby(false);
  clear();
  show();

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
  _vMode = V_IDLE;
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE + x);
  tx(c);
  selectHigh(); endTxn();
}

void FutabaVFD::writeString(uint8_t x, const char* s) {
  if (!_initialised || s == nullptr) return;
  if (x >= _digits) return;
  _scrollActive = false;
  _vMode = V_IDLE;

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
void FutabaVFD::printScroll(const char* s, uint16_t stepMs, bool loop_,
                           uint16_t holdMs, bool enterRight) {
  if (!_initialised || s == nullptr) return;
  _vMode = V_IDLE;

  // Translate UTF-8 source into our scroll buffer once. Subsequent update()
  // calls just slice a window out of it - no allocation per step.
  _scrollLen      = translateUtf8(s, _scrollBuf, sizeof(_scrollBuf));
  _scrollStepMs   = (stepMs == 0) ? 1 : stepMs;
  _scrollLoop     = loop_;
  _scrollLastMs   = millis();
  _scrollEnterRight = enterRight;

  if (enterRight) {
    // Ticker mode: text starts off-screen to the right. _scrollPos begins
    // at -_digits so the first visible character appears at the right edge
    // on the first scroll step. No hold pause in this mode.
    _scrollPos     = -(int16_t)_digits;
    _scrollHoldMs  = 0;
    _scrollHolding = false;
    _scrollActive  = (_scrollLen > 0);
    // Render initial frame (all blanks — text is still off-screen)
    renderScrollWindow(_scrollPos);
  } else {
    // Classic mode: text starts left-aligned with optional hold pause.
    _scrollPos     = 0;
    _scrollHoldMs  = holdMs;
    _scrollHolding = (holdMs > 0 && _scrollLen > _digits);
    _scrollActive  = (_scrollLen > 0);
    // Render initial frame so the user sees something immediately.
    renderScrollWindow(0);
    // If the message fits entirely on the display, no scrolling needed.
    if (_scrollLen <= _digits) _scrollActive = false;
  }
}

void FutabaVFD::printScroll(const String& s, uint16_t stepMs, bool loop_,
                           uint16_t holdMs, bool enterRight) {
  printScroll(s.c_str(), stepMs, loop_, holdMs, enterRight);
}

void FutabaVFD::stopScroll() {
  _scrollActive = false;
}

void FutabaVFD::renderScrollWindow(int16_t offset) {
  // Send one frame: _digits chars taken from _scrollBuf starting at offset.
  // Negative offsets mean the text hasn't entered the display yet (right-enter
  // mode) — those positions are padded with spaces. Same for positions beyond
  // the buffer end.
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE);
  for (uint8_t i = 0; i < _digits; ++i) {
    int16_t idx = offset + (int16_t)i;
    if (idx >= 0 && (size_t)idx < _scrollLen)
      tx(_scrollBuf[idx]);
    else
      tx(0x20);   // space for out-of-range positions
  }
  selectHigh(); endTxn();
}

void FutabaVFD::advanceScroll() {
  // Move window one step to the right (i.e. text moves left).
  ++_scrollPos;

  // End condition: the last character has scrolled off the left edge,
  // i.e. _scrollPos has passed _scrollLen.
  if (_scrollPos > (int16_t)_scrollLen) {
    if (_scrollLoop) {
      if (_scrollEnterRight) {
        // Ticker mode: restart from off-screen right, no hold
        _scrollPos = -(int16_t)_digits;
      } else {
        // Classic mode: restart from position 0 with hold
        _scrollPos = 0;
        if (_scrollHoldMs > 0) {
          _scrollHolding = true;
          _scrollLastMs  = millis();
        }
      }
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
  if (_scrollActive) {
    if (_scrollHolding) {
      // Initial hold: wait holdMs before starting to scroll
      if ((uint32_t)(now - _scrollLastMs) >= _scrollHoldMs) {
        _scrollHolding = false;
        _scrollLastMs  = now;      // reset timer for first scroll step
      }
    } else if ((uint32_t)(now - _scrollLastMs) >= _scrollStepMs) {
      _scrollLastMs = now;
      advanceScroll();
      changed = true;
    }
  }

  // Blink
  if (_blinkActive && (uint32_t)(now - _blinkLastMs) >= _blinkPeriodMs) {
    _blinkLastMs   = now;
    _blinkPhaseOn  = !_blinkPhaseOn;
    standby(!_blinkPhaseOn);    // standby=true hides the display
    changed = true;
  }

  // Vertical animation
  if (_vMode != V_IDLE && (uint32_t)(now - _vLastMs) >= _vStepMs) {
    _vLastMs = now;

    int8_t pixelOffset = 0;

    switch (_vMode) {
      case V_IN:
        // _vStep counts from 0 (text fully above) up to 7 (settled).
        // Pixel offset starts at -7 (off-screen above, i.e. text needs
        // to slide down 7 pixels to arrive). At step 7 offset = 0.
        pixelOffset = (int8_t)(_vStep - 7);   // -7 .. 0
        renderVerticalFrame(pixelOffset);
        ++_vStep;
        if (_vStep > _vTotalSteps) {
          // Animation complete - commit as plain CGROM and free CGRAM
          commitVerticalAsCgrom();
          _vMode = V_IDLE;
        }
        break;

      case V_OUT:
        // Text starts at offset 0, slides DOWN out the bottom: offset
        // grows positively from 0 to 7 (each pixel further down).
        // We drive that as the BUFFER offset; in our convention positive
        // = shift up, so OUT actually wants negative offsets going more
        // negative. Use offset = -_vStep so column>>step pulls top in.
        // Actually simpler: re-render with column << _vStep (down).
        pixelOffset = (int8_t)(_vStep);       // 0 .. 7 (positive = shift DOWN here)
        // We invert the sign convention: positive arg to renderVerticalFrame
        // means UP. For OUT we want DOWN, so pass -offset.
        renderVerticalFrame(-pixelOffset);
        ++_vStep;
        if (_vStep > _vTotalSteps) {
          clear();
          _vMode = V_IDLE;
        }
        break;

      case V_SCROLL:
        // Continuous shift in given direction. _vStep wraps every 7
        // pixel-rows. direction: -1 = up, +1 = down.
        pixelOffset = (int8_t)((_vDirection < 0) ? _vStep : -_vStep);
        renderVerticalFrame(pixelOffset);
        ++_vStep;
        if (_vStep >= 7) {
          if (_vLoop) _vStep = 0;
          else { _vMode = V_IDLE; }
        }
        break;

      default: break;
    }
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

// ===========================================================================
//  Vertical animation engine (CGRAM-based, pixel-accurate)
// ===========================================================================
#include "FutabaVFD_Font5x7.h"

// CGRAM data-write command. Top 3 bits = 010, bottom 3 bits = slot index 0..7.
// (See datasheet section 2.2: "0 1 0 * * Y2 Y1 Y0".)
static const uint8_t CMD_CGRAM_BASE = 0x40;   // 0100 0000

// ---------------------------------------------------------------------------
//  Slot allocation: pick CGRAM slots for the codepoints in this frame.
//  Returns false if more than 8 distinct codepoints would be needed.
// ---------------------------------------------------------------------------
bool FutabaVFD::buildSlotTable(const uint8_t* codes, uint8_t numVisible,
                               uint8_t outSlot[]) {
  uint8_t distinct[8];
  uint8_t numDistinct = 0;

  for (uint8_t i = 0; i < numVisible; ++i) {
    uint8_t c = codes[i];
    int8_t found = -1;
    for (uint8_t k = 0; k < numDistinct; ++k) {
      if (distinct[k] == c) { found = (int8_t)k; break; }
    }
    if (found < 0) {
      if (numDistinct >= 8) return false;     // too many distinct chars
      distinct[numDistinct] = c;
      found = (int8_t)numDistinct;
      ++numDistinct;
    }
    outSlot[i] = (uint8_t)found;
  }

  // Remember which code each slot now holds so renderVerticalFrame()
  // can find them again on subsequent frames.
  for (uint8_t k = 0; k < 8; ++k) {
    _vSlotCodes[k] = (k < numDistinct) ? distinct[k] : 0xFF;
  }
  return true;
}

// ---------------------------------------------------------------------------
//  Upload one 5-byte glyph to CGRAM slot.
//  Datasheet 2.2: 1st byte = 0x40 | slot, then 5 column bytes.
//  Bytes between transactions need t_DOFF >= 2us; SPI bit-banging at
//  100..500 kHz easily exceeds that.
// ---------------------------------------------------------------------------
void FutabaVFD::writeCgramSlot(uint8_t slot, const uint8_t glyph[5]) {
  beginTxn(); selectLow();
  tx(CMD_CGRAM_BASE | (slot & 0x07));
  for (uint8_t i = 0; i < 5; ++i) tx(glyph[i]);
  selectHigh(); endTxn();
}

// ---------------------------------------------------------------------------
//  Vertical pixel shift for a single 5-byte glyph.
//  Convention used here: positive offset = shift UP (pixels move toward
//  the top, bottom rows fill with 0). Negative = shift DOWN.
//  Each column is 7 bits (bit 0 = top, bit 6 = bottom).
// ---------------------------------------------------------------------------
static inline void shiftGlyph(const uint8_t in[5], uint8_t out[5], int8_t offset) {
  if (offset == 0) {
    for (uint8_t i = 0; i < 5; ++i) out[i] = in[i] & 0x7F;
    return;
  }
  if (offset > 0) {
    // shift UP: in column, top bit was bit 0, want it gone -> right-shift in
    // column-bit-space. Since bit 0 = top, shift toward LSB out, equivalent
    // to (col >> offset) on the 7-bit field. New bottom rows fill with 0.
    uint8_t s = (uint8_t)offset;
    for (uint8_t i = 0; i < 5; ++i) out[i] = (uint8_t)((in[i] >> s) & 0x7F);
  } else {
    uint8_t s = (uint8_t)(-offset);
    for (uint8_t i = 0; i < 5; ++i) out[i] = (uint8_t)((in[i] << s) & 0x7F);
  }
}

// ---------------------------------------------------------------------------
//  Render one vertical-animation frame.
//  pixelOffset: positive = text shifted UP, negative = text shifted DOWN.
//  Re-uploads each distinct glyph once, then writes DCRAM with slot codes.
// ---------------------------------------------------------------------------
void FutabaVFD::renderVerticalFrame(int8_t pixelOffset) {
  // 1) Build / re-upload one CGRAM glyph per distinct slot.
  for (uint8_t k = 0; k < 8; ++k) {
    uint8_t code = _vSlotCodes[k];
    if (code == 0xFF) continue;        // slot unused

    const uint8_t* progGlyph = FutabaVFDFont::glyphFor(code, _digits == 16);
    uint8_t base[5], shifted[5];
    FutabaVFDFont::readGlyph(progGlyph, base);

    // For SCROLL we want a SECOND glyph (the next character coming in)
    // bleeding into the first as it scrolls. That's a polish item; for
    // now we treat scrolling as the same glyph wrapping, which produces
    // a "rotating wheel" effect.
    if (_vMode == V_SCROLL) {
      uint8_t s = (uint8_t)((pixelOffset < 0) ? -pixelOffset : pixelOffset);
      s = s % 7;
      uint8_t a[5], b[5];
      shiftGlyph(base, a, pixelOffset);
      // wrapped portion: opposite shift of (7-s)
      int8_t op = (pixelOffset >= 0) ? (int8_t)(s - 7) : (int8_t)(7 - s);
      shiftGlyph(base, b, op);
      for (uint8_t i = 0; i < 5; ++i) shifted[i] = (uint8_t)((a[i] | b[i]) & 0x7F);
    } else {
      shiftGlyph(base, shifted, pixelOffset);
    }

    writeCgramSlot(k, shifted);
  }

  // 2) Write DCRAM: slot indices 0x00..0x07 for the visible positions,
  //    spaces (0x20) for the rest.
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE);
  for (uint8_t i = 0; i < _digits; ++i) {
    if (i < _vCodesLen) tx(_vSlotMap[i] & 0x07);
    else                tx(0x20);
  }
  selectHigh(); endTxn();
}

// ---------------------------------------------------------------------------
//  After IN finishes: replace slot codes with real CGROM codes so CGRAM
//  is freed up for any subsequent animation.
// ---------------------------------------------------------------------------
void FutabaVFD::commitVerticalAsCgrom() {
  beginTxn(); selectLow();
  tx(CMD_DCRAM_BASE);
  for (uint8_t i = 0; i < _digits; ++i) {
    if (i < _vCodesLen) tx(_vCodes[i]);
    else                tx(0x20);
  }
  selectHigh(); endTxn();
  for (uint8_t k = 0; k < 8; ++k) _vSlotCodes[k] = 0xFF;
}

// ---------------------------------------------------------------------------
//  Public: printVerticalIn - drop characters in from above
// ---------------------------------------------------------------------------
bool FutabaVFD::printVerticalIn(const char* s, uint16_t totalMs) {
  if (!_initialised || s == nullptr) return false;

  _scrollActive = false;
  _vMode = V_IDLE;

  // 1) Translate UTF-8 to device codepoints; clip to display width.
  uint8_t buf[FUTABA_VFD_SCROLL_BUF];
  size_t n = translateUtf8(s, buf, sizeof(buf));
  if (n > _digits) n = _digits;
  _vCodesLen = (uint8_t)n;
  for (uint8_t i = 0; i < n; ++i) _vCodes[i] = buf[i];

  // 2) Allocate slots.
  if (!buildSlotTable(_vCodes, _vCodesLen, _vSlotMap)) return false;

  // 3) Animation parameters: 8 frames (offset -7..0), totalMs evenly split.
  _vTotalSteps = 7;
  _vStep       = 0;
  _vStepMs     = (uint16_t)(totalMs / 8);
  if (_vStepMs == 0) _vStepMs = 1;
  _vLastMs     = millis();
  _vMode       = V_IN;

  // 4) Render initial frame (text fully off-screen above).
  renderVerticalFrame(-7);
  return true;
}

bool FutabaVFD::printVerticalIn(const String& s, uint16_t totalMs) {
  return printVerticalIn(s.c_str(), totalMs);
}

// ---------------------------------------------------------------------------
//  Public: printVerticalOut - drop current text out the bottom
// ---------------------------------------------------------------------------
bool FutabaVFD::printVerticalOut(uint16_t totalMs) {
  if (!_initialised) return false;

  // If there's nothing currently shown via the vertical engine, the
  // "current text" lives in DCRAM as plain CGROM codes. We need to read
  // them back conceptually - the M66004 doesn't support DCRAM read, so
  // we rely on the codes we last committed. Fall back: animate spaces.
  // For the IN -> OUT pattern, _vCodes still holds the last text.
  if (_vCodesLen == 0) return false;

  _scrollActive = false;

  // Re-allocate slots in case CGRAM was freed.
  if (!buildSlotTable(_vCodes, _vCodesLen, _vSlotMap)) return false;

  _vTotalSteps = 7;
  _vStep       = 0;
  _vStepMs     = (uint16_t)(totalMs / 8);
  if (_vStepMs == 0) _vStepMs = 1;
  _vLastMs     = millis();
  _vMode       = V_OUT;

  // Initial frame = text at rest (offset 0)
  renderVerticalFrame(0);
  return true;
}

// ---------------------------------------------------------------------------
//  Public: scrollVertical - endless vertical motion
// ---------------------------------------------------------------------------
bool FutabaVFD::scrollVertical(const char* s, uint16_t stepMs,
                               int8_t direction, bool loop_) {
  if (!_initialised || s == nullptr) return false;

  _scrollActive = false;
  _vMode = V_IDLE;

  uint8_t buf[FUTABA_VFD_SCROLL_BUF];
  size_t n = translateUtf8(s, buf, sizeof(buf));
  if (n > _digits) n = _digits;
  _vCodesLen = (uint8_t)n;
  for (uint8_t i = 0; i < n; ++i) _vCodes[i] = buf[i];

  if (!buildSlotTable(_vCodes, _vCodesLen, _vSlotMap)) return false;

  _vDirection = (direction >= 0) ? 1 : -1;
  _vLoop      = loop_;
  _vStep      = 0;
  _vStepMs    = (stepMs == 0) ? 1 : stepMs;
  _vLastMs    = millis();
  _vMode      = V_SCROLL;

  renderVerticalFrame(0);
  return true;
}

bool FutabaVFD::scrollVertical(const String& s, uint16_t stepMs,
                               int8_t direction, bool loop_) {
  return scrollVertical(s.c_str(), stepMs, direction, loop_);
}

void FutabaVFD::stopVertical() {
  if (_vMode == V_IDLE) return;
  // Leave CGRAM as-is but stop pumping. A subsequent direct write will
  // replace DCRAM and the user will see plain CGROM glyphs again.
  _vMode = V_IDLE;
  for (uint8_t k = 0; k < 8; ++k) _vSlotCodes[k] = 0xFF;
}
