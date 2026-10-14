#!/usr/bin/env bash
# ChatADHD / Loom: przechodzi "Pierwsze kroki" (docs/PIERWSZE_KROKI.md) naprawde, bez root i bez systemd:
#   1. instalacja archiwum serwera (install.sh --from-tarball do swiezego katalogu tymczasowego),
#   2. pierwsze konto (loom-server user add, haslo ze stdin),
#   3. start serwera tak, jak robi to usluga systemd,
#   4. logowanie, klucz dostawcy w Ustawieniach (wobec lokalnego falszywego dostawcy),
#   5. piec sprawdzen z okienka rozmowy (first-steps-api.py),
#   6. z --ui: to samo w przegladarce (Chromium przez Playwright, first-steps-ui.mjs).
# Uzycie: deploy/first-steps-check.sh PLIK.tar.gz [--ui] [--shots KATALOG] [--keep]
# Potrzebuje: bash, curl, python3, libssl3. Z --ui: node + pakiet playwright (loom/web: npm ci) i Chromium.
# Nic nie wychodzi poza 127.0.0.1; po pracy wszystko jest sprzatane (chyba ze --keep).
set -euo pipefail

TB="" UI=0 SHOTS="" KEEP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ui) UI=1; shift;;
    --shots) SHOTS="${2:?--shots KATALOG}"; shift 2;;
    --keep) KEEP=1; shift;;
    -h|--help) sed -n 2,12p "$0"; exit 0;;
    -*) echo "nieznana opcja: $1" >&2; exit 2;;
    *) TB="$1"; shift;;
  esac
done
[ -f "$TB" ] || { echo "uzycie: $0 PLIK.tar.gz [--ui] [--shots KATALOG] [--keep]" >&2; exit 2; }
TB="$(cd "$(dirname "$TB")" && pwd)/$(basename "$TB")"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }
free_port() { python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])'; }

W="$(mktemp -d)"
PIDS=()
cleanup() {
  local rc=$?
  for p in "${PIDS[@]:-}"; do [ -z "$p" ] || kill "$p" 2>/dev/null || true; done
  if [ "$rc" -ne 0 ]; then
    for f in server.log provider.log; do [ ! -f "$W/$f" ] || { echo "--- $f (koniec) ---" >&2; tail -n 30 "$W/$f" >&2; }; done
  fi
  [ "$KEEP" = 1 ] && echo "zostawiono: $W" >&2 || rm -rf "$W"
  exit "$rc"
}
trap cleanup EXIT

PREFIX="$W/opt" DATA="$W/data" LOGIN="anna" PASS="haslo-pierwsze-kroki-1" KEY="sk-pierwsze-kroki-test"
PORT="$(free_port)"; PPORT="$(free_port)"

# 1. instalacja (ta sama sciezka, co na VM, tylko bez apt/systemd/Caddy)
log "1. instalacja archiwum do $PREFIX (dane: $DATA)"
mkdir -p "$DATA/profiles"
# Rejestracja zamknieta (konta zaklada operator), a lokalny falszywy dostawca jest dozwolony: serwer domyslnie
# nie laczy sie z adresami prywatnymi (ochrona przed SSRF). Na prawdziwej VM nic z tego nie jest potrzebne.
printf '{"schema":"loom.runtime_profile_overlay/1","domain":"server_accounts","overrides":{"registration":"closed","allow_private_hosts":["127.0.0.1"],"throttle":{"trust_forwarded_for":true}}}\n' \
  > "$DATA/profiles/server_accounts.pack"
printf '%s\n' "$PASS" | bash "$HERE/install.sh" --from-tarball "$TB" --prefix "$PREFIX" --data-dir "$DATA" --port "$PORT" \
  --no-deps --no-systemd --no-caddy --admin-login "$LOGIN" >&2
BIN="$PREFIX/current/bin/loom-server"
[ -x "$BIN" ] || die "po instalacji brak $BIN"
"$BIN" --version >&2

# 2. konto zalozone przez install.sh --admin-login; sprawdz na liscie
"$BIN" user list --accounts-root "$DATA" | grep -q "[[:space:]]$LOGIN[[:space:]]" || die "konto $LOGIN nie jest na liscie"
log "2. konto '$LOGIN' istnieje"

# 3. falszywy dostawca + serwer (te same argumenty co w jednostce systemd)
python3 "$HERE/first-steps-provider.py" "$PPORT" "$W/provider.jsonl" >"$W/provider.log" 2>&1 & PIDS+=($!)
"$BIN" --host 127.0.0.1 --port "$PORT" --accounts-root "$DATA" --data-dir "$DATA" --static-dir "$PREFIX/current/web" >"$W/server.log" 2>&1 & PIDS+=($!)
ok=0
for _ in $(seq 1 60); do
  kill -0 "${PIDS[1]}" 2>/dev/null || die "serwer zakonczyl sie przy starcie"
  curl -fsS --max-time 2 "http://127.0.0.1:$PORT/api/healthz" >"$W/healthz.json" 2>/dev/null && { ok=1; break; }
  sleep 0.5
done
[ "$ok" = 1 ] || die "/api/healthz nie odpowiada po 30 s"
log "3. serwer dziala: $(head -c 120 "$W/healthz.json")"
curl -fsS "http://127.0.0.1:$PORT/" | grep -qi '<html' || die "GET / nie zwraca strony web"

# 4-6. logowanie, klucz, piec sprawdzen (HTTP)
log "4-6. logowanie, klucz dostawcy, piec sprawdzen (HTTP)"
python3 "$HERE/first-steps-api.py" "http://127.0.0.1:$PORT" "$LOGIN" "$PASS" "http://127.0.0.1:$PPORT" "$KEY" "$W/provider.jsonl" >"$W/api.json"

# 7. to samo w przegladarce
if [ "$UI" = 1 ]; then
  command -v node >/dev/null || die "--ui wymaga node"
  log "7. przegladarka (Chromium, headless)"
  # swiezy serwer-konto dla UI: inna rozmowa, ten sam katalog danych (sesja z kroku HTTP nie jest uzywana)
  : > "$W/provider.jsonl"
  FS_BASE="http://127.0.0.1:$PORT" FS_LOGIN="$LOGIN" FS_PASS="$PASS" FS_PROVIDER="http://127.0.0.1:$PPORT" \
    FS_KEY="$KEY" FS_PROVIDER_LOG="$W/provider.jsonl" FS_SHOTS="$SHOTS" node "$HERE/first-steps-ui.mjs" >&2
fi
log "OK: pierwsze kroki dzialaja na tym archiwum"
