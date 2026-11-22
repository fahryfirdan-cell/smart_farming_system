#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ===================== PIN =====================
#define PIN_SOIL    34   // potensio (analog)
#define PIN_DHT      4
#define PIN_TRIG     5
#define PIN_ECHO    18
#define PIN_SERVO   13
#define PIN_RELAY   26
#define PIN_LED_R   25
#define PIN_LED_G   27
#define PIN_BUZZER  14

// ===================== WIFI & MQTT =====================
#define WIFI_SSID     "Wokwi-GUEST"   // WiFi bawaan simulator Wokwi
#define WIFI_PASS     ""
#define WIFI_CHANNEL  6

#define MQTT_SERVER   "broker.hivemq.com"
#define MQTT_PORT     1883

// GANTI "farm01" dengan nama unik milikmu (broker publik bisa dilihat siapa saja)
#define TOPIC_BASE        "smartfarm/farm01"

// Topik yang DIKIRIM ESP32 (dibaca dashboard)
#define T_STATUS          TOPIC_BASE "/status"
#define T_DATA            TOPIC_BASE "/data"
#define T_TANAH           TOPIC_BASE "/tanah"
#define T_SUHU            TOPIC_BASE "/suhu"
#define T_LEMBAP          TOPIC_BASE "/lembap"
#define T_TANDON          TOPIC_BASE "/tandon"
#define T_ATAP            TOPIC_BASE "/atap"
#define T_POMPA           TOPIC_BASE "/pompa"
#define T_MODE            TOPIC_BASE "/mode"

// Topik yang DITERIMA ESP32 (perintah dari dashboard)
#define T_MODE_SET        TOPIC_BASE "/mode/set"    // AUTO / MANUAL
#define T_ATAP_SET        TOPIC_BASE "/atap/set"    // BUKA / TUTUP   (mode MANUAL)
#define T_POMPA_SET       TOPIC_BASE "/pompa/set"   // ON / OFF       (mode MANUAL)

// ===================== PENGATURAN =====================
// Jadwal atap (servo): terbuka dari jam BUKA sampai sebelum jam TUTUP
#define ATAP_BUKA_JAM     7
#define ATAP_TUTUP_JAM    11
#define SERVO_BUKA        90   // derajat
#define SERVO_TUTUP       0

// Jadwal siram: jam 6 dan 17, durasi jendela 30 menit
#define SIRAM_JAM_1       6
#define SIRAM_JAM_2       17
#define SIRAM_MENIT_MAX   30

// Batas kelembapan tanah (%), histeresis supaya pompa tidak on-off terus
#define TANAH_KERING      40
#define TANAH_CUKUP       60

// Level tandon (HC-SR04 dipasang di atas tandon), cm
#define JARAK_TANDON_KOSONG 20

// Relay aktif HIGH (modul relay Wokwi)
#define RELAY_ON   HIGH
#define RELAY_OFF  LOW

// Interval (milidetik)
#define INTERVAL_SENSOR   1000
#define INTERVAL_PUBLISH  5000
#define INTERVAL_WIFI     10000
#define INTERVAL_MQTT     5000

// ===================== OBJEK =====================
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(PIN_DHT, DHT22);
Servo atap;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ===================== STATUS =====================
bool atapTerbuka = false;
bool pompaNyala  = false;

// Mode kontrol dari dashboard
bool modeManual   = false;   // false = AUTO (RTC), true = MANUAL (dashboard)
bool manualAtap   = false;
bool manualPompa  = false;

// Data sensor terakhir (untuk dikirim ke MQTT)
int   gTanah = 0;
float gSuhu = 0, gLembap = 0, gJarak = 0;
bool  gTandonKosong = false;
char  gWaktu[9] = "00:00:00";

unsigned long tSensor = 0, tPublish = 0, tWifi = 0, tMqtt = 0;
bool perluPublish = false;

// ===================== FUNGSI BANTU =====================
int bacaTanahPersen() {
  int raw = analogRead(PIN_SOIL);               // 0 - 4095
  // Potensio: putar ke kanan = nilai besar = tanah basah
  return map(raw, 0, 4095, 0, 100);
}

float bacaJarakCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long durasi = pulseIn(PIN_ECHO, HIGH, 30000); // timeout 30 ms
  if (durasi == 0) return 999;                  // tidak ada echo
  return durasi * 0.0343 / 2.0;
}

