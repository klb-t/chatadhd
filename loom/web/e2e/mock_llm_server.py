#!/usr/bin/env python3
"""A tiny OpenAI-compatible SSE server used only by the Playwright e2e test
(loom/web/e2e/run.mjs) so loom-server's real streaming chat path gets
exercised without a live API key. Emits a `reasoning` delta then a couple
of `content` deltas, matching what engine/chat_engine.py / Loom's chat
engine expect from a streaming /chat/completions response.
"""
import http.server
import json
import sys
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        self.rfile.read(length)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        chunks = [
            {"id": "1", "choices": [{"delta": {"role": "assistant"}}]},
            {"id": "1", "choices": [{"delta": {"reasoning": "Checking the archive graph for prior forks..."}}]},
            {"id": "1", "choices": [{"delta": {"content": "Hello from the mock model! "}}]},
            {"id": "1", "choices": [{"delta": {"content": "Streaming works end to end."}}]},
            {"id": "1", "choices": [{"delta": {}, "finish_reason": "stop"}]},
        ]
        for c in chunks:
            self.wfile.write(f"data: {json.dumps(c)}\n\n".encode())
            self.wfile.flush()
            time.sleep(0.03)
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8901
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    srv.serve_forever()
