#!/usr/bin/env bash
# Flag for tools/gcp/spot_guard.yaml: tells the guard whether tasks remain on this VM.
#   work.sh pending [note]   tasks remain: after a preemption the guard promotes the VM to Standard and restarts it
#   work.sh done             no tasks: a promoted VM goes back to Spot after spot-guard-return-idle-min
#   work.sh status           current flags and the guard's last action
#   work.sh set KEY=VALUE…   any guard setting (see the header of spot_guard.yaml), e.g. spot-guard=off
# Runs on the VM (instance name/zone from the metadata server) or anywhere with INSTANCE/ZONE set.
set -euo pipefail
md() { curl -s -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }
INSTANCE="${INSTANCE:-$(md instance/name)}"
ZONE="${ZONE:-$(md instance/zone | awk -F/ '{print $NF}')}"
add() { gcloud compute instances add-metadata "$INSTANCE" --zone "$ZONE" --metadata "$1" >/dev/null; }
case "${1:-status}" in
  pending) set -- "$1" "${2:-}"; add "work-pending=true,work-pending-since=$(date +%s),work-note=${2//,/;}"; echo "work-pending=true";;
  done)    add "work-pending=false,work-idle-since=$(date +%s),work-note="; echo "work-pending=false";;
  set)     shift; add "$(IFS=,; echo "$*")"; echo "set: $*";;
  status)  gcloud compute instances describe "$INSTANCE" --zone "$ZONE" --format=json \
             | python3 -c 'import json,sys;[print(i["key"]+"="+i.get("value","")) for i in json.load(sys.stdin)["metadata"]["items"] if i["key"].startswith(("spot-guard","work-"))]'
           gcloud compute instances describe "$INSTANCE" --zone "$ZONE" --format='value(status,scheduling.provisioningModel)';;
  *) sed -n '2,7p' "$0"; exit 2;;
esac
