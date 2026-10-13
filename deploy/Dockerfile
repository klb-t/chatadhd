# Budowanie z KORZENIA repo:  docker build -f deploy/Dockerfile -t chatadhd .
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build libssl-dev ca-certificates nodejs npm \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY loom/ loom/
RUN cmake --preset release -S loom -B /out/build -G Ninja \
      -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_CLI=OFF -DLOOM_BUILD_TESTS=OFF -DLOOM_WERROR=OFF \
    && cmake --build /out/build --target loom-server -j"$(nproc)"
# web importuje ../../data, wiec zachowujemy uklad loom/web + loom/data
RUN cd loom/web && rm -rf dist node_modules && npm ci --no-audit --no-fund && npm run build

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libssl3 ca-certificates curl tini \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --home-dir /data --shell /usr/sbin/nologin chatadhd \
    && mkdir -p /data/profiles && chown -R chatadhd /data
COPY --from=build /out/build/server/loom-server /usr/local/bin/loom-server
COPY --from=build /src/loom/web/dist /app/web
COPY deploy/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
ENV LOOM_SERVER_HOST=0.0.0.0 LOOM_SERVER_PORT=8787 LOOM_ACCOUNTS_ROOT=/data \
    CHATADHD_DATA=/data LOOM_SERVER_STATIC_DIR=/app/web CHATADHD_REGISTRATION=closed
VOLUME /data
EXPOSE 8787
USER chatadhd
ENTRYPOINT ["/usr/bin/tini","--","/usr/local/bin/docker-entrypoint.sh"]
CMD ["loom-server"]
