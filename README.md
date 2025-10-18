# Suhu & Kelembapan Kandang Hamster (ESP32)

Monitoring suhu & kelembapan kandang hamster dengan **ESP32 + DHT21 + OLED SSD1306 + RTC DS3231 + microSD + Ubidots**.
Sketch menampilkan intro animasi (bintang → hamster + termometer → hamster lari dengan bendera, awan, bunga → nama → progress), lalu menampilkan data realtime, menyimpan log ke SD tiap menit di detik `:00`, dan mengirim data ke Ubidots tiap 15 detik (jika data valid).

---

## Fitur
- Intro OLED bertahap:
  1) Starfield + judul
  2) Hamster (kepala) + termometer + tetes air
  3) Hamster full-body lari (kanan→kiri) dengan bendera, awan, bunga
  4) Slide nama tim
  5) Progress “Menyiapkan sistem”
- UI OLED: label `TEMP`/`HUM` dan nilai rata-tengah per kolom + garis pemisah.
- DHT21 robust: throttle 3 s, prime awal ≤10 s, retry aman.
- Logging SD: `/logXX.txt`, tulis **setiap menit di detik :00**.
- Ubidots: publish tiap **15 s** ketika data valid.
- WiFi via WiFiManager (portal konfigurasi awal).

## Hardware
- ESP32 Dev Board
- DHT21 (AM2301) — DATA ke **GPIO 25** (dengan pull-up 10 kΩ ke 3.3V)
- OLED SSD1306 I²C 128×64 (0x3C) — SDA **GPIO 21**, SCL **GPIO 22**
- RTC DS3231 (I²C)
- microSD (CS = **GPIO 5**)

## Library
`WiFiManager`, `UbidotsEsp32Mqtt`, `Adafruit_GFX`, `Adafruit_SSD1306`, `RTClib`, `DHT`, `SD`, `FS`.

## Build & Setup
1. Buat file **`secrets.h`** (lokal, jangan di-commit):
   ```cpp
   #pragma once
   #define UBIDOTS_TOKEN "ISI_TOKEN_UBIDOTS_KAMU"

## Lisensi
Proyek ini berlisensi **MIT**. Lihat berkas [LICENSE](LICENSE).
