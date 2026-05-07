/*
  FutabaVFD - Basic example
  Drop-in equivalent of the original ESP32_VFD_16bit.ino, but using the
  class-based API. Pin mapping unchanged: DIN=23, CLK=18, CS=5, RESET=19.

  This sketch uses delay() for simplicity. For a real non-blocking demo
  see the NonBlockingScroll and MultiTask examples.
*/

#include <FutabaVFD.h>

// 16-digit display, CS=5, no RESET pin needed (use -1 for 16-bit module).
FutabaVFD vfd(16, /*CS*/5, /*RESET*/-1);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nStarting VFD...");

  // Pass explicit pins to be safe across ESP32 variants. Pass -1 for MISO
  // because the display is write-only.
  vfd.begin(/*SCLK*/18, /*MISO*/-1, /*MOSI*/23,
            /*spiHz*/100000, /*selfTest*/true);

  vfd.setBrightness(80);
  vfd.print("Hello!");
}

void loop() {
  // Direct write at column 0
  vfd.print("Hello world!");
  delay(1000);

  // German umlauts and the degree sign work straight from UTF-8 source.
  vfd.print("Grüße: 23°C");
  delay(1500);

  // Walk through the printable ASCII range
  for (uint8_t c = 32; c < 128; ++c) {
    vfd.clear();
    vfd.writeChar(0, c);
    delay(80);
  }
}
