# loom/deploy/Dockerfile — loom-server + the loom/web UI, one deployable
# image. Multi-stage: build the web UI (node), build loom-server (C++), then
# assemble a slim, non-root runtime image with just the two build outputs.
#
# Build from the REPO ROOT (the web and server stages both need more of the
# tree than loom/deploy/ itself):
#   docker build -f loom/deploy/Dockerfile -t loom-server:latest .
# (docker-compose.yml in this directory sets that context/dockerfile pair
# for you.)
#
# Expected inputs from the other agents' work, both under loom/:
#   loom/web    -> `npm run build` -> loom/web/dist        (static assets)
#   loom/server -> CMake target `loom-server`               (the binary)
# Neither exists yet as of this Dockerfile; every stage below degrades to a
# clear build-time error naming the missing piece rather than silently
# producing a broken image (see the `test -d`/`test -f` guards).

# ── Stage 1: web UI ──────────────────────────────────────────────────────
FROM node:22-bookworm-slim AS web-builder
WORKDIR /src/loom/web
COPY loom/web/ ./
RUN --mount=type=cache,target=/root/.npm \
    test -f package.json || { echo "loom/web/package.json not found - nothing to build yet" >&2; exit 1; } \
    && npm ci \
    && npm run build \
    && test -d dist || { echo "npm run build did not produce loom/web/dist" >&2; exit 1; }

# ── Stage 2: loom-server (C++) ───────────────────────────────────────────
FROM ubuntu:24.04 AS server-builder
ENV DEBIAN_FRONTEND=noninteractive
# Deliberately unpinned: these track ubuntu:24.04's own security updates,
# and this stage never ships in the final image.
# hadolint ignore=DL3008
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential cmake ninja-build ccache libssl-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY loom/ ./loom/
RUN test -d loom/server || { echo "loom/server not found yet - loom-server cannot be built" >&2; exit 1; }
RUN cmake --preset release -S loom -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_TESTS=OFF -DLOOM_BUILD_CLI=OFF \
    && cmake --build loom/build/release --target loom-server -j"$(nproc)" \
    && test -f loom/build/release/loom-server \
    || { echo "build finished but loom/build/release/loom-server is missing - check the CMake target name" >&2; exit 1; }

# ── Stage 3: runtime ──────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
# Deliberately unpinned: tracks ubuntu:24.04's own security updates rather
# than a version this Dockerfile would silently go stale against.
# hadolint ignore=DL3008
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       libssl3 ca-certificates curl tini \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 loom \
    && useradd --system --create-home --home-dir /home/loom --shell /usr/sbin/nologin \
       --uid 10001 --gid 10001 loom

WORKDIR /app
COPY --from=server-builder /src/loom/build/release/loom-server /app/loom-server
COPY --from=web-builder /src/loom/web/dist /app/web

# The data directory is a volume (see docker-compose.yml); KOD != DANE (see
# loom/README.md / engine/paths.py) — code lives in the image, data does not.
RUN mkdir -p /data && chown -R loom:loom /data /app

ENV CHATADHD_DATA=/data \
    LOOM_SERVER_PORT=8080 \
    LOOM_WEB_DIR=/app/web
# LOOM_SERVER_TOKEN has no default on purpose: the server should refuse to
# start without one rather than come up unauthenticated. Set it via
# docker-compose.yml's environment (from a .env file) or `docker run -e`.

EXPOSE 8080
VOLUME ["/data"]

USER 10001:10001
# Shell form is required here: it needs $LOOM_SERVER_PORT expansion and the
# `|| exit 1` fallback, neither of which the JSON exec form provides (no
# shell involved).
# hadolint ignore=DL3025
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS "http://127.0.0.1:${LOOM_SERVER_PORT}/health" || exit 1

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/app/loom-server"]
