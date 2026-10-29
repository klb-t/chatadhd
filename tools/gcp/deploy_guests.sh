#!/usr/bin/env bash
# deploy_guests.sh — first production deployment of use without an account (R103 B3) on the VM.
#
# What it does, in this order (every step printed; --dry-run prints them and does nothing):
#   1. preflight: tools, the current release (/opt/chatadhd/current), free disk for the backup, the package
#   2. stops the service and backs up the data directory (timestamped copy, size and file count compared)
#   3. unpacks the new release next to the old one (deploy/install.sh --from-tarball, without switching)
#   4. merges the production overlay deploy/profiles/server_accounts.guests.pack into <data>/profiles/server_accounts.pack:
#      guests.enabled = true and throttle.trust_forwarded_for = true (everything else kept; old overlay backed up)
#   5. Caddy: copies deploy/caddy/guests.caddy to /etc/caddy/chatadhd-guests.caddy and imports it in the site block of
#      the chat domain (guests-ratelimit.caddy only with --ratelimit and when `caddy list-modules` has rate_limit);
#      `caddy validate`, else the old Caddyfile is put back
#   6. switches /opt/chatadhd/current to the new release and starts the service
#   7. health check: /api/healthz, a guest challenge + a guest start with the proof of work, /api/pool/summary with the
#      owner's session when OWNER_LOGIN is given (password asked, never stored)
#   8. prints the exact rollback command (data backup + previous release + previous Caddyfile)
#
# Usage:
#   bash tools/gcp/deploy_guests.sh --dry-run [--tarball FILE]
#   bash tools/gcp/deploy_guests.sh --tarball chatadhd-server-linux-x64-<v>.tar.gz [--domain chat.<ip>.sslip.io]
#   bash tools/gcp/deploy_guests.sh --build            (builds the package first: deploy/install.sh --package)
#   bash tools/gcp/deploy_guests.sh --rollback --release <previous> --backup <dir> [--caddyfile <copy>]
# Environment: OWNER_LOGIN (optional, for the pool summary check). Every path below is a preset (flags).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PREFIX=/opt/chatadhd
DATA_DIR=/var/lib/chatadhd
BACKUP_ROOT=/var/backups/chatadhd
SERVICE=chatadhd.service
PORT=8787
CADDYFILE=/etc/caddy/Caddyfile
CADDY_SNIPPET=/etc/caddy/chatadhd-guests.caddy
CADDY_RL_SNIPPET=/etc/caddy/chatadhd-guests-ratelimit.caddy
DOMAIN=""
TARBALL=""
BUILD=0
DRY=0
RATELIMIT=0
ROLLBACK=0
RB_RELEASE=""
RB_BACKUP=""
RB_CADDYFILE=""
TS="$(date -u +%Y%m%dT%H%M%SZ)"
ADVISORIES=()

usage() { sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; }
die() { echo "deploy_guests: $*" >&2; exit 1; }
note() { echo; echo "== $*"; }
advise() { ADVISORIES+=("$*"); echo "   advisory: $*"; }
# run: print the command; execute it unless --dry-run.
run() {
  printf '   + %s\n' "$*"
  [ "$DRY" = 1 ] || "$@"
}
need() { [ $# -ge 2 ] || die "$1 needs a value"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1; shift;;
    --tarball) need "$@"; TARBALL="$2"; shift 2;;
    --build) BUILD=1; shift;;
    --domain) need "$@"; DOMAIN="$2"; shift 2;;
    --ratelimit) RATELIMIT=1; shift;;
    --prefix) need "$@"; PREFIX="$2"; shift 2;;
    --data-dir) need "$@"; DATA_DIR="$2"; shift 2;;
    --backup-root) need "$@"; BACKUP_ROOT="$2"; shift 2;;
    --port) need "$@"; PORT="$2"; shift 2;;
    --rollback) ROLLBACK=1; shift;;
    --release) need "$@"; RB_RELEASE="$2"; shift 2;;
    --backup) need "$@"; RB_BACKUP="$2"; shift 2;;
    --caddyfile) need "$@"; RB_CADDYFILE="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) die "unknown option $1 (see --help)";;
  esac
done

SUDO=""
[ "$(id -u)" = 0 ] || SUDO=sudo
[ "$DRY" = 1 ] && echo "DRY RUN — nothing is changed; every step below is only printed."

