#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include "UbidotsEsp32Mqtt.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "RTClib.h"
#include "DHT.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <math.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

#define DHTPIN 25
#define DHTTYPE DHT21

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;
DHT dht(DHTPIN, DHTTYPE);
File dataFile;

int logNumber = 0;
char fileName[20];

// Global untuk simpanData()
char buffer[25];
float t = NAN; 
float h = NAN;

// Ubidots
const char *UBIDOTS_TOKEN = "BBUS-T7wWsXQYkF522mdg0eJNEyWjP7vfJs";
const char *DEVICE_LABEL  = "esp32-dht";
const char *VARIABLE_TEM  = "temperature";
const char *VARIABLE_HUM  = "humidity";

const int PUBLISH_FREQUENCY = 15000;  // 15 detik
unsigned long timer;
Ubidots ubidots(UBIDOTS_TOKEN);

// Sampling
const uint32_t SAMPLE_INTERVAL_S = 60; // tiap menit di detik == 00
long lastSampleSlot = -1;
int  lastSecond = -1;

inline void print2d(uint8_t v) {
  if (v < 10) display.print('0');
  display.print(v);
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Pesan diterima [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) Serial.print((char)payload[i]);
  Serial.println();
}

int cariLogTerakhir() {
  for (int i = 0; i < 1000; i++) {
    char testName[20];
    sprintf(testName, "/log%03d.txt", i);
    if (!SD.exists(testName)) return i - 1;
  }
  return 999;
}

// animasi awal
void drawHamster(int x, int y, bool blink) {
  int cx = x + 16;
  int cy = y + 18;
  display.fillCircle(cx - 8, cy - 14, 6, SSD1306_WHITE);
  display.fillCircle(cx + 8, cy - 14, 6, SSD1306_WHITE);
  display.fillCircle(cx, cy, 14, SSD1306_WHITE);
  display.fillRoundRect(cx - 10, cy + 4, 20, 8, 4, SSD1306_BLACK);
  display.fillTriangle(cx, cy + 1, cx - 3, cy + 4, cx + 3, cy + 4, SSD1306_BLACK);
  if (blink) {
    display.drawLine(cx - 6, cy - 4, cx - 2, cy - 4, SSD1306_BLACK);
    display.drawLine(cx + 2, cy - 4, cx + 6, cy - 4, SSD1306_BLACK);
  } else {
    display.fillCircle(cx - 4, cy - 4, 2, SSD1306_BLACK);
    display.fillCircle(cx + 4, cy - 4, 2, SSD1306_BLACK);
  }
  display.drawLine(cx - 12, cy + 2, cx - 5, cy + 1, SSD1306_BLACK);
  display.drawLine(cx - 12, cy + 5, cx - 5, cy + 3, SSD1306_BLACK);
  display.drawLine(cx + 12, cy + 2, cx + 5, cy + 1, SSD1306_BLACK);
  display.drawLine(cx + 12, cy + 5, cx + 5, cy + 3, SSD1306_BLACK);
}

// TERMOMETER: rapi & otomatis menyesuaikan tinggi layar
void drawThermometer(int x, int y, int stemH, int level /*0..100*/) {
  const int stemW = 10;     // lebar “batang”
  const int r     = 7;      // jari-jari “bulb” (bola bawah)

  int maxStemH = SCREEN_HEIGHT - y - r - 1;
  if (stemH > maxStemH) stemH = maxStemH;
  if (stemH < 12) stemH = 12; // minimal biar bentuk tetap bagus

  // Outline
  display.drawRoundRect(x, y, stemW, stemH, 4, SSD1306_WHITE);
  // Bulb
  int cx = x + stemW/2;
  int cy = y + stemH + r;
  display.fillCircle(cx, cy, r, SSD1306_WHITE);

  // “Liquid” isi batang (putih, dari bawah naik)
  int innerX = x + 2;
  int innerW = stemW - 4;
  int maxFill = stemH - 4;                 // margin 2px atas-bawah
  int filled  = (level * maxFill) / 100;
  if (filled > 0) {
    display.fillRect(innerX, y + (stemH - 2 - filled), innerW, filled, SSD1306_WHITE);
  }

  // Garis kecil penutup sambungan batang-bulb supaya mulus
  display.drawLine(x+1, y+stemH, x+stemW-2, y+stemH, SSD1306_WHITE);
}

void drawDroplet(int x, int y) {
  display.fillCircle(x, y + 5, 6, SSD1306_WHITE);
  display.fillTriangle(x, y - 4, x - 5, y + 2, x + 5, y + 2, SSD1306_WHITE);
}

