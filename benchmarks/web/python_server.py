# The benchmark API with Python's standard library (ThreadingHTTPServer).
import json, os, re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

NOTE = re.compile(r"^/notes/(\d+)$")

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        m = NOTE.match(self.path)
        if not m:
            self.send_error(404)
            return
        body = json.dumps({"id": int(m.group(1)), "text": "buy milk", "done": False}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass

ThreadingHTTPServer(("127.0.0.1", int(os.environ.get("PORT", "18082"))), Handler).serve_forever()
