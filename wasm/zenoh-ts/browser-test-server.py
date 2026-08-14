#!/usr/bin/env python3
import argparse
import http.server
import json
import os
import signal
import subprocess
import threading
from pathlib import Path


class Harness:
    def __init__(self, zenohd: Path, cert: Path, key: Path):
        self.zenohd = zenohd
        self.cert = cert
        self.key = key
        self.process = None
        self.lock = threading.Lock()

    def start(self):
        with self.lock:
            if self.process is not None and self.process.poll() is None:
                return
            locator = (
                f"webtransport/127.0.0.1:7448#listen_certificate_file={self.cert};"
                f"listen_private_key_file={self.key}"
            )
            # A 1 MiB publication spans 17 negotiated 65,535-byte batches.
            # zenohd's default data queue holds only two batches, which can
            # intermittently drop a fragmented publication while forwarding
            # between the two browser sessions used by this harness.
            self.process = subprocess.Popen([
                str(self.zenohd), "-l", locator,
                "--cfg", "transport/link/tx/queue/size/data:16",
                "--cfg", "timestamping/enabled:true",
            ])

    def stop(self):
        with self.lock:
            if self.process is None or self.process.poll() is not None:
                return
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zenohd", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8001)
    args = parser.parse_args()
    source_dir = Path(__file__).resolve().parent
    repo = source_dir.parents[1]
    dist = repo / "dist-wasm" / "zenoh-ts"
    harness = Harness(
        args.zenohd.resolve(),
        repo / "wasm" / "browser" / "certs" / "localhost.crt",
        repo / "wasm" / "browser" / "certs" / "localhost.key",
    )
    harness.start()
    os.chdir(dist)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def do_POST(self):
            if self.path == "/control/report":
                length = int(self.headers.get("Content-Length", "0"))
                report = json.loads(self.rfile.read(length) or b"{}")
                print("BROWSER_RESULT " + json.dumps(report, separators=(",", ":")), flush=True)
            elif self.path == "/control/router/stop":
                harness.stop()
            elif self.path == "/control/router/start":
                harness.start()
            else:
                self.send_error(404)
                return
            self.send_response(204)
            self.end_headers()

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    signal.signal(signal.SIGTERM, lambda *_: server.shutdown())
    try:
        print(f"Test dashboard: http://127.0.0.1:{args.port}/browser-tests.html", flush=True)
        server.serve_forever()
    finally:
        harness.stop()


if __name__ == "__main__":
    main()