# ── rollback ────────────────────────────────────────────────────────────────────────────────────────────────────
if [ "$ROLLBACK" = 1 ]; then
  [ -n "$RB_RELEASE" ] && [ -n "$RB_BACKUP" ] || die "--rollback needs --release <dir name in $PREFIX> and --backup <dir>"
  [ "$DRY" = 1 ] || [ -d "$PREFIX/$RB_RELEASE" ] || die "no release $PREFIX/$RB_RELEASE"
  [ "$DRY" = 1 ] || $SUDO test -d "$RB_BACKUP" || die "no backup $RB_BACKUP"
  note "rollback: stop, put the data back (the current data is kept aside), previous release, previous Caddyfile"
  run $SUDO systemctl stop "$SERVICE"
  run $SUDO mv "$DATA_DIR" "$BACKUP_ROOT/rolled-back-$TS"
  run $SUDO cp -a "$RB_BACKUP" "$DATA_DIR"
  run $SUDO ln -sfn "$PREFIX/$RB_RELEASE" "$PREFIX/current"
  if [ -n "$RB_CADDYFILE" ]; then
    run $SUDO cp -a "$RB_CADDYFILE" "$CADDYFILE"
    run $SUDO caddy validate --config "$CADDYFILE" --adapter caddyfile
    run $SUDO systemctl reload caddy
  fi
  run $SUDO systemctl start "$SERVICE"
  run curl -fsS -m 10 "http://127.0.0.1:$PORT/api/healthz"
  echo; echo "rollback done (the data from before the rollback: $BACKUP_ROOT/rolled-back-$TS)"
  exit 0
fi

# ── 1. preflight ────────────────────────────────────────────────────────────────────────────────────────────────
note "1. preflight"
for t in curl python3 tar; do command -v "$t" >/dev/null || die "missing $t"; done
command -v caddy >/dev/null || advise "caddy not found: the Caddy step will fail"
PREV="$(readlink "$PREFIX/current" 2>/dev/null || true)"
PREV_NAME="$(basename "${PREV:-unknown}")"
echo "   current release: ${PREV:-none} (rollback target)"
if [ -z "$DOMAIN" ]; then
  DOMAIN="$(grep -oE '^chat\.[^ {]+' "$CADDYFILE" 2>/dev/null | head -1 || true)"
  [ -n "$DOMAIN" ] || advise "chat domain not found in $CADDYFILE; pass --domain (the public guest check is then skipped)"
fi
echo "   chat domain: ${DOMAIN:-unknown}"
if [ "$BUILD" = 1 ]; then
  PKG_DIR="$REPO/dist-guests-$TS"
  run bash "$REPO/deploy/install.sh" --package "$PKG_DIR"
  if [ "$DRY" = 1 ]; then TARBALL="$PKG_DIR/chatadhd-server-linux-<arch>-<version>.tar.gz"
  else TARBALL="$(find "$PKG_DIR" -maxdepth 1 -name 'chatadhd-server-linux-*.tar.gz' | sort | tail -1)"; fi
fi
if [ -z "$TARBALL" ]; then
  [ "$DRY" = 1 ] || die "give --tarball FILE or --build"
  TARBALL="<chatadhd-server-linux-x64-VERSION.tar.gz>"
  advise "no --tarball given: the dry run shows the steps with a placeholder package"
fi
[ "$DRY" = 1 ] || [ -f "$TARBALL" ] || die "no package $TARBALL"
if [ -f "$TARBALL.sha256" ]; then run sh -c "cd '$(dirname "$TARBALL")' && sha256sum -c '$(basename "$TARBALL").sha256'"; fi
NEW_VERSION="$(tar -xzOf "$TARBALL" --wildcards '*/VERSION' 2>/dev/null | head -1 | tr -d '[:space:]' || true)"
NEW_VERSION="${NEW_VERSION:-guests-$TS}"
[ "$NEW_VERSION" != "$PREV_NAME" ] || NEW_VERSION="$NEW_VERSION-$TS"
echo "   new release: $PREFIX/$NEW_VERSION"
BACKUP="$BACKUP_ROOT/chatadhd-$TS"
CADDY_BACKUP="$BACKUP_ROOT/Caddyfile-$TS"
if [ "$DRY" = 1 ]; then
  echo "   (would measure: du -sb $DATA_DIR, df -B1 --output=avail $BACKUP_ROOT; needs data size + 1 GiB free)"
else
  DATA_BYTES="$($SUDO du -sb "$DATA_DIR" | cut -f1)"
  $SUDO mkdir -p "$BACKUP_ROOT"
  FREE_BYTES="$(df -B1 --output=avail "$BACKUP_ROOT" | tail -1 | tr -d ' ')"
  echo "   data: $DATA_BYTES bytes, free for the backup: $FREE_BYTES bytes"
  [ "$FREE_BYTES" -gt $((DATA_BYTES + 1073741824)) ] || die "not enough disk for the backup (data + 1 GiB)"
fi

