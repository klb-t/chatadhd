#!/usr/bin/env bash
# ChatADHD / Loom: instalator na VM Debian/Ubuntu. Idempotentny.
# Kod:  <prefix>/<wersja>  + symlink <prefix>/current   (upgrade = nowy katalog, rollback = przelacz symlink)
# Dane: <data-dir> (domyslnie /var/lib/chatadhd) - instalator ich nie kasuje ani nie nadpisuje.
# Trzy sposoby uzycia:
#   install.sh [opcje]                   buduje ze zrodel i instaluje (domyslnie)
#   install.sh --package KATALOG         tylko buduje archiwum .tar.gz (bez root, bez instalacji) - robi to workflow "Package"
#   install.sh --from-tarball PLIK.tar.gz  instaluje gotowe archiwum (bez kompilatora i node na VM)
set -euo pipefail

SRC="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${CHATADHD_PREFIX:-/opt/chatadhd}"
DATA_DIR="${CHATADHD_DATA_DIR:-/var/lib/chatadhd}"
DOMAIN="${CHATADHD_DOMAIN:-}"
PORT="${CHATADHD_PORT:-8787}"
VERSION="${CHATADHD_VERSION:-}"
ADMIN_LOGIN="${CHATADHD_ADMIN_LOGIN:-}"
REGISTRATION="${CHATADHD_REGISTRATION:-}"      # closed|invite|open; puste = nie zmieniaj
SERVICE_USER="${CHATADHD_USER:-chatadhd}"
CMAKE_EXTRA="${CHATADHD_CMAKE_ARGS:-}"
DO_DEPS=1 DO_BUILD=1 DO_SYSTEMD=1 DO_CADDY=1 SWITCH=1
PACKAGE_DIR=""    # --package: tylko zbuduj archiwum do tego katalogu
FROM_TARBALL=""   # --from-tarball: zainstaluj gotowe archiwum zamiast budowac

usage() {
  cat <<USAGE
Uzycie: sudo deploy/install.sh [opcje]
  --domain NAZWA       domena -> Caddy pobierze certyfikat Let's Encrypt (DNS musi wskazywac na VM)
  --port N             port serwera na 127.0.0.1 (domyslnie $PORT)
  --version V          nazwa katalogu wersji (domyslnie: git describe lub data)
  --prefix DIR         gdzie instalowac kod (domyslnie $PREFIX)
  --data-dir DIR       katalog danych (domyslnie $DATA_DIR)
  --admin-login LOGIN  utworz uzytkownika (haslo zapytane interaktywnie lub z stdin)
  --registration TRYB  closed | invite | open (zapis do <data-dir>/profiles)
  --user NAZWA         uzytkownik systemowy uslugi (domyslnie $SERVICE_USER)
  --no-deps  --no-build  --no-systemd  --no-caddy  --no-switch
  --package KATALOG    tylko zbuduj archiwum chatadhd-server-linux-x64-<wersja>.tar.gz (+ .sha256) w KATALOGU; nic nie instaluje
  --from-tarball PLIK  zainstaluj gotowe archiwum (z --package lub z GitHub Actions); bez kompilatora i node.
                       Archiwum z Actions jest budowane na Ubuntu 24.04: potrzebuje glibc >= 2.38 (Ubuntu 24.04+, Debian 13+)
  --rollback WERSJA    tylko przelacz current na istniejaca wersje i zrestartuj
Srodowisko: CHATADHD_{PREFIX,DATA_DIR,DOMAIN,PORT,VERSION,ADMIN_LOGIN,REGISTRATION,USER,CMAKE_ARGS}
USAGE
}

