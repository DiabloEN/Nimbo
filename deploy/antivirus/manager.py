#!/usr/bin/env python3
"""Unprivileged signature updater. Private Unix socket; no shell/user arguments."""

import http.server, socketserver, socket, os, subprocess, threading, time, json

SOCKET = "/run/nimbo-antivirus/control.sock"
STATE = "/var/lib/clamav/nimbo-update.json"
lock = threading.Lock()
engine = None
status = {"busy": False, "lastCheck": 0, "lastSuccess": 0, "error": ""}
try:
    saved = json.load(open(STATE))
    for key in ("lastCheck", "lastSuccess", "error"):
        status[key] = saved.get(key, status[key])
except (OSError, ValueError):
    pass


def update():
    global engine
    try:
        # Do not hold two signature engines in memory on a small host.
        if engine and engine.poll() is None:
            engine.terminate()
            try:
                engine.wait(timeout=15)
            except subprocess.TimeoutExpired:
                engine.kill()
                engine.wait()
        result = subprocess.run(
            ["/usr/bin/freshclam", "--config-file=/etc/nimbo-antivirus/freshclam.conf", "--stdout"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=180,
        )
        with lock:
            status["lastCheck"] = time.time()
            if result.returncode == 0:
                status["lastSuccess"] = status["lastCheck"]
                status["error"] = ""
            else:
                status["error"] = (
                    "Не удалось обновить базы. Повторите позже; текущие базы сохранены."
                )
    except (OSError, subprocess.TimeoutExpired):
        with lock:
            status["lastCheck"] = time.time()
            status["error"] = "Обновление баз не завершилось. Проверьте соединение сервера."
    finally:
        launch_engine()
        with lock:
            status["busy"] = False
            try:
                with open(STATE + ".tmp", "w") as f:
                    json.dump(status, f)
                os.replace(STATE + ".tmp", STATE)
            except OSError:
                pass


def start_update():
    with lock:
        if status["busy"] or time.time() - status["lastCheck"] < 300:
            return
        status["busy"] = True
    threading.Thread(target=update, daemon=True).start()


def scheduled():
    while True:
        if time.time() - status["lastCheck"] >= 6 * 3600:
            start_update()
        elif not status["busy"] and (engine is None or engine.poll() is not None):
            launch_engine()
        time.sleep(60)


def launch_engine():
    global engine
    if engine is None or engine.poll() is not None:
        try:
            engine = subprocess.Popen(
                [
                    "/usr/sbin/clamd",
                    "--config-file=/etc/nimbo-antivirus/clamd.conf",
                    "--foreground=true",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except OSError:
            engine = None
            with lock:
                status["error"] = (
                    "Не удалось запустить ClamAV. Администратору хоста нужно проверить службу."
                )


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path != "/status":
            self.send_error(404)
            return
        self.respond()

    def do_POST(self):
        if self.path != "/update":
            self.send_error(404)
            return
        start_update()
        self.respond()

    def respond(self):
        with lock:
            result = dict(status)
        try:
            pid = engine.pid if engine else 0
            lines = open("/proc/%d/status" % pid).readlines()
            result["memoryBytes"] = next(
                int(x.split()[1]) * 1024 for x in lines if x.startswith("VmRSS:")
            )
        except (OSError, ValueError, StopIteration):
            pass
        try:
            group = next(
                x.strip().split(":", 2)[2] for x in open("/proc/self/cgroup") if x.startswith("0::")
            )
            base = "/sys/fs/cgroup" + group
            result["serviceMemoryBytes"] = int(open(base + "/memory.current").read())
            result["memoryLimitBytes"] = int(open(base + "/memory.max").read())
            quota, period = open(base + "/cpu.max").read().split()
            result["cpuLimitPercentOfHost"] = round(
                int(quota) / int(period) / (os.cpu_count() or 1) * 100
            )
        except (OSError, ValueError, StopIteration):
            pass
        body = json.dumps(result).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class Server(socketserver.UnixStreamServer):
    def get_request(self):
        conn, addr = super().get_request()
        conn.settimeout(5)
        return conn, addr


if __name__ == "__main__":
    if os.path.exists(SOCKET):
        os.unlink(SOCKET)
    server = Server(SOCKET, Handler)
    os.chmod(SOCKET, 0o660)
    threading.Thread(target=scheduled, daemon=True).start()
    server.serve_forever()
