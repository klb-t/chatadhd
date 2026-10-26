#!/bin/bash
# Instance shutdown-script for spot-guard (installed by tools/gcp/install_spot_guard.sh). Runs as root on every
# shutdown; acts only on a preemption (the notice gives 120 s here). If work is visibly running and nobody said
# `work.sh done`, it sets work-pending=true so the guard promotes the VM to Standard and restarts it.
# What counts as running work is a preset in metadata: spot-guard-busy-procs (an extended regex over process
# command lines; empty = never auto-detect, only work.sh pending counts).
md() { curl -s -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }
[ "$(md instance/preempted)" = "TRUE" ] || exit 0
[ "$(md instance/attributes/work-pending)" = "true" ] && exit 0
# IW-8 (2026-10-26): a run record `running` with where = this VM means work in progress. Repos to scan come from
# metadata spot-guard-runs-root (one path, its .claude/worktrees/* are scanned too; empty = skip); the records decide
# before the process heuristic below. Runs as root: python3 reads the owner's checkout, work.sh calls gcloud.
rr="$(md instance/attributes/spot-guard-runs-root)"
if [ -n "$rr" ] && [ -f "$rr/tools/vm_place.py" ]; then
  timeout 90 python3 "$rr/tools/vm_place.py" --root "$rr" preempt >/tmp/spot-guard-runs.txt 2>&1
  [ "$(md instance/attributes/work-pending)" = "true" ] && exit 0
fi
re="$(md instance/attributes/spot-guard-busy-procs)"
[ -n "$re" ] || exit 0
if pgrep -f -a -- "$re" >/tmp/spot-guard-busy.txt 2>/dev/null; then
  gcloud compute instances add-metadata "$(md instance/name)" --zone "$(md instance/zone | awk -F/ '{print $NF}')" \
    --metadata "work-pending=true,work-pending-since=$(date +%s),work-note=auto: $(head -c 200 /tmp/spot-guard-busy.txt | tr ',\n' '; ')"
fi