ROLLBACK=""
while [ $# -gt 0 ]; do
  need() { [ $# -ge 2 ] || { echo "brak wartosci dla $1" >&2; exit 2; }; }
  case "$1" in
    --domain) need "$@"; DOMAIN="$2"; shift 2;;
    --port) need "$@"; PORT="$2"; shift 2;;
    --version) need "$@"; VERSION="$2"; shift 2;;
    --prefix) need "$@"; PREFIX="$2"; shift 2;;
    --data-dir) need "$@"; DATA_DIR="$2"; shift 2;;
    --admin-login) need "$@"; ADMIN_LOGIN="$2"; shift 2;;
    --registration) need "$@"; REGISTRATION="$2"; shift 2;;
    --user) need "$@"; SERVICE_USER="$2"; shift 2;;
    --rollback) need "$@"; ROLLBACK="$2"; shift 2;;
    --no-deps) DO_DEPS=0; shift;;
    --no-build) DO_BUILD=0; shift;;
    --no-systemd) DO_SYSTEMD=0; shift;;
    --no-caddy) DO_CADDY=0; shift;;
    --no-switch) SWITCH=0; shift;;
    --package) need "$@"; PACKAGE_DIR="$2"; shift 2;;
    --from-tarball) need "$@"; FROM_TARBALL="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "nieznana opcja: $1" >&2; usage >&2; exit 2;;
  esac
done

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }
case "$REGISTRATION" in ""|closed|invite|open) ;; *) die "--registration: closed|invite|open";; esac
case "$PORT" in ''|*[!0-9]*) die "--port musi byc liczba";; esac
[ -z "$PACKAGE_DIR" ] || [ -z "$FROM_TARBALL" ] || die "--package i --from-tarball wykluczaja sie"
if [ -n "$PACKAGE_DIR" ]; then
  # tylko budowa: bez systemd/Caddy, bez przelaczania, zawsze buduje
  DO_SYSTEMD=0; DO_CADDY=0; SWITCH=0; DO_BUILD=1
  [ -z "$REGISTRATION$ADMIN_LOGIN" ] || die "--package nie instaluje: --registration/--admin-login nie maja sensu"
fi
if [ -n "$FROM_TARBALL" ]; then
  [ -f "$FROM_TARBALL" ] || die "brak pliku $FROM_TARBALL"
  DO_BUILD=0
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if [ "$DO_DEPS" = 1 ] || [ "$DO_SYSTEMD" = 1 ] || [ "$DO_CADDY" = 1 ]; then
    command -v sudo >/dev/null || die "uruchom jako root albo uzyj --no-deps --no-systemd --no-caddy"
    SUDO="sudo"
  fi
fi

# --- rollback ---------------------------------------------------------------
restart_service() {
  if [ "$DO_SYSTEMD" = 1 ] && command -v systemctl >/dev/null; then $SUDO systemctl restart chatadhd.service; fi
}
if [ -n "$ROLLBACK" ]; then
  [ -d "$PREFIX/$ROLLBACK" ] || die "brak $PREFIX/$ROLLBACK"
  $SUDO ln -sfn "$PREFIX/$ROLLBACK" "$PREFIX/current"
  restart_service
  log "current -> $ROLLBACK"
  exit 0
fi

if [ -z "$VERSION" ] && [ -n "$FROM_TARBALL" ]; then
  # wersja z samego archiwum (plik VERSION zapisany przez --package)
  VERSION="$(tar -xzOf "$FROM_TARBALL" --wildcards '*/VERSION' 2>/dev/null | head -1 | tr -d '[:space:]' || true)"
  [ -n "$VERSION" ] || die "archiwum nie ma pliku VERSION; podaj --version"
fi
if [ -z "$VERSION" ]; then
  VERSION="$(git -C "$SRC" describe --tags --always --dirty 2>/dev/null || date +%Y%m%d-%H%M%S)"
