#!/usr/bin/env bash
# ChatADHD / Loom: sprawdza archiwum serwera zbudowane przez `install.sh --package`.
# Uzycie: deploy/verify-package.sh chatadhd-server-linux-x64-<wersja>.tar.gz
# Sprawdza: sume kontrolna (jesli obok jest .sha256), zawartosc, biblioteki dynamiczne,
# potem uruchamia serwer z archiwum na 127.0.0.1 (losowy port, pusty katalog danych w /tmp)
# i pyta /api/healthz, strone glowna (web) oraz /api/auth/me. Nic nie instaluje, nie potrzebuje root.
set -euo pipefail

TB="${1:-}"
[ -f "$TB" ] || { echo "uzycie: $0 PLIK.tar.gz" >&2; exit 2; }
TB="$(cd "$(dirname "$TB")" && pwd)/$(basename "$TB")"
log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

if [ -f "$TB.sha256" ]; then
  log "suma kontrolna"
  (cd "$(dirname "$TB")" && sha256sum -c "$(basename "$TB").sha256") >&2 || die "suma kontrolna sie nie zgadza"
else
  log "brak $TB.sha256 - pomijam sume kontrolna"
fi

WORK="$(mktemp -d)"
SERVER_PID=""
cleanup() {
  [ -z "$SERVER_PID" ] || kill "$SERVER_PID" 2>/dev/null || true
  [ -z "$SERVER_PID" ] || wait "$SERVER_PID" 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

tar -xzf "$TB" -C "$WORK" --no-same-owner
shopt -s nullglob
roots=("$WORK"/*/)
[ "${#roots[@]}" -eq 1 ] || die "archiwum ma miec jeden katalog glowny, jest ${#roots[@]}"
ROOT="${roots[0]%/}"
for f in bin/loom-server web/index.html VERSION BUILD_INFO; do [ -e "$ROOT/$f" ] || die "brak $f w archiwum"; done
[ -x "$ROOT/bin/loom-server" ] || die "bin/loom-server nie jest wykonywalny"
log "wersja: $(cat "$ROOT/VERSION")"
sed 's/^/    /' "$ROOT/BUILD_INFO" >&2

log "biblioteki dynamiczne"
missing="$(ldd "$ROOT/bin/loom-server" 2>&1 | grep -i 'not found' || true)"
[ -z "$missing" ] || die "brakuje bibliotek lub glibc jest za stary:
$missing"
ldd "$ROOT/bin/loom-server" | sed 's/^/    /' >&2

PORT="${VERIFY_PORT:-$(( (RANDOM % 20000) + 20000 ))}"
DATA="$WORK/data"; mkdir -p "$DATA"
log "start serwera na 127.0.0.1:$PORT"
"$ROOT/bin/loom-server" --host 127.0.0.1 --port "$PORT" --accounts-root "$DATA" --data-dir "$DATA" \
  --static-dir "$ROOT/web" >"$WORK/server.log" 2>&1 &
SERVER_PID=$!

ok=0
for _ in $(seq 1 60); do
  kill -0 "$SERVER_PID" 2>/dev/null || { cat "$WORK/server.log" >&2; die "serwer zakonczyl sie przy starcie"; }
  if curl -fsS --max-time 2 "http://127.0.0.1:$PORT/api/healthz" >"$WORK/healthz.json" 2>/dev/null; then ok=1; break; fi
  sleep 0.5
done
[ "$ok" = 1 ] || { cat "$WORK/server.log" >&2; die "/api/healthz nie odpowiada po 30 s"; }
log "/api/healthz: $(head -c 200 "$WORK/healthz.json")"

curl -fsS --max-time 5 "http://127.0.0.1:$PORT/" >"$WORK/index.html" || die "GET / nie dziala"
grep -qi '<html' "$WORK/index.html" || die "GET / nie zwrocilo strony web"
log "GET / zwraca strone web"

code="$(curl -sS --max-time 5 -o "$WORK/me.json" -w '%{http_code}' "http://127.0.0.1:$PORT/api/auth/me" || true)"
case "$code" in
  200|401) ;;
  *) cat "$WORK/server.log" >&2; die "/api/auth/me zwrocilo HTTP $code (oczekiwano 200 lub 401)";;
esac
head -c 1 "$WORK/me.json" | grep -q '{' || die "/api/auth/me nie zwrocilo JSON"
log "/api/auth/me: HTTP $code"
log "OK: archiwum dziala"
