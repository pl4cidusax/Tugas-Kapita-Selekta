import socket
import threading
import argparse
from datetime import datetime
from pathlib import Path

LOCK = threading.Lock()
MESSAGES = []  # list[dict] in memory: {"ts": str, "user": str, "addr": str, "msg": str}

def log_to_file(log_path: Path, line: str) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as f:
        f.write(line + "\n")

def format_message(m: dict) -> str:
    # Example: [2026-03-03 14:22:01] alice (127.0.0.1:52144): hello
    return f'[{m["ts"]}] {m["user"]} ({m["addr"]}): {m["msg"]}'

def handle_client(conn: socket.socket, addr, log_path: Path) -> None:
    addr_str = f"{addr[0]}:{addr[1]}"
    try:
        data = conn.recv(4096)
        if not data:
            return

        text = data.decode("utf-8", errors="replace").strip()
        if not text:
            conn.sendall("ERROR empty command\n".encode("utf-8"))
            return

        parts = text.split(" ", 2)
        cmd = parts[0].upper()

        if cmd == "MSG":
            if len(parts) < 3:
                conn.sendall("ERROR usage: MSG <username> <message>\n".encode("utf-8"))
                return
            user = parts[1].strip()
            msg = parts[2].strip()
            if not user or not msg:
                conn.sendall("ERROR username/message cannot be empty\n".encode("utf-8"))
                return

            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            item = {"ts": now, "user": user, "addr": addr_str, "msg": msg}

            with LOCK:
                MESSAGES.append(item)

            log_to_file(log_path, format_message(item))
            conn.sendall("OK stored\n".encode("utf-8"))

        elif cmd == "HISTORY":
            n = 10
            if len(parts) >= 2 and parts[1].strip():
                try:
                    n = int(parts[1].strip())
                except ValueError:
                    conn.sendall("ERROR HISTORY expects a number, e.g. HISTORY 10\n".encode("utf-8"))
                    return
            with LOCK:
                items = MESSAGES[-n:]
            if not items:
                conn.sendall("EMPTY no messages yet\n".encode("utf-8"))
                return
            payload = "HISTORY\n" + "\n".join(format_message(m) for m in items) + "\n"
            conn.sendall(payload.encode("utf-8"))

        elif cmd == "HELP":
            help_text = (
                "COMMANDS\n"
                "  MSG <username> <message>   store a message\n"
                "  HISTORY [n]                get last n messages (default 10)\n"
                "  HELP                       show this help\n"
            )
            conn.sendall(help_text.encode("utf-8"))

        else:
            conn.sendall(f"ERROR unknown command: {cmd}. Try HELP\n".encode("utf-8"))

    except Exception as e:
        try:
            conn.sendall(f"ERROR {e}\n".encode("utf-8"))
        except Exception:
            pass
    finally:
        try:
            conn.close()
        except Exception:
            pass

def main() -> int:
    ap = argparse.ArgumentParser(description="Mini chat TCP server (one command per connection).")
    ap.add_argument("--host", default="0.0.0.0", help="bind host (default: 0.0.0.0)")
    ap.add_argument("--port", type=int, default=5019, help="bind port (default: 5019)")
    ap.add_argument("--log", default="chat.log", help="log file path (default: chat.log)")
    args = ap.parse_args()

    log_path = Path(args.log)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        # Allow quick restart after stopping the server
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        s.bind((args.host, args.port))
        s.listen(50)  # backlog
        print(f"Server listening on {args.host}:{args.port}")
        print("Press Ctrl+C to stop.")

        while True:
            conn, addr = s.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr, log_path), daemon=True)
            t.start()

if __name__ == "__main__":
    raise SystemExit(main())