# ── 2. backup ───────────────────────────────────────────────────────────────────────────────────────────────────
note "2. stop the service and back up $DATA_DIR -> $BACKUP (a consistent copy of SQLite needs the server stopped)"
run $SUDO systemctl stop "$SERVICE"
run $SUDO mkdir -p "$BACKUP_ROOT"
run $SUDO cp -a "$DATA_DIR" "$BACKUP"
if [ "$DRY" = 1 ]; then
  echo "   (would compare: du -sb and find | wc -l of $DATA_DIR and $BACKUP; stop on a difference)"
else
  a="$($SUDO du -sb "$DATA_DIR" | cut -f1)"; b="$($SUDO du -sb "$BACKUP" | cut -f1)"
  fa="$($SUDO find "$DATA_DIR" | wc -l)"; fb="$($SUDO find "$BACKUP" | wc -l)"
  echo "   sizes: data $a / backup $b bytes; files: $fa / $fb"
  if [ "$a" != "$b" ] || [ "$fa" != "$fb" ]; then
    $SUDO systemctl start "$SERVICE"
    die "backup differs from the data; the service was started again on the old release, nothing else changed"
  fi
fi

# ── 3. new release ──────────────────────────────────────────────────────────────────────────────────────────────
note "3. unpack the new release next to the old one (no switch yet)"
run $SUDO bash "$REPO/deploy/install.sh" --from-tarball "$TARBALL" --version "$NEW_VERSION" --prefix "$PREFIX" \
  --data-dir "$DATA_DIR" --no-deps --no-systemd --no-caddy --no-switch

# ── 4. production overlay ───────────────────────────────────────────────────────────────────────────────────────
PROFILE="$DATA_DIR/profiles/server_accounts.pack"
note "4. overlay $PROFILE: guests.enabled = true, throttle.trust_forwarded_for = true (rest kept)"
MERGE='import json,sys,os
p,src=sys.argv[1],sys.argv[2]
d={"schema":"loom.runtime_profile_overlay/1","domain":"server_accounts","overrides":{}}
if os.path.exists(p):
    d=json.load(open(p))
def merge(a,b):
    for k,v in b.items():
        if isinstance(v,dict) and isinstance(a.get(k),dict): merge(a[k],v)
        else: a[k]=v
merge(d.setdefault("overrides",{}),json.load(open(src))["overrides"])
open(p+".tmp","w").write(json.dumps(d)+"\n"); os.replace(p+".tmp",p)
print("   overlay:",json.dumps(d))'
run $SUDO sh -c "[ ! -e '$PROFILE' ] || cp -a '$PROFILE' '$BACKUP_ROOT/server_accounts.pack-$TS'"
run $SUDO python3 -c "$MERGE" "$PROFILE" "$REPO/deploy/profiles/server_accounts.guests.pack"
run $SUDO chown chatadhd:chatadhd "$PROFILE"

# ── 5. Caddy ────────────────────────────────────────────────────────────────────────────────────────────────────
note "5. Caddy: guest wall snippet imported in the chat site block"
run $SUDO cp -a "$CADDYFILE" "$CADDY_BACKUP"
run $SUDO cp "$REPO/deploy/caddy/guests.caddy" "$CADDY_SNIPPET"
IMPORTS="$CADDY_SNIPPET"
if [ "$RATELIMIT" = 1 ]; then
  if caddy list-modules 2>/dev/null | grep -q '^http.handlers.rate_limit$'; then
    run $SUDO cp "$REPO/deploy/caddy/guests-ratelimit.caddy" "$CADDY_RL_SNIPPET"
    IMPORTS="$IMPORTS $CADDY_RL_SNIPPET"
    advise "rate_limit also needs 'order rate_limit before reverse_proxy' in the global options of $CADDYFILE (see the snippet header)"
  else
    advise "--ratelimit asked but this Caddy has no rate_limit module (xcaddy build --with github.com/mholt/caddy-ratelimit); only the stock snippet is imported"
  fi
fi
IMPORT_PY='import sys,re
p,dom=sys.argv[1],sys.argv[2]; imps=sys.argv[3:]
s=open(p).read()
m=re.search(r"(^"+re.escape(dom)+r"\s*\{\n)", s, re.M)
if not m: sys.exit("site block "+dom+" not found in "+p)
add="".join("  import "+i+"\n" for i in imps if ("import "+i) not in s)
s=s[:m.end()]+add+s[m.end():]
open(p,"w").write(s)
print("   imported:", add.strip() or "(already there)")'
if [ -n "$DOMAIN" ]; then
  # shellcheck disable=SC2086
  run $SUDO python3 -c "$IMPORT_PY" "$CADDYFILE" "$DOMAIN" $IMPORTS
  if [ "$DRY" = 1 ]; then
    run $SUDO caddy validate --config "$CADDYFILE" --adapter caddyfile
  elif ! $SUDO caddy validate --config "$CADDYFILE" --adapter caddyfile >/dev/null; then
    $SUDO cp -a "$CADDY_BACKUP" "$CADDYFILE"
    advise "caddy validate failed: the old Caddyfile is back, guests run without the Caddy wall (the server limits still apply)"
  fi
  run $SUDO systemctl reload caddy
  # aliases.sh (documents branch) regenerates the Caddyfile; advise only when that copy lacks the guests import.
  ALIASES_SH="${ALIASES_SH:-$HOME/chatadhd/tools/gcp/aliases.sh}"
  if [ ! -f "$ALIASES_SH" ]; then
    advise "could not check $ALIASES_SH (missing; set ALIASES_SH): if aliases.sh regenerates $CADDYFILE, it must import chatadhd-guests"
  elif ! grep -q "chatadhd-guests" "$ALIASES_SH"; then
    advise "$ALIASES_SH regenerates $CADDYFILE without the chatadhd-guests import; add it there before its next run"
  fi