// Hamster full-body (40–44 px lebar, 28 px tinggi kira-kira)
void drawHamsterFull(int x, int y, bool facingLeft, bool blink) {
  // BODY (oval besar)
  display.fillRoundRect(x + 12, y + 8, 28, 18, 6, SSD1306_WHITE);

  // HEAD
  display.fillCircle(x + 14, y + 12, 9, SSD1306_WHITE);
  // EARS
  display.fillCircle(x + 9,  y + 5, 4, SSD1306_WHITE);
  display.fillCircle(x + 19, y + 5, 4, SSD1306_WHITE);

  // EYE (blink = garis tipis)
  int eyeX = facingLeft ? (x + 12) : (x + 16);
  if (blink) {
    display.drawLine(eyeX - 2, y + 12, eyeX + 2, y + 12, SSD1306_BLACK);
  } else {
    display.fillCircle(eyeX, y + 12, 1, SSD1306_BLACK);
  }

  // MUZZLE + NOSE sederhana
  int noseX = facingLeft ? (x + 6) : (x + 22);
  display.fillTriangle(noseX, y + 14, noseX + (facingLeft ? 2 : -2), y + 15, noseX, y + 16, SSD1306_BLACK);

  // WHISKERS (kumis)
  if (facingLeft) {
    display.drawLine(x + 7,  y + 14, x + 1,  y + 12, SSD1306_BLACK);
    display.drawLine(x + 7,  y + 16, x + 1,  y + 16, SSD1306_BLACK);
    display.drawLine(x + 7,  y + 18, x + 1,  y + 20, SSD1306_BLACK);
  } else {
    display.drawLine(x + 21, y + 14, x + 27, y + 12, SSD1306_BLACK);
    display.drawLine(x + 21, y + 16, x + 27, y + 16, SSD1306_BLACK);
    display.drawLine(x + 21, y + 18, x + 27, y + 20, SSD1306_BLACK);
  }

  // FEET
  display.fillRect(x + 18, y + 25, 4, 3, SSD1306_WHITE);
  display.fillRect(x + 30, y + 25, 4, 3, SSD1306_WHITE);

  // TAIL kecil
  int tailX = facingLeft ? (x + 40) : (x + 10);
  display.fillCircle(tailX, y + 18, 2, SSD1306_WHITE);
}

// Awan sederhana (w = lebar awan, 18–36 bagus)
void drawCloud(int x, int y, int w) {
  if (w < 12) w = 12;
  int r1 = w / 6 + 1;
  int r2 = w / 5 + 1;
  int r3 = w / 6 + 1;

  display.fillCircle(x + w * 1 / 6, y + 7, r1, SSD1306_WHITE);
  display.fillCircle(x + w * 3 / 6, y + 5, r2, SSD1306_WHITE);
  display.fillCircle(x + w * 5 / 6, y + 7, r3, SSD1306_WHITE);
  display.fillRoundRect(x + w / 8, y + 7, w * 3 / 4, 6, 3, SSD1306_WHITE);
}

// Bendera segitiga bergelombang halus di bagian atas
void drawBendera(int step) {
  // talinya
  int baseY = 8 + (step % 16 < 8 ? 0 : 1);  // goyang halus 0–1 px
  display.drawLine(0, baseY, SCREEN_WIDTH - 1, baseY, SSD1306_WHITE);

  // segitiga bendera tiap 16 px, warna putih (fill)
  for (int x = 6, i = 0; x < SCREEN_WIDTH; x += 16, ++i) {
    int dy = ( (i + step/4) % 2 == 0 ) ? 4 : 6; // turun-naik dikit biar ada variasi
    display.fillTriangle(
      x, baseY,            // puncak di tali
      x + 6, baseY,        // sisi kanan di tali
      x + 3, baseY + dy,   // ujung bawah
      SSD1306_WHITE
    );
  }
}

// Bunga kecil yang goyang halus
void drawFlower(int x, int groundY, int h, int step) {
  if (h < 10) h = 10;
  if (h > 20) h = 20;

  // goyangan pucuk (−2..+2 px), beda fase per-x biar variatif
  int sway = (int)(sinf(0.25f * step + 0.10f * x) * 2.0f);

  int topX = x + sway;
  int topY = groundY - h;

  // batang (sedikit miring mengikuti sway)
  display.drawLine(x, groundY, topX, topY, SSD1306_WHITE);

  // daun kiri/kanan di tengah batang
  int midY = groundY - h/2;
  display.fillTriangle(x - 6, midY, x - 1, midY - 2, x - 1, midY + 2, SSD1306_WHITE);
  display.fillTriangle(x + 6, midY, x + 1, midY - 2, x + 1, midY + 2, SSD1306_WHITE);

  // kelopak (4–5 lingkaran kecil) + pusat bunga warna hitam (lubang)
  display.fillCircle(topX,     topY - 1, 2, SSD1306_WHITE);
  display.fillCircle(topX - 2, topY + 1, 2, SSD1306_WHITE);
  display.fillCircle(topX + 2, topY + 1, 2, SSD1306_WHITE);
  display.fillCircle(topX,     topY + 3, 2, SSD1306_WHITE);
  display.fillCircle(topX,     topY + 1, 1, SSD1306_BLACK); // pusat
}

