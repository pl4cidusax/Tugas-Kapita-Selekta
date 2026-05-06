/* =============================================================================
 *  IoT TCP Socket Programming - ESP32 Client (Hardware Nyata)
 *  -----------------------------------------------------------------------------
 *  Penulis  : Kelompok 4
 *  Hardware : ESP32 DevKit + SSD1306 OLED + HC-SR04 + LDR module + Water sensor
 *             + Servo SG90 + 3 Push Button + 3 LED + MB-102 power supply
 *  Protokol : TCP socket murni (port default 5000) langsung ke laptop di LAN
 *             yang sama. Tidak pakai HTTP, tidak pakai ngrok.
 *
 *  ===========================================================================
 *  STRUKTUR MODULAR
 *  ===========================================================================
 *  Untuk MENAMBAH SENSOR baru:
 *    1. Tulis fungsi `void initX()` dan `float readX()`.
 *    2. Tambah satu baris di array `sensors[]`:
 *         {"key_json", initX, readX, 0}
 *    3. (Opsional) Tambah satu baris di updateDisplay() supaya muncul di OLED.
 *
 *  Untuk MENAMBAH COMMAND dari server:
 *    1. Tulis fungsi `void cmdX(const String& arg)`.
 *    2. Tambah satu baris di array `commands[]`:
 *         {"NAMA_COMMAND", cmdX}
 *
 *  ===========================================================================
 *  TOMBOL
 *  ===========================================================================
 *    Tombol A (GPIO 14) : Toggle AUTO mode ON/OFF
 *    Tombol B (GPIO 32) : Kirim data sensor SEKARANG (manual)
 *    Tombol C (GPIO 33) : Cycle posisi servo (0 -> 90 -> 180 -> 0 ...)
 *
 *  ===========================================================================
 *  LED INDIKATOR
 *  ===========================================================================
 *    HIJAU  (GPIO 27) : SUKSES (terhubung & kirim/terima OK)
 *    KUNING (GPIO 26) : BUSY (sedang menyambung / mengirim)
 *    MERAH  (GPIO 25) : ERROR (WiFi mati / koneksi terputus)
 *
 *  ===========================================================================
 *  PROTOKOL TCP (line-delimited, satu pesan per baris diakhiri "\n")
 *  ===========================================================================
 *  ESP32 -> Server (JSON):
 *    {"type":"hello","client":"esp32-kel4"}
 *    {"type":"data","dist":12.34,"light":2150,"water":456}
 *    {"type":"event","button":"A","auto":true}
 *
 *  Server -> ESP32 (plain text, "NAMA <argumen>"):
 *    SERVO 90
 *    MSG hello dunia
 *    LED red on
 *    LED green off
 *    PING
 * ============================================================================= */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// ---------------------------------------------------------------------------
// 1. KONFIGURASI WIFI & SERVER
// ---------------------------------------------------------------------------
// GANTI dengan SSID & password WiFi yang sama dengan laptop server
const char* WIFI_SSID = "GANTI_NAMA_WIFI";
const char* WIFI_PASS = "GANTI_PASSWORD_WIFI";

// GANTI dengan IP laptop di jaringan lokal.
// Cek di laptop: `ipconfig` (Windows) atau `ifconfig` / `ip a` (Linux/Mac).
// Cari "IPv4 Address" di adapter WiFi yang aktif. Contoh: 192.168.1.42
const char* SERVER_IP   = "192.168.1.42";
const uint16_t SERVER_PORT = 5000;

// ---------------------------------------------------------------------------
// 2. PIN MAPPING
// ---------------------------------------------------------------------------
// Sensor
#define PIN_TRIG     5    // HC-SR04 trigger (output)
#define PIN_ECHO    18    // HC-SR04 echo    (input)
#define PIN_LDR     34    // LDR module AO   (ADC1, input-only)
#define PIN_WATER   35    // Water sensor S  (ADC1, input-only)

// Aktuator
#define PIN_SERVO    4    // Servo signal (PWM)

// Tombol (active LOW, INPUT_PULLUP)
// Hindari GPIO 0/2/12/15 - itu strapping pin yang bisa bikin boot fail
#define BTN_A       14    // Toggle AUTO mode
#define BTN_B       32    // Kirim manual
#define BTN_C       33    // Cycle servo

// LED status (active HIGH)
#define LED_RED     25
#define LED_GREEN   27
#define LED_YELLOW  26