void setAtap(bool buka) {
  if (buka == atapTerbuka) return;              // hanya gerak jika berubah
  atap.write(buka ? SERVO_BUKA : SERVO_TUTUP);
  atapTerbuka = buka;
  perluPublish = true;
}

void setPompa(bool nyala) {
  if (nyala == pompaNyala) return;
  pompaNyala = nyala;
  digitalWrite(PIN_RELAY, nyala ? RELAY_ON : RELAY_OFF);
  perluPublish = true;
}

// ===================== MQTT =====================
void publishData() {
  if (!mqtt.connected()) return;

  char buf[256];
  char angka[12];

  snprintf(buf, sizeof(buf),
           "{\"waktu\":\"%s\",\"tanah\":%d,\"suhu\":%.1f,\"lembap\":%.0f,"
           "\"jarak\":%.0f,\"tandon\":\"%s\",\"atap\":\"%s\",\"pompa\":\"%s\",\"mode\":\"%s\"}",
           gWaktu, gTanah, gSuhu, gLembap, gJarak,
           gTandonKosong ? "KOSONG" : "OK",
           atapTerbuka ? "BUKA" : "TUTUP",
           pompaNyala ? "ON" : "OFF",
           modeManual ? "MANUAL" : "AUTO");
  mqtt.publish(T_DATA, buf, true);

  snprintf(angka, sizeof(angka), "%d", gTanah);
  mqtt.publish(T_TANAH, angka, true);
  snprintf(angka, sizeof(angka), "%.1f", gSuhu);
  mqtt.publish(T_SUHU, angka, true);
  snprintf(angka, sizeof(angka), "%.0f", gLembap);
  mqtt.publish(T_LEMBAP, angka, true);
  mqtt.publish(T_TANDON, gTandonKosong ? "KOSONG" : "OK", true);
  mqtt.publish(T_ATAP, atapTerbuka ? "BUKA" : "TUTUP", true);
  mqtt.publish(T_POMPA, pompaNyala ? "ON" : "OFF", true);
  mqtt.publish(T_MODE, modeManual ? "MANUAL" : "AUTO", true);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String m = "";
  for (unsigned int i = 0; i < length; i++) m += (char)payload[i];
  m.trim();
  m.toUpperCase();

  Serial.print("MQTT masuk [");
  Serial.print(t);
  Serial.print("] ");
  Serial.println(m);

  if (t == T_MODE_SET) {
    modeManual = (m == "MANUAL");
    if (modeManual) {                           // mulai dari kondisi saat ini
      manualAtap  = atapTerbuka;
      manualPompa = pompaNyala;
    }
  } else if (t == T_ATAP_SET) {
    manualAtap = (m == "BUKA" || m == "OPEN" || m == "1" || m == "ON");
  } else if (t == T_POMPA_SET) {
    manualPompa = (m == "ON" || m == "1" || m == "NYALA");
  }
  perluPublish = true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
  Serial.print("Menghubungkan WiFi");
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 15000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" tersambung");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" gagal (lanjut tanpa WiFi, akan dicoba lagi)");
  }
}

void connectMQTT() {
  String clientId = "SmartFarm-" + String(random(0xffff), HEX);
  Serial.print("Menghubungkan MQTT...");
  // Last Will: otomatis "offline" jika ESP32 terputus
  if (mqtt.connect(clientId.c_str(), NULL, NULL, T_STATUS, 0, true, "offline")) {
    Serial.println(" tersambung");
    mqtt.publish(T_STATUS, "online", true);
    mqtt.subscribe(T_MODE_SET);
    mqtt.subscribe(T_ATAP_SET);
    mqtt.subscribe(T_POMPA_SET);
    perluPublish = true;
  } else {
    Serial.print(" gagal, rc=");
    Serial.println(mqtt.state());
  }
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_OFF);

  Wire.begin(21, 22);                           // SDA, SCL

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Farming");
  lcd.setCursor(0, 1);
  lcd.print("Memulai...");

  dht.begin();

  atap.setPeriodHertz(50);
  atap.attach(PIN_SERVO, 500, 2400);
  atap.write(SERVO_TUTUP);

  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi!");
    lcd.clear();
    lcd.print("RTC ERROR");
    while (true) delay(1000);
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  lcd.setCursor(0, 1);
  lcd.print("Hubung WiFi... ");
  connectWiFi();

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  lcd.clear();
}