void showIntroAnimation() {
  const char* L1 = "HAMSTER HABITAT";
  const char* L2 = "Temp & Humidity";
  const char* L3 = "Sensor";

  // FASE 1: STARFIELD
  struct Star { int x, y, v; };
  const int N = 28;
  Star stars[N];
  for (int i = 0; i < N; ++i) {
    stars[i].x = random(0, SCREEN_WIDTH);
    stars[i].y = random(0, SCREEN_HEIGHT);
    stars[i].v = (random(0, 100) < 60) ? 1 : 2;
  }

  for (int frame = 0; frame < 90; ++frame) {
    display.clearDisplay();

    for (int i = 0; i < N; ++i) {
      display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
      stars[i].x -= stars[i].v;
      if (stars[i].x < 0) {
        stars[i].x = SCREEN_WIDTH - 1;
        stars[i].y = random(0, SCREEN_HEIGHT);
        stars[i].v = (random(0, 100) < 60) ? 1 : 2;
      }
    }

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    int16_t x1,y1; uint16_t w,h; int x;

    display.getTextBounds((char*)L1, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 8);  display.print(L1);

    display.getTextBounds((char*)L2, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 20); display.print(L2);

    display.getTextBounds((char*)L3, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 32); display.print(L3);

    // underline berjalan di bawah L1
    display.getTextBounds((char*)L1, 0, 0, &x1, &y1, &w, &h);
    int ux = (SCREEN_WIDTH - (int)w)/2; if (ux<0) ux=0;
    int uw = (frame * (int)w) / 90;
    display.drawLine(ux, 17, ux + uw, 17, SSD1306_WHITE);

    display.display();
    delay(35);
  }
  delay(450);

  // FASE 2: HAMSTER (kepala) + TERMOMETER + TETES AIR
  for (int i = 0; i <= 100; i += 4) {
    display.clearDisplay();
    bool blink = ((i / 8) % 3 == 0);

    // hamster (kepala) muncul di fase ini
    drawHamster(6, 16, blink);

    // termometer & tetes air
    drawThermometer(100, 10, 36, i);
    int dropY = 10 + (i % 30);
    drawDroplet(80, dropY);

    // banner teks agar kontras
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.fillRect(0, 0, 128, 12, SSD1306_BLACK);
    display.setCursor(12, 2);  display.print("Sensor Suhu & RH");
    display.fillRect(0, 52, 128, 12, SSD1306_BLACK);
    display.setCursor(16, 54); display.print("Kandang Hamster");

    display.display();
    delay(70);
  }
  delay(500);

    // FASE 3: HAMSTER FULL-BODY lari (kanan→kiri) + bendera + awan + BUNGA
  int startX = SCREEN_WIDTH + 4;   // mulai di luar kanan
  int endX   = -48;                // keluar kiri (lebar hamster ~44px)
  int yRun   = 28;                 // jalur lari
  int ground = yRun + 30;          // garis tanah

  // Awan parallax
  int cX[3] = {128, 178, 220};
  int cY[3] = {  6,  12,   8};
  int cW[3] = { 28,  22,  18};
  int cV[3] = {  1,   1,   2};

  // Posisi bunga di tanah
  const int NB = 5;
  int fx[NB] = {16, 40, 72, 96, 116};
  int fh[NB] = {14, 12, 18, 16, 13};

  for (int x = startX, step = 0; x >= endX; x -= 2, ++step) {
    display.clearDisplay();

    // 1) Bendera atas
    drawBendera(step);

    // 2) Awan parallax
    for (int i = 0; i < 3; ++i) {
      drawCloud(cX[i], cY[i], cW[i]);
      cX[i] -= cV[i];
      if (cX[i] < -cW[i]) {
        cX[i] = SCREEN_WIDTH + random(6, 30);
        cY[i] = random(3, 14);
      }
    }

    // 3) Bunga-bunga di tanah (goyang halus)
    for (int i = 0; i < NB; ++i) {
      drawFlower(fx[i], ground, fh[i], step);
    }

    // 4) Hamster lari (menghadap kiri) + kedip sesekali
    bool blink = ((step % 18) == 0);
    drawHamsterFull(x, yRun, true /*facingLeft*/, blink);

    // 5) Garis "lantai"
    display.drawLine(0, ground, SCREEN_WIDTH - 1, ground, SSD1306_WHITE);

    display.display();
    delay(28); // kecepatan lari
  }
  delay(400);

  // FASE 4: Nama-nama
  const char* N1 = "Yunisa Wessy Riauna";
  const char* N2 = "Joy Veronica Purba";
  const char* N3 = "Agnes Dyan Pangesty";
  for (int off = 64; off >= 0; off -= 3) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    int16_t x1,y1; uint16_t w,h; int x;
    display.getTextBounds((char*)N1, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 12 + off); display.print(N1);

    display.getTextBounds((char*)N2, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 28 + off); display.print(N2);

    display.getTextBounds((char*)N3, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 44 + off); display.print(N3);

    display.display();
    delay(30);
  }
  delay(1200);

  // FASE 5: Progress bar "Menyiapkan sistem"
  for (int p = 0; p <= 100; p += 3) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    int16_t x1,y1; uint16_t w,h; int x;
    const char* msg = "Menyiapkan sistem";
    display.getTextBounds((char*)msg, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 24); display.print(msg);

    char pct[8]; sprintf(pct, "%d%%", p);
    display.getTextBounds(pct, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w)/2; if (x<0) x=0;
    display.setCursor(x, 34); display.print(pct);

    display.drawRect(8, 46, 112, 10, SSD1306_WHITE);
    int wbar = (p * 110) / 100;
    display.fillRect(9, 47, wbar, 8, SSD1306_WHITE);

    display.display();
    delay(45);
  }
  delay(350);
}