// I2C OLED: SDA=21, SCL=22 (default ESP32)

// ---------------------------------------------------------------------------
// 3. OBJEK GLOBAL
// ---------------------------------------------------------------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo servo;
WiFiClient client;          // socket TCP ke server

// ---------------------------------------------------------------------------
// 4. STATE
// ---------------------------------------------------------------------------
bool autoMode               = true;
int  servoAngle             = 0;
unsigned long lastSendMs    = 0;
unsigned long lastBtnMs     = 0;
unsigned long lastSensorMs  = 0;
unsigned long lastDrawMs    = 0;
unsigned long lastReconMs   = 0;
unsigned long sentCount     = 0;
String        lastMsgFromServer = "(none)";
String        rxBuffer;     // buffer baca line-by-line dari server

const unsigned long PERIOD_MS    = 3000;   // interval kirim AUTO
const unsigned long DEBOUNCE_MS  = 250;
const unsigned long RECONNECT_MS = 5000;   // jeda antar percobaan reconnect

// ---------------------------------------------------------------------------
// 5. UTILITY: LED status (mutually exclusive)
// ---------------------------------------------------------------------------
enum LedState { LED_OFF, LED_OK, LED_BUSY, LED_ERR };
void setLed(LedState s) {
  digitalWrite(LED_GREEN,  s == LED_OK   ? HIGH : LOW);
  digitalWrite(LED_YELLOW, s == LED_BUSY ? HIGH : LOW);
  digitalWrite(LED_RED,    s == LED_ERR  ? HIGH : LOW);
}

// ===========================================================================
// 6. SENSOR REGISTRY  -- bagian utama untuk extensibility
// ===========================================================================
typedef void  (*SensorInitFn)();
typedef float (*SensorReadFn)();

struct Sensor {
  const char*  key;        // nama field di JSON yang dikirim ke server
  SensorInitFn init;       // fungsi inisialisasi (panggil di setup)
  SensorReadFn read;       // fungsi baca, kembalikan float
  float        lastValue;  // cache nilai terakhir untuk display & kirim
};

// ---- Implementasi sensor: HC-SR04 ----
void  initUltrasonic() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
}
float readUltrasonic() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long dur = pulseIn(PIN_ECHO, HIGH, 30000UL);   // timeout 30 ms
  if (dur == 0) return -1.0f;
  return (dur * 0.0343f) / 2.0f;                 // cm
}

// ---- Implementasi sensor: LDR module ----
void  initLDR() { /* analog input - tidak perlu setup pin */ }
float readLDR() { return (float)analogRead(PIN_LDR); }   // 0..4095

// ---- Implementasi sensor: Water sensor ----
void  initWater() { /* analog input - tidak perlu setup pin */ }
float readWater() { return (float)analogRead(PIN_WATER); }

// ---- Daftar sensor aktif ----
// Tambah sensor baru cukup tambah satu baris di sini.
Sensor sensors[] = {
  { "dist",  initUltrasonic, readUltrasonic, 0 },
  { "light", initLDR,        readLDR,        0 },
  { "water", initWater,      readWater,      0 }
};
const int N_SENSORS = sizeof(sensors) / sizeof(sensors[0]);

// ===========================================================================
// 7. COMMAND REGISTRY  -- handler untuk pesan dari server
// ===========================================================================
typedef void (*CmdFn)(const String& arg);

struct Command {
  const char* name;
  CmdFn       handler;
};

// ---- Handler: SERVO <angle> ----
void cmdServo(const String& arg) {
  int a = arg.toInt();
  if (a < 0)   a = 0;
  if (a > 180) a = 180;
  servoAngle = a;
  servo.write(a);
  Serial.println("[CMD] SERVO -> " + String(a));
}

// ---- Handler: MSG <text> ----
void cmdMsg(const String& arg) {
  lastMsgFromServer = arg;
  Serial.println("[CMD] MSG -> " + arg);
}

