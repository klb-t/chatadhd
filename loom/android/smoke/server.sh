#!/usr/bin/env bash
# Android smoke, host side: loom-server in accounts mode + the fake model provider of the first-steps walk-through.
#   server.sh start ARCHIVE.tar.gz OUTDIR    install the archive, create the account, start server + provider,
#                                            put the provider key / address into the account (same calls as
#                                            deploy/first-steps-api.py), write OUTDIR/smoke.env
#   server.sh stop OUTDIR                    stop both (logs stay in OUTDIR)
# The emulator reaches the host's 127.0.0.1 as 10.0.2.2, so the server listens on loopback only (nothing is exposed
# to the network); 10.0.2.2 is allowed next to 127.0.0.1 in allow_private_hosts (the server refuses private provider
# addresses by default, SSRF guard). The provider itself is addressed as 127.0.0.1 (the server talks to it, not the phone).
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY="$HERE/../../../deploy"
log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
free_port() { python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])'; }

cmd="${1:-}"
case "$cmd" in
  start)
    TB="${2:?archive}"; OUT="${3:?outdir}"
    [ -f "$TB" ] || die "no archive: $TB"
    mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"; TB="$(cd "$(dirname "$TB")" && pwd)/$(basename "$TB")"
    PREFIX="$OUT/opt"; DATA="$OUT/data"
    LOGIN="${SMOKE_LOGIN:-anna}" PASS="${SMOKE_PASS:-haslo-smoke-android-1}" KEY="${SMOKE_KEY:-sk-android-smoke-test}"
    PORT="${SMOKE_PORT:-$(free_port)}" PPORT="${SMOKE_PROVIDER_PORT:-$(free_port)}"
    mkdir -p "$DATA/profiles"
    printf '{"schema":"loom.runtime_profile_overlay/1","domain":"server_accounts","overrides":{"registration":"closed","allow_private_hosts":["127.0.0.1","10.0.2.2"],"throttle":{"trust_forwarded_for":true}}}\n' \
      > "$DATA/profiles/server_accounts.pack"
    log "install $TB -> $PREFIX"
    printf '%s\n' "$PASS" | bash "$DEPLOY/install.sh" --from-tarball "$TB" --prefix "$PREFIX" --data-dir "$DATA" --port "$PORT" \
      --no-deps --no-systemd --no-caddy --admin-login "$LOGIN" >"$OUT/install.log" 2>&1 || { tail -n 30 "$OUT/install.log" >&2; die "install.sh failed"; }
    BIN="$PREFIX/current/bin/loom-server"
    [ -x "$BIN" ] || die "no $BIN after install"
    "$BIN" user list --accounts-root "$DATA" | grep -q "[[:space:]]$LOGIN[[:space:]]" || die "account $LOGIN is not listed"

    nohup python3 "$DEPLOY/first-steps-provider.py" "$PPORT" "$OUT/provider.jsonl" >"$OUT/provider.log" 2>&1 &
    echo $! > "$OUT/provider.pid"
    nohup "$BIN" --host 127.0.0.1 --port "$PORT" --accounts-root "$DATA" --data-dir "$DATA" --static-dir "$PREFIX/current/web" >"$OUT/server.log" 2>&1 &
    echo $! > "$OUT/server.pid"
    ok=0
    for _ in $(seq 1 60); do
      kill -0 "$(cat "$OUT/server.pid")" 2>/dev/null || { tail -n 30 "$OUT/server.log" >&2; die "server died at start"; }
      curl -fsS --max-time 2 "http://127.0.0.1:$PORT/api/healthz" >/dev/null 2>&1 && { ok=1; break; }
      sleep 0.5
    done
    [ "$ok" = 1 ] || die "/api/healthz does not answer after 30 s"
    for _ in $(seq 1 20); do curl -fsS --max-time 2 "http://127.0.0.1:$PPORT/" >/dev/null 2>&1 && break; sleep 0.25; done

    # the same account set-up as deploy/first-steps-api.py (steps 5), done once here so the phone only has to log in
    JAR="$OUT/cookies.txt"; : > "$JAR"
    api() { curl -fsS --max-time 30 -b "$JAR" -c "$JAR" -H 'Content-Type: application/json' "$@"; }
    BASE="http://127.0.0.1:$PORT"
    api -X POST "$BASE/api/auth/login" -d "$(python3 -c 'import json,sys;print(json.dumps({"login":sys.argv[1],"password":sys.argv[2]}))' "$LOGIN" "$PASS")" >/dev/null || die "login of $LOGIN failed"
    api -X PATCH "$BASE/api/config" -d "{\"base_url\":\"http://127.0.0.1:$PPORT\",\"default_model\":\"test/first-steps\",\"stream\":true}" >/dev/null || die "PATCH /api/config failed"
    api -X POST "$BASE/api/secrets/api_key" -d "{\"value\":\"$KEY\"}" >/dev/null || die "saving the provider key failed"
    api "$BASE/api/secrets/api_key/has" | grep -q true || die "the provider key is not visible as set"
    api -X POST "$BASE/api/auth/logout" -d '{}' >/dev/null 2>&1 || true
    rm -f "$JAR"
    cat > "$OUT/smoke.env" <<ENV
SMOKE_OUT=$OUT
SMOKE_PORT=$PORT
SMOKE_PROVIDER_PORT=$PPORT
SMOKE_LOGIN=$LOGIN
SMOKE_PASS=$PASS
SMOKE_KEY=$KEY
SMOKE_URL=http://10.0.2.2:$PORT
ENV
    log "server on 127.0.0.1:$PORT (phone: 10.0.2.2:$PORT), provider on 127.0.0.1:$PPORT, account $LOGIN"
    ;;
  stop)
    OUT="${2:?outdir}"
    for f in server.pid provider.pid; do
      [ -f "$OUT/$f" ] && kill "$(cat "$OUT/$f")" 2>/dev/null || true
    done
    ;;
  *) echo "usage: $0 start ARCHIVE.tar.gz OUTDIR | stop OUTDIR" >&2; exit 2;;
esac
