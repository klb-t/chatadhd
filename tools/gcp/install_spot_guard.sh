#!/usr/bin/env bash
# Installs spot-guard (tools/gcp/spot_guard.yaml) for one Spot VM: a GCP Workflow, a Cloud Scheduler tick that runs it,
# the preemption hook as the instance shutdown-script, and preset settings in instance metadata (existing values are
# kept). Idempotent; --uninstall removes the workflow, the job and the hook (settings stay, harmless).
#   bash tools/gcp/install_spot_guard.sh [--instance NAME] [--zone ZONE] [--region REGION] [--every MINUTES]
#                                        [--sa EMAIL] [--uninstall]
# Defaults come from the metadata server when run on the VM itself. Cost: Workflows bills internal steps
# (≈ 10–20 per tick); at a tick every 2 minutes that is a few hundred thousand steps a month, around a dollar
# beyond the free tier — check current pricing. --every changes the tick.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
md() { curl -s -m 2 -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1" || true; }
INSTANCE="$(md instance/name)"; ZONE="$(md instance/zone | awk -F/ '{print $NF}')"; EVERY=2; SA=""; UNINSTALL=0; REGION=""
while [ $# -gt 0 ]; do case "$1" in
  --instance) INSTANCE="$2"; shift 2;; --zone) ZONE="$2"; shift 2;; --region) REGION="$2"; shift 2;;
  --every) EVERY="$2"; shift 2;; --sa) SA="$2"; shift 2;; --uninstall) UNINSTALL=1; shift;;
  *) sed -n '2,10p' "$0"; exit 2;; esac; done
[ -n "$INSTANCE" ] && [ -n "$ZONE" ] || { echo "podaj --instance i --zone"; exit 2; }
PROJECT="$(gcloud config get-value project 2>/dev/null)"
REGION="${REGION:-${ZONE%-*}}"
SA="${SA:-$(gcloud compute instances describe "$INSTANCE" --zone "$ZONE" --format='value(serviceAccounts[0].email)')}"
WF=spot-guard JOB="spot-guard-tick-$INSTANCE"

if [ "$UNINSTALL" = 1 ]; then
  gcloud scheduler jobs delete "$JOB" --location "$REGION" --quiet || true
  gcloud workflows delete "$WF" --location "$REGION" --quiet || true
  gcloud compute instances remove-metadata "$INSTANCE" --zone "$ZONE" --keys shutdown-script || true
  echo "spot-guard usunięty (ustawienia w metadanych zostały)"; exit 0
fi

gcloud services enable workflows.googleapis.com workflowexecutions.googleapis.com cloudscheduler.googleapis.com
gcloud workflows deploy "$WF" --location "$REGION" --source "$here/spot_guard.yaml" --service-account "$SA" \
  --description "Spot->Standard promotion when work is pending at preemption, back to Spot when idle (chatadhd tools/gcp)"

arg="$(printf '{"project":"%s","zone":"%s","instance":"%s"}' "$PROJECT" "$ZONE" "$INSTANCE")"
body="$(python3 -c 'import json,sys;print(json.dumps({"argument":sys.argv[1]}))' "$arg")"
uri="https://workflowexecutions.googleapis.com/v1/projects/$PROJECT/locations/$REGION/workflows/$WF/executions"
verb=create; gcloud scheduler jobs describe "$JOB" --location "$REGION" >/dev/null 2>&1 && verb=update
gcloud scheduler jobs "$verb" http "$JOB" --location "$REGION" --schedule "*/$EVERY * * * *" --uri "$uri" \
  --http-method POST --message-body "$body" --oauth-service-account-email "$SA" \
  --oauth-token-scope https://www.googleapis.com/auth/cloud-platform

# preset settings: only keys that are not set yet
have="$(gcloud compute instances describe "$INSTANCE" --zone "$ZONE" --format=json \
  | python3 -c 'import json,sys;print("\n".join(i["key"] for i in json.load(sys.stdin)["metadata"].get("items",[])))')"
set_default() { grep -qx "$1" <<<"$have" || gcloud compute instances add-metadata "$INSTANCE" --zone "$ZONE" --metadata "^~^$1=$2" >/dev/null; }
set_default spot-guard on
set_default work-pending false
set_default spot-guard-return-idle-min 10
set_default spot-guard-pending-ttl-hours 8
set_default spot-guard-try-spot-first false
set_default spot-guard-restart-idle false
set_default spot-guard-busy-procs 'cc1plus|ninja|ctest|loom_tests|gate\.sh|playwright'
# IW-8: checkout whose run records (running, where = this VM) count as work in progress at preemption (preset: this repo)
set_default spot-guard-runs-root "${RUNS_ROOT:-$(cd "$here/../.." && pwd)}"
if grep -qx shutdown-script <<<"$have" && ! gcloud compute instances describe "$INSTANCE" --zone "$ZONE" \
     --format='value(metadata.items.shutdown-script)' | grep -q 'spot-guard'; then
  echo "UWAGA: instancja ma już inny shutdown-script; nie nadpisuję. Hak: $here/preempt_hook.sh"
else
  gcloud compute instances add-metadata "$INSTANCE" --zone "$ZONE" --metadata-from-file "shutdown-script=$here/preempt_hook.sh" >/dev/null
fi
echo "spot-guard: workflow $WF ($REGION), tick co $EVERY min ($JOB), SA $SA, instancja $INSTANCE ($ZONE)"
echo "flaga z VM: tools/gcp/work.sh pending|done|status|set KEY=VALUE"