//Helper pembacaan DHT aman
const uint32_t DHT_MIN_INTERVAL_MS = 3000; // DHT21 aman >=2s, kita pakai 3s
unsigned long lastDHTRead = 0;

bool readDHTSafe(float &outT, float &outH, uint8_t attempts = 3) {
  unsigned long nowMs = millis();
  if (lastDHTRead != 0 && (nowMs - lastDHTRead) < DHT_MIN_INTERVAL_MS) {
    return true; // pakai nilai terakhir
  }
  for (uint8_t i = 0; i < attempts; i++) {
    float tt = dht.readTemperature();
    float hh = dht.readHumidity();
    if (!isnan(tt) && !isnan(hh)) {
      outT = tt; outH = hh;
      lastDHTRead = millis();
      return true;
    }
    delay(100);
  }
  return false;
}

// Prime pembacaan pertama (maks 10s)
bool primeDHT(uint32_t timeout_ms = 10000) {
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    float tt = dht.readTemperature();
    float hh = dht.readHumidity();
    if (!isnan(tt) && !isnan(hh)) {
      t = tt; h = hh;
      lastDHTRead = millis();
      return true;
    }
    delay(300);
  }
  return false;
}

void setup() {
  Serial.begin(9600);
  WiFiManager wm;
  bool res;

  // tarik PULLUP internal (membantu jika resistor eksternal lemah/panjang kabel)
  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin();

  // WiFi
  if (!wm.autoConnect("iichiy", "yunisa14")) {
    Serial.println("Gagal konek WiFi, restart...");
    ESP.restart();
  } else {
    Serial.println("connected...yeey :)");
  }

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  // Animasi pembuka
  showIntroAnimation();

  // RTC
  if (!rtc.begin()) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.setCursor(10, 26);
    display.print("RTC ERROR");
    display.display();
    Serial.println("RTC tidak terdeteksi!");
    while (1);
  }

  // SD Card
  if (!SD.begin(5)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("SD Card");
    display.println("gagal inisialisasi!");
    display.display();
    while (true);
  }

  // Cari & buat log baru
  logNumber = cariLogTerakhir() + 1;
  sprintf(fileName, "/log%02d.txt", logNumber);

  // ke detik 00
  while (true) {
    DateTime now = rtc.now();
    uint8_t s = now.second();
    uint8_t remain = (60 - s) % 60;
    if (remain == 0) break;
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    char txt[8];
    sprintf(txt, "%02u", remain);
    int16_t x1, y1; uint16_t w, htxt;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &htxt);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - htxt) / 2);
    display.print(txt);
    display.display();
  }

  // OLED tampilkan log aktif
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.print("Log Aktif:");
  display.println(fileName);
  display.display();

  Serial.print("Log Aktif: ");
  Serial.println(fileName);

  // Ubidots
  ubidots.setCallback(callback);
  ubidots.setup();
  ubidots.reconnect();

  // Prime DHT: ambil bacaan pertama yang valid (maks 10 detik)
  if (primeDHT(10000)) {
    Serial.println("DHT first reading OK.");
  } else {
    Serial.println("DHT first reading timeout; will retry in loop.");
  }

  timer = millis();
}
// Tulis teks rata-tengah di dalam kotak [x..x+w)
void printCenteredInBox(int x, int y, int w, const char* txt, int textSize = 1) {
  display.setTextSize(textSize);
  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds((char*)txt, 0, 0, &x1, &y1, &tw, &th);
  int cx = x + (w - (int)tw) / 2;
  if (cx < x) cx = x;                       // fallback aman
  display.setCursor(cx, y);
  display.print(txt);
}

