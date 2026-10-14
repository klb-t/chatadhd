#!/usr/bin/env python3
"""Kroki 2-6 z docs/PIERWSZE_KROKI.md przez HTTP (to samo, co robi przegladarka): logowanie, klucz dostawcy,
piec sprawdzen z okienka rozmowy. Tylko biblioteka standardowa.
Uzycie: first-steps-api.py BAZA_URL LOGIN HASLO ADRES_DOSTAWCY KLUCZ DZIENNIK_DOSTAWCY
Kod wyjscia 0 = wszystko dziala; wynik (JSON na krok) idzie na stdout."""
import http.cookiejar
import json
import sys
import urllib.error
import urllib.request

base, login, password, provider, key, provider_log = sys.argv[1:7]
jar = http.cookiejar.CookieJar()
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
results = []


def req(method, path, body=None, sse=False):
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(base + path, data=data, method=method,
                               headers={"Content-Type": "application/json", "Accept": "text/event-stream" if sse else "application/json"})
    try:
        with opener.open(r, timeout=120) as resp:
            if sse:
                out = []
                for line in resp:
                    line = line.decode("utf-8", "replace").strip()
                    if line.startswith("data:"):
                        line = line[5:].strip()
                    if line.startswith("{"):
                        out.append(json.loads(line))
                return resp.status, out
            raw = resp.read()
            return resp.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, {"raw": raw[:300]}


def record(name, ok, why):
    results.append({"step": name, "ok": bool(ok), "why": why})
    print(("OK    " if ok else "BLAD  ") + name + " - " + why, file=sys.stderr)
    return ok


def final(chunks):
    return chunks[-1] if chunks else {}


def text_of(chunks):
    t = "".join(c.get("text", "") or c.get("delta", "") or "" for c in chunks if c.get("type") in ("delta", "done", "action_result"))
    return t.strip() or str(final(chunks).get("text") or "")


conv = None


def say(message):
    global conv
    body = {"message": message}
    if conv:
        body["conv_id"] = conv
    code, chunks = req("POST", "/api/chat", body, sse=True)
    for c in chunks if isinstance(chunks, list) else []:
        if c.get("conv_id"):
            conv = c["conv_id"]
    return code, chunks


# 2-3. konto: logowanie
code, me = req("POST", "/api/auth/login", {"login": login, "password": password})
record("logowanie", code == 200, "HTTP %s" % code)
code, me = req("GET", "/api/auth/me")
record("sesja (/api/auth/me)", code == 200 and login in json.dumps(me), "HTTP %s" % code)
# zle haslo nie wpuszcza
bad_code, _ = req("POST", "/api/auth/login", {"login": login, "password": password + "x"})
record("zle haslo jest odrzucone", bad_code in (401, 403, 429), "HTTP %s" % bad_code)

# 5. ustawienia: adres dostawcy, model, klucz
code, _ = req("PATCH", "/api/config", {"base_url": provider, "default_model": "test/first-steps", "stream": True})
record("ustawienia (adres, model)", code == 200, "HTTP %s" % code)
code, _ = req("POST", "/api/secrets/api_key", {"value": key})
record("klucz dostawcy zapisany", code in (200, 204), "HTTP %s" % code)
code, has = req("GET", "/api/secrets/api_key/has")
record("klucz dostawcy widoczny jako ustawiony", code == 200 and (has or {}).get("has") is True, str(has))

# 6. piec sprawdzen
code, ch = say("Zespol Acme planuje wydac wersje 2.0 w piatek, jesli testy przejda. Odpowiedz dwoma zdaniami.")
record("0 zwykla wiadomosc (strumien)", code == 200 and final(ch).get("type") == "done" and text_of(ch), "koniec: %s" % final(ch).get("type"))


def command(name, message, action_hint):
    code, ch = say(message)
    f = final(ch)
    act = str((f.get("proposal") or {}).get("action") or "") + " " + " ".join(str(i.get("action")) for i in (f.get("proposal") or {}).get("items", []))
    if not record(name + " (propozycja)", f.get("type") == "action_proposal" and action_hint in act, "koniec: %s, akcja: %s" % (f.get("type"), act.strip())):
        return None
    # propozycja -> pokwitowanie (receipt_id) -> potwierdzenie: tak samo jak przycisk "Run it" w web (od P14 sam
    # `confirmed:true` bez pokwitowania jest odrzucany)
    prop = f.get("proposal") or {}
    if not record(name + " (pokwitowanie)", prop.get("run_id") and prop.get("receipt_id"), "run_id=%s receipt_id=%s" % (prop.get("run_id"), prop.get("receipt_id"))):
        return None
    code, res = req("POST", "/api/actions/confirm", {"run_id": prop["run_id"], "approved": True, "receipt_id": prop["receipt_id"], "ref": "web"})
    return record(name + " (potwierdzone)", code == 200 and (res or {}).get("status") in ("completed", "executed"),
                  "HTTP %s, status: %s" % (code, (res or {}).get("status")))


command("1 streść tę rozmowę", "Streść tę rozmowę", "summar")
command("2 przetłumacz na niemiecki", "Przetłumacz tę rozmowę na niemiecki", "translat")
command("3 wytłumacz prościej", "Wytłumacz prościej tę rozmowę", "explain")
if command("4 zawsze pytaj przed streszczeniem", "zawsze pytaj mnie przed summarise", "rules.set"):
    code, rules = req("POST", "/api/rules/list", {})
    record("4 reguła jest na liście", code == 200 and "summarise" in json.dumps(rules), "HTTP %s" % code)
command("5 pokaż ustawienie (temperature)", "pokaż temperature", "settings.get")

# klucz dotarl do dostawcy
try:
    calls = [json.loads(l) for l in open(provider_log, encoding="utf-8") if l.strip()]
except OSError:
    calls = []
record("dostawca dostal klucz z Ustawien", any(c["authorization"] == "Bearer " + key for c in calls), "%d zapytan do dostawcy" % len(calls))

print(json.dumps(results, ensure_ascii=False, indent=1))
sys.exit(0 if all(r["ok"] for r in results) else 1)
