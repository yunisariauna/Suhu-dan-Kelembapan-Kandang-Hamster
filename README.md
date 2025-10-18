# Suhu & Kelembapan Kandang Hamster (ESP32)
ESP32 + DHT21 + OLED SSD1306 + RTC DS3231 + microSD + Ubidots.
- Intro OLED: starfield → hamster + termometer → hamster lari (bendera, awan, bunga) → nama → progress
- Logging ke SD tiap menit di detik :00
- Publish ke Ubidots tiap 15 detik (jika data valid)

**Token**: simpan di `secrets.h` (tidak di repo). Lihat `secrets.example.h`.