// Format float 1 desimal tanpa padding (atau “--.-” jika NAN)
void fmt1(float v, char* out, size_t n) {
  if (isnan(v)) { strncpy(out, "--.-", n); out[n-1] = 0; return; }
  dtostrf(v, 0, 1, out);                    // width=0 -> tidak ada spasi depan
}

void loop() {
  if (!ubidots.connected()) {
    ubidots.reconnect();
  }

  DateTime now = rtc.now();

  // Baca DHT (throttle + retry)
  bool ok = readDHTSafe(t, h);
  sprintf(buffer, "%04d/%02d/%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());

  if (!ok) {
    Serial.println(F("Failed to read from DHT sensor! (retry next loop)"));
  }

  // Serial Monitor
  Serial.print("Waktu: ");
  Serial.println(buffer);
  Serial.print("Suhu: ");
  if (isnan(t)) Serial.print("--.-"); else Serial.print(t);
  Serial.print(" °C, Kelembapan: ");
  if (isnan(h)) Serial.print("--.-"); else Serial.print(h);
  Serial.println(" %");

  // OLED
    //Tampilan OLED
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // waktu
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(buffer);

  // log aktif
  display.setCursor(0, 10);
  display.print("Log Aktif: ");
  display.println(fileName);

  // --- area pengukuran (rata-tengah + pemisah tengah) ---
  const int COL_W = 64;   // lebar tiap kolom
  // Label
  printCenteredInBox(0,   24, COL_W, "TEMP", 1);
  printCenteredInBox(64,  24, COL_W, "HUM",  1);

  // Garis pemisah vertikal (hanya area bawah supaya tidak ganggu header)
  display.drawLine(64, 22, 64, 63, SSD1306_WHITE);

  // Nilai (1 desimal, rata-tengah per kolom)
  display.setTextSize(2);
  char vbuf[12];

  fmt1(t, vbuf, sizeof(vbuf));
  printCenteredInBox(0,   36, COL_W, vbuf, 2);

  fmt1(h, vbuf, sizeof(vbuf));
  printCenteredInBox(64,  36, COL_W, vbuf, 2);

  display.display();

  // Simpan ke SD tiap menit di detik 00
  if (now.second() != lastSecond) {
    lastSecond = now.second();
    if (lastSecond == 0) simpanData(now);
  }

  // Kirim ke Ubidots (hanya jika valid)
  if (millis() - timer >= PUBLISH_FREQUENCY) {
    if (!isnan(t)) ubidots.add(VARIABLE_TEM, t);
    if (!isnan(h)) ubidots.add(VARIABLE_HUM, h);
    if (!isnan(t) || !isnan(h)) {
      ubidots.publish(DEVICE_LABEL);
      Serial.println("Data terkirim ke Ubidots!");
    } else {
      Serial.println("Skip publish: belum ada data DHT valid.");
    }
    timer = millis();
  }

  ubidots.loop();
  delay(1000);
}

// simpan ke sd card
void simpanData(DateTime now) {
  bool fileBaru = !SD.exists(fileName);
  dataFile = SD.open(fileName, FILE_APPEND);

  if (dataFile) {
    if (fileBaru) {
      dataFile.println("Tanggal/Waktu\tSuhu\tKelembapan");
    }
    dataFile.print(buffer);
    dataFile.print("\t");
    if (isnan(t)) dataFile.print("nan"); else dataFile.print(t, 2);
    dataFile.print("\t");
    if (isnan(h)) dataFile.println("nan"); else dataFile.println(h, 2);
    dataFile.close();
    Serial.println("Data berhasil disimpan ke SD");
  } else {
    Serial.println("Gagal membuka file");
  }
}