// ---- Handler: LED <color> <on|off> ----
void cmdLed(const String& arg) {
  int sp = arg.indexOf(' ');
  if (sp < 0) { Serial.println("[CMD] LED format salah"); return; }
  String color = arg.substring(0, sp); color.trim();
  String state = arg.substring(sp + 1); state.trim();
  bool on = (state == "on" || state == "1" || state == "ON");

  int pin = -1;
  if      (color == "red")    pin = LED_RED;
  else if (color == "green")  pin = LED_GREEN;
  else if (color == "yellow") pin = LED_YELLOW;
  else { Serial.println("[CMD] LED warna tidak dikenal: " + color); return; }

  digitalWrite(pin, on ? HIGH : LOW);
  Serial.println("[CMD] LED " + color + " -> " + (on ? "ON" : "OFF"));
}

// ---- Handler: PING (server cek apakah ESP32 hidup) ----
void cmdPing(const String& arg) {
  if (client.connected()) client.println("{\"type\":\"pong\"}");
  Serial.println("[CMD] PING -> pong");
}

// ---- Daftar command aktif ----
// Tambah command baru cukup tambah satu baris di sini.
Command commands[] = {
  { "SERVO", cmdServo },
  { "MSG",   cmdMsg   },
  { "LED",   cmdLed   },
  { "PING",  cmdPing  }
};
const int N_COMMANDS = sizeof(commands) / sizeof(commands[0]);

// ---------------------------------------------------------------------------
// 8. OLED: satu fungsi untuk redraw seluruh layar
// ---------------------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Baris 1: mode + status koneksi
  display.print(F("Mode:"));
  display.print(autoMode ? F("AUTO ") : F("MANL "));
  display.println(client.connected() ? F("ON") : F("OFF"));

  // Baris 2: IP
  if (WiFi.status() == WL_CONNECTED) display.println(WiFi.localIP());
  else                               display.println(F("WiFi offline"));

  // Baris 3-5: nilai sensor (bisa diperbanyak/dipersingkat sesuai daftar sensor)
  display.print(F("Dst:"));
  if (sensors[0].lastValue < 0) display.println(F(" --"));
  else { display.print(sensors[0].lastValue, 1); display.println(F(" cm")); }

  display.print(F("Lit:"));
  display.println((int)sensors[1].lastValue);

  display.print(F("Wtr:"));
  display.println((int)sensors[2].lastValue);

  // Baris 6: pesan terakhir dari server (potong jika kepanjangan)
  display.print(F("M:"));
  String m = lastMsgFromServer;
  if (m.length() > 18) m = m.substring(0, 18);
  display.println(m);

  display.display();
}

// ---------------------------------------------------------------------------
// 9. Baca semua sensor sekaligus (panggil tiap N ms)
// ---------------------------------------------------------------------------
void readAllSensors() {
  for (int i = 0; i < N_SENSORS; i++) {
    sensors[i].lastValue = sensors[i].read();
  }
}

// ---------------------------------------------------------------------------
// 10. Kirim data sensor ke server (satu baris JSON)
// ---------------------------------------------------------------------------
bool sendData() {
  if (!client.connected()) return false;

  setLed(LED_BUSY);

  // Bangun JSON secara dinamis berdasarkan isi sensors[]
  String json = "{\"type\":\"data\"";
  for (int i = 0; i < N_SENSORS; i++) {
    json += ",\"";
    json += sensors[i].key;
    json += "\":";
    json += String(sensors[i].lastValue, 2);
  }
  json += "}\n";

  size_t written = client.print(json);
  if (written == json.length()) {
    sentCount++;
    setLed(LED_OK);
    Serial.println("[TX] " + json);
    return true;
  } else {
    setLed(LED_ERR);
    Serial.println("[TX] gagal kirim (short write)");
    return false;
  }
}

// ---------------------------------------------------------------------------
// 11. Polling pesan masuk dari server (non-blocking)
// ---------------------------------------------------------------------------
void handleCommandLine(const String& line) {
  // Format: "NAMA <args...>"
  int sp = line.indexOf(' ');
  String name = (sp < 0) ? line : line.substring(0, sp);
  String arg  = (sp < 0) ? ""   : line.substring(sp + 1);
  name.trim(); arg.trim();
  name.toUpperCase();

  for (int i = 0; i < N_COMMANDS; i++) {
    if (name == commands[i].name) {
      commands[i].handler(arg);
      return;
    }
  }
  Serial.println("[CMD] command tidak dikenal: " + name);
}

void pollIncoming() {
  while (client.connected() && client.available()) {
    char c = client.read();
    if (c == '\n') {
      rxBuffer.trim();
      if (rxBuffer.length() > 0) handleCommandLine(rxBuffer);
      rxBuffer = "";
    } else if (c != '\r') {
      rxBuffer += c;
      if (rxBuffer.length() > 200) rxBuffer = "";   // safety guard
    }
  }
}

