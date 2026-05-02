/* =============================================================================
 *  IoT Socket Programming - ESP32 Client (Wokwi)  -- v3 (HTTP plain)
 *  -----------------------------------------------------------------------------
 *  Penulis  : Fathiyah Audyna Ramadhani
 *
 *  KENAPA HTTP, BUKAN HTTPS:
 *    Diagnostik menunjukkan TLS handshake dari ESP32 simulasi Wokwi kadang
 *    gagal (bahkan ke situs publik seperti howsmyssl.com). Ini terjadi
 *    di simulator, bukan di hardware nyata. Solusi paling stabil untuk
 *    tugas adalah memakai HTTP plain - dan kebetulan endpoint ngrok
 *    free tier juga bisa diakses lewat http://, bukan hanya https://.
 *    Hasilnya: tanpa beban TLS, tanpa setInsecure(), koneksi cepat
 *    dan stabil.
 *
 *  RINGKASAN TOMBOL:
 *    Tombol A (BTN_MODE  - GPIO 14) : Ganti mode (cycle 1 -> 2 -> 3 -> 1)
 *    Tombol B (BTN_SEND  - GPIO 12) : Kirim data sensor sekarang (manual)
 *    Tombol C (BTN_RECV  - GPIO 13) : Minta command dari server sekarang
 *
 *  RINGKASAN MODE:
 *    1 AUTO    - Otomatis kirim tiap 3 detik (B/C tetap aktif)
 *    2 SEND    - Tidak otomatis. Hanya kirim saat tombol B
 *    3 RECEIVE - Tidak otomatis. Hanya minta command saat tombol C
 *
 *  RINGKASAN LED:
 *    HIJAU  (GPIO 27) -> SUKSES
 *    KUNING (GPIO 26) -> BUSY
 *    MERAH  (GPIO 25) -> GAGAL
 * ============================================================================= */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>          // tanpa WiFiClientSecure - kita pakai HTTP plain

// ---------------------------------------------------------------------------
// 1. KONFIGURASI WIFI & SERVER
// ---------------------------------------------------------------------------
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// PERHATIKAN: gunakan "http://" (plain HTTP), bukan "https://".
// Endpoint ngrok free tier menerima keduanya.
// Jangan kasih trailing slash di akhir URL.
const char* SERVER_URL = "http://vertical-cobbler-decal.ngrok-free.dev";

// ---------------------------------------------------------------------------
// 2. PIN MAPPING
// ---------------------------------------------------------------------------
#define PIN_TRIG     5
#define PIN_ECHO    18
#define PIN_NTC     34

#define BTN_MODE    14
#define BTN_SEND    12
#define BTN_RECV    13

#define LED_RED     25
#define LED_GREEN   27
#define LED_YELLOW  26

// ---------------------------------------------------------------------------
// 3. OLED
// ---------------------------------------------------------------------------
#define SCREEN_W   128
#define SCREEN_H    64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ---------------------------------------------------------------------------
// 4. STATE
// ---------------------------------------------------------------------------
enum Mode { MODE_AUTO = 1, MODE_SEND = 2, MODE_RECEIVE = 3 };
Mode currentMode = MODE_AUTO;

const unsigned long PERIODIC_MS = 3000;
const unsigned long DEBOUNCE_MS = 250;

unsigned long lastPeriodicSend = 0;
unsigned long lastBtnPress     = 0;
unsigned long lastSensorRead   = 0;
unsigned long lastDisplayDraw  = 0;

float  lastDistance = -1;
float  lastTemp     = -1;
String lastStatus   = "READY";
String lastCommand  = "(none)";

// ---------------------------------------------------------------------------
// 5. LED
// ---------------------------------------------------------------------------
enum LedState { LED_OFF, LED_OK, LED_BUSY, LED_ERR };
void setLed(LedState s) {
  digitalWrite(LED_GREEN,  s == LED_OK   ? HIGH : LOW);
  digitalWrite(LED_YELLOW, s == LED_BUSY ? HIGH : LOW);
  digitalWrite(LED_RED,    s == LED_ERR  ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// 6. OLED redraw
// ---------------------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print(F("Mode "));
  display.print((int)currentMode);
  display.print(F(": "));
  switch (currentMode) {
    case MODE_AUTO:    display.println(F("AUTO"));    break;
    case MODE_SEND:    display.println(F("SEND"));    break;
    case MODE_RECEIVE: display.println(F("RECV"));    break;
  }

  if (WiFi.status() == WL_CONNECTED) {
    display.print(F("IP:"));
    display.println(WiFi.localIP());
  } else {
    display.println(F("WiFi: DISCONNECTED"));
  }

  display.print(F("Dist:"));
  if (lastDistance < 0) display.println(F(" --"));
  else { display.print(lastDistance, 1); display.println(F(" cm")); }

  display.print(F("Temp:"));
  display.print(lastTemp, 1);
  display.println(F(" C"));

  display.print(F("S:"));
  display.println(lastStatus);

  display.print(F("C:"));
  String c = lastCommand;
  if (c.length() > 18) c = c.substring(0, 18);
  display.println(c);

  display.display();
}

// ---------------------------------------------------------------------------
// 7. Sensor
// ---------------------------------------------------------------------------
float readDistanceCM() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long dur = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (dur == 0) return -1.0f;
  return (dur * 0.0343f) / 2.0f;
}

