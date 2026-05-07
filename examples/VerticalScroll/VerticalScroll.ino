/*
  FutabaVFD - VerticalScroll example

  Demonstrates the three pixel-accurate vertical animations:
    - printVerticalIn   : characters drop in from above
    - printVerticalOut  : characters drop out the bottom
    - scrollVertical    : endless rotating-band effect

  These run pixel-by-pixel via the M66004's CGRAM, sharing 8 user-glyph
  slots. On the 16-digit module that means at most 8 *distinct* characters
  can be visible at once. Repeated chars share a slot, so e.g.
  "1.1.2026 :)" is fine despite being 11 visible characters.

  As with the horizontal animations, vfd.update() is the pump - the loop()
  body never blocks.
*/

#include <FutabaVFD.h>

FutabaVFD vfd(16, /*CS*/5, /*RESET*/-1);

enum class Phase { IN_DEMO, HOLD, OUT_DEMO, GAP, BAND };
Phase    phase = Phase::IN_DEMO;
uint32_t phaseStart = 0;

void setup() {
  Serial.begin(115200);
  // Bump SPI to 500 kHz - the M66004 datasheet allows up to that, and
  // the CGRAM uploads benefit from the headroom (one frame = up to 8
  // glyph writes of 6 bytes each).
  vfd.begin(/*SCLK*/18, /*MISO*/-1, /*MOSI*/23,
            /*spiHz*/500000, /*selfTest*/false);
  vfd.setBrightness(120);

  vfd.printVerticalIn("hifilabor.ch", 700);   // 700 ms total drop-in
  phaseStart = millis();
}

void loop() {
  vfd.update();

  uint32_t elapsed = millis() - phaseStart;

  switch (phase) {
    case Phase::IN_DEMO:
      if (!vfd.isVerticalActive() && elapsed > 1500) {
        phase = Phase::HOLD;
        phaseStart = millis();
      }
      break;

    case Phase::HOLD:
      // Hold the text on screen for a bit
      if (elapsed > 2000) {
        vfd.printVerticalOut(500);
        phase = Phase::OUT_DEMO;
        phaseStart = millis();
      }
      break;

    case Phase::OUT_DEMO:
      if (!vfd.isVerticalActive() && elapsed > 1000) {
        phase = Phase::GAP;
        phaseStart = millis();
      }
      break;

    case Phase::GAP:
      if (elapsed > 500) {
        // Endless upward band of "88:88:88" - clock-style flip effect.
        // Only 3 distinct chars (8, :, space) so it works fine even
        // on the 16-digit module.
        vfd.scrollVertical("88:88:88", /*stepMs*/120, /*dir*/-1, /*loop*/true);
        phase = Phase::BAND;
        phaseStart = millis();
      }
      break;

    case Phase::BAND:
      if (elapsed > 6000) {
        vfd.stopVertical();
        vfd.printVerticalIn("done. =23 C", 600);  // simple ASCII for portability
        phase = Phase::IN_DEMO;                   // restart the cycle
        phaseStart = millis();
      }
      break;
  }
}
