#!/usr/bin/env bash
# loom/deploy/ec2-setup.sh — idempotent Ubuntu host setup for loom-server.
#
# Installs Docker + the Compose plugin, writes a systemd unit that brings the
# stack (loom/deploy/docker-compose.yml) up on boot, and — only if you pass
# a domain — installs Caddy as a reverse proxy that gets you automatic TLS
# for free. Safe to re-run: every step checks whether it already happened
# before doing anything (package installs are naturally idempotent via apt;
# file writes compare content before overwriting; service enables are
# no-ops when already enabled).
#
# Usage (run as root, or with sudo, on the target Ubuntu host, from inside a
# checkout of this repo — it operates on the loom/deploy directory it lives
# in):
#   sudo loom/deploy/ec2-setup.sh                          # docker + systemd unit only
#   sudo loom/deploy/ec2-setup.sh --domain chat.example.com  # + Caddy/TLS for that domain
#
# Env vars (only used to fill in loom/deploy/.env if it doesn't exist yet):
#   LOOM_SERVER_TOKEN   Bearer token for loom-server. Generated with
#                        `openssl rand -hex 32` if not set and .env is new.
#   LOOM_SERVER_PORT    Default 8080.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
deploy_dir="${script_dir}"

domain=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --domain)
      domain="${2:?--domain needs a value}"
      shift 2
      ;;
    --domain=*)
      domain="${1#--domain=}"
      shift
      ;;
    -h|--help)
      sed -n '2,25p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "unknown argument: $1 (see --help)" >&2
      exit 1
      ;;
  esac
done

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root (sudo loom/deploy/ec2-setup.sh ...)" >&2
  exit 1
fi

log() { echo "==> $*" >&2; }

# ── 1. Docker + Compose plugin ───────────────────────────────────────────
if ! command -v docker >/dev/null 2>&1; then
  log "installing docker.io + compose plugin"
  apt-get update -qq
  apt-get install -y --no-install-recommends docker.io docker-compose-v2 uidmap
else
  log "docker already installed ($(docker --version))"
fi

systemctl enable --now docker >/dev/null 2>&1 || systemctl start docker

target_user="${SUDO_USER:-}"
if [[ -n "${target_user}" ]] && ! id -nG "${target_user}" | tr ' ' '\n' | grep -qx docker; then
  log "adding ${target_user} to the docker group (log out/in to take effect)"
  usermod -aG docker "${target_user}"
fi

# ── 2. .env (only created, never overwritten) ────────────────────────────
env_file="${deploy_dir}/.env"
if [[ ! -f "${env_file}" ]]; then
  log "writing ${env_file}"
  token="${LOOM_SERVER_TOKEN:-$(openssl rand -hex 32)}"
  port="${LOOM_SERVER_PORT:-8080}"
  {
    echo "LOOM_SERVER_TOKEN=${token}"
    echo "LOOM_SERVER_PORT=${port}"
  } > "${env_file}"
  chmod 600 "${env_file}"
  log "generated a new LOOM_SERVER_TOKEN in ${env_file} (back it up - clients need it)"
else
  log "${env_file} already exists, leaving it alone"
fi

# ── 3. systemd unit ───────────────────────────────────────────────────────
unit_path="/etc/systemd/system/loom-server.service"
unit_content="[Unit]
Description=Loom server (docker compose)
Requires=docker.service
After=docker.service network-online.target
Wants=network-online.target

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=${deploy_dir}
ExecStart=/usr/bin/docker compose up -d --build
ExecStop=/usr/bin/docker compose down
TimeoutStartSec=0

[Install]
WantedBy=multi-user.target
"
if [[ ! -f "${unit_path}" ]] || [[ "$(cat "${unit_path}")" != "${unit_content}" ]]; then
  log "writing ${unit_path}"
  printf '%s' "${unit_content}" > "${unit_path}"
  systemctl daemon-reload
else
  log "${unit_path} already up to date"
fi
systemctl enable loom-server.service >/dev/null 2>&1
log "starting loom-server.service (docker compose up -d --build)"
systemctl restart loom-server.service

# ── 4. Caddy (optional: only with --domain) ──────────────────────────────
if [[ -n "${domain}" ]]; then
  if ! command -v caddy >/dev/null 2>&1; then
    log "installing caddy (official apt repo)"
    apt-get install -y --no-install-recommends debian-keyring debian-archive-keyring apt-transport-https curl gnupg
    if [[ ! -f /usr/share/keyrings/caddy-stable-archive-keyring.gpg ]]; then
      curl -1sSf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' \
        | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
    fi
    if [[ ! -f /etc/apt/sources.list.d/caddy-stable.list ]]; then
      echo 'deb [signed-by=/usr/share/keyrings/caddy-stable-archive-keyring.gpg] https://dl.cloudsmith.io/public/caddy/stable/deb/debian any-version main' \
        > /etc/apt/sources.list.d/caddy-stable.list
    fi
    apt-get update -qq
    apt-get install -y --no-install-recommends caddy
  else
    log "caddy already installed ($(caddy version))"
  fi

  port="${LOOM_SERVER_PORT:-8080}"
  # shellcheck disable=SC2154 # $port is set two lines above, not inherited
  caddyfile_content="${domain} {
	reverse_proxy 127.0.0.1:${port}
}
"
  caddyfile_path="/etc/caddy/Caddyfile"
  if [[ ! -f "${caddyfile_path}" ]] || [[ "$(cat "${caddyfile_path}")" != "${caddyfile_content}" ]]; then
    log "writing ${caddyfile_path} for ${domain}"
    printf '%s' "${caddyfile_content}" > "${caddyfile_path}"
  else
    log "${caddyfile_path} already up to date"
  fi
  systemctl enable --now caddy >/dev/null 2>&1
  systemctl reload caddy || systemctl restart caddy
  log "Caddy is reverse-proxying https://${domain} -> 127.0.0.1:${port} (TLS cert is fetched" \
      "automatically on first request - make sure DNS for ${domain} already points at this host)"
  log "loom-server itself still authenticates every request with LOOM_SERVER_TOKEN from" \
      "${env_file}; Caddy only terminates TLS and forwards, it does not check the token"
else
  log "no --domain given: skipping Caddy. The server is reachable at 127.0.0.1:\${LOOM_SERVER_PORT}" \
      "on this host only (see docker-compose.yml) - put your own TLS terminator in front of it," \
      "or re-run with --domain your.domain to let this script set up Caddy."
fi

log "done. Check status with: systemctl status loom-server; docker compose -f ${deploy_dir}/docker-compose.yml ps"
