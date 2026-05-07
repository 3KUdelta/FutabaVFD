/*
  FutabaVFD - MultiTask example

  Demonstrates that several display behaviours can run concurrently
  without blocking the main loop:
    - a scrolling banner during boot
    - then a blinking value display that reflects a periodically read
      "sensor" (here: a faked sine wave so you can run it without HW)

  Pattern: cooperative scheduling using millis() timestamps and the
  vfd.update() pump.
*/

#include <FutabaVFD.h>
#include <math.h>

FutabaVFD vfd(16, 5, -1);

enum class Phase { BANNER, READOUT };
Phase    phase        = Phase::BANNER;
uint32_t phaseStartMs = 0;

uint32_t lastReadMs   = 0;
const uint32_t READ_PERIOD_MS = 500;

float fakeSensor() {
  // 0.1 Hz sine, mapped to 18.0 .. 26.0 °C
  float t = millis() / 1000.0f;
  return 22.0f + 4.0f * sinf(t * 0.628f);
}

void setup() {
  Serial.begin(115200);
  vfd.begin(18, -1, 23);
  vfd.setBrightness(100);

  vfd.printScroll("Booting hifilabor.ch sensor...", 180, /*loop=*/false);
  phaseStartMs = millis();
}

void loop() {
  vfd.update();

  switch (phase) {
    case Phase::BANNER:
      // Switch to readout once the banner has finished scrolling, but
      // also enforce a minimum visible time of 4 s.
      if (!vfd.isScrolling() && (millis() - phaseStartMs) > 4000) {
        phase = Phase::READOUT;
        vfd.clear();
        vfd.blink(800);              // gentle blink at 1.25 Hz
      }
      break;

    case Phase::READOUT: {
      uint32_t now = millis();
      if (now - lastReadMs >= READ_PERIOD_MS) {
        lastReadMs = now;
        char buf[17];
        snprintf(buf, sizeof(buf), "Temp: %.1f °C", fakeSensor());
        vfd.print(buf);              // direct, non-blocking write
      }
      break;
    }
  }
}
