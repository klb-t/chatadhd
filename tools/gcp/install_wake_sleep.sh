#!/usr/bin/env bash
# R107 (owner, 2026-10-30): install the waker (Cloud Run, outside the VM) and the idle sleep (timer on the VM). Idempotent.
#   bash tools/gcp/install_wake_sleep.sh [--dry-run] [--sleep on|dry-run|off]
# Waker: public URL https://wake-<hash>.europe-west4.run.app/{watchdog,chat,ageds}; service account r107-waker with
# compute.instances.get/start on this instance only (custom role). Cost: Cloud Run free tier (min-instances 0).
# Sleep: /etc/systemd/system/sleep-when-idle.{service,timer}, every 5 min; presets in instance metadata
# (sleep-when-idle, sleep-idle-min 30, sleep-max-load 1.0, sleep-min-uptime-min 20). Default after install: dry-run
# (logs what it would do: journalctl -t sleep-when-idle); --sleep on makes it real. Off: --sleep off.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; DRY=0; SLEEP=dry-run
while [ $# -gt 0 ]; do case "$1" in --dry-run) DRY=1; shift;; --sleep) SLEEP="$2"; shift 2;; *) sed -n '2,9p' "$0"; exit 2;; esac; done
run() { echo "+ $*"; [ "$DRY" = 1 ] || "$@"; }
md() { curl -sf -m 2 -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }
INSTANCE="$(md instance/name)"; ZONE="$(md instance/zone | awk -F/ '{print $NF}')"; REGION="${ZONE%-*}"
PROJECT="$(gcloud config get-value project 2>/dev/null)"; SA="r107-waker@$PROJECT.iam.gserviceaccount.com"
echo "== 1. APIs"
run gcloud services enable run.googleapis.com cloudbuild.googleapis.com artifactregistry.googleapis.com --project "$PROJECT"
echo "== 2. service account with get/start on $INSTANCE only"
gcloud iam service-accounts describe "$SA" >/dev/null 2>&1 || run gcloud iam service-accounts create r107-waker --display-name "R107 waker (start the VM on a visit)"
gcloud iam roles describe r107VmWaker --project "$PROJECT" >/dev/null 2>&1 || run gcloud iam roles create r107VmWaker --project "$PROJECT" \
  --title "R107 VM waker" --permissions compute.instances.get,compute.instances.start --stage GA
run gcloud compute instances add-iam-policy-binding "$INSTANCE" --zone "$ZONE" \
  --member "serviceAccount:$SA" --role "projects/$PROJECT/roles/r107VmWaker"
echo "== 3. waker on Cloud Run (public, min-instances 0)"
run gcloud run deploy wake --source "$here/waker" --region "$REGION" --service-account "$SA" --allow-unauthenticated \
  --min-instances 0 --max-instances 2 --memory 256Mi --cpu 1 --concurrency 20 \
  --set-env-vars "PROJECT=$PROJECT,ZONE=$ZONE,INSTANCE=$INSTANCE"
[ "$DRY" = 1 ] || gcloud run services describe wake --region "$REGION" --format 'value(status.url)'
echo "== 4. idle sleep timer on this VM (mode: $SLEEP)"
run gcloud compute instances add-metadata "$INSTANCE" --zone "$ZONE" --metadata "sleep-when-idle=$SLEEP"
run sudo install -m 755 "$here/sleep_when_idle.sh" /usr/local/sbin/sleep_when_idle.sh
unit='[Unit]
Description=R107: shut the VM down when nobody uses it (tools/gcp/sleep_when_idle.sh)
[Service]
Type=oneshot
ExecStart=/usr/local/sbin/sleep_when_idle.sh'
timer='[Unit]
Description=R107 idle check every 5 minutes
[Timer]
OnBootSec=10min
OnUnitActiveSec=5min
[Install]
WantedBy=timers.target'
if [ "$DRY" = 1 ]; then echo "+ write sleep-when-idle.service/.timer, enable timer"; else
  printf '%s\n' "$unit" | sudo tee /etc/systemd/system/sleep-when-idle.service >/dev/null
  printf '%s\n' "$timer" | sudo tee /etc/systemd/system/sleep-when-idle.timer >/dev/null
  sudo systemctl daemon-reload && sudo systemctl enable --now sleep-when-idle.timer
fi
echo "== 5. Caddy access log (read by the idle check)"
run sudo bash "$here/aliases.sh"
echo "done. Waker links: <url>/watchdog, <url>/chat, <url>/ageds"
