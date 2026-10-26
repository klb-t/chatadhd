#!/usr/bin/env bash
# IW-8: installs the VM heartbeat as a systemd --user timer (no root, no gcloud). Reverse:
#   systemctl --user disable --now vm-heartbeat.timer && rm ~/.config/systemd/user/vm-heartbeat.{service,timer}
# Linger (heartbeat without a login session) is the owner's setting: loginctl enable-linger "$USER" (needs sudo).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dst="$HOME/.config/systemd/user"
mkdir -p "$dst"
cp "$here/vm-heartbeat.service" "$here/vm-heartbeat.timer" "$dst/"
systemctl --user daemon-reload
systemctl --user enable --now vm-heartbeat.timer
systemctl --user list-timers vm-heartbeat.timer --no-pager || true
echo "linger: $(loginctl show-user "$USER" -p Linger 2>/dev/null || echo '?')"
