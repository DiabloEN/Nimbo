"""A running updater is not enough: require a responding ClamAV engine."""

import socket
import sys


def healthy():
    try:
        with socket.socket(socket.AF_UNIX) as conn:
            conn.settimeout(3)
            conn.connect("/run/nimbo-antivirus/clamd.sock")
            conn.sendall(b"zPING\0")
            data = b""
            while len(data) < 16 and not data.endswith(b"\0"):
                chunk = conn.recv(16 - len(data))
                if not chunk:
                    break
                data += chunk
            return data == b"PONG\0"
    except OSError:
        return False


if __name__ == "__main__":
    sys.exit(0 if healthy() else 1)
