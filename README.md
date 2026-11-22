# smart_farming_system
# 🌱 Smart Farming System berbasis ESP32 & MQTT

Sistem smart farming berbasis **ESP32** yang memantau kondisi lahan (kelembapan tanah, suhu, kelembapan udara, dan level air tandon) lalu mengontrol **pompa air** dan **atap otomatis**. Data dikirim secara real-time lewat protokol **MQTT**, sehingga bisa dipantau dari jarak jauh. Proyek ini disimulasikan di **[Wokwi](https://wokwi.com/projects/481056907686318081)**.

---

## 📌 Fitur Utama

- 🌡️ Monitoring **suhu & kelembapan udara** (DHT22)
- 💧 Monitoring **kelembapan tanah** (disimulasikan dengan potensiometer)
- 🪣 Monitoring **kapasitas air tandon** (sensor ultrasonik HC-SR04)
- 🚰 **Pompa air otomatis** berdasarkan kondisi tanah (disimulasikan dengan LED kuning + relay)
- 🏠 **Atap otomatis** dengan servo yang dikontrol RTC (buka jam 07:00, tutup jam 18:00)
- 📟 Tampilan data langsung di **LCD I2C**
- 🔔 **Buzzer & LED indikator** sebagai peringatan
- 📡 Pengiriman data ke **MQTT broker** untuk dashboard monitoring

---

## 🧰 Komponen yang Digunakan

| Komponen | Fungsi | Pin ESP32 |
|---|---|---|
| ESP32 DevKit | Mikrokontroler utama + WiFi | - |
| Potensiometer | **Pengganti soil moisture sensor** | GPIO 34 (analog) |
| DHT22 | Sensor suhu & kelembapan udara | GPIO 4 |
| HC-SR04 | Sensor jarak (level air tandon) | TRIG: GPIO 5, ECHO: GPIO 18 |
| Servo | Penggerak atap | GPIO 13 |
| Relay | Saklar pompa air | GPIO 26 |
| LED Kuning (via relay) | **Pengganti pompa air** | Terhubung ke relay |
| LED Merah | Indikator peringatan | GPIO 25 |
| LED Hijau | Indikator peringatan | GPIO 27 |
| Buzzer | Alarm peringatan | GPIO 14 |
| RTC DS1307 | Jadwal buka/tutup atap | I2C (SDA/SCL) |
| LCD 16x2 I2C | Menampilkan data sensor | I2C (SDA/SCL) |

---

## 🔄 Cara Kerja Sistem

### 1. Pembacaan Sensor
- **Potensiometer** menggantikan *soil moisture sensor*. Nilai analog (0–4095) dikonversi menjadi persentase kelembapan tanah (0–100%). Memutar potensiometer = mensimulasikan tanah yang makin kering atau makin basah.
- **DHT22** membaca suhu dan kelembapan udara.
- **HC-SR04** mengukur jarak permukaan air ke sensor untuk mengetahui sisa air di tandon.

### 2. Klasifikasi Kondisi Tanah

| Kelembapan Tanah | Status |
|---|---|
| < 40% | 🟤 Kering |
| 40% – 60% | 🟢 Lembab |
| > 60% | 🔵 Basah |

### 3. Kontrol Pompa Air
- Pompa dikontrol **hanya berdasarkan sensor tanah**.
- Saat tanah **kering**, relay aktif sehingga **LED kuning menyala** (mewakili pompa air menyala).
- Saat tanah sudah cukup lembab/basah, relay mati sehingga **LED kuning padam** (pompa mati).
- Pada implementasi nyata, LED kuning dapat diganti dengan **pompa air DC** melalui relay yang sama.

### 4. Kontrol Atap (RTC + Servo)
- **RTC** hanya dipakai untuk mengatur atap.
- Atap **dibuka jam 07:00** agar sinar matahari masuk, dan **ditutup jam 18:00**.
- Status atap (`BUKA` / `TUTUP`) ditampilkan di Serial Monitor dan dikirim lewat MQTT.

### 5. Peringatan
- LED dan buzzer memberi peringatan bila tanah kering atau air tandon hampir habis.
- Kondisi peringatan juga tampil di LCD.

### 6. Monitoring via MQTT
ESP32 terhubung ke WiFi lalu mengirim (*publish*) data ke broker MQTT publik **HiveMQ** (`broker.hivemq.com`, port `1883`).

| Topik | Isi |
|---|---|
| `smartfarm/farm01/status` | Status koneksi / sistem |
| `smartfarm/farm01/data` | Data sensor gabungan |
| `smartfarm/farm01/tanah` | Data kelembapan tanah |

> ⚠️ Broker HiveMQ bersifat **publik**, jadi siapa saja bisa melihat topiknya. Ganti `farm01` dengan nama unik milikmu agar datamu tidak bercampur dengan orang lain.

Contoh output di Serial Monitor:

```
18:22:48 | Tanah 52% | Suhu 29.0C | RH 50% | Jarak 2cm | Atap TUTUP | Pompa OFF | AUTO | WiFi OK | MQTT OK
```

---

## 🚀 Cara Menjalankan

### Simulasi di Wokwi
1. Buka proyek Wokwi: [smart farming](https://wokwi.com/projects/481056907686318081)
2. Klik tombol **▶ Start Simulation**.
3. Putar **potensiometer** untuk mengubah kelembapan tanah dan lihat pompa (LED kuning) menyala/mati.
4. Lihat output di **Serial Monitor** dan LCD.

### Memantau lewat MQTT
1. Buka MQTT client, misalnya (https://smartfarminghiu.netlify.app).
3. Hubungkan ke broker `broker.hivemq.com`, port `1883` (atau port WebSocket `8000`/`8884` untuk web client).
4. *Subscribe* ke topik `smartfarm/farm01/#`.
5. Data sensor akan muncul secara real-time.

---

## 📶 Konfigurasi WiFi & MQTT

Di Wokwi, ESP32 memakai WiFi bawaan simulator:

```cpp
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASS     ""
#define WIFI_CHANNEL  6

#define MQTT_SERVER   "broker.hivemq.com"
#define MQTT_PORT     1883
#define TOPIC_BASE    "smartfarm/farm01"
```

Jika dipasang di ESP32 asli, ganti `WIFI_SSID` dan `WIFI_PASS` dengan WiFi rumahmu.

---

## 📚 Library yang Digunakan

- `Wire.h`
- `WiFi.h`
- `PubSubClient.h`
- `RTClib.h`
- `LiquidCrystal_I2C.h`
- `DHT.h`
- `ESP32Servo.h`

---

## 📁 Struktur Repository

```
smart_farming_system/
├── sketch.ino        # Kode program utama ESP32
├── diagram.json      # Rangkaian Wokwi
├── libraries.txt     # Daftar library
└── README.md         # Dokumentasi proyek
```

---

## 🔮 Pengembangan Selanjutnya

- Mengganti potensiometer dengan **soil moisture sensor** asli
- Mengganti LED kuning dengan **pompa air DC** asli
- Membuat **dashboard web / mobile** untuk monitoring dan kontrol manual
- Menambahkan notifikasi (Telegram / email) saat tanah kering atau tandon kosong

---

## 👤 Author

Dibuat oleh 
- **Muhammad Firdan Fahry** — [Teknik Komputer, Telkom University]
- **Sabrina Aprillia** — [Teknik Telekomunikasi, Telkom University]
- **Sabrina Aprillia** — [Teknik Biomedis, Telkom University]
