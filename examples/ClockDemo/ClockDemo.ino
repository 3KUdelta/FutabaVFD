/*
  FutabaVFD v3.0 — ClockDemo
  ===========================
  hh:mm:ss clock seeded from compile time (__TIME__), no RTC needed.
  Only digits that actually change are animated with flip(), right-to-left.
  Since only one column can animate at a time, changed digits are queued
  and fired one after the other. Colons never change and are never touched.

  ── SELECT DISPLAY ──────────────────────────────────────────────────────────
  Uncomment ONE line:
*/
//#define DISPLAY_16
 #define DISPLAY_8
// ────────────────────────────────────────────────────────────────────────────

#include <FutabaVFD.h>

#ifdef DISPLAY_16
  FutabaVFD vfd(16, /*CS*/ 5, /*RESET*/ -1);
  #define VFD_MISO 19
  #define TIME_COL 4   // centre "hh:mm:ss" (8 chars) in 16 digits
#else
  FutabaVFD vfd(8,  /*CS*/ 5, /*RESET*/ 19);
  #define VFD_MISO -1
  #define TIME_COL 0
#endif

// ── Compile-time clock seed ───────────────────────────────────────────────────
static uint32_t compileTimeSeconds() {
  const char* t = __TIME__;
  uint8_t h = (t[0]-'0')*10 + (t[1]-'0');
  uint8_t m = (t[3]-'0')*10 + (t[4]-'0');
  uint8_t s = (t[6]-'0')*10 + (t[7]-'0');
  return (uint32_t)h*3600u + m*60u + s;
}

uint32_t startSec;
uint32_t startMs;

uint32_t nowSeconds() {
  return (startSec + (millis() - startMs) / 1000u) % 86400u;
}

void formatTime(uint32_t sec, char buf[9]) {
  uint8_t h = sec / 3600u;
  uint8_t m = (sec % 3600u) / 60u;
  uint8_t s = sec % 60u;
  buf[0] = '0' + h/10;  buf[1] = '0' + h%10;
  buf[2] = ':';
  buf[3] = '0' + m/10;  buf[4] = '0' + m%10;
  buf[5] = ':';
  buf[6] = '0' + s/10;  buf[7] = '0' + s%10;
  buf[8] = '\0';
}

// ── Flip queue ────────────────────────────────────────────────────────────────
// Collects display columns to animate right-to-left, fires one at a time.
struct FlipQueue {
  uint8_t col[6];
  char    ch[6];
  uint8_t head  = 0;
  uint8_t count = 0;

  void clear()                   { head = 0; count = 0; }
  bool empty() const             { return count == 0; }
  void push(uint8_t c, char ch_) { col[head+count] = c; ch[head+count] = ch_; count++; }

  void fireNext(FutabaVFD& d, uint16_t ms) {
    if (empty()) return;
    d.flip(col[head], ch[head], ms);
    head++; count--;
  }
} queue;

// ── State ─────────────────────────────────────────────────────────────────────
char     prev[9];
uint32_t lastTickSec = 0;

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  vfd.begin(/*SCLK*/ 18, /*MISO*/ VFD_MISO, /*MOSI*/ 23, /*spiHz*/ 500000);
  vfd.setBrightness(120);

  startMs  = millis();
  startSec = compileTimeSeconds();

  // Draw initial time without animation
  formatTime(startSec, prev);
  // Write digit by digit so colons land at the right display columns
  for (uint8_t i = 0; i < 8; ++i)
    vfd.writeChar(TIME_COL + i, prev[i]);

  lastTickSec = startSec;

  Serial.print("ClockDemo — compile time: ");
  Serial.printf("%02d:%02d:%02d  display col %d\n",
                (int)startSec/3600, (int)(startSec%3600)/60, (int)startSec%60,
                TIME_COL);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  vfd.update();

  // Fire next queued digit as soon as the current animation finishes
  if (!vfd.isAnimating() && !queue.empty()) {
    queue.fireNext(vfd, 220);
    return;
  }

  // While animating or draining queue, don't start a new tick
  if (vfd.isAnimating() || !queue.empty()) return;

  // Check if a new second has arrived
  uint32_t sec = nowSeconds();
  if (sec == lastTickSec) return;
  lastTickSec = sec;

  char curr[9];
  formatTime(sec, curr);

  // Build flip queue: scan right-to-left, collect changed digits
  queue.clear();
  for (int8_t i = 7; i >= 0; --i) {
    if (curr[i] != prev[i] && curr[i] != ':') {
      queue.push(TIME_COL + i, curr[i]);
    }
  }

  memcpy(prev, curr, 9);

  // Fire first animation immediately
  queue.fireNext(vfd, 220);

  Serial.printf("%02d:%02d:%02d\n",
                (int)sec/3600, (int)(sec%3600)/60, (int)sec%60);
}

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
