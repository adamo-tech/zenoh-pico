#!/usr/bin/env python3
import argparse
import heapq
import http.server
import json
import os
import random
import selectors
import signal
import socket
import subprocess
import threading
import time
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

    def kill(self):
        with self.lock:
            if self.process is None or self.process.poll() is not None:
                return
            self.process.kill()
            self.process.wait()

    def pause(self):
        with self.lock:
            if self.process is not None and self.process.poll() is None:
                self.process.send_signal(signal.SIGSTOP)

    def resume(self):
        with self.lock:
            if self.process is not None and self.process.poll() is None:
                self.process.send_signal(signal.SIGCONT)


class UdpChaosProxy:
    """A transparent QUIC datagram fault injector.

    WebTransport remains end-to-end encrypted; this proxy only delays, drops,
    duplicates, reorders and rate-limits the UDP packets carrying QUIC.
    """

    def __init__(self, listen_port=7449, upstream_port=7448):
        self.listen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.listen.bind(("127.0.0.1", listen_port))
        self.listen.setblocking(False)
        self.upstream = ("127.0.0.1", upstream_port)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.listen, selectors.EVENT_READ, None)
        self.clients = {}
        self.pending = []
        self.serial = 0
        self.running = True
        self.lock = threading.Lock()
        self.policy = self._clean_policy()
        self.stats = self._empty_stats()
        self.next_send = {"up": 0.0, "down": 0.0}
        self.rng = random.Random()
        self.thread = threading.Thread(target=self._run, name="udp-chaos-proxy", daemon=True)
        self.thread.start()

    @staticmethod
    def _clean_policy():
        return {
            "loss": 0.0, "duplicate": 0.0, "reorder": 0.0,
            "latencyMs": 0.0, "jitterMs": 0.0, "reorderMs": 0.0,
            "bandwidthBytesPerSecond": 0.0,
        }

    @staticmethod
    def _empty_stats():
        return {
            "upPackets": 0, "downPackets": 0, "upBytes": 0, "downBytes": 0,
            "dropped": 0, "duplicated": 0, "reordered": 0, "sendErrors": 0,
        }

    def configure(self, update):
        allowed = set(self._clean_policy())
        unknown = set(update) - allowed - {"seed", "resetStats"}
        if unknown:
            raise ValueError(f"unknown network policy fields: {sorted(unknown)}")
        with self.lock:
            for key in allowed:
                if key in update:
                    value = float(update[key])
                    if value < 0 or (key in {"loss", "duplicate", "reorder"} and value > 1):
                        raise ValueError(f"invalid {key}: {value}")
                    self.policy[key] = value
            if "seed" in update:
                self.rng.seed(int(update["seed"]))
            if update.get("resetStats"):
                self.stats = self._empty_stats()
            return self.snapshot()

    def reset(self):
        with self.lock:
            self.policy = self._clean_policy()
            self.stats = self._empty_stats()
            self.next_send = {"up": 0.0, "down": 0.0}
            self.pending.clear()
            return self.snapshot()

    def snapshot(self):
        # Callers may already hold the lock.
        return {"policy": dict(self.policy), "stats": dict(self.stats), "clients": len(self.clients)}

    def _backend(self, client):
        state = self.clients.get(client)
        if state is None:
            backend = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            backend.connect(self.upstream)
            backend.setblocking(False)
            state = {"socket": backend, "lastSeen": time.monotonic()}
            self.clients[client] = state
            self.selector.register(backend, selectors.EVENT_READ, client)
        state["lastSeen"] = time.monotonic()
        return state["socket"]

    def _queue(self, deadline, sock, payload, address):
        self.serial += 1
        heapq.heappush(self.pending, (deadline, self.serial, sock, payload, address))

    def _impair(self, direction, payload, sock, address=None):
        now = time.monotonic()
        with self.lock:
            policy = dict(self.policy)
            packet_key = f"{direction}Packets"
            byte_key = f"{direction}Bytes"
            self.stats[packet_key] += 1
            self.stats[byte_key] += len(payload)
            if self.rng.random() < policy["loss"]:
                self.stats["dropped"] += 1
                return
            copies = 2 if self.rng.random() < policy["duplicate"] else 1
            if copies == 2:
                self.stats["duplicated"] += 1
            for copy in range(copies):
                delay = policy["latencyMs"] + self.rng.random() * policy["jitterMs"]
                if self.rng.random() < policy["reorder"]:
                    delay += policy["reorderMs"] * (0.5 + self.rng.random())
                    self.stats["reordered"] += 1
                deadline = now + delay / 1000.0 + copy * 0.0001
                bandwidth = policy["bandwidthBytesPerSecond"]
                if bandwidth > 0:
                    deadline = max(deadline, self.next_send[direction])
                    self.next_send[direction] = deadline + len(payload) / bandwidth
                self._queue(deadline, sock, payload, address)

    def _flush(self):
        now = time.monotonic()
        while self.pending and self.pending[0][0] <= now:
            _, _, sock, payload, address = heapq.heappop(self.pending)
            try:
                sock.send(payload) if address is None else sock.sendto(payload, address)
            except OSError:
                with self.lock:
                    self.stats["sendErrors"] += 1

    def _expire_clients(self):
        cutoff = time.monotonic() - 60
        for client, state in list(self.clients.items()):
            if state["lastSeen"] >= cutoff:
                continue
            try:
                self.selector.unregister(state["socket"])
            except Exception:
                pass
            state["socket"].close()
            del self.clients[client]

    def _run(self):
        last_expiry = time.monotonic()
        while self.running:
            timeout = 0.05
            if self.pending:
                timeout = max(0.0, min(timeout, self.pending[0][0] - time.monotonic()))
            for key, _ in self.selector.select(timeout):
                try:
                    payload, client = key.fileobj.recvfrom(65536) if key.data is None else (key.fileobj.recv(65536), key.data)
                except (BlockingIOError, ConnectionRefusedError, OSError):
                    continue
                if key.data is None:
                    self._impair("up", payload, self._backend(client))
                else:
                    state = self.clients.get(client)
                    if state is not None:
                        state["lastSeen"] = time.monotonic()
                    self._impair("down", payload, self.listen, client)
            self._flush()
            if time.monotonic() - last_expiry > 5:
                self._expire_clients()
                last_expiry = time.monotonic()

    def close(self):
        self.running = False
        self.thread.join(timeout=1)
        for state in self.clients.values():
            state["socket"].close()
        self.listen.close()


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
    network = UdpChaosProxy()
    harness.start()
    os.chdir(dist)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def end_headers(self):
            # The generated module filenames are stable while iterating on the
            # bridge. Force browser reloads to exercise the just-built JS/WASM.
            self.send_header("Cache-Control", "no-store, max-age=0")
            self.send_header("Pragma", "no-cache")
            super().end_headers()

        def do_POST(self):
            if self.path == "/control/report":
                length = int(self.headers.get("Content-Length", "0"))
                report = json.loads(self.rfile.read(length) or b"{}")
                print("BROWSER_RESULT " + json.dumps(report, separators=(",", ":")), flush=True)
            elif self.path == "/control/router/stop":
                harness.stop()
            elif self.path == "/control/router/start":
                harness.start()
            elif self.path == "/control/router/kill":
                harness.kill()
            elif self.path == "/control/router/pause":
                harness.pause()
            elif self.path == "/control/router/resume":
                harness.resume()
            elif self.path == "/control/router/freeze":
                length = int(self.headers.get("Content-Length", "0"))
                request = json.loads(self.rfile.read(length) or b"{}")

                def freeze():
                    time.sleep(max(0, float(request.get("delayMs", 0))) / 1000)
                    harness.pause()
                    time.sleep(max(0, float(request.get("durationMs", 1000))) / 1000)
                    harness.resume()

                threading.Thread(target=freeze, name="zenohd-freeze", daemon=True).start()
            elif self.path == "/control/router/flap":
                length = int(self.headers.get("Content-Length", "0"))
                request = json.loads(self.rfile.read(length) or b"{}")

                def flap():
                    time.sleep(max(0, float(request.get("delayMs", 0))) / 1000)
                    harness.kill() if request.get("hard", True) else harness.stop()
                    time.sleep(max(0, float(request.get("downMs", 500))) / 1000)
                    harness.start()

                threading.Thread(target=flap, name="zenohd-flap", daemon=True).start()
            elif self.path == "/control/network/set":
                length = int(self.headers.get("Content-Length", "0"))
                try:
                    network.configure(json.loads(self.rfile.read(length) or b"{}"))
                except ValueError as error:
                    self.send_error(400, str(error))
                    return
            elif self.path == "/control/network/reset":
                network.reset()
            elif self.path == "/control/network/stats":
                body = json.dumps(network.snapshot(), separators=(",", ":")).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
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
        network.close()
        harness.stop()


if __name__ == "__main__":
    main()
