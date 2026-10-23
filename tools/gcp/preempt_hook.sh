#!/bin/bash
# Instance shutdown-script for spot-guard (installed by tools/gcp/install_spot_guard.sh). Runs as root on every
# shutdown; acts only on a preemption (the notice gives 120 s here). If work is visibly running and nobody said
# `work.sh done`, it sets work-pending=true so the guard promotes the VM to Standard and restarts it.
# What counts as running work is a preset in metadata: spot-guard-busy-procs (an extended regex over process
# command lines; empty = never auto-detect, only work.sh pending counts).
md() { curl -s -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }
[ "$(md instance/preempted)" = "TRUE" ] || exit 0
[ "$(md instance/attributes/work-pending)" = "true" ] && exit 0
re="$(md instance/attributes/spot-guard-busy-procs)"
[ -n "$re" ] || exit 0
if pgrep -f -a -- "$re" >/tmp/spot-guard-busy.txt 2>/dev/null; then
  gcloud compute instances add-metadata "$(md instance/name)" --zone "$(md instance/zone | awk -F/ '{print $NF}')" \
    --metadata "work-pending=true,work-pending-since=$(date +%s),work-note=auto: $(head -c 200 /tmp/spot-guard-busy.txt | tr ',\n' '; ')"
fi
