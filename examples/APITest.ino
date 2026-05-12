/*
  FutabaVFD v3.0 — APITest
  =========================
  Sequential test of every API call.
  Watch Serial monitor (115200) for progress.

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

#define PAUSE 1500   // ms between tests

// Run update() for a fixed duration without blocking
void pump(uint32_t ms) {
  uint32_t t = millis();
  while (millis() - t < ms) vfd.update();
}

void separator(const char* label) {
  Serial.print("\n--- ");
  Serial.print(label);
  Serial.println(" ---");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.print("\nFutabaVFD v3.0 API Test — ");
  Serial.print(vfd.digits());
  Serial.println("-digit display");

  vfd.begin(/*SCLK*/ 18, /*MISO*/ VFD_MISO, /*MOSI*/ 23, /*spiHz*/ 500000,
            /*selfTest*/ true);
  vfd.setBrightness(120);
  delay(500);
}

void loop() {

  // ── 1) print() ───────────────────────────────────────────────────────────
  separator("1: print()");
  vfd.print("Hello!");
  delay(PAUSE);

  // ── 2) writeChar() ────────────────────────────────────────────────────────
  separator("2: writeChar() 0-9 on all columns");
  for (char c = '0'; c <= '9'; ++c) {
    vfd.clear();
    for (uint8_t col = 0; col < vfd.digits(); ++col)
      vfd.writeChar(col, c);
    delay(200);
  }
  delay(PAUSE);

  // ── 3) writeString() ──────────────────────────────────────────────────────
  separator("3: writeString() at col 0 and col 4");
  vfd.clear();
  vfd.writeString(0, "AB");
  vfd.writeString(4, "CD");
  delay(PAUSE);

  // ── 4) setBrightness() ────────────────────────────────────────────────────
  separator("4: setBrightness() sweep 0->240->120");
  vfd.print("Brightness");
  for (int b = 0; b <= 240; b += 20) {
    vfd.setBrightness((uint8_t)b);
    delay(80);
  }
  vfd.setBrightness(120);
  delay(PAUSE);

  // ── 5) standby() ──────────────────────────────────────────────────────────
  separator("5: standby on/off");
  vfd.print("Standby...");
  delay(800);
  vfd.standby(true);
  delay(1200);
  vfd.standby(false);
  delay(PAUSE);

  // ── 6) scroll() ───────────────────────────────────────────────────────────
  separator("6: scroll() — hold then scroll left");
  vfd.scroll("  Classic scroll  holdMs=800  ", /*speedMs*/ 180, /*holdMs*/ 800);
  pump(6000);
  vfd.stopScroll();
  delay(PAUSE);

  // ── 7) ticker() ───────────────────────────────────────────────────────────
  separator("7: ticker() — enters from right");
  vfd.ticker("  Ticker enters from right  ", /*speedMs*/ 150);
  pump(5000);
  vfd.stopScroll();
  delay(PAUSE);

  // ── 8) blink() ────────────────────────────────────────────────────────────
  separator("8: blink() 600 ms period");
  vfd.print("Blinking!");
  vfd.blink(600);
  pump(4000);
  vfd.stopBlink();
  delay(PAUSE);

  // ── 9) flipIn() ───────────────────────────────────────────────────────────
  separator("9: flipIn() full display");
  vfd.flipIn("hifilabor", 600);
  pump(1500);
  delay(PAUSE);

  // ── 10) flipOut() ─────────────────────────────────────────────────────────
  separator("10: flipOut() full display");
  vfd.flipOut(600);
  pump(1500);
  delay(PAUSE);

  // ── 11) flip() single digit ───────────────────────────────────────────────
  separator("11: flip() single digit 0->9 on each column");
  vfd.print("00000000");
  delay(300);
  for (uint8_t col = 0; col < vfd.digits(); ++col) {
    for (char c = '1'; c <= '9'; ++c) {
      vfd.flip(col, c, 220);
      pump(300);
    }
  }
  delay(PAUSE);

  // ── 12) band() upward ─────────────────────────────────────────────────────
  separator("12: band() rotating UP");
  vfd.band("12:34:56", /*speedMs*/ 100, UP);
  pump(3000);
  vfd.stopVertical();
  delay(PAUSE);

  // ── 13) band() downward ───────────────────────────────────────────────────
  separator("13: band() rotating DOWN");
  vfd.band("12:34:56", 100, DOWN);
  pump(3000);
  vfd.stopVertical();
  delay(PAUSE);

  // ── 14) isAnimating() ─────────────────────────────────────────────────────
  separator("14: isAnimating() check");
  vfd.flipIn("test", 800);
  Serial.print("isAnimating during flipIn: ");
  Serial.println(vfd.isAnimating() ? "true (correct)" : "false (WRONG)");
  pump(1200);
  Serial.print("isAnimating after flipIn:  ");
  Serial.println(vfd.isAnimating() ? "true (WRONG)" : "false (correct)");
  delay(PAUSE);

  // ── 15) clear() ───────────────────────────────────────────────────────────
  separator("15: clear()");
  vfd.print("clear test");
  delay(600);
  vfd.clear();
  delay(PAUSE);

  separator("All tests done — restarting in 2 s");
  delay(2000);
}
