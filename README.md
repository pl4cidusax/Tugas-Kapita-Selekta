# IoT TCP Socket Programming — ESP32 ↔ Server Laptop

> Tugas Praktikum IoT — komunikasi dua arah antara ESP32 (hardware nyata) dan server Python di laptop melalui TCP socket murni di jaringan lokal yang sama.

**Penulis:** Kelompok 4

---

## Daftar Isi

1. [Gambaran Umum](#gambaran-umum)
2. [Arsitektur](#arsitektur)
3. [Daftar Komponen](#daftar-komponen)
4. [Pinout ESP32](#pinout-esp32)
5. [Diagram Wiring](#diagram-wiring)
6. [Protokol Komunikasi](#protokol-komunikasi)
7. [Tombol & LED](#tombol--led)
8. [Cara Menjalankan](#cara-menjalankan)
9. [Menambah Sensor / Command Baru](#menambah-sensor--command-baru)
10. [Troubleshooting](#troubleshooting)
11. [Catatan Keamanan & Limitasi](#catatan-keamanan--limitasi)

---

## Gambaran Umum

Sistem ini terdiri dari dua bagian:

- **ESP32 (client)** — membaca tiga sensor (jarak ultrasonik, cahaya, air), menggerakkan servo, menampilkan status di OLED, dan berkomunikasi dengan server.
- **Server Python (laptop)** — menerima data sensor, menyimpannya ke file log, dan menyediakan REPL bagi operator untuk mengirim command balik ke ESP32 (menggerakkan servo, mengontrol LED, menampilkan pesan di OLED).

Komunikasi memakai **TCP socket murni** pada port 5000. Tidak menggunakan HTTP, tidak menggunakan ngrok, tidak butuh internet — keduanya cukup berada di jaringan WiFi yang sama.

## Arsitektur

```
                    Wi-Fi LAN (mis. 192.168.1.0/24)

  ┌──────────────────┐                          ┌──────────────────┐
  │  ESP32 (kel-4)   │                          │  Laptop          │
  │                  │                          │                  │
  │  IP 192.168.1.50 │  ──────  TCP :5000 ───►  │  IP 192.168.1.42 │
  │                  │                          │  server.py       │
  │  - HC-SR04       │  ◄─── command teks ───   │                  │
  │  - LDR module    │                          │  - data.jsonl    │
  │  - Water sensor  │                          │  - REPL operator │
  │  - Servo SG90    │                          └──────────────────┘
  │  - 3 button      │
  │  - 3 LED status  │
  │  - OLED 128x64   │
  │  - MB-102 5V/3V3 │
  └──────────────────┘
```

## Daftar Komponen

| Jumlah | Komponen | Catatan |
|---|---|---|
| 1 | ESP32 DevKit (30 / 38 pin) | Mikrokontroler utama |
| 1 | OLED SSD1306 128×64 (I2C) | Alamat 0x3C |
| 1 | HC-SR04 Ultrasonic | Sensor jarak |
| 1 | LDR Module (KY-018 / serupa) | Modul cahaya dengan AO + DO |
| 1 | Water Sensor (S, +, −) | Modul deteksi air analog |
| 1 | Servo SG90 (kecil) | Aktuator |
| 3 | Push button | Tactile 6 mm |
| 3 | LED 5 mm | Merah, hijau, kuning |
| 3 | Resistor 220Ω – 1 kΩ | Pembatas arus LED |
| 1 | MB-102 power supply module | Output 5V dan 3.3V dari adapter 9V atau USB |
| 1 | Breadboard + kabel jumper | Untuk perakitan |
| 1 | Adapter 9V atau kabel USB | Suplai untuk MB-102 |

## Pinout ESP32

| Komponen | Pin Komponen | Pin ESP32 | Keterangan |
|---|---|---|---|
| **HC-SR04** | TRIG | GPIO 5 | Output pulsa pemicu |
| | ECHO | GPIO 18 | Input pulsa pantulan |
| | VCC | **5V** (MB-102) | HC-SR04 butuh 5V |
| | GND | GND | |
| **LDR module** | AO (analog) | GPIO 34 | ADC1, input-only |
| | VCC | **3.3V** (MB-102) | Aman untuk ADC ESP32 |
| | GND | GND | |
| **Water sensor** | S (sinyal) | GPIO 35 | ADC1, input-only |
| | + (VCC) | **3.3V** (MB-102) | Pakai 3.3V; 5V bisa merusak ADC |
| | − (GND) | GND | |
| **Servo SG90** | Sinyal (oranye) | GPIO 4 | PWM via library ESP32Servo |
| | VCC (merah) | **5V** (MB-102) | JANGAN dari ESP32 |
| | GND (coklat) | GND | |
| **Tombol A** | Sinyal | GPIO 14 | INPUT_PULLUP, ke GND saat ditekan |
| **Tombol B** | Sinyal | GPIO 32 | INPUT_PULLUP, ke GND saat ditekan |
| **Tombol C** | Sinyal | GPIO 33 | INPUT_PULLUP, ke GND saat ditekan |
| **LED Merah** | Anoda (via R) | GPIO 25 | Indikator ERROR |
| **LED Hijau** | Anoda (via R) | GPIO 27 | Indikator SUCCESS |
| **LED Kuning** | Anoda (via R) | GPIO 26 | Indikator BUSY |
| **OLED SSD1306** | SDA | GPIO 21 | I2C data |
| | SCL | GPIO 22 | I2C clock |
| | VCC | **5V** atau **3.3V** | Tergantung modul |
| | GND | GND | |

> ⚠️ **Hindari** GPIO **0, 2, 12, 15** untuk input dengan pull-up. Itu strapping pin yang bisa membuat ESP32 gagal boot kalau dipull HIGH saat power-on.
>
> ⚠️ **ADC2** (GPIO 0, 2, 4, 12, 13, 14, 15, 25, 26, 27) **tidak bisa dipakai untuk analogRead saat WiFi aktif**. Karena itu sensor analog di sini diletakkan di GPIO 34 dan 35 yang merupakan **ADC1**.

## Diagram Wiring

Power supply MB-102 menjadi sumber daya utama. ESP32 boleh ditenagai via USB sambil tetap berbagi GND dengan MB-102.

```
                    MB-102 power module
                    ┌─────────────────────┐
                    │   5V rail   3.3V rail│
                    └──┬──────────┬───────┘
                       │          │
        ┌──────────────┴──┐   ┌──┴──────────┐
        │ HC-SR04 VCC     │   │ LDR VCC      │
        │ Servo VCC (red) │   │ Water +      │
        │ OLED VCC        │   │              │
        └─────────────────┘   └──────────────┘

   GND MB-102 ──── GND ESP32 ──── GND semua sensor (common ground)

   ESP32 GPIO -> sinyal sensor/aktuator (lihat tabel di atas)
```

Penting:
- **Common ground** wajib. GND MB-102, GND ESP32, dan GND semua modul harus tersambung.
- **Servo SG90** bisa menarik arus puncak hingga ~600 mA. Powerkan dari MB-102, JANGAN dari pin 3.3V/5V ESP32 — bisa restart sendiri.
- **OLED** kebanyakan modul mendukung 3.3V dan 5V (cek spesifikasi modulmu).

## Protokol Komunikasi

Format: **line-delimited**, satu pesan per baris diakhiri `\n`.

### ESP32 → Server (JSON)

```json
{"type":"hello","client":"esp32-kel4"}
{"type":"data","dist":12.34,"light":2150,"water":456}
{"type":"event","button":"A","auto":true}
{"type":"event","button":"C","servo":90}
{"type":"pong"}
```

Field `dist` dalam cm. `light` dan `water` adalah nilai ADC mentah 0–4095 (semakin tinggi `light` = semakin terang atau gelap, tergantung modul; semakin tinggi `water` = semakin banyak air).

### Server → ESP32 (plain text)

```
SERVO 90
MSG hello dunia
LED red on
LED green off
LED yellow on
PING
```

Format: `NAMA <argumen-bebas>`. Diparse oleh ESP32 dengan memisahkan di spasi pertama.

## Tombol & LED

### Tombol

| Tombol | Pin | Fungsi |
|---|---|---|
| **A** | GPIO 14 | Toggle AUTO mode (pengiriman periodik ON/OFF) |
| **B** | GPIO 32 | Kirim data sensor SEKARANG (manual trigger) |
| **C** | GPIO 33 | Cycle posisi servo: 0° → 90° → 180° → 0° … |

### LED

| Warna | Pin | Arti |
|---|---|---|
| 🟢 Hijau | GPIO 27 | Operasi terakhir SUKSES (terkoneksi & kirim/terima OK) |
| 🟡 Kuning | GPIO 26 | BUSY (sedang menyambung / mengirim) |
| 🔴 Merah | GPIO 25 | ERROR (WiFi mati / koneksi terputus) |

LED juga bisa dikontrol manual oleh server lewat command `LED <warna> <on|off>` — berguna untuk demo aktuator.

### Tampilan OLED

```
Mode:AUTO ON          <- mode + status koneksi (ON/OFF)
192.168.1.50          <- IP ESP32
Dst:23.4 cm           <- HC-SR04
Lit:2150              <- LDR (raw ADC)
Wtr:456               <- water sensor (raw ADC)
M:hello dunia         <- pesan terakhir dari server
```

## Cara Menjalankan

### 1. Persiapan Arduino IDE

Install:
- **Board**: ESP32 by Espressif (Boards Manager)
- **Library**: `Adafruit GFX Library`, `Adafruit SSD1306`, `ESP32Servo`

### 2. Persiapan Laptop

Pastikan Python 3 terinstall. Tidak perlu `pip install` apa pun — server hanya pakai standard library.

### 3. Cek IP Laptop

Di terminal laptop:

- **Windows (PowerShell):** `ipconfig`
- **Linux / macOS:** `ifconfig` atau `ip a`

Cari "IPv4 Address" di adapter WiFi yang aktif. Contoh: `192.168.1.42`.

### 4. Buka Firewall (Windows saja)

Saat menjalankan `server.py` pertama kali, Windows Defender Firewall biasanya bertanya. **Pilih "Allow access"** untuk jaringan Private. Tanpa ini, ESP32 tidak akan bisa connect.

Atau buat rule manual:
```powershell
New-NetFirewallRule -DisplayName "ESP32 Kel4" -Direction Inbound -Protocol TCP -LocalPort 5000 -Action Allow
```

### 5. Jalankan Server

Di laptop:

```bash
python server.py
```

Output yang diharapkan:

```
================================================================
 Kelompok 4 - IoT TCP Server
 Listening di 0.0.0.0:5000
 IP laptop  : 192.168.1.42
 -> set SERVER_IP di .ino menjadi: "192.168.1.42"
 Log data   : /path/to/data.jsonl
================================================================

==================================================================
 ESP32 TCP Server - Kelompok 4
 Ketik command lalu ENTER untuk mengirim ke SEMUA ESP32:
   SERVO 0..180                 putar servo
   MSG <teks bebas>             tampilkan di OLED
   LED <red|green|yellow> <on|off>   kontrol LED
   PING                         minta pong
 Server-side commands (awali "/"):
   /list   tampilkan client terhubung
   /help   tampilkan bantuan ini
   /quit   matikan server
==================================================================
> 
```

Catat IP yang ditampilkan.

### 6. Konfigurasi ESP32

Di file `kelompok4_iot_client.ino`, ubah tiga konstanta di bagian atas:

```cpp
const char* WIFI_SSID = "GANTI_NAMA_WIFI";       // SSID WiFi-mu
const char* WIFI_PASS = "GANTI_PASSWORD_WIFI";   // password
const char* SERVER_IP = "192.168.1.42";          // IP laptop tadi
```

Pastikan `SERVER_PORT` cocok dengan server (default 5000).

### 7. Upload & Lihat Hasil

1. Pilih board: **ESP32 Dev Module** (atau yang sesuai dengan boardmu).
2. Pilih COM port yang benar.
3. Klik Upload.
4. Buka Serial Monitor di **115200 baud**.
5. Tunggu sampai muncul `[NET] CONNECTED`.

Di terminal server akan tampil:

```
[+] 192.168.1.50:54321 connected. Total client = 1
<< 192.168.1.50 {'type': 'hello', 'client': 'esp32-kel4'}
<< 192.168.1.50 {'type': 'data', 'dist': 12.34, 'light': 2150.0, 'water': 456.0}
```

### 8. Kirim Command dari Server

Di prompt `>` server, ketik salah satu:

```
SERVO 45
MSG halo kelompok 4
LED green on
LED red off
PING
```

Setiap baris langsung dikirim ke ESP32. OLED ESP32 akan menampilkan pesan, servo bergerak, LED menyala, dst.

## Menambah Sensor / Command Baru

Salah satu fokus desain kode ini adalah **mudah dikembangkan**. Pola yang dipakai: tabel function-pointer.

### Tambah Sensor Baru

Misalnya kamu ingin menambah **DHT22** (sensor suhu+kelembaban). Cukup tiga langkah:

**1.** Tambah `#include` dan inisialisasi objek:

```cpp
#include <DHT.h>
#define PIN_DHT 16
DHT dht(PIN_DHT, DHT22);
```

**2.** Tulis fungsi init dan read:

```cpp
void  initTemp() { dht.begin(); }
float readTemp() { return dht.readTemperature(); }

void  initHum()  { /* sudah di-init oleh initTemp */ }
float readHum()  { return dht.readHumidity(); }
```

**3.** Tambahkan baris di array `sensors[]`:

```cpp
Sensor sensors[] = {
  { "dist",  initUltrasonic, readUltrasonic, 0 },
  { "light", initLDR,        readLDR,        0 },
  { "water", initWater,      readWater,      0 },
  { "temp",  initTemp,       readTemp,       0 },   // <-- baru
  { "hum",   initHum,        readHum,        0 }    // <-- baru
};
```

Selesai. Otomatis:
- Sensor di-init di `setup()` (looping `sensors[]`).
- Dibaca tiap 500 ms (looping `sensors[]`).
- Dikirim ke server di JSON dengan key sesuai field `key` (`"temp"`, `"hum"`).
- Server mencatatnya ke `data.jsonl` apa adanya.

(Opsional) tambahkan tampilan di `updateDisplay()` kalau mau muncul di OLED.

### Tambah Command Baru

Misalnya kamu ingin command `BUZZER <on|off>`. Dua langkah:

**1.** Tulis handler:

```cpp
void cmdBuzzer(const String& arg) {
  bool on = (arg == "on" || arg == "1");
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}
```

**2.** Tambah ke array `commands[]`:

```cpp
Command commands[] = {
  { "SERVO",  cmdServo  },
  { "MSG",    cmdMsg    },
  { "LED",    cmdLed    },
  { "PING",   cmdPing   },
  { "BUZZER", cmdBuzzer }   // <-- baru
};
```

Selesai. Sekarang server bisa kirim `BUZZER on` dan ESP32 akan menjalankan handler.

## Troubleshooting

| Gejala | Kemungkinan Penyebab | Solusi |
|---|---|---|
| `[NET] connect FAILED` terus-menerus | IP server salah, atau firewall memblokir | Cek IP dengan `ipconfig`, allow port 5000 di firewall |
| ESP32 tidak booting (loop reset) | GPIO strapping pin (0/2/12/15) di-pull HIGH | Pastikan tidak ada button di pin 12 |
| Servo bergerak liar / ESP32 restart saat servo bergerak | Servo ditenagai dari ESP32 | Pakai 5V dari MB-102, jangan dari ESP32 |
| `analogRead` selalu 0 | Pin di ADC2 sementara WiFi aktif | Pakai pin ADC1 (GPIO 32–39) |
| OLED kosong / tidak terdeteksi | Wiring SDA/SCL atau alamat I2C salah | Cek SDA=21, SCL=22, alamat 0x3C. Coba 0x3D |
| `Dist: --` terus | Tidak ada pantulan dari objek di depan HC-SR04 | Letakkan benda dalam jangkauan 2 cm – 4 m |
| WiFi tidak konek | SSID/password salah, atau WiFi 5 GHz | ESP32 hanya support 2.4 GHz |
| Server menerima data tapi nilai aneh | Voltase sensor 5V tapi pin ADC max 3.3V | Pakai pembagi tegangan, atau pasok sensor di 3.3V |
| OLED muncul "WiFi offline" tapi WiFi sebenarnya OK | `WiFi.status()` belum WL_CONNECTED saat first draw | Tunggu beberapa detik, akan auto-update |

### Verifikasi server bisa dihubungi

Di laptop yang sama, jalankan dari terminal lain:

```bash
# Test apakah server listen
python -c "import socket; s=socket.socket(); s.connect(('127.0.0.1',5000)); s.send(b'PING\n'); s.close()"
```

Tidak ada error = server hidup. Server log akan menampilkan koneksi yang masuk.

### Verifikasi ESP32 bisa lihat laptop

Dari ESP32, cara cepat: ping IP laptop dengan `WiFi.ping()` di kode (atau pakai library `ESP32Ping`). Atau lihat di Serial Monitor apakah `client.connect(SERVER_IP, SERVER_PORT)` mengembalikan `true`.

## Catatan Keamanan & Limitasi

- **Komunikasi tidak terenkripsi.** Karena raw TCP di LAN privat, isi pesan visible di siapa pun yang sniffing jaringan. Untuk tugas akademik ini cukup; untuk produksi gunakan TLS.
- **Tidak ada autentikasi.** Siapa pun yang tahu IP+port bisa konek ke server. Cocok untuk tugas, tidak untuk publik.
- **Konversi nilai sensor analog disederhanakan.** LDR dan water sensor dikirim sebagai nilai ADC mentah 0–4095. Untuk konversi ke unit fisik (lux, kadar air %), perlu kalibrasi.
- **ESP32 hanya support WiFi 2.4 GHz.** Tidak bisa konek ke jaringan WiFi 5 GHz. Pastikan router beroperasi di 2.4 GHz, atau pakai dual-band.
- **Tidak ada queue offline.** Kalau koneksi putus, data yang ingin dikirim saat itu hilang. Untuk reliability tinggi tambahkan circular buffer di ESP32.

---

## Lisensi

Tugas akademik. Bebas dipakai untuk pembelajaran dengan mencantumkan kredit **Kelompok 4**.
