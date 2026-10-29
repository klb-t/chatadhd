#!/usr/bin/env bash
# Changes this VM's machine type (e.g. more RAM) from inside the VM: deploys tools/gcp/resize_vm.yaml as a Workflow and
# runs it; the workflow stops the VM, sets the type and starts it again (a few minutes; every session restarts).
#   bash tools/gcp/resize_vm.sh --type e2-highmem-8 [--deploy-only]
# Before running: let agents commit and stop builds — the stop is a normal shutdown, work in memory is lost.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
md() { curl -s -m 2 -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }
INSTANCE="$(md instance/name)"; ZONE="$(md instance/zone | awk -F/ '{print $NF}')"; TYPE=""; DEPLOY_ONLY=0
while [ $# -gt 0 ]; do case "$1" in
  --type) TYPE="$2"; shift 2;; --deploy-only) DEPLOY_ONLY=1; shift;; *) sed -n '2,6p' "$0"; exit 2;; esac; done
[ -n "$TYPE" ] || { sed -n '2,6p' "$0"; exit 2; }
PROJECT="$(gcloud config get-value project 2>/dev/null)"; REGION="${ZONE%-*}"
SA="$(gcloud compute instances describe "$INSTANCE" --zone "$ZONE" --format='value(serviceAccounts[0].email)')"
gcloud compute machine-types describe "$TYPE" --zone "$ZONE" --format='value(name,guestCpus,memoryMb)'
gcloud workflows deploy resize-vm --location "$REGION" --source "$here/resize_vm.yaml" --service-account "$SA" \
  --description "one-off machine type change (chatadhd tools/gcp/resize_vm.sh)" >/dev/null
[ "$DEPLOY_ONLY" = 1 ] && { echo "workflow resize-vm deployed; not run"; exit 0; }
arg="$(printf '{"project":"%s","zone":"%s","instance":"%s","machine_type":"%s"}' "$PROJECT" "$ZONE" "$INSTANCE" "$TYPE")"
gcloud workflows run resize-vm --location "$REGION" --data "$arg" --async
echo "resize to $TYPE started; the VM stops now and comes back in a few minutes"
