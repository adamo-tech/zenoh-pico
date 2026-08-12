from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os


class IsolatedHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()


root = Path(__file__).resolve().parents[2] / "dist-wasm" / "browser"
os.chdir(root)
ThreadingHTTPServer(("127.0.0.1", 8000), IsolatedHandler).serve_forever()
