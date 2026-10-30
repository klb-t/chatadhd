#!/usr/bin/env bash
# R107 (owner, 2026-10-30): the VM sleeps when nobody uses it; the waker (tools/gcp/waker, Cloud Run) starts it on a visit.
# Run every 5 min by sleep-when-idle.timer (root). Shuts down only when ALL hold:
#   - no outside request in the Caddy access log for sleep-idle-min minutes (the waker's own checks, user agent r107-waker, ignored)
#   - no work: none of the processes in spot-guard-busy-procs, 5-min load below sleep-max-load, work-pending != true
#   - at least sleep-min-uptime-min minutes since boot (a fresh start always gets time to be used)
# Presets live in instance metadata (switch: sleep-when-idle = on | off | dry-run). A guest shutdown is not a preemption,
# so spot-guard leaves the VM stopped ("stopped, not by preemption: left alone") until the waker starts it.
#   sudo bash tools/gcp/sleep_when_idle.sh [--dry-run]
set -uo pipefail
DRY=0; [ "${1:-}" = --dry-run ] && DRY=1
md() { curl -sf -m 2 -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/instance/attributes/$1"; }
mode="$(md sleep-when-idle)"; mode="${mode:-off}"
idle_min="$(md sleep-idle-min)"; idle_min="${idle_min:-30}"
max_load="$(md sleep-max-load)"; max_load="${max_load:-1.0}"
min_up="$(md sleep-min-uptime-min)"; min_up="${min_up:-20}"
busy="$(md spot-guard-busy-procs)"; pending="$(md work-pending)"
LOG=/var/log/caddy/access.log
say() { echo "sleep-when-idle: $*"; logger -t sleep-when-idle "$*" 2>/dev/null || true; }
[ "$mode" = off ] && [ "$DRY" = 0 ] && { say "off"; exit 0; }
up_min=$(( $(cut -d. -f1 /proc/uptime) / 60 ))
last_req="$(python3 -I - "$LOG" <<'PY'
import json, sys, os
p = sys.argv[1]; last = 0
for f in (p + ".1", p):  # rolled file first, then the live one (Caddy names rolled files differently; best effort)
    if not os.path.exists(f): continue
    with open(f, "rb") as fh:
        fh.seek(0, 2); fh.seek(max(0, fh.tell() - 2_000_000))
        for line in fh.read().decode("utf-8", "replace").splitlines():
            try: e = json.loads(line)
            except Exception: continue
            ua = " ".join(e.get("request", {}).get("headers", {}).get("User-Agent", []))
            if "r107-waker" in ua: continue
            last = max(last, float(e.get("ts", 0)))
print(int(last))
PY
)"
now=$(date +%s); idle=$(( (now - ${last_req:-0}) / 60 ))
load=$(cut -d' ' -f2 /proc/loadavg)
busy_hit=""; [ -n "$busy" ] && busy_hit="$(pgrep -a -f -- "$busy" | grep -v -e sleep_when_idle -e pgrep | head -1)"
reason=""
[ "$up_min" -lt "$min_up" ] && reason="uptime ${up_min} min < ${min_up}"
[ -z "$reason" ] && [ "$idle" -lt "$idle_min" ] && reason="last outside request ${idle} min ago < ${idle_min}"
[ -z "$reason" ] && [ "$pending" = true ] && reason="work-pending=true"
[ -z "$reason" ] && [ -n "$busy_hit" ] && reason="busy process: ${busy_hit:0:80}"
[ -z "$reason" ] && awk -v l="$load" -v m="$max_load" 'BEGIN{exit !(l>=m)}' && reason="load ${load} >= ${max_load}"
if [ -n "$reason" ]; then say "stay up ($reason)"; exit 0; fi
if [ "$DRY" = 1 ] || [ "$mode" = dry-run ]; then say "WOULD SLEEP now (idle ${idle} min, load ${load})"; exit 0; fi
say "sleeping: idle ${idle} min, load ${load}, no work"
shutdown -h now "R107: idle"
