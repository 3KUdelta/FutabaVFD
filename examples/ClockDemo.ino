/*
  FutabaVFD v3.0 — ClockDemo
  ===========================
  hh:mm:ss clock display using flip() for each digit change.
  Only digits that actually change are animated — the colon
  positions stay perfectly still.

  Time is simulated by counting seconds from 00:00:00.
  Replace getTime() with your RTC or NTP source.

  ── SELECT DISPLAY ──────────────────────────────────────────────────────────
  Uncomment ONE line:
*/
#define DISPLAY_16
// #define DISPLAY_8
// ────────────────────────────────────────────────────────────────────────────

#include <FutabaVFD.h>

#ifdef DISPLAY_16
  FutabaVFD vfd(16, /*CS*/ 5, /*RESET*/ -1);
  #define VFD_MISO 19
  // 16-digit: centre the 8-character time string, starting at col 4
  // "hh:mm:ss" = 8 chars, centred in 16 = col 4..11
  #define TIME_COL 4
#else
  FutabaVFD vfd(8,  /*CS*/ 5, /*RESET*/ 19);
  #define VFD_MISO -1
  // 8-digit: fits exactly
  #define TIME_COL 0
#endif

// ── Simulated time ───────────────────────────────────────────────────────────
// Replace this function with your RTC or NTP time source.
uint32_t startMs = 0;

void getTime(uint8_t& hh, uint8_t& mm, uint8_t& ss) {
  uint32_t total = (millis() - startMs) / 1000UL;
  ss = total % 60;
  mm = (total / 60) % 60;
  hh = (total / 3600) % 24;
}

// ── Digit positions in the display ───────────────────────────────────────────
// Layout: "hh:mm:ss"
//          01 23 45 67  (offsets from TIME_COL)
// col TIME_COL+0 = tens of hours
// col TIME_COL+1 = units of hours
// col TIME_COL+2 = ':'
// col TIME_COL+3 = tens of minutes
// col TIME_COL+4 = units of minutes
// col TIME_COL+5 = ':'
// col TIME_COL+6 = tens of seconds
// col TIME_COL+7 = units of seconds

uint8_t prevHH = 99, prevMM = 99, prevSS = 99;   // force full refresh on first tick
uint32_t lastSecMs = 0;

// Write a digit at the given offset from TIME_COL.
// Uses flip() if the digit changed, writeChar() on first draw.
void updateDigit(uint8_t offset, char newChar, char& prevChar, bool firstDraw) {
  uint8_t col = TIME_COL + offset;
  if (firstDraw) {
    vfd.writeChar(col, newChar);
    prevChar = newChar;
  } else if (newChar != prevChar) {
    vfd.flip(col, newChar, 220);
    prevChar = newChar;
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  vfd.begin(/*SCLK*/ 18, /*MISO*/ VFD_MISO, /*MOSI*/ 23, /*spiHz*/ 500000);
  vfd.setBrightness(120);

  // Draw static colons once
  vfd.writeChar(TIME_COL + 2, ':');
  vfd.writeChar(TIME_COL + 5, ':');

  startMs = millis();
  lastSecMs = millis();

  Serial.print("ClockDemo — ");
  Serial.print(vfd.digits());
  Serial.println("-digit display");
}

// ── State for flip() tracking ─────────────────────────────────────────────────
char dHH0 = ' ', dHH1 = ' ';
char dMM0 = ' ', dMM1 = ' ';
char dSS0 = ' ', dSS1 = ' ';
bool firstDraw = true;

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  vfd.update();

  // Tick once per second
  if ((uint32_t)(millis() - lastSecMs) < 1000) return;
  lastSecMs += 1000;

  uint8_t hh, mm, ss;
  getTime(hh, mm, ss);

  char nHH0 = '0' + hh / 10;
  char nHH1 = '0' + hh % 10;
  char nMM0 = '0' + mm / 10;
  char nMM1 = '0' + mm % 10;
  char nSS0 = '0' + ss / 10;
  char nSS1 = '0' + ss % 10;

  updateDigit(0, nHH0, dHH0, firstDraw);
  updateDigit(1, nHH1, dHH1, firstDraw);
  updateDigit(3, nMM0, dMM0, firstDraw);
  updateDigit(4, nMM1, dMM1, firstDraw);
  updateDigit(6, nSS0, dSS0, firstDraw);
  updateDigit(7, nSS1, dSS1, firstDraw);

  firstDraw = false;

  Serial.printf("%02d:%02d:%02d\n", hh, mm, ss);
}