float readTempC() {
  int raw = analogRead(PIN_NTC);
  return (raw / 4095.0f) * 100.0f;
}

// ---------------------------------------------------------------------------
// 8. Network: HTTP plain (TANPA TLS)
// ---------------------------------------------------------------------------
bool sendSensorData() {
  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "NO WIFI";
    setLed(LED_ERR);
    updateDisplay();
    return false;
  }

  setLed(LED_BUSY);
  lastStatus = "SENDING...";
  updateDisplay();

  HTTPClient http;
  http.setTimeout(10000);
  // Versi sederhana: HTTPClient akan membuat WiFiClient internal sendiri
  // karena URL diawali "http://" (bukan https://).
  if (!http.begin(String(SERVER_URL) + "/data")) {
    lastStatus = "BEGIN FAIL";
    setLed(LED_ERR);
    updateDisplay();
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  String body = "{\"dist\":" + String(lastDistance, 2) +
                ",\"temp\":" + String(lastTemp,    2) + "}";

  int code = http.POST(body);
  bool ok  = (code >= 200 && code < 300);

  if (ok) {
    lastStatus = "SEND OK " + String(code);
    setLed(LED_OK);
    Serial.println("[SEND] " + body + "  -> " + String(code));
  } else {
    lastStatus = "SEND ERR " + String(code);
    setLed(LED_ERR);
    Serial.println("[SEND] FAIL code=" + String(code) +
                   "  msg=" + http.errorToString(code));
  }

  http.end();
  updateDisplay();
  return ok;
}

bool fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "NO WIFI";
    setLed(LED_ERR);
    updateDisplay();
    return false;
  }

  setLed(LED_BUSY);
  lastStatus = "RECV...";
  updateDisplay();

  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(String(SERVER_URL) + "/command")) {
    lastStatus = "BEGIN FAIL";
    setLed(LED_ERR);
    updateDisplay();
    return false;
  }
  http.addHeader("ngrok-skip-browser-warning", "true");

  int code = http.GET();
  bool ok  = (code >= 200 && code < 300);

  if (ok) {
    lastCommand = http.getString();
    lastCommand.trim();
    lastStatus  = "RECV OK";
    setLed(LED_OK);
    Serial.println("[RECV] command = " + lastCommand);
  } else {
    lastStatus = "RECV ERR " + String(code);
    setLed(LED_ERR);
    Serial.println("[RECV] FAIL code=" + String(code) +
                   "  msg=" + http.errorToString(code));
  }

  http.end();
  updateDisplay();
  return ok;
}

// ---------------------------------------------------------------------------
// 9. SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 IoT Client booting (v3 HTTP plain) ===");
  Serial.println("Server: " + String(SERVER_URL));

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_SEND, INPUT_PULLUP);
  pinMode(BTN_RECV, INPUT_PULLUP);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  setLed(LED_OFF);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED 0x3C tidak ditemukan!");
    setLed(LED_ERR);
    while (true) delay(100);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Booting..."));
  display.println(F("Connect WiFi..."));
  display.display();

  setLed(LED_BUSY);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000UL) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    lastStatus = "WiFi OK";
    setLed(LED_OK);
    Serial.print("IP : "); Serial.println(WiFi.localIP());
  } else {
    lastStatus = "WiFi FAIL";
    setLed(LED_ERR);
  }

  updateDisplay();
}

// ---------------------------------------------------------------------------
// 10. LOOP
// ---------------------------------------------------------------------------
void loop() {

  if (millis() - lastSensorRead > 500) {
    lastDistance   = readDistanceCM();
    lastTemp       = readTempC();
    lastSensorRead = millis();
  }

  if (millis() - lastDisplayDraw > 500) {
    updateDisplay();
    lastDisplayDraw = millis();
  }

  if (digitalRead(BTN_MODE) == LOW && millis() - lastBtnPress > DEBOUNCE_MS) {
    lastBtnPress = millis();
    int next = ((int)currentMode % 3) + 1;
    currentMode = (Mode)next;
    lastStatus = "MODE -> " + String(next);
    Serial.println("[BTN A] " + lastStatus);
    setLed(LED_OK);
    updateDisplay();
  }

  if (digitalRead(BTN_SEND) == LOW && millis() - lastBtnPress > DEBOUNCE_MS) {
    lastBtnPress = millis();
    Serial.println("[BTN B] manual send");
    sendSensorData();
  }

  if (digitalRead(BTN_RECV) == LOW && millis() - lastBtnPress > DEBOUNCE_MS) {
    lastBtnPress = millis();
    Serial.println("[BTN C] manual receive");
    fetchCommand();
  }

  if (currentMode == MODE_AUTO &&
      millis() - lastPeriodicSend > PERIODIC_MS) {
    lastPeriodicSend = millis();
    Serial.println("[AUTO] periodic send");
    sendSensorData();
  }
}
