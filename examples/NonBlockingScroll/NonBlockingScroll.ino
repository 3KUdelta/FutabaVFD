/*
  FutabaVFD - NonBlockingScroll example

  Shows how the new update() pump replaces blocking delay() calls.
  The loop() body runs at full speed and can do other work (here: a
  blinking onboard LED) while a long message scrolls across the display.

  Compare to the original sketch where VFD_WriteStr() blocked for
  ~100 ms per character of overflow.
*/

#include <FutabaVFD.h>

FutabaVFD vfd(16, 5, -1);

const uint8_t LED = 2;       // onboard LED on most ESP32 dev boards

uint32_t ledLastMs = 0;
bool     ledOn     = false;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);

  vfd.begin(18, -1, 23);
  vfd.setBrightness(120);

  // Start a looping scroll. update() will advance it one digit every 200 ms.
  vfd.printScroll("hifilabor.ch  -  Futaba VFD library demo  -  ", 200, true);
}

void loop() {
  // Pump the display state machine. Returns quickly without blocking.
  vfd.update();

  // Independent task: blink the LED at 5 Hz, also without delay().
  uint32_t now = millis();
  if (now - ledLastMs >= 100) {
    ledLastMs = now;
    ledOn = !ledOn;
    digitalWrite(LED, ledOn);
  }

  // Anything else (WiFi, sensor reads, MQTT publish, ...) can live here
  // and it will not stall the scrolling display.
}
