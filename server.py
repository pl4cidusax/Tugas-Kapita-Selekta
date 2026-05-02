"""
================================================================================
 IoT Socket Programming - HTTP Server (Laptop side)
--------------------------------------------------------------------------------
 Penulis : Fathiyah Audyna Ramadhani
 Bahasa  : Python 3 (cukup pakai standard library, tidak perlu Flask)

 Server HTTP sederhana berbasis http.server (di balik layar memakai socket TCP
 bawaan Python). Bertugas:
   - Menerima data sensor dari ESP32 (POST /data)  -> dicatat ke log + file
   - Mengirim teks command ke ESP32 (GET /command)
   - Mengubah teks command dari sisi laptop (POST /command)
   - Menyajikan halaman status sederhana di browser (GET /)
   - Menyajikan log data sensor JSON (GET /log)

 Cara pakai:
   1. python server.py
   2. (terminal lain) ngrok http 5000
   3. Salin URL https://xxx.ngrok-free.app dari ngrok, paste ke
      konstanta SERVER_URL di file .ino, lalu jalankan simulasi Wokwi.
   4. Buka http://localhost:5000/ di browser laptop untuk memantau.

 Mengubah command yang dikirim ke ESP32:
   curl -X POST --data "PESAN BARU" http://localhost:5000/command
   (atau lewat halaman web /  jika kamu menambahkan form sendiri)
================================================================================
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime
import json
import threading
import os

# --------------------------- KONFIGURASI --------------------------------------
HOST       = "0.0.0.0"          # listen di semua interface
PORT       = 5000               # port lokal yang akan di-tunnel oleh ngrok
MAX_LOG    = 200                # jumlah maksimum data di buffer memori
LOG_FILE   = "sensor_log.csv"   # data sensor juga di-append ke file ini

# State global -----------------------------------------------------------------
current_command = "HELLO ESP32"   # command default yang akan dijawab ke ESP32
sensor_log      = []              # buffer in-memory data sensor
lock            = threading.Lock()  # supaya thread-safe (HTTP server multi-thread)


# --------------------------- HANDLER ------------------------------------------
class IoTHandler(BaseHTTPRequestHandler):
    """Satu instance dibuat per request oleh ThreadingHTTPServer."""

    # ---- helper kecil ----------------------------------------------------
    def _respond(self, status: int = 200, body: str = "", ctype: str = "text/plain"):
        """Tulis response HTTP secara konsisten."""
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")  # CORS bebas
        self.end_headers()
        if data:
            self.wfile.write(data)

    def log_message(self, format, *args):
        """Override log default supaya output terminal lebih rapi."""
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"[{ts}] {self.address_string()} - {format % args}")

    # =====================================================================
    # GET handler
    # =====================================================================
    def do_GET(self):
        global current_command, sensor_log

        # ----------- ESP32 minta command terbaru -------------------------
        if self.path == "/command":
            with lock:
                cmd = current_command
            print(f"   -> ESP32 minta command, dijawab: {cmd!r}")
            self._respond(200, cmd)
            return

        # ----------- Lihat 50 data sensor terakhir (JSON) ----------------
        if self.path == "/log":
            with lock:
                body = json.dumps(sensor_log[-50:], indent=2, ensure_ascii=False)
            self._respond(200, body, "application/json")
            return

        # ----------- Halaman status HTML untuk browser laptop ------------
        if self.path in ("/", "/status"):
            with lock:
                last  = sensor_log[-1] if sensor_log else None
                cmd   = current_command
                cnt   = len(sensor_log)
                tail  = sensor_log[-10:][::-1]   # 10 terbaru, urut terbaru di atas

            rows = "".join(
                f"<tr><td>{r.get('ts','-')}</td>"
                f"<td>{r.get('dist','-')}</td>"
                f"<td>{r.get('temp','-')}</td></tr>"
                for r in tail
            )

            html = f"""<!doctype html><html><head>
              <title>IoT Server Dashboard</title>
              <meta http-equiv="refresh" content="2">
              <style>
                body {{ font-family: monospace; background:#0b0b0b; color:#cfffcf; padding:24px; }}
                h2   {{ color:#7CFC00; }}
                table{{ border-collapse:collapse; margin-top:12px; }}
                td,th{{ border:1px solid #2a2a2a; padding:6px 12px; }}
                th   {{ background:#143014; }}
                .box {{ background:#1a1a1a; padding:12px 18px; margin:8px 0;
                        border-left:4px solid #7CFC00; }}
              </style></head><body>
              <h2>ESP32 IoT Server Dashboard</h2>
              <div class="box">Total data diterima : <b>{cnt}</b></div>
              <div class="box">Data terakhir       : <b>{last}</b></div>
              <div class="box">Command saat ini    : <b>{cmd}</b></div>
              <p>Ubah command via terminal:</p>
              <pre>curl -X POST --data "PESAN" http://localhost:{PORT}/command</pre>
              <h3>10 data terakhir</h3>
              <table>
                <tr><th>Waktu</th><th>Jarak (cm)</th><th>Suhu (degC)</th></tr>
                {rows or '<tr><td colspan="3">(belum ada data)</td></tr>'}
              </table>
              <p style="color:#777">Halaman ini auto-refresh tiap 2 detik.</p>
              </body></html>"""
            self._respond(200, html, "text/html")
            return

        # ----------- selain itu 404 --------------------------------------
        self._respond(404, "not found")

    # =====================================================================
    # POST handler
    # =====================================================================
    def do_POST(self):
        global current_command, sensor_log

        length = int(self.headers.get("Content-Length", 0))
        raw    = self.rfile.read(length).decode("utf-8", errors="replace")

        # ----------- ESP32 kirim data sensor -----------------------------
        if self.path == "/data":
            try:
                payload = json.loads(raw)
            except json.JSONDecodeError:
                payload = {"raw": raw}

            payload["ts"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            with lock:
                sensor_log.append(payload)
                if len(sensor_log) > MAX_LOG:
                    sensor_log.pop(0)

            # Append juga ke CSV supaya tidak hilang saat server di-restart
            try:
                new_file = not os.path.exists(LOG_FILE)
                with open(LOG_FILE, "a", encoding="utf-8") as f:
                    if new_file:
                        f.write("timestamp,dist_cm,temp_c\n")
                    f.write(f"{payload.get('ts','')},"
                            f"{payload.get('dist','')},"
                            f"{payload.get('temp','')}\n")
            except OSError as e:
                print(f"   !! gagal tulis CSV: {e}")

            print(f"   -> DATA dari ESP32: {payload}")
            self._respond(200, "OK")
            return

        # ----------- Update command dari laptop --------------------------
        if self.path == "/command":
            new_cmd = raw.strip()
            if not new_cmd:
                self._respond(400, "command kosong")
                return
            with lock:
                current_command = new_cmd
            print(f"   -> COMMAND DIUBAH menjadi: {new_cmd!r}")
            self._respond(200, f"command updated: {new_cmd}")
            return

        # ----------- selain itu 404 --------------------------------------
        self._respond(404, "not found")


# --------------------------- ENTRY POINT --------------------------------------
def main():
    server = ThreadingHTTPServer((HOST, PORT), IoTHandler)
    print("=" * 64)
    print(f" IoT HTTP Server jalan di http://{HOST}:{PORT}")
    print(" Endpoint:")
    print("   POST /data    -> data sensor dari ESP32 (JSON {dist,temp})")
    print("   GET  /command -> kirim teks command ke ESP32")
    print("   POST /command -> ubah teks command (dari laptop)")
    print("   GET  /        -> dashboard HTML (auto-refresh tiap 2 detik)")
    print("   GET  /log     -> 50 data sensor terakhir (JSON)")
    print("=" * 64)
    print(" Tekan Ctrl+C untuk berhenti.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer dihentikan.")
        server.server_close()


if __name__ == "__main__":
    main()