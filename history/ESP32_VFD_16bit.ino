#include "vfd_controls.h"

void setup() {

  Serial.begin(115200); while (!Serial); delay(200);
  Serial.println();
  Serial.println("Starting VFD...");
  Serial.println();

  vspi = new SPIClass(VSPI);
  vspi->begin(VSPI_SCLK, VSPI_MISO, VSPI_MOSI, VSPI_SS); //SCLK, MISO, MOSI, SS

  pinMode(VSPI_SS, OUTPUT);  //VSPI SS
  
  pinMode(RESET, OUTPUT);    //only needed for 8bit module
  digitalWrite(RESET, HIGH); //only needed for 8bit module

  VFD_init();                //initializing the display
}

void loop()
{
  VFD_WriteStr(8, "Writing a long String to see scrolling.");
  delay(1000);
  VFD_WriteStr(0, "ÄäÖöÜü°");
  delay(1000);

  VFD_clearScreen();
  for (int i = 0; i < 255; i++) {
    VFD_WriteStr(0, String(i));   //show available ASCii chars
    VFD_WriteASCII(4, i);
    delay(800);
  }
}
