"""
================================================================================
 IoT TCP Socket Programming - Server (Laptop side)
--------------------------------------------------------------------------------
 Penulis : Kelompok 4
 Bahasa  : Python 3 (cukup standard library, tidak butuh pip install)

 Server TCP socket murni. Tidak pakai HTTP, tidak pakai ngrok.
 Berjalan di laptop yang berada di JARINGAN WiFi YANG SAMA dengan ESP32.

 Fitur:
   - Menerima banyak client ESP32 sekaligus (multi-thread).
   - Mencatat data sensor ke file `data.jsonl` (satu baris JSON per data).
   - REPL operator: ketik command, ENTER, langsung dikirim ke SEMUA ESP32
     yang sedang terhubung.

 Cara pakai:
   1. python server.py
   2. Catat IP laptop (lihat output server, atau jalankan `ipconfig`).
   3. Edit kode .ino: ubah SERVER_IP ke IP laptop, upload ke ESP32.
   4. Setelah ESP32 terkoneksi, di terminal server akan tampil "[+] ... connected".
   5. Ketik command di prompt `>` untuk mengirim ke ESP32.

 Command yang dikenali ESP32 (default):
   SERVO <0..180>          - putar servo
   MSG <teks>              - tampilkan teks di OLED
   LED <red|green|yellow> <on|off>   - kontrol LED
   PING                    - minta pong

 Server-side commands (awali dengan /):
   /list   - lihat client yang terhubung
   /help   - tampilkan bantuan
   /quit   - keluar
================================================================================
"""

import socket
import threading
import json
import sys
import os
from datetime import datetime

# --------------------------- KONFIGURASI --------------------------------------
HOST     = "0.0.0.0"     # listen di semua interface
PORT     = 5000          # port yang akan dihubungi ESP32
LOG_FILE = "data.jsonl"  # data sensor dicatat di sini (1 baris JSON per data)

# State global -----------------------------------------------------------------
clients_lock = threading.Lock()
clients      = []       # list of (socket, address)


# --------------------------- UTILITAS -----------------------------------------
def get_local_ip() -> str:
    """Cara hemat untuk dapat IP lokal di adapter yang punya akses ke internet."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def log(msg: str) -> None:
    """Print dengan timestamp."""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


def append_jsonl(payload: dict) -> None:
    """Append satu baris JSON (dengan timestamp) ke LOG_FILE."""
    payload = {"ts": datetime.now().isoformat(timespec="seconds"), **payload}
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(json.dumps(payload, ensure_ascii=False) + "\n")
    except OSError as e:
        log(f"!! gagal tulis log: {e}")


def broadcast(text: str) -> int:
    """Kirim teks ke semua client yang terhubung. Tambahkan newline kalau belum ada.
    Mengembalikan jumlah client yang berhasil menerima."""
    if not text.endswith("\n"):
        text += "\n"
    data = text.encode("utf-8")

    sent_count = 0
    dead = []
    with clients_lock:
        for sock, addr in clients:
            try:
                sock.sendall(data)
                sent_count += 1
            except OSError:
                dead.append((sock, addr))
        # bersihkan client mati
        for entry in dead:
            if entry in clients:
                clients.remove(entry)
            try:
                entry[0].close()
            except OSError:
                pass
    return sent_count


# --------------------------- HANDLER PER CLIENT -------------------------------
def handle_client(sock: socket.socket, addr: tuple) -> None:
    """Membaca pesan baris-demi-baris dari satu ESP32. Berjalan di thread sendiri."""
    with clients_lock:
        clients.append((sock, addr))
    log(f"[+] {addr[0]}:{addr[1]} connected. Total client = {len(clients)}")

    buf = ""
    try:
        while True:
            chunk = sock.recv(1024)
            if not chunk:
                break               # connection closed by peer
            buf += chunk.decode("utf-8", errors="replace")

            # Proses semua baris lengkap di buffer
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    payload = json.loads(line)
                    log(f"<< {addr[0]} {payload}")
                    if payload.get("type") == "data":
                        append_jsonl(payload)
                except json.JSONDecodeError:
                    log(f"<< {addr[0]} (raw) {line}")
    except (ConnectionResetError, OSError) as e:
        log(f"!! {addr[0]} error: {e}")
    finally:
        with clients_lock:
            entry = (sock, addr)
            if entry in clients:
                clients.remove(entry)
        try:
            sock.close()
        except OSError:
            pass
        log(f"[-] {addr[0]}:{addr[1]} disconnected. Total client = {len(clients)}")


# --------------------------- REPL OPERATOR ------------------------------------
HELP_TEXT = """
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
"""


def operator_repl() -> None:
    """Loop interaksi operator. Berjalan di thread sendiri."""
    print(HELP_TEXT)
    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nQuit.")
            os._exit(0)

        if not line:
            continue

        # Server-side command
        if line.startswith("/"):
            parts = line[1:].split()
            cmd = parts[0].lower() if parts else ""
            if cmd in ("q", "quit", "exit"):
                print("Bye.")
                os._exit(0)
            elif cmd in ("h", "help", "?"):
                print(HELP_TEXT)
            elif cmd == "list":
                with clients_lock:
                    if not clients:
                        print("  (tidak ada client)")
                    for _, addr in clients:
                        print(f"  - {addr[0]}:{addr[1]}")
            else:
                print(f"  perintah server tidak dikenal: /{cmd}")
            continue

        # Selain itu, broadcast ke semua client
        n = broadcast(line)
        if n == 0:
            print("  (tidak ada client terhubung)")
        else:
            print(f"  >> dikirim ke {n} client")


# --------------------------- ENTRY POINT --------------------------------------
def main() -> None:
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server_sock.bind((HOST, PORT))
    except OSError as e:
        print(f"Gagal bind ke port {PORT}: {e}")
        sys.exit(1)
    server_sock.listen(8)

    local_ip = get_local_ip()
    print("=" * 64)
    print(f" Kelompok 4 - IoT TCP Server")
    print(f" Listening di {HOST}:{PORT}")
    print(f" IP laptop  : {local_ip}")
    print(f" -> set SERVER_IP di .ino menjadi: \"{local_ip}\"")
    print(f" Log data   : {os.path.abspath(LOG_FILE)}")
    print("=" * 64)

    # REPL berjalan di thread sendiri supaya tidak nge-block accept loop
    threading.Thread(target=operator_repl, daemon=True).start()

    try:
        while True:
            conn, addr = server_sock.accept()
            threading.Thread(target=handle_client, args=(conn, addr),
                             daemon=True).start()
    except KeyboardInterrupt:
        print("\nServer dihentikan.")
    finally:
        server_sock.close()


if __name__ == "__main__":
    main()
