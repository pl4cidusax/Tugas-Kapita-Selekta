# IoT Socket Programming — ESP32 to Server Laptop

> Tugas KSJ Kelompok 4 — komunikasi dua arah antara ESP32 (disimulasikan di Wokwi) dan server Python di laptop, melalui HTTP yang di-tunnel oleh ngrok.

---

## Daftar Isi

1. [Gambaran Umum](#gambaran-umum)
2. [Arsitektur Sistem](#arsitektur-sistem)
3. [Daftar File](#daftar-file)
4. [Hardware & Pinout](#hardware--pinout)
5. [Tombol, Mode, dan LED](#tombol-mode-dan-led)
6. [Cara Menjalankan](#cara-menjalankan)
7. [Endpoint Server](#endpoint-server)
8. [Mengirim Command ke ESP32](#mengirim-command-ke-esp32)
9. [Troubleshooting](#troubleshooting)
10. [Catatan Teknis](#catatan-teknis)

---

## Gambaran Umum

ESP32 membaca dua sensor — **HC-SR04** (jarak) dan **NTC** (suhu) — lalu mengirim data ke server Python di laptop dalam format JSON. Server menyimpan data ke CSV dan menampilkannya di dashboard web. Server juga menyediakan endpoint balik untuk mengirim **teks command** yang akan ditampilkan di OLED ESP32.

Karena ESP32 berjalan di Wokwi (cloud), ia tidak bisa menjangkau `localhost` laptop secara langsung. **Ngrok** dipakai untuk membuat URL publik yang diteruskan ke `localhost:5000`.

Sistem mendukung tiga mode operasi (otomatis, kirim manual, terima manual) yang dapat diganti dengan tombol fisik, dan tiga LED indikator (hijau/kuning/merah) untuk status transmisi.

---

## Arsitektur Sistem

```
┌─────────────────┐         HTTP          ┌─────────────────┐
│  ESP32 (Wokwi)  │  ───────────────────► │   ngrok cloud   │
│                 │                        │                 │
│  - HC-SR04      │  ◄───── command ────  │                 │
│  - NTC          │                        └────────┬────────┘
│  - 3 button     │                                 │ tunnel
│  - 3 LED        │                                 ▼
│  - OLED         │                        ┌─────────────────┐
│  - WiFi client  │                        │ Laptop          │
└─────────────────┘                        │  server.py      │
                                           │  port 5000      │
                                           │  - dashboard    │
                                           │  - sensor_log   │
                                           └─────────────────┘
```

---

## Daftar File

| File | Fungsi |
|---|---|
| `esp32_iot_client.ino` | Sketch Arduino untuk ESP32. Membaca sensor, mengirim/menerima HTTP, mengelola OLED, tombol, dan LED. |
| `server.py` | Server HTTP Python (standard library) yang menerima data sensor dan menyajikan dashboard + endpoint command. |
| `diagram.json` | Konfigurasi rangkaian Wokwi (komponen + koneksi). |
| `diag_https.ino` | Sketch diagnostik singkat untuk menguji konektivitas HTTPS dari Wokwi. Hanya dipakai bila ada masalah jaringan. |
| `Laporan_IoT_Socket_Programming.docx` | Laporan lengkap (BAB I–VII) yang dapat di-upload ke Google Docs. |
| `sensor_log.csv` | Otomatis dibuat oleh `server.py` saat menerima data pertama. |

---

## Hardware & Pinout

### Komponen

| No | Komponen | Jumlah |
|---|---|---|
| 1 | ESP32 DevKit C V4 | 1 |
| 2 | OLED SSD1306 128×64 (I2C) | 1 |
| 3 | HC-SR04 Ultrasonic | 1 |
| 4 | NTC Temperature Sensor | 1 |
| 5 | Push Button 6 mm | 3 |
| 6 | LED 5 mm (merah, hijau, kuning) | 3 |
| 7 | Resistor 10 kΩ | 6 |

### Pinout

| Komponen | Pin Komponen | Pin ESP32 | Keterangan |
|---|---|---|---|
| HC-SR04 | TRIG | GPIO 5 | Output pulsa pemicu (10 µs) |
| HC-SR04 | ECHO | GPIO 18 | Input pulsa pantulan |
| NTC | OUT | GPIO 34 | ADC1_CH6 (input-only) |
| NTC | VCC / GND | 3V3 / GND | Catu daya sensor |
| Tombol A | Sinyal | GPIO 14 | INPUT_PULLUP, aktif LOW. **Ganti mode** |
| Tombol B | Sinyal | GPIO 12 | INPUT_PULLUP, aktif LOW. **Kirim manual** |
| Tombol C | Sinyal | GPIO 13 | INPUT_PULLUP, aktif LOW. **Terima manual** |
| LED Merah | Anoda (via R) | GPIO 25 | Indikator ERROR |
| LED Hijau | Anoda (via R) | GPIO 27 | Indikator SUCCESS |
| LED Kuning | Anoda (via R) | GPIO 26 | Indikator BUSY |
| OLED | SDA | GPIO 21 | I2C alamat 0x3C |
| OLED | SCL | GPIO 22 | I2C clock |
| OLED | VCC / GND | 5V / GND | Catu daya OLED |

---

## Tombol, Mode, dan LED

### Tombol

| Tombol | Pin | Fungsi |
|---|---|---|
| **A** (BTN_MODE) | GPIO 14 | Cycle mode: 1 → 2 → 3 → 1 … |
| **B** (BTN_SEND) | GPIO 12 | Kirim data sensor sekarang juga |
| **C** (BTN_RECV) | GPIO 13 | Minta command dari server sekarang juga |

### Mode

| Mode | Nama | Perilaku |
|---|---|---|
| **1** | AUTO | Otomatis kirim data sensor tiap 3 detik. Tombol B & C tetap aktif. |
| **2** | SEND | Tidak ada pengiriman otomatis. Hanya kirim saat tombol B ditekan. |
| **3** | RECV | Tidak ada pengiriman otomatis. Hanya minta command saat tombol C ditekan. |

### LED Status

| Warna | Arti |
|---|---|
| 🟢 Hijau | Operasi terakhir SUKSES (HTTP 2xx) |
| 🟡 Kuning | Sedang BUSY (sedang mengirim/menerima) |
| 🔴 Merah | Operasi terakhir GAGAL (WiFi terputus / HTTP error) |

### Tampilan OLED

```
Mode 1: AUTO
IP:10.10.0.2
Dist:23.4 cm
Temp:27.8 C
S:SEND OK 200
C:HELLO ESP32
```

---

## Cara Menjalankan

### Prasyarat

- **Python 3** (sudah ada di kebanyakan sistem)
- **ngrok** dengan authtoken (gratis di [ngrok.com](https://ngrok.com/))
- Akun **Wokwi** dengan project ESP32 yang sudah dirangkai sesuai `diagram.json`
- Library Arduino: `Adafruit_GFX`, `Adafruit_SSD1306`, `HTTPClient` (bawaan core ESP32)

### Langkah 1 — Jalankan server di laptop

Buka terminal pertama:

```bash
python server.py
```

Output yang diharapkan:

```
================================================================
 IoT HTTP Server jalan di http://0.0.0.0:5000
 Endpoint:
   POST /data    -> data sensor dari ESP32 (JSON {dist,temp})
   GET  /command -> kirim teks command ke ESP32
   POST /command -> ubah teks command (dari laptop)
   GET  /        -> dashboard HTML (auto-refresh tiap 2 detik)
   GET  /log     -> 50 data sensor terakhir (JSON)
================================================================
```

Buka `http://localhost:5000/` di browser — dashboard hijau-hitam akan tampil.

### Langkah 2 — Jalankan ngrok

> ⚠️ **PENTING**: ngrok versi 3 secara default hanya membuat endpoint HTTPS. Karena Wokwi sering kesulitan dengan TLS handshake, kita paksa ngrok menyediakan endpoint HTTP juga.

Buka terminal kedua:

```bash
ngrok http --scheme http,https 5000
```

Output yang diharapkan (URL akan berbeda setiap kali dijalankan di free tier):

```
Session Status                online
Forwarding   http://abc-1234.ngrok-free.dev  -> http://localhost:5000
Forwarding   https://abc-1234.ngrok-free.dev -> http://localhost:5000
```

**Salin URL yang `http://...`** (bukan `https://`).

> 💡 Jika ngrok tidak mengizinkan `--scheme http,https`, coba `ngrok http --scheme http 5000`. Browser laptop tidak akan bisa membuka URL ngrok HTTP karena HSTS, tapi **ESP32 tetap bisa**. Untuk memantau dashboard, langsung buka `http://localhost:5000/` di browser laptop tanpa lewat ngrok.

### Langkah 3 — Konfigurasi ESP32

Di Wokwi, buka `esp32_iot_client.ino`. Cari baris:

```cpp
const char* SERVER_URL = "http://vertical-cobbler-decal.ngrok-free.dev";
```

Ganti dengan URL HTTP dari ngrok yang baru saja Anda salin (tanpa garis miring di akhir).

### Langkah 4 — Run simulasi

Klik tombol Play di Wokwi. Tunggu 5–10 detik:

- LED kuning menyala (booting + connect WiFi)
- LED hijau menyala saat WiFi tersambung
- OLED menampilkan `IP: 10.10.x.x`
- Mode default AUTO: tiap 3 detik LED kuning → hijau, dan dashboard browser bertambah 1 baris data

---

## Endpoint Server

| Method | Path | Deskripsi |
|---|---|---|
| `POST` | `/data` | ESP32 mengirim JSON `{"dist": ..., "temp": ...}`. Server simpan ke buffer + CSV. |
| `GET` | `/command` | ESP32 minta command terbaru. Server balas dengan teks command. |
| `POST` | `/command` | Laptop ubah command yang akan dikirim ke ESP32. Body: teks plain. |
| `GET` | `/` atau `/status` | Dashboard HTML auto-refresh tiap 2 detik. |
| `GET` | `/log` | 50 data sensor terakhir dalam JSON. |

---

## Mengirim Command ke ESP32

Di terminal ketiga (saat server dan ngrok sudah jalan):

```bash
# Linux / macOS / Git Bash
curl -X POST --data "MATIKAN_LAMPU" http://localhost:5000/command

# Windows PowerShell — gunakan curl.exe (bukan curl)
curl.exe -X POST --data "MATIKAN_LAMPU" http://localhost:5000/command
```

Lalu di Wokwi:

1. Tekan **Tombol A** sampai mode jadi **3 RECV** (atau biarkan mode AUTO/SEND)
2. Tekan **Tombol C**
3. OLED baris terakhir akan menampilkan `C: MATIKAN_LAMPU`

---

## Troubleshooting

| Gejala di Serial Monitor | Penyebab | Solusi |
|---|---|---|
| `code=-1 connection refused` (HTTPS) | TLS handshake gagal di Wokwi | Pakai HTTP, jalankan ngrok dengan `--scheme http,https` |
| `code=307` | ngrok me-redirect HTTP ke HTTPS | Idem — pakai endpoint HTTP eksplisit |
| `code=-1 connection refused` (HTTP) | Server `server.py` mati / ngrok mati | Cek kedua terminal masih hidup |
| `code=502` | ngrok hidup tapi server.py mati | Jalankan ulang `python server.py` |
| `code=404` | Endpoint salah | Cek URL: harus diakhiri `/data` atau `/command` |
| OLED kosong | Wiring SDA/SCL atau alamat I2C | Cek SDA=GPIO 21, SCL=GPIO 22, alamat 0x3C |
| `Dist: --` | Tidak ada objek di depan HC-SR04 | Geser objek atau tambahkan dari panel Wokwi |
| WiFi tidak konek | SSID salah | Pastikan `Wokwi-GUEST` (case-sensitive), tanpa password |

### Diagnostik Jaringan

Bila masih ragu apakah masalah di Wokwi atau di server, jalankan `diag_https.ino` di project Wokwi terpisah. Sketch ini mencoba dua URL dan mencetak hasilnya. Hasil dari kedua URL akan menunjukkan di mana letak masalahnya:

| Test 1 (howsmyssl) | Test 2 (ngrok kamu) | Diagnosis |
|---|---|---|
| ✅ 200 | ✅ 200 | Semua oke — bug di sketch utama |
| ✅ 200 | ❌ -1 | Endpoint ngrok bermasalah |
| ❌ -1 | ❌ -1 | HTTPS Wokwi tidak jalan → pakai HTTP plain |

---

## Catatan Teknis

### Kenapa pakai HTTP, bukan HTTPS?

Simulator Wokwi kadang gagal melakukan TLS handshake (kemungkinan karena keterbatasan cipher atau memori RAM untuk buffer TLS). Pada hardware ESP32 nyata, HTTPS biasanya lancar. Untuk keperluan tugas ini, HTTP plain via ngrok sudah cukup karena:

- ngrok masih bisa menerima request HTTP, walau default agent v3-nya HTTPS.
- Beban CPU & memori jauh lebih ringan.
- Tidak perlu menyimpan sertifikat di flash.

### Kenapa tetap disebut "Socket Programming"?

HTTP berjalan di atas TCP socket. Library `HTTPClient` ESP32 secara internal:

1. Membuat objek `WiFiClient` (yang membungkus socket TCP)
2. Memanggil `connect(host, port)` — di sini socket TCP terbuka
3. Mengirim baris HTTP request via `client.print(...)`
4. Membaca jawaban via `client.read(...)`
5. Memanggil `client.stop()` untuk menutup socket

Jadi walaupun kita menulis kode level HTTP, di balik layar yang terjadi adalah komunikasi socket TCP yang persis sama dengan socket programming "tradisional".

### Konversi NTC

Konversi tegangan NTC → derajat Celsius dalam sketch ini disederhanakan menjadi linear (`raw / 4095 * 100`). Pada hardware nyata, gunakan persamaan **Steinhart-Hart** yang memperhitungkan karakteristik nonlinier termistor.

### Format JSON yang dikirim

```json
{
  "dist": 23.45,
  "temp": 27.83
}
```

Field `ts` (timestamp) ditambahkan oleh server, bukan oleh ESP32, sehingga tidak ada beban memori untuk format waktu di sisi mikrokontroler.

---

## Lisensi

Tugas akademik. Bebas dipakai untuk pembelajaran dengan mencantumkan kredit penulis.
