# Odtworzenie

python tools/ecosystem-audit-2026-10-09/pass4/watchdog/run.py --repo CHECKOUT --sha SHA --out RECEIPT.json

Wymagane zależności checkoutu (`npm ci --ignore-scripts` samo nie buduje better-sqlite3; w sesji wykorzystano działające zainstalowane zależności). Run.py sprawdza HEAD i czystość śledzonych źródeł. Kod wyjścia 1 jest celowym rzeczywistym FAIL akceptacji, 2 oznacza awarię harnessu. Po naprawie reprodukcja może przestać przechodzić; akceptacja pozostaje osobną kategorią. Nie wykonuje płatnych wywołań.
