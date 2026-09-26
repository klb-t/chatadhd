# Loom deployment

Everything needed to run `loom-server` (+ the `loom/web` UI it serves) in a
container, and to get that container running unattended on a fresh Ubuntu
box. Nothing here is Android-specific — see `loom/android/README.md` for the
mobile shell.

## Status: waiting on two other pieces

This directory's Dockerfile builds:
- `loom/web` &rarr; `npm run build` &rarr; `loom/web/dist`
- `loom/server` &rarr; a CMake target named `loom-server`

**Neither exists in this repo yet** — both are another agent's work in
progress. Every stage of the Dockerfile checks for its input and fails with
a clear message (not a confusing generic error) if it's missing, so once
either lands, `docker build` (or `build-linux.sh`) just starts working; no
Dockerfile change should be needed unless the actual CMake target name or
`npm run build`'s output directory differs from the above. **Docker itself
also wasn't available to build with in this sandbox** (the CLI is installed
but there's no daemon reachable — `docker info` fails to connect to
`/var/run/docker.sock`), so nothing here has been build-tested end to end.
What *was* done instead, since the task allows skipping what genuinely can't
be reached: `bash -n` and `shellcheck -S style` (zero warnings) on every
script, and `hadolint` (downloaded from its GitHub release, zero warnings
after two rounds of fixes — see the Dockerfile's `hadolint ignore=` comments
for the three deliberate exceptions and why) on the Dockerfile.

## Files

| File | What it is |
|---|---|
| `Dockerfile` | Multi-stage: `web-builder` (node, `loom/web` &rarr; `dist`), `server-builder` (ubuntu:24.04, CMake `release` preset + `LOOM_BUILD_SERVER=ON`), `runtime` (slim, non-root uid 10001, `tini` PID 1, `HEALTHCHECK` against `/health`). |
| `docker-compose.yml` | One service, a named volume for `/data`, binds `127.0.0.1:$LOOM_SERVER_PORT` only (never publishes on every interface). |
| `.env.example` | Copy to `.env`; sets `LOOM_SERVER_TOKEN` and `LOOM_SERVER_PORT`. `.env` itself is gitignored (repo root `.gitignore`'s generic `.env` rule already covers it). |
| `build-linux.sh` | `cmake --preset release` + `ctest` for the Loom C++ kernel, no Docker — the same build the Dockerfile's `server-builder` stage does, for a quick local/CI check. |
| `build-android.sh` | Wraps `loom/android/gradlew`; checks `ANDROID_HOME`/`JAVA_HOME` first and explains what's missing instead of a raw Gradle stack trace. |
| `ec2-setup.sh` | Idempotent Ubuntu host setup: Docker + Compose plugin, a systemd unit that runs `docker compose up -d --build` at boot, and (only with `--domain`) Caddy as an automatic-TLS reverse proxy. |

## Quick start (once loom/web and loom/server exist)

```bash
cd loom/deploy
cp .env.example .env
# edit .env: set LOOM_SERVER_TOKEN (openssl rand -hex 32), or let
# ec2-setup.sh generate one for you (see below)
docker compose up -d --build
curl -fsS http://127.0.0.1:8080/health
```

Or on a fresh Ubuntu EC2 instance, from a checkout of this repo:

```bash
sudo loom/deploy/ec2-setup.sh                             # local-only, no TLS
sudo loom/deploy/ec2-setup.sh --domain chat.example.com    # + Caddy/automatic TLS
```

`ec2-setup.sh` is safe to re-run (every step checks before acting): installs
`docker.io` + `docker-compose-v2` via apt, enables the `docker` service,
adds the invoking (`sudo`) user to the `docker` group, generates
`loom/deploy/.env` with a fresh `LOOM_SERVER_TOKEN` **only if one doesn't
already exist**, writes and enables a `loom-server.service` systemd unit
(`WorkingDirectory` = this directory, `ExecStart=docker compose up -d
--build`), and — only when you pass `--domain` — installs Caddy from its
official apt repo and writes a `Caddyfile` that reverse-proxies
`https://<domain>` (automatic Let's Encrypt cert on first request) to
`127.0.0.1:$LOOM_SERVER_PORT`. Caddy only terminates TLS and forwards; the
`LOOM_SERVER_TOKEN` bearer-auth check happens in `loom-server` itself, on
every request, proxied or not — the script does not duplicate that check at
the edge.

## Design notes

- **Why bind `127.0.0.1` instead of `0.0.0.0`**: the container is meant to
  sit behind a reverse proxy (Caddy, or your own) that owns the public
  interface and TLS. Binding the published port to loopback means a
  misconfigured security group / firewall can't expose the bare HTTP API
  directly.
- **Why a named volume, not a bind mount, for `/data`**: keeps the compose
  file host-filesystem-agnostic; switch `loom_data:/data` to a host path if
  you want the data directory (`chatadhd.db`, `config.json`, `secrets.json`,
  ... — the same files `engine/paths.py`/`loom/include/loom/config.h`
  resolve on desktop/Android) at a known location for backup tooling.
- **Why `tini` as PID 1**: `loom-server` is a long-running process that will
  get `docker stop`'s SIGTERM directly and reap no children of its own by
  default under most init-less setups; `tini` forwards signals correctly and
  reaps zombies, which matters once the server shells out to anything (git,
  for GitHub sync; media provider CLIs, if configured).
- **Why the runtime image still installs `curl`**: only for `HEALTHCHECK`.
  If `loom-server` ends up with its own lightweight health-check mode this
  can be dropped in favour of `CMD ["/app/loom-server", "--healthcheck"]`
  (JSON form, no shell needed) — worth revisiting once that binary exists.
- **`LOOM_WEB_DIR=/app/web`**: set in case `loom-server` serves the built web
  UI itself (a single deployable artifact, matching "web build via node ->
  slim runtime" in this task's brief). If it doesn't — if the web UI is
  meant to be served by a separate static host or by Caddy directly — this
  env var is simply unused and harmless; `Caddyfile`/compose can be pointed
  at `/app/web` instead once that's decided.
- **`/health` is assumed**, not confirmed: `loom-server`'s actual health
  endpoint (or whether it has one yet) isn't known from this worktree. If it
  turns out to be a different path, update the `HEALTHCHECK` line in the
  Dockerfile and the `healthcheck.test` line in `docker-compose.yml` (both
  reference the same `/health` path so there's exactly one place to change
  per file).