// ---------------------------------------------------------------------------
// 12. Pastikan terhubung ke server (reconnect bila perlu)
// ---------------------------------------------------------------------------
void ensureConnected() {
  if (client.connected()) return;
  if (millis() - lastReconMs < RECONNECT_MS) return;
  lastReconMs = millis();

  setLed(LED_BUSY);
  Serial.print("[NET] connect ke ");
  Serial.print(SERVER_IP); Serial.print(":"); Serial.println(SERVER_PORT);

  if (client.connect(SERVER_IP, SERVER_PORT)) {
    client.setNoDelay(true);
    setLed(LED_OK);
    Serial.println("[NET] CONNECTED");
    // Kirim hello supaya server tahu siapa
    client.println("{\"type\":\"hello\",\"client\":\"esp32-kel4\"}");
  } else {
    setLed(LED_ERR);
    Serial.println("[NET] connect FAILED");
  }
}

// ---------------------------------------------------------------------------
// 13. SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Kelompok 4 - ESP32 IoT TCP Client ===");

  // GPIO non-sensor
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_C, INPUT_PULLUP);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  setLed(LED_OFF);

  // OLED
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERR] OLED 0x3C tidak ditemukan!");
    setLed(LED_ERR);
    while (true) delay(200);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Kelompok 4"));
  display.println(F("IoT TCP Client"));
  display.println(F("Booting..."));
  display.display();

  // Inisialisasi semua sensor (loop di atas sensors[])
  for (int i = 0; i < N_SENSORS; i++) sensors[i].init();

  // Servo
  servo.attach(PIN_SERVO);
  servo.write(servoAngle);

  // WiFi
  setLed(LED_BUSY);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] connecting");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000UL) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] OK, IP=" + WiFi.localIP().toString());
    setLed(LED_OK);
  } else {
    Serial.println("[WiFi] FAILED");
    setLed(LED_ERR);
  }
}

// ---------------------------------------------------------------------------
// 14. LOOP UTAMA
// ---------------------------------------------------------------------------
void loop() {

  // 14a. Pastikan TCP tersambung
  ensureConnected();

  // 14b. Baca semua sensor tiap 500 ms
  if (millis() - lastSensorMs > 500) {
    lastSensorMs = millis();
    readAllSensors();
  }

  // 14c. Refresh OLED tiap 500 ms
  if (millis() - lastDrawMs > 500) {
    lastDrawMs = millis();
    updateDisplay();
  }

  // 14d. Cek pesan masuk dari server
  pollIncoming();

  // 14e. Tombol A: toggle AUTO
  if (digitalRead(BTN_A) == LOW && millis() - lastBtnMs > DEBOUNCE_MS) {
    lastBtnMs = millis();
    autoMode = !autoMode;
    Serial.println(autoMode ? "[BTN A] AUTO ON" : "[BTN A] AUTO OFF");
    if (client.connected()) {
      String ev = "{\"type\":\"event\",\"button\":\"A\",\"auto\":";
      ev += (autoMode ? "true" : "false");
      ev += "}\n";
      client.print(ev);
    }
  }

  // 14f. Tombol B: kirim manual
  if (digitalRead(BTN_B) == LOW && millis() - lastBtnMs > DEBOUNCE_MS) {
    lastBtnMs = millis();
    Serial.println("[BTN B] manual send");
    sendData();
  }

  // 14g. Tombol C: cycle servo 0 -> 90 -> 180 -> 0 ...
  if (digitalRead(BTN_C) == LOW && millis() - lastBtnMs > DEBOUNCE_MS) {
    lastBtnMs = millis();
    servoAngle = (servoAngle == 0) ? 90 : (servoAngle == 90 ? 180 : 0);
    servo.write(servoAngle);
    Serial.println("[BTN C] servo -> " + String(servoAngle));
    if (client.connected()) {
      String ev = "{\"type\":\"event\",\"button\":\"C\",\"servo\":";
      ev += servoAngle;
      ev += "}\n";
      client.print(ev);
    }
  }

  // 14h. AUTO mode: kirim periodik
  if (autoMode && client.connected() && millis() - lastSendMs > PERIOD_MS) {
    lastSendMs = millis();
    sendData();
  }
}