// ===================== LOOP =====================
void loop() {
  unsigned long sekarang = millis();

  // ---------- Jaga koneksi WiFi & MQTT (tanpa memblokir program) ----------
  if (WiFi.status() != WL_CONNECTED) {
    if (sekarang - tWifi > INTERVAL_WIFI) {
      tWifi = sekarang;
      WiFi.reconnect();
    }
  } else if (!mqtt.connected()) {
    if (sekarang - tMqtt > INTERVAL_MQTT) {
      tMqtt = sekarang;
      connectMQTT();
    }
  }
  mqtt.loop();

  // ---------- Kirim data ke dashboard ----------
  if (perluPublish || sekarang - tPublish >= INTERVAL_PUBLISH) {
    tPublish = sekarang;
    perluPublish = false;
    publishData();
  }

  // ---------- Proses sensor & kontrol tiap 1 detik ----------
  if (sekarang - tSensor < INTERVAL_SENSOR) return;
  tSensor = sekarang;

  DateTime now = rtc.now();
  int jam   = now.hour();
  int menit = now.minute();
  snprintf(gWaktu, sizeof(gWaktu), "%02d:%02d:%02d", jam, menit, now.second());

  gTanah = bacaTanahPersen();
  float suhu   = dht.readTemperature();
  float lembap = dht.readHumidity();
  gSuhu   = isnan(suhu) ? 0 : suhu;
  gLembap = isnan(lembap) ? 0 : lembap;
  gJarak  = bacaJarakCm();
  gTandonKosong = (gJarak > JARAK_TANDON_KOSONG);

  // ---------- 1. ATAP ----------
  if (modeManual) {
    setAtap(manualAtap);                        // perintah dari dashboard
  } else {
    setAtap(jam >= ATAP_BUKA_JAM && jam < ATAP_TUTUP_JAM);  // jadwal RTC
  }

  // ---------- 2. POMPA ----------
  if (gTandonKosong) {
    setPompa(false);                            // pengaman: air habis
  } else if (modeManual) {
    setPompa(manualPompa);                      // perintah dari dashboard
  } else {
    bool jendelaSiram = ((jam == SIRAM_JAM_1 || jam == SIRAM_JAM_2) &&
                         menit < SIRAM_MENIT_MAX);
    if (!jendelaSiram)                setPompa(false);
    else if (gTanah < TANAH_KERING)   setPompa(true);
    else if (gTanah >= TANAH_CUKUP)   setPompa(false);
    // antara 40-60: pertahankan status terakhir
  }

  // ---------- 3. INDIKATOR ----------
  digitalWrite(PIN_LED_R, gTanah < TANAH_KERING ? HIGH : LOW);
  digitalWrite(PIN_LED_G, gTanah >= TANAH_KERING ? HIGH : LOW);
  if (gTandonKosong) tone(PIN_BUZZER, 1000, 200);

  // ---------- 4. LCD ----------
  // Baris 1: jam, suhu, kelembapan udara, huruf M = MQTT tersambung
  // Baris 2: T = tanah, status atap, status pompa, A/M = mode AUTO/MANUAL
  char baris1[17], baris2[17];
  snprintf(baris1, sizeof(baris1), "%02d:%02d %4.1fC %2.0f%%%c",
           jam, menit, gSuhu, gLembap, mqtt.connected() ? 'M' : '-');
  snprintf(baris2, sizeof(baris2), "T:%3d%% %s %s%c",
           gTanah, atapTerbuka ? "BUKA" : "TTP ", pompaNyala ? "ON " : "OFF",
           modeManual ? 'M' : 'A');
  lcd.setCursor(0, 0);
  lcd.print(baris1);
  lcd.setCursor(0, 1);
  lcd.print(baris2);

  // ---------- 5. SERIAL MONITOR ----------
  Serial.printf("%s | Tanah %d%% | Suhu %.1fC | RH %.0f%% | Jarak %.0fcm | Atap %s | Pompa %s | %s | WiFi %s | MQTT %s%s\n",
                gWaktu, gTanah, gSuhu, gLembap, gJarak,
                atapTerbuka ? "BUKA" : "TUTUP",
                pompaNyala ? "ON" : "OFF",
                modeManual ? "MANUAL" : "AUTO",
                WiFi.status() == WL_CONNECTED ? "OK" : "putus",
                mqtt.connected() ? "OK" : "putus",
                gTandonKosong ? " | TANDON KOSONG" : "");
}
