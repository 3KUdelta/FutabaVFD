#include <SPI.h>

#define VER            1.3

#define DIGITS         8           // which display do you have 8 or 16 bits?
#define VSPI_MISO      MISO        // not connected
#define VSPI_MOSI      MOSI        // ESP32 Pin 23
#define VSPI_SCLK      SCK         // ESP32 Pin 18
#define VSPI_SS        SS          // ESP32 Pin 5
#define RESET          19          // ESP32 Pin 19 (not needed for 16 bit module)

static const int spiClk = 100000;  // 0.1 MHz
#define VSPI_SETTINGS  SPISettings(spiClk, LSBFIRST, SPI_MODE3)

SPIClass * vspi = NULL;           //uninitalised pointers to SPI objects

void VFD_brightness(unsigned int level) {
  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  vspi->transfer(0xe4);            //first byte address for setting DIGITS
  vspi->transfer(level);
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
}
void VFD_clearScreen(void) {
  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  vspi->transfer(0x20);
  for (int i = 0; i < DIGITS; i++) {
    vspi->transfer(0x20);
  }
  vspi->transfer(0xe8);  // refresh
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
}
void VFD_show(void) {
  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  vspi->transfer(0xe8);
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
}
void VFD_standby(bool on) {
  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  if (on) {
    vspi->transfer(0xed);     // standby mode
  }
  else {
    vspi->transfer(0xec);     // running mode
  }
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
}
void VFD_WriteASCII(int x, unsigned char chr) {
  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  vspi->transfer(0x20 + x);
  vspi->transfer(chr);
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
}
String translateSpecialChars(String str) {
  unsigned int l = str.length();   // how many chars were sent?
  char bla[l];                     // define working array of chars
  str.toCharArray(bla, l + 1);     // move string into array of chars
  for (int i = 0; i < l; i++) {
    if (DIGITS == 16) {
      if (int(bla[i]) == 195) {      // special char is being announced (first is 195)
        bla[i] = 255;                // filling with marker (255 not used)
        switch (int(bla[i + 1])) {
          case 132: bla[i + 1] = 196; break; //Ä
          case 164: bla[i + 1] = 228; break; //ä
          case 150: bla[i + 1] = 214; break; //Ö
          case 182: bla[i + 1] = 246; break; //ö
          case 156: bla[i + 1] = 220; break; //Ü
          case 188: bla[i + 1] = 252; break; //ü
        }
      }
      if (int(bla[i]) == 194) {      // special char is announced (first is 194)
        bla[i] = 255;                // filling with marker (255 not used)
        bla[i + 1] = 176;            // Degree sign (°)
      }
    }
    else {
      if (int(bla[i]) == 195) {      // special char is being announced (first is 195)
        bla[i] = 255;                // filling with marker (255 not used)
        switch (int(bla[i + 1])) {
          case 132: bla[i + 1] = 142; break; //Ä
          case 164: bla[i + 1] = 132; break; //ä
          case 150: bla[i + 1] = 153; break; //Ö
          case 182: bla[i + 1] = 148; break; //ö
          case 156: bla[i + 1] = 154; break; //Ü
          case 188: bla[i + 1] = 129; break; //ü
        }
      }
      if (int(bla[i]) == 194) {      // special char is announced (first is 194)
        bla[i] = 255;                // filling with marker (255 not used)
        bla[i + 1] = 239;            // Degree sign (°)
      }
    }
  }
  for (int i = 0; i < l; i++) {  // deleting marker und shifting to left
    if (int(bla[i]) == 255) {
      for (int ii = i; ii < l; ii++) {
        bla[ii] = bla[ii + 1];
      }
    }
  }
  str = String(bla);
  return str;
}
void VFD_WriteStr(int x, String str) {
  str = translateSpecialChars(str);
  if (x > 0) {
    for (int i = 0; i < x; i++) {
      str = " " + str;             // fill with blanks
    }
  }
  unsigned int l = str.length();   // how many chars were sent?
  char bla[l];                     // define working array of chars
  str.toCharArray(bla, l + 1);     // move string into array of chars
  Serial.print("Sending to display: ");
  Serial.println(bla);

  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  vspi->transfer(0x20);                // base register DCRAM 0H

  if (l > DIGITS) {
    for (int i = 0; i < DIGITS - x; i++) {
      vspi->transfer(bla[i]);         // fill first batch to end of visible chars
    }
    digitalWrite(VSPI_SS, HIGH);
    for (int i = 0; i < l - DIGITS; i++) {
      digitalWrite(VSPI_SS, LOW);
      vspi->transfer(0x20);                // base register DCRAM 0H
      for (int ii = 0; ii < DIGITS; ii++) {
        vspi->transfer(bla[ii + 1 + i]);   // shift (scroll to left)
      }
      digitalWrite(VSPI_SS, HIGH);
      delay(100);                 // delay time for scrolling
    }
  }
  else {
    for (int i = 0; i < l; i++) {
      vspi->transfer(bla[i]);
    }
    for (int i = 0; i < DIGITS - x - l; i++) {
      vspi->transfer(0x20);            // fill ends with blanks
    }
  }
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
}
void VFD_init() {
  /* Setting Digits ****************************************************/
  vspi->beginTransaction(VSPI_SETTINGS);
  digitalWrite(VSPI_SS, LOW);
  if (DIGITS == 8) {
    vspi->transfer(0xe0);            //first byte address for setting DIGITS
    vspi->transfer(0x07);            //8 DIGITS equals value 7 (hex 0x07)
  }
  if (DIGITS == 16) {
    vspi->transfer(0xe0);            //first byte address for setting DIGITS
    vspi->transfer(0x0f);            //16 DIGITS equals (0x0f)
  }
  digitalWrite(VSPI_SS, HIGH);
  vspi->endTransaction();
  /* *******************************************************************/
  VFD_brightness(50);              //setting brightness to initial value
  VFD_standby(false);              //switch standby off
  VFD_clearScreen();               //clear the screen
  VFD_show();                      //activate the screen
  /* Quick test of all DIGITS ******************************************/
  for (int i = 10; i-- > 0;) {
    for (int ii = 0; ii < DIGITS; ii++) {
      VFD_WriteASCII(ii, i + 0x30);
      delay(8);
    }
  }
  /* *******************************************************************/
  VFD_clearScreen();               //clear the screen
}
