import socket
import argparse

def send_command(host: str, port: int, command: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.sendall(command.encode("utf-8"))

        chunks = []
        while True:
            data = s.recv(4096)
            if not data:
                break
            chunks.append(data)
    return b"".join(chunks).decode("utf-8", errors="replace")

def main() -> int:
    ap = argparse.ArgumentParser(description="Mini chat TCP client (interactive).")
    ap.add_argument("--host", default="127.0.0.1", help="server host/IP (default: 127.0.0.1)")
    ap.add_argument("--port", type=int, default=5019, help="server port (default: 5019)")
    ap.add_argument("--user", default="user", help="your username (default: user)")
    args = ap.parse_args()

    print(f"Connected target: {args.host}:{args.port}")
    print("Type messages and press Enter.")
    print("Commands: /history [n], /help, /quit\n")

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            break

        if not line:
            continue

        if line.lower().startswith("/quit"):
            print("Bye.")
            break
        elif line.lower().startswith("/history"):
            parts = line.split()
            n = parts[1] if len(parts) > 1 else ""
            cmd = f"HISTORY {n}".strip()
        elif line.lower().startswith("/help"):
            cmd = "HELP"
        else:
            # One command string: MSG <user> <message>
            cmd = f"MSG {args.user} {line}"

        try:
            resp = send_command(args.host, args.port, cmd)
            print(resp.rstrip("\n"))
        except ConnectionRefusedError:
            print("ERROR: cannot connect. Is the server running and the host/port correct?")
        except Exception as e:
            print(f"ERROR: {e}")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
