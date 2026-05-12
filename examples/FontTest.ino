/*
  FutabaVFD v3.0 — FontTest
  ==========================
  Cycles through all 62 printable characters (0-9, A-Z, a-z)
  using flipIn and flipOut to verify the built-in font.

  Enter in Serial monitor (115200) to advance manually.
  After 1.5 s the next character advances automatically.

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
#else
  FutabaVFD vfd(8,  /*CS*/ 5, /*RESET*/ 19);
  #define VFD_MISO -1
#endif

const char* TEST_CHARS =
  "0123456789"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz";
const uint8_t TOTAL = 62;

enum class Phase { FLIP_IN, HOLD, FLIP_OUT, GAP };
Phase    phase   = Phase::FLIP_IN;
uint32_t phaseMs = 0;
uint8_t  idx     = 0;

void startPhase(Phase p) { phase = p; phaseMs = millis(); }
uint32_t elapsed()       { return millis() - phaseMs; }

void showCurrent() {
  char buf[17];
  for (uint8_t i = 0; i < vfd.digits(); ++i) buf[i] = TEST_CHARS[idx];
  buf[vfd.digits()] = '\0';

  Serial.print("'");
  Serial.print(TEST_CHARS[idx]);
  Serial.print("'  (");
  Serial.print(idx + 1);
  Serial.print("/62) — Enter to advance");
  Serial.println();

  vfd.flipIn(buf, 500);
  startPhase(Phase::FLIP_IN);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  vfd.begin(/*SCLK*/ 18, /*MISO*/ VFD_MISO, /*MOSI*/ 23, /*spiHz*/ 500000);
  vfd.setBrightness(120);

  Serial.print("=== Font Test — ");
  Serial.print(vfd.digits());
  Serial.println("-digit display ===");
  Serial.println("Enter to advance, or wait 1.5 s for auto-advance.");
  Serial.println();

  showCurrent();
}

void loop() {
  vfd.update();

  // Serial Enter = advance immediately
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    if (!vfd.isAnimating()) {
      idx = (idx + 1) % TOTAL;
      showCurrent();
    }
    return;
  }

  switch (phase) {
    case Phase::FLIP_IN:
      if (!vfd.isAnimating()) startPhase(Phase::HOLD);
      break;
    case Phase::HOLD:
      if (elapsed() >= 1500) {
        vfd.flipOut(400);
        startPhase(Phase::FLIP_OUT);
      }
      break;
    case Phase::FLIP_OUT:
      if (!vfd.isAnimating()) startPhase(Phase::GAP);
      break;
    case Phase::GAP:
      if (elapsed() >= 300) {
        idx = (idx + 1) % TOTAL;
        showCurrent();
      }
      break;
  }
}
