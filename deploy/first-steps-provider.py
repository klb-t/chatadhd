#!/usr/bin/env python3
"""Falszywy dostawca modeli (zgodny z OpenAI/OpenRouter) dla deploy/first-steps-check.sh.
Tylko biblioteka standardowa. Uzycie: first-steps-provider.py PORT [PLIK_DZIENNIKA]
Odpowiada na POST .../chat/completions (strumieniem SSE albo zwykla odpowiedzia) i zapisuje kazde
zapytanie (jedna linia JSON: czy byl naglowek Authorization, jaki klucz, czy stream, ostatnie zdanie)
do PLIKU_DZIENNIKA. Rozpoznanie intencji ("- summarise:" w tresci) odpowiada JSON-em polecenia."""
import http.server
import json
import sys

LOG = sys.argv[2] if len(sys.argv) > 2 else None
RULES = ((("boil", "streść", "streszcz", "summar"), "summarise"),
         (("wytłumacz", "wytlumacz", "explain"), "explain_simply"),
         (("tłumacz", "tlumacz", "translat"), "translate"))


def last_text(body):
    last = (body.get("messages") or [{}])[-1].get("content") or ""
    if isinstance(last, list):
        last = " ".join(str(p.get("text", "")) for p in last if isinstance(p, dict))
    return last


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_a):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"data":[]}')

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n) or b"{}")
        text = json.dumps(body, ensure_ascii=False)
        last = last_text(body)
        reply = "Odpowiedz testowego dostawcy."
        classify = "- summarise:" in text
        if classify:
            low = last.strip().splitlines()[-1].lower() if last.strip() else ""
            action = next((a for words, a in RULES if any(w in low for w in words)), None)
            reply = json.dumps({"kind": "command" if action else "message", "action": action,
                                "reference": "conversation" if action else None, "args": {},
                                "confidence": 0.9 if action else 0.5})
        elif "Summarise" in text or "Streść" in text or "summar" in text.lower():
            reply = "Streszczenie testowe: rozmowa dotyczy planu wydania wersji 2.0."
        if LOG:
            with open(LOG, "a", encoding="utf-8") as f:
                f.write(json.dumps({"path": self.path, "authorization": self.headers.get("Authorization", ""),
                                    "stream": bool(body.get("stream")), "classify": classify,
                                    "last": last[-120:]}, ensure_ascii=False) + "\n")
        usage = {"prompt_tokens": 20, "completion_tokens": 8, "cost": 0.00001}
        if body.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for chunk in ({"choices": [{"delta": {"content": reply}}]},
                          {"choices": [{"delta": {}, "finish_reason": "stop"}], "usage": usage}):
                self.wfile.write(("data: " + json.dumps(chunk, ensure_ascii=False) + "\n\n").encode())
            self.wfile.write(b"data: [DONE]\n\n")
        else:
            data = json.dumps({"id": "t", "choices": [{"message": {"role": "assistant", "content": reply},
                                                       "finish_reason": "stop"}], "usage": usage}, ensure_ascii=False).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)


if __name__ == "__main__":
    http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