fi
case "$VERSION" in */*|""|.|..) die "zla nazwa wersji";; esac
DEST="$PREFIX/$VERSION"

# --- zaleznosci -------------------------------------------------------------
if [ "$DO_DEPS" = 1 ]; then
  command -v apt-get >/dev/null || die "wymagany Debian/Ubuntu (apt-get); albo uzyj --no-deps"
  log "instalacja zaleznosci (apt)"
  export DEBIAN_FRONTEND=noninteractive
  $SUDO apt-get update -y
  if [ -n "$FROM_TARBALL" ]; then
    # gotowe archiwum: tylko to, czego potrzebuje dzialajacy serwer (biblioteki sprawdzamy po rozpakowaniu)
    $SUDO apt-get install -y --no-install-recommends ca-certificates curl
  else
    $SUDO apt-get install -y --no-install-recommends build-essential cmake ninja-build libssl-dev \
      ca-certificates curl git nodejs npm
  fi
  if [ "$DO_CADDY" = 1 ] && ! command -v caddy >/dev/null; then
    $SUDO apt-get install -y --no-install-recommends caddy \
      || die "pakiet caddy niedostepny; zainstaluj Caddy recznie albo uzyj --no-caddy"
  fi
fi
for t in cmake g++ npm; do [ "$DO_BUILD" = 0 ] || command -v "$t" >/dev/null || die "brak: $t"; done

# --- build ------------------------------------------------------------------
# Ustawia BIN_SRC i WEB_SRC (w $TMP). Ten sam kod buduje instalacje i archiwum (--package).
build_all() {
  TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
  log "build serwera (preset release) -> $TMP/build"
  # shellcheck disable=SC2086
  cmake --preset release -S "$SRC/loom" -B "$TMP/build" -G Ninja \
    -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_CLI=OFF -DLOOM_BUILD_TESTS=OFF -DLOOM_WERROR=OFF $CMAKE_EXTRA
  cmake --build "$TMP/build" --target loom-server -j"$(nproc)"
  log "build web"
  # praca na kopii, zeby nie zasmiecac drzewa zrodel
  # (web importuje ../../data, wiec kopiujemy loom/web i loom/data w tym samym ukladzie)
  mkdir -p "$TMP/w/web"; cp -a "$SRC/loom/web/." "$TMP/w/web/"; cp -a "$SRC/loom/data" "$TMP/w/data"
  rm -rf "$TMP/w/web/dist" "$TMP/w/web/node_modules"
  (cd "$TMP/w/web" && npm ci --no-audit --no-fund && npm run build)
  BIN_SRC="$TMP/build/server/loom-server"
  [ -x "$BIN_SRC" ] || BIN_SRC="$(find "$TMP/build" -name loom-server -type f | head -1)"
  [ -n "$BIN_SRC" ] && [ -x "$BIN_SRC" ] || die "nie znaleziono zbudowanego loom-server"
  WEB_SRC="$TMP/w/web/dist"
  [ -f "$WEB_SRC/index.html" ] || die "web nie zbudowal sie ($WEB_SRC/index.html)"
}

# Archiwum: <nazwa>/{bin/loom-server, web/, VERSION, BUILD_INFO}. Rozpakowane w $DEST daje dokladnie to,
# co instalacja ze zrodel (bin/loom-server + web), wiec --from-tarball tylko je rozpakowuje.
make_package() {
  local name="chatadhd-server-linux-x64-$VERSION" out stage
  mkdir -p "$PACKAGE_DIR"; out="$(cd "$PACKAGE_DIR" && pwd)"
  stage="$TMP/stage/$name"
  mkdir -p "$stage/bin"
  install -m 0755 "$BIN_SRC" "$stage/bin/loom-server"
  cp -a "$WEB_SRC" "$stage/web"
  printf '%s\n' "$VERSION" > "$stage/VERSION"
  {
    echo "version: $VERSION"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit: $(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "machine: $(uname -m)"
    echo "glibc: $(ldd --version 2>&1 | head -1)"
    echo "cmake preset: release"
  } > "$stage/BUILD_INFO"
  ( cd "$TMP/stage" && tar --sort=name --owner=0 --group=0 --numeric-owner -czf "$out/$name.tar.gz" "$name" )
  ( cd "$out" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256" )
  log "archiwum: $out/$name.tar.gz"
  cat "$out/$name.tar.gz.sha256" >&2
}

# Rozpakowanie gotowego archiwum do $DEST (sprawdza sume kontrolna, jesli obok jest .sha256).
unpack_tarball() {
  local tb sum listing
  tb="$(cd "$(dirname "$FROM_TARBALL")" && pwd)/$(basename "$FROM_TARBALL")"
  if [ -f "$tb.sha256" ]; then
    log "suma kontrolna: $tb.sha256"
    (cd "$(dirname "$tb")" && sha256sum -c "$(basename "$tb").sha256") >&2 || die "suma kontrolna archiwum sie nie zgadza"
  else
    log "brak $tb.sha256 - pomijam sprawdzenie sumy kontrolnej"
  fi
  # lista do zmiennej: "grep -q" w potoku z tar konczy sie SIGPIPE, a przy pipefail to wygladaloby na blad
  listing="$(tar -tzf "$tb")" || die "nie mozna odczytac archiwum $tb"
  grep -qE '^[^/]+/bin/loom-server$' <<<"$listing" || die "to nie jest archiwum serwera ChatADHD (brak bin/loom-server)"
  grep -qE '^[^/]+/web/index.html$' <<<"$listing" || die "to nie jest archiwum serwera ChatADHD (brak web/index.html)"
  log "rozpakowanie do $DEST"
  $SUDO rm -rf "$DEST/bin" "$DEST/web"
  $SUDO mkdir -p "$DEST"
  $SUDO tar -xzf "$tb" -C "$DEST" --strip-components=1 --no-same-owner
  [ -x "$DEST/bin/loom-server" ] || die "po rozpakowaniu brak $DEST/bin/loom-server"
  # biblioteki dynamiczne (np. libssl3) i wersja glibc: lepiej wiedziec teraz niz po restarcie uslugi
  sum="$(ldd "$DEST/bin/loom-server" 2>&1 | grep -i 'not found' || true)"
  [ -z "$sum" ] || die "serwer nie uruchomi sie na tej maszynie:
$sum
(brakujace biblioteki doinstaluj przez apt, np. libssl3; 'GLIBC_x.y not found' = za stary system: potrzebny Ubuntu 24.04+/Debian 13+ albo budowa ze zrodel bez --from-tarball)"
}

if [ -n "$PACKAGE_DIR" ]; then
  build_all
  make_package
  exit 0
elif [ "$DO_BUILD" = 1 ]; then
  build_all
  log "instalacja do $DEST"
  $SUDO mkdir -p "$DEST/bin"
  $SUDO install -m 0755 "$BIN_SRC" "$DEST/bin/loom-server"
  $SUDO rm -rf "$DEST/web"; $SUDO cp -a "$WEB_SRC" "$DEST/web"
elif [ -n "$FROM_TARBALL" ]; then
  unpack_tarball
else
  [ -x "$DEST/bin/loom-server" ] || die "--no-build, a $DEST/bin/loom-server nie istnieje"
fi

# --- uzytkownik i dane ------------------------------------------------------
if [ "$(id -u)" -eq 0 ] && [ "$DO_SYSTEMD" = 1 ]; then
  id "$SERVICE_USER" >/dev/null 2>&1 || useradd --system --home-dir "$DATA_DIR" --shell /usr/sbin/nologin "$SERVICE_USER"
fi
$SUDO mkdir -p "$DATA_DIR/profiles"
OWNER=""; id "$SERVICE_USER" >/dev/null 2>&1 && [ "$DO_SYSTEMD" = 1 ] && OWNER="$SERVICE_USER"
[ -z "$OWNER" ] || $SUDO chown "$OWNER:$OWNER" "$DATA_DIR" "$DATA_DIR/profiles"
$SUDO chmod 750 "$DATA_DIR" 2>/dev/null || true

# Rejestracja: nakladka profilu. Tylko gdy podano --registration, albo przy pierwszej instalacji.
PROFILE="$DATA_DIR/profiles/server_accounts.pack"
if [ -n "$REGISTRATION" ] || [ ! -e "$PROFILE" ]; then
  MODE="${REGISTRATION:-closed}"
  # trust_forwarded_for: serwer stoi za naszym Caddy, wiec limit prob logowania ma liczyc adres klienta.
  printf '{"schema":"loom.runtime_profile_overlay/1","domain":"server_accounts","overrides":{"registration":"%s","throttle":{"trust_forwarded_for":true}}}\n' "$MODE" \
    | $SUDO tee "$PROFILE" >/dev/null
  [ -z "$OWNER" ] || $SUDO chown "$OWNER:$OWNER" "$PROFILE"
  log "rejestracja: $MODE"
fi

# --- przelaczenie current ---------------------------------------------------
if [ "$SWITCH" = 1 ]; then
  PREV="$(readlink "$PREFIX/current" 2>/dev/null || true)"
  $SUDO ln -sfn "$DEST" "$PREFIX/current"
  [ -z "$PREV" ] || log "poprzednia wersja: $PREV (rollback: install.sh --rollback $(basename "$PREV"))"
fi

# Pomocnik do zarzadzania uzytkownikami (haslo zawsze interaktywnie albo ze stdin, nigdy w argv).
HELPER="$PREFIX/chatadhd-user"
$SUDO tee "$HELPER" >/dev/null <<HELP
#!/usr/bin/env bash
# uzycie: chatadhd-user add|passwd|disable|enable LOGIN | invite | list
set -e
if [ "\$(id -u)" -eq 0 ] && id "$SERVICE_USER" >/dev/null 2>&1 && command -v runuser >/dev/null; then
  exec runuser -u "$SERVICE_USER" -- "$PREFIX/current/bin/loom-server" user "\$@" --accounts-root "$DATA_DIR"
fi
exec "$PREFIX/current/bin/loom-server" user "\$@" --accounts-root "$DATA_DIR"
HELP
$SUDO chmod 0755 "$HELPER"

# --- systemd ----------------------------------------------------------------
if [ "$DO_SYSTEMD" = 1 ]; then
  log "systemd: chatadhd.service"
  $SUDO tee /etc/systemd/system/chatadhd.service >/dev/null <<UNIT
[Unit]
Description=ChatADHD (Loom server)
After=network-online.target
Wants=network-online.target

[Service]
User=$SERVICE_USER
Group=$SERVICE_USER
ExecStart=$PREFIX/current/bin/loom-server --host 127.0.0.1 --port $PORT --accounts-root $DATA_DIR --data-dir $DATA_DIR --static-dir $PREFIX/current/web
Restart=on-failure
RestartSec=3
NoNewPrivileges=true
ProtectSystem=full
PrivateTmp=true
ReadWritePaths=$DATA_DIR

[Install]
WantedBy=multi-user.target
UNIT
  $SUDO systemctl daemon-reload
  $SUDO systemctl enable chatadhd.service >/dev/null
  $SUDO systemctl restart chatadhd.service
fi

# --- Caddy ------------------------------------------------------------------
if [ "$DO_CADDY" = 1 ]; then
  CF=/etc/caddy/Caddyfile
  if [ -n "$DOMAIN" ]; then SITE="$DOMAIN"; TLS=""
  else SITE=":443"; TLS="  tls internal"; log "brak --domain: HTTPS z certyfikatem wewnetrznym Caddy (przegladarka ostrzeze); do prawdziwego certyfikatu podaj --domain"
  fi
  $SUDO mkdir -p /etc/caddy
  $SUDO tee "$CF" >/dev/null <<CADDY
# zarzadzane przez chatadhd install.sh
$SITE {
$TLS
  reverse_proxy 127.0.0.1:$PORT {
    header_up X-Forwarded-Proto {scheme}
    header_up X-Forwarded-For {remote_host}
  }
}
CADDY
  if command -v systemctl >/dev/null; then $SUDO systemctl enable caddy >/dev/null 2>&1 || true; $SUDO systemctl reload caddy 2>/dev/null || $SUDO systemctl restart caddy || true; fi
fi

# --- pierwszy uzytkownik ----------------------------------------------------
if [ -n "$ADMIN_LOGIN" ]; then
  log "tworzenie uzytkownika '$ADMIN_LOGIN' (haslo zostanie zapytane)"
  if [ "$(id -u)" -eq 0 ] && [ "$DO_SYSTEMD" = 1 ]; then
    "$HELPER" add "$ADMIN_LOGIN" || log "uzytkownik juz istnieje lub blad - patrz wyzej"
  else
    "$PREFIX/current/bin/loom-server" user add "$ADMIN_LOGIN" --accounts-root "$DATA_DIR" || true
  fi
fi

log "gotowe: $DEST (current -> $(readlink "$PREFIX/current" 2>/dev/null || echo '?'))"