fi

# ── 6. switch and start ─────────────────────────────────────────────────────────────────────────────────────────
note "6. switch $PREFIX/current -> $NEW_VERSION and start"
run $SUDO ln -sfn "$PREFIX/$NEW_VERSION" "$PREFIX/current"
run $SUDO systemctl start "$SERVICE"

# ── 7. health check ─────────────────────────────────────────────────────────────────────────────────────────────
note "7. health check"
HEALTH_PY='import hashlib,json,sys,urllib.request,http.cookiejar
base=sys.argv[1]
cj=http.cookiejar.CookieJar(); op=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
c=json.load(op.open(base+"/api/auth/guest/challenge",timeout=15))
n=0
while c.get("bits",0) and int.from_bytes(hashlib.sha256(("%s:%d"%(c["challenge"],n)).encode()).digest(),"big")>>(256-c["bits"]): n+=1
req=urllib.request.Request(base+"/api/auth/guest",data=json.dumps({"challenge":c.get("challenge",""),"nonce":str(n)}).encode(),
    headers={"Content-Type":"application/json","Origin":base},method="POST")
r=op.open(req,timeout=30); g=json.load(r)["guest"]
me=json.load(op.open(base+"/api/auth/me",timeout=15))
src=me.get("guest",{}).get("model_source") or {}
print("   guest start: HTTP",r.status,g["id"],"source:",src.get("provider"),src.get("model"),"why:",src.get("why"))
sys.exit(0 if src.get("provider") else 3)'
SUMMARY_PY='import getpass,json,os,sys,urllib.request,http.cookiejar
base=sys.argv[1]; login=os.environ["OWNER_LOGIN"]
cj=http.cookiejar.CookieJar(); op=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
pw=getpass.getpass("   password of %s (not stored): "%login)
op.open(urllib.request.Request(base+"/api/auth/login",data=json.dumps({"login":login,"password":pw}).encode(),
    headers={"Content-Type":"application/json","Origin":base},method="POST"),timeout=60)
s=json.load(op.open(base+"/api/pool/summary",timeout=15))
print("   pool summary: guests_enabled",s.get("guests_enabled"),"live",s.get("live_guest_sessions"),"spend",json.dumps(s.get("spend",{}).get("providers",{})))'
run curl -fsS -m 10 --retry 10 --retry-delay 1 --retry-connrefused "http://127.0.0.1:$PORT/api/healthz"
if [ -n "$DOMAIN" ]; then
  run python3 -c "$HEALTH_PY" "https://$DOMAIN"
  if [ -n "${OWNER_LOGIN:-}" ]; then run python3 -c "$SUMMARY_PY" "https://$DOMAIN"
  else advise "OWNER_LOGIN not set: /api/pool/summary not checked (OWNER_LOGIN=<login> bash $0 ... to check it)"; fi
fi

# ── 8. rollback command ─────────────────────────────────────────────────────────────────────────────────────────
note "8. rollback (if anything is wrong):"
echo "   bash $0 --rollback --release $PREV_NAME --backup $BACKUP --caddyfile $CADDY_BACKUP"
echo "   by hand: sudo systemctl stop $SERVICE && sudo mv $DATA_DIR $BACKUP_ROOT/rolled-back-$TS && sudo cp -a $BACKUP $DATA_DIR \\"
echo "            && sudo ln -sfn $PREFIX/$PREV_NAME $PREFIX/current && sudo cp -a $CADDY_BACKUP $CADDYFILE \\"
echo "            && sudo systemctl reload caddy && sudo systemctl start $SERVICE"
if [ "${#ADVISORIES[@]}" -gt 0 ]; then
  echo; echo "advisories:"; for a in "${ADVISORIES[@]}"; do echo " - $a"; done
fi
[ "$DRY" = 1 ] && echo && echo "DRY RUN finished — nothing was changed."
exit 0
