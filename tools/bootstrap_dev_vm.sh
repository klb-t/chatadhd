#!/usr/bin/env bash
# =============================================================================
# bootstrap_dev_vm.sh - przygotowanie maszyny deweloperskiej pod ChatADHD / Loom (sprawdzony pod Ubuntu 24.04 = runner CI;
#   inne wydania: jedzie dalej, czego nie da się zainstalować, zgłasza jako uwagę - nic nie zatrzymuje się na nazwie systemu)
# =============================================================================
#
# !!! UWAGA: TEN SKRYPT NIE BYŁ URUCHOMIONY ANI PRZETESTOWANY (stan: 2026-10-23, pakiet OPS1). !!!
#   Powstał bez sudo, bez apt i bez dostępu do GCP. Sprawdzono tylko składnię (`bash -n`) i tryb
#   `--dry-run` (wypisuje polecenia, niczego nie wykonuje). Pierwsze prawdziwe uruchomienie na świeżej VM
#   to pierwszy test: zacznij od `--dry-run`, przeczytaj wypisane polecenia, dopiero potem uruchom bez niego.
#   Skrypt jest idempotentny z założenia (każdy krok sprawdza, czy już jest zrobiony) - to też niesprawdzone.
#
# Po co: dziś kod powstaje w kontenerze sesji, który restartuje się, gdy dysk dobija do ~95 % (dwa
# restarty 2026-10-23, patrz CLAUDE.md, "Correction after a second restart"). Ten skrypt stawia na
# zwykłej VM (np. w GCP) to samo środowisko, które instaluje CI (.github/workflows/loom.yml), tak żeby
# `tools/gate.sh` mógł tam przejść. Opis całej przeprowadzki: docs/ops/PRZEPROWADZKA_GCP_2026-10-23.md.
#
# JEDNA KOMENDA DO WKLEJENIA PRZEZ SSH na świeżej VM (R71) to osobny skrypt tools/vm_oneliner.sh: klonuje repozytorium,
# uruchamia TEN skrypt (przekazuje mu swoje flagi), instaluje Claude Code i wypisuje kroki dla człowieka.
# Ten skrypt zakłada, że repozytorium już jest (albo podasz --repo DIR).
#
# Uruchamianie z klona (jako ZWYKŁY użytkownik z sudo, nie jako root):
#   tools/bootstrap_dev_vm.sh --dry-run                    # tylko pokaż, co zostałoby zrobione
#   tools/bootstrap_dev_vm.sh                              # rdzeń + web + Python 3.11 + ccache
#   tools/bootstrap_dev_vm.sh --android --emulator         # dodatkowo Java 17 + Android SDK/NDK i test KVM
#
# Opcje (wszystko da się wyłączyć albo nadpisać; nic nie jest ukryte):
#   --dry-run     tylko wypisz polecenia (nic nie instaluje, nie pobiera, nie zapisuje)
#   --android     Java 17 + Android cmdline-tools + platforma/build-tools/NDK/CMake wg loom/android/app/build.gradle.kts
#   --emulator    sprawdź, czy jest /dev/kvm i czy procesor ma wirtualizację (emulator Androida jej wymaga); tylko odczyt
#   --libcxx      dodaj libc++-dev i libc++abi-dev (job `libcxx` z CI)
#   --caddy       dodaj pakiet apt `caddy` (serwer HTTPS dla deploy/install.sh; sama instalacja serwera to osobny krok)
#   --no-web      pomiń `npm ci` i instalację Chromium dla Playwright
#   --bashrc      dopisz do ~/.bashrc jedną linię `source ~/.config/chatadhd-dev-env.sh` (domyślnie tylko wypisuje)
#   --strict-os   ZATRZYMAJ się, gdy to nie jest Ubuntu 24.04 (domyślnie skrypt tylko mówi, że sprawdzono go pod 24.04,
#                 i jedzie dalej; pakiet, którego nie ma w danym wydaniu, jest pomijany z uwagą, nie zatrzymuje reszty)
#   --any-os      (dawna opcja, dziś to zachowanie domyślne; przyjmowana dla zgodności)
#   --no-home-link gdy HOME to nie /home/user, skrypt DOMYŚLNIE tworzy dowiązanie /home/user -> $HOME (tools/gate.sh i briefy
#                 mają /home/user na sztywno; OPS2 to odpina); ta opcja to wyłącza. --home-link = jawnie włącz (domyślne)
#   --no-py311    nie próbuj instalować Pythona 3.11 (CI używa 3.11); domyślnie: próba z PPA deadsnakes, a gdy się nie uda
#                 (np. wydanie Ubuntu, dla którego PPA nie ma paczek) - uwaga i systemowy python3
#   --repo DIR    katalog repozytorium (domyślnie: katalog nad tym skryptem, a gdy go tam nie widać: ~/chatadhd)
# Zmienne środowiska (presety): CHATADHD_CCACHE_MAXSIZE (12G), CHATADHD_NODE_MAJOR (22), CHATADHD_VENV (~/.venvs/loom311),
#   CHATADHD_ANDROID_HOME (~/android-sdk), CHATADHD_ANDROID_CMDLINE_URL, CHATADHD_ANDROID_CMDLINE_SHA256, CHATADHD_REPO.
#
# Skąd wersje (jedno źródło prawdy: CI, nie kopia):
#   Python 3.11 - loom.yml `setup-python` 3.11 (tablice Unicode są generowane z CPython 3.11, testy zgodności porównują z tym samym)
#   Node 22     - loom.yml / package.yml `setup-node` 22
#   Java 17     - loom.yml (JNI) i package.yml `setup-java` 17
#   Playwright  - loom/web/package.json (devDependency, przypięta wersja)
#   apt         - loom.yml "Install toolchain": ninja-build ccache libsqlite3-dev libssl-dev clang (+ libc++ dla `libcxx`);
#                 kompilator, cmake i git ma w obrazie runnera GitHub, na zwykłej VM dodajemy je sami.
#
# Czego skrypt NIE robi: nie buduje (komenda jest wypisana na końcu), nie klonuje repozytorium, nie instaluje Claude Code,
# nie loguje do GitHub ani do Claude, nie uruchamia Remote Control (to robi vm_oneliner.sh albo Ty), nie otwiera portów,
# nie rusza danych (/var/lib/chatadhd).
# =============================================================================
set -euo pipefail

# Gdy nie da się ustalić położenia skryptu (np. uruchomiony z potoku), repozytorium to ~/chatadhd.
SELF="${BASH_SOURCE[0]:-}"
REPO_GUESS="$HOME/chatadhd"
if [ -n "$SELF" ] && [ -f "$SELF" ]; then
  _up="$(cd -- "$(dirname -- "$SELF")/.." && pwd)"
  [ -d "$_up/loom" ] && REPO_GUESS="$_up"
fi
REPO="${CHATADHD_REPO:-$REPO_GUESS}"
CCACHE_SIZE="${CHATADHD_CCACHE_MAXSIZE:-12G}"      # preset: tyle ma tools/gate.sh (CI ma 3G)
NODE_MAJOR="${CHATADHD_NODE_MAJOR:-22}"            # jak w CI
PY_MM="3.11"                                       # jak w CI
VENV="${CHATADHD_VENV:-$HOME/.venvs/loom311}"
ANDROID_HOME_DIR="${CHATADHD_ANDROID_HOME:-$HOME/android-sdk}"
# Adres z pamięci, NIEZWERYFIKOWANY: numer w nazwie pliku zmienia się z wydaniami. Przy błędzie 404 podaj aktualny
# adres (strona "Command line tools" w dokumentacji Android Studio) w CHATADHD_ANDROID_CMDLINE_URL.
ANDROID_CMDLINE_URL="${CHATADHD_ANDROID_CMDLINE_URL:-https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip}"
ANDROID_CMDLINE_SHA256="${CHATADHD_ANDROID_CMDLINE_SHA256:-}"   # puste = bez sprawdzania sumy (ostrzeżenie)
ENV_FILE="$HOME/.config/chatadhd-dev-env.sh"

DRY=0 DO_ANDROID=0 DO_EMULATOR=0 DO_LIBCXX=0 DO_CADDY=0 DO_WEB=1 DO_BASHRC=0 STRICT_OS=0 DO_HOMELINK=1 DO_PY311=1
ADVISORIES=()   # uwagi niewiążące zebrane po drodze, powtórzone w podsumowaniu (żeby nic nie zginęło w przewijaniu)

log()  { printf '\n== %s\n' "$*"; }
warn() { printf 'UWAGA: %s\n' "$*" >&2; ADVISORIES+=("$*"); }
die()  { printf 'BŁĄD: %s\n' "$*" >&2; exit 1; }
# run: wypisz polecenie i (poza --dry-run) wykonaj je
run()  { printf '+ %s\n' "$*"; [ "$DRY" = 1 ] || "$@"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run)  DRY=1; shift;;
    --android)  DO_ANDROID=1; shift;;
    --emulator) DO_EMULATOR=1; shift;;
    --libcxx)   DO_LIBCXX=1; shift;;
    --caddy)    DO_CADDY=1; shift;;
    --no-web)   DO_WEB=0; shift;;
    --bashrc)   DO_BASHRC=1; shift;;
    --strict-os) STRICT_OS=1; shift;;
    --any-os)   shift;;   # dawna opcja: dziś domyślne
    --home-link) DO_HOMELINK=1; shift;;
    --no-home-link) DO_HOMELINK=0; shift;;
    --no-py311) DO_PY311=0; shift;;
    --repo)     [ $# -ge 2 ] || die "brak wartości dla --repo"; REPO="$2"; shift 2;;
    -h|--help)  if [ -n "$SELF" ] && [ -f "$SELF" ]; then sed -n '2,/^set -euo pipefail/p' "$SELF" | sed '$d'; else echo "pomoc jest w nagłówku pliku: pobierz go (curl -o) i uruchom z --help"; fi; exit 0;;
    *) die "nieznana opcja: $1 (spróbuj --help)";;
  esac
done

# --- 0. kontrola środowiska ---------------------------------------------------
log "0. kontrola środowiska"
[ "$DRY" = 1 ] && echo "(tryb --dry-run: nic nie zostanie zainstalowane ani zapisane)"
if [ "$(id -u)" -eq 0 ] && [ "$DRY" = 0 ]; then
  die "uruchom jako zwykły użytkownik z sudo (Chromium i venv mają należeć do niego, nie do roota)"
fi
SUDO="sudo"   # (jako root bez --dry-run skrypt zatrzymał się wyżej)
if [ -r /etc/os-release ]; then
  # shellcheck disable=SC1091
  . /etc/os-release
  echo "system: ${PRETTY_NAME:-nieznany}"
  if [ "${ID:-}" != "ubuntu" ] || [ "${VERSION_ID:-}" != "24.04" ]; then
    # Obserwacja z datą, nie ograniczenie (CLAUDE.md: zasada właściciela, R58-R60): skrypt SPRAWDZONO pod Ubuntu 24.04
    # (runner CI, 2026-10-23); na innym wydaniu jedzie dalej, a to, czego nie da się zainstalować, zgłasza jako uwagę.
    warn "sprawdzono pod Ubuntu 24.04 (runner CI); tu jest ${PRETTY_NAME:-inny system} - jadę dalej, brakujące pakiety zgłoszę w podsumowaniu (zatrzymanie: --strict-os)"
    [ "$STRICT_OS" = 1 ] && die "--strict-os: to nie jest Ubuntu 24.04"
  fi
fi
command -v apt-get >/dev/null || die "brak apt-get"
export DEBIAN_FRONTEND=noninteractive

# --- 1. pakiety apt (każda pozycja z powodem) ---------------------------------
log "1. pakiety apt"
APT_PKGS=(
  build-essential        # gcc/g++/make: kompilator C++20 dla jądra (preset dev, GCC)
  cmake                  # CMakePresets.json wymaga 3.24+; Ubuntu 24.04 ma nowszy (z pamięci 3.28)
  ninja-build            # generator Ninja z CMakePresets.json (loom.yml: ninja-build)
  ccache                 # pamięć podręczna kompilacji: ponowny build ~15 min zamiast godziny (loom.yml: ccache)
  libsqlite3-dev         # SQLite systemowy, LOOM_USE_SYSTEM_SQLITE=ON w presetach dev/asan (loom.yml)
  libssl-dev             # OpenSSL dla jądra i serwera (loom.yml)
  clang                  # tools/clang_inc_check.py i joby vendored/libcxx (loom.yml: clang)
  git                    # klon repozytorium i worktree
  curl                   # pobieranie Node i narzędzi Androida w tym skrypcie
  ca-certificates        # HTTPS dla curl i git
  unzip                  # rozpakowanie Android cmdline-tools
  xz-utils               # rozpakowanie archiwum Node (.tar.xz)
  pkg-config             # standardowe zależności buildów natywnych
  tmux                   # sesja agenta ma żyć po zamknięciu SSH
  software-properties-common   # add-apt-repository (potrzebne tylko do Pythona 3.11, krok 2)
)
if [ "$DO_LIBCXX" = 1 ]; then
  APT_PKGS+=(
    libc++-dev           # nagłówki libc++ dla joba `libcxx` (clang++ + libc++)
    libc++abi-dev        # środowisko uruchomieniowe libc++abi dla tego samego joba
  )
fi
if [ "$DO_CADDY" = 1 ]; then
  APT_PKGS+=(
    caddy                # HTTPS + reverse proxy dla deploy/install.sh (tak samo jak instaluje go sam install.sh)
  )
fi
run $SUDO apt-get update -y
# Najpierw cała lista naraz (szybko); gdy w tym wydaniu brakuje którejś paczki, apt odrzuca CAŁĄ listę,
# więc wtedy pojedynczo: to, co jest, się instaluje, brak idzie do uwag (nie zatrzymuje reszty skryptu).
if ! run $SUDO apt-get install -y --no-install-recommends "${APT_PKGS[@]}"; then
  warn "instalacja całej listy apt nie powiodła się - próbuję paczka po paczce"
  for pkg in "${APT_PKGS[@]}"; do
    run $SUDO apt-get install -y --no-install-recommends "$pkg" || warn "pakietu '$pkg' nie udało się zainstalować w tym wydaniu (zobacz powód wyżej)"
  done
fi

if [ ! -d "$REPO/loom" ]; then
  warn "nie widzę $REPO/loom - kroki z requirements i 'web' zostaną pominięte (sklonuj repozytorium albo użyj --repo DIR)"
fi

# --- 2. Python 3.11 + venv ------------------------------------------------------
# Ubuntu 24.04 ma domyślnie Pythona 3.12, a CI używa 3.11 (actions/setup-python). 3.11 bierzemy z PPA deadsnakes
# (zewnętrzne repozytorium - to Twoja decyzja zaufania; inne drogi: pyenv albo uv, ręcznie).
log "2. Python $PY_MM"
PY_BIN="python$PY_MM"
if command -v "python$PY_MM" >/dev/null 2>&1; then
  echo "python$PY_MM już jest: $(command -v "python$PY_MM")"
elif [ "$DO_PY311" = 0 ]; then
  PY_BIN=python3
  warn "--no-py311: używam systemowego $(python3 --version 2>&1); CI używa $PY_MM (testy zgodności mogą się różnić)"
else
  warn "dodaję zewnętrzne repozytorium ppa:deadsnakes/ppa (python$PY_MM nie ma w archiwum Ubuntu 24.04)"
  if run $SUDO add-apt-repository -y ppa:deadsnakes/ppa && run $SUDO apt-get update -y \
     && run $SUDO apt-get install -y --no-install-recommends "python$PY_MM" "python$PY_MM-venv" "python$PY_MM-dev"; then
    :
  else
    PY_BIN=python3
    warn "python$PY_MM z PPA deadsnakes nie zainstalował się (PPA może nie mieć paczek dla tego wydania) - używam systemowego $(python3 --version 2>&1); CI używa $PY_MM, testy zgodności (loom/tests/compat) mogą się różnić. Inne drogi: pyenv, uv."
    $SUDO apt-get update -y >/dev/null 2>&1 || true   # po nieudanym PPA lista źródeł może zgłaszać błąd; nie zatrzymuje nas
  fi
fi
if [ -x "$VENV/bin/python" ]; then
  echo "venv już jest: $VENV ($("$VENV/bin/python" --version 2>&1))"
else
  run mkdir -p "$(dirname "$VENV")"
  run "$PY_BIN" -m venv "$VENV"
  [ "$DRY" = 1 ] || [ -x "$VENV/bin/python" ] || die "venv nie powstał ($PY_BIN -m venv $VENV); czy jest pakiet python3-venv? ($SUDO apt-get install -y python3-venv)"
fi
PIP_ARGS=(requests cryptography)
for req in loom/tools/contracts/requirements.txt loom/tools/resource_graph/requirements.txt; do
  [ -f "$REPO/$req" ] && PIP_ARGS+=(-r "$REPO/$req") || warn "brak $REPO/$req (jest w CI w kroku 'Install Python engine dependencies')"
done
run "$VENV/bin/python" -m pip install --upgrade pip
run "$VENV/bin/python" -m pip install "${PIP_ARGS[@]}"

# --- 3. Node ${NODE_MAJOR} z oficjalnego archiwum nodejs.org (suma SHA-256 sprawdzana) ------------------
# Dlaczego nie apt: Ubuntu 24.04 ma starszego Node'a niż CI (z pamięci 18). SHASUMS256.txt jest pobierany z tego samego
# serwera co archiwum (sprawdza spójność pobrania, nie podpis wydawcy; podpis GPG pomijamy).
log "3. Node $NODE_MAJOR"
node_ok=0
if command -v node >/dev/null 2>&1 && [ "$(node -p 'process.versions.node.split(".")[0]')" = "$NODE_MAJOR" ]; then
  node_ok=1; echo "node już jest: $(node --version) ($(command -v node))"
fi
if [ "$node_ok" = 0 ]; then
  case "$(uname -m)" in x86_64) NARCH=x64;; aarch64) NARCH=arm64;; *) die "nieobsługiwana architektura: $(uname -m)";; esac
  NODE_BASE="https://nodejs.org/dist/latest-v${NODE_MAJOR}.x"
  if [ "$DRY" = 1 ]; then
    echo "+ [dry-run] pobiorę $NODE_BASE/SHASUMS256.txt, wybiorę linux-$NARCH.tar.xz, sprawdzę sha256, rozpakuję do /opt/node$NODE_MAJOR,"
    echo "+ [dry-run] zlinkuję node/npm/npx do /usr/local/bin"
  else
    NTMP="$(mktemp -d)"; trap 'rm -rf "$NTMP"' EXIT
    curl -fsSL "$NODE_BASE/SHASUMS256.txt" -o "$NTMP/SHASUMS256.txt"
    NFILE="$(awk -v a="linux-$NARCH.tar.xz" '$2 ~ a"$" {print $2; exit}' "$NTMP/SHASUMS256.txt")"
    [ -n "$NFILE" ] || die "w SHASUMS256.txt nie ma linux-$NARCH.tar.xz"
    curl -fsSL "$NODE_BASE/$NFILE" -o "$NTMP/$NFILE"
    (cd "$NTMP" && grep " $NFILE\$" SHASUMS256.txt | sha256sum -c -)
    $SUDO mkdir -p "/opt/node$NODE_MAJOR"
    $SUDO tar -xJf "$NTMP/$NFILE" -C "/opt/node$NODE_MAJOR" --strip-components=1
    for b in node npm npx; do $SUDO ln -sf "/opt/node$NODE_MAJOR/bin/$b" "/usr/local/bin/$b"; done
    echo "node: $(node --version)"
  fi
fi

# --- 4. ccache ---------------------------------------------------------------------
# tools/gate.sh sam ustawia CCACHE_DIR/CCACHE_MAXSIZE (na sztywno /home/user/.ccache, 12G); tu tylko rozsądne wartości domyślne.
log "4. ccache (max_size=$CCACHE_SIZE, kompresja włączona)"
run ccache -o max_size="$CCACHE_SIZE"
run ccache -o compression=true

# --- 5. web: npm ci + Chromium dla Playwright ------------------------------------------
log "5. web (npm ci + Playwright Chromium)"
if [ "$DO_WEB" = 0 ]; then
  echo "pominięte (--no-web)"
elif [ ! -f "$REPO/loom/web/package.json" ]; then
  warn "brak $REPO/loom/web/package.json - pomijam (sklonuj repozytorium i uruchom skrypt jeszcze raz)"
else
  # Node z kroku 3 musi być w PATH także dla sudo (sudo resetuje PATH, więc przekazujemy go jawnie).
  run bash -c "cd '$REPO/loom/web' && npm ci --no-audit --no-fund"
  # biblioteki systemowe Chromium (apt, wymaga roota) - ten sam efekt co `--with-deps` w loom.yml, rozbity na dwa kroki,
  # żeby sama przeglądarka trafiła do katalogu użytkownika, a nie roota
  run bash -c "cd '$REPO/loom/web' && $SUDO env \"PATH=\$PATH\" npx playwright install-deps chromium"
  run bash -c "cd '$REPO/loom/web' && npx playwright install chromium"
fi

# --- 6. (opcja) Java 17 + Android SDK ----------------------------------------------------
if [ "$DO_ANDROID" = 1 ]; then
  log "6. Java 17 + Android SDK (--android)"
  # CI używa Temurin 17; tu OpenJDK 17 z archiwum Ubuntu (różnice nieistotne dla Gradle/JNI, ale niesprawdzone)
  run $SUDO apt-get install -y --no-install-recommends openjdk-17-jdk-headless
  SDKMGR="$ANDROID_HOME_DIR/cmdline-tools/latest/bin/sdkmanager"
  if [ -x "$SDKMGR" ]; then
    echo "cmdline-tools już są: $SDKMGR"
  elif [ "$DRY" = 1 ]; then
    echo "+ [dry-run] pobiorę $ANDROID_CMDLINE_URL, (sprawdzę sha256, jeśli podano), rozpakuję do $ANDROID_HOME_DIR/cmdline-tools/latest"
  else
    ATMP="$(mktemp -d)"
    curl -fSL "$ANDROID_CMDLINE_URL" -o "$ATMP/cmdline-tools.zip" \
      || die "pobranie cmdline-tools nie powiodło się (adres z pamięci mógł się zmienić) - ustaw CHATADHD_ANDROID_CMDLINE_URL"
    if [ -n "$ANDROID_CMDLINE_SHA256" ]; then
      echo "$ANDROID_CMDLINE_SHA256  $ATMP/cmdline-tools.zip" | sha256sum -c -
    else
      warn "brak CHATADHD_ANDROID_CMDLINE_SHA256 - suma archiwum nie jest sprawdzana"
    fi
    mkdir -p "$ANDROID_HOME_DIR/cmdline-tools"
    unzip -q "$ATMP/cmdline-tools.zip" -d "$ATMP/x"
    rm -rf "$ANDROID_HOME_DIR/cmdline-tools/latest"
    mv "$ATMP/x/cmdline-tools" "$ANDROID_HOME_DIR/cmdline-tools/latest"
    rm -rf "$ATMP"
  fi
  # wersje NDK i platformy z jedynego źródła prawdy, tak jak robi to package.yml
  G="$REPO/loom/android/app/build.gradle.kts"
  if [ -f "$G" ]; then
    NDK_V="$(sed -n 's/^[[:space:]]*ndkVersion = "\([^"]*\)".*/\1/p' "$G")"
    SDK_V="$(sed -n 's/^[[:space:]]*compileSdk = \([0-9][0-9]*\).*/\1/p' "$G")"
    echo "z $G: ndkVersion=${NDK_V:-?} compileSdk=${SDK_V:-?}"
    if [ -n "$NDK_V" ] && [ -n "$SDK_V" ]; then
      # licencje (yes | sdkmanager --licenses) to akceptacja warunków Google - robisz to świadomie, uruchamiając ten krok
      run bash -c "yes | '$SDKMGR' --sdk_root='$ANDROID_HOME_DIR' --licenses >/dev/null || true"
      run "$SDKMGR" --sdk_root="$ANDROID_HOME_DIR" --install "platforms;android-$SDK_V" "build-tools;34.0.0" "ndk;$NDK_V" "platform-tools"
      warn "pakiet 'cmake;3.x' z SDK (package.yml bierze najnowszy 3.x) nie jest tu instalowany - dodaj ręcznie, jeśli Gradle o niego poprosi: sdkmanager --list | grep 'cmake;3'"
    else
      warn "nie udało się odczytać ndkVersion/compileSdk z $G - zainstaluj platformę i NDK ręcznie"
    fi
  else
    warn "brak $G - zainstaluj platformę i NDK ręcznie (sdkmanager)"
  fi
else
  log "6. Java + Android: pominięte (dodaj --android, gdy VM ma budować APK)"
fi

# --- 7. (opcja) KVM dla emulatora - tylko odczyt ---------------------------------------------
if [ "$DO_EMULATOR" = 1 ]; then
  log "7. KVM / wirtualizacja zagnieżdżona (--emulator, tylko odczyt)"
  if [ -e /dev/kvm ]; then
    echo "/dev/kvm: JEST ($(ls -l /dev/kvm | awk '{print $1, $3, $4}'))"
  else
    echo "/dev/kvm: NIE MA - emulator Androida z akceleracją nie ruszy."
    echo "  Na VM w chmurze oznacza to zwykle, że wirtualizacja zagnieżdżona nie jest włączona (albo typ maszyny jej nie obsługuje)."
    echo "  Co zrobić: sprawdź w dokumentacji dostawcy hasło 'nested virtualization' (nie podaję tu nazwy opcji z pamięci)."
  fi
  VMX="$(grep -Ec '(vmx|svm)' /proc/cpuinfo 2>/dev/null || true)"
  echo "procesory z flagą vmx/svm w /proc/cpuinfo: ${VMX:-0} (0 = wirtualizacja niewidoczna dla tej maszyny)"
  echo "wirtualizacja (systemd-detect-virt): $(systemd-detect-virt 2>/dev/null || echo nieznana)"
  echo "grupa kvm: $(getent group kvm >/dev/null && echo 'jest' || echo 'brak') ; Twoje grupy: $(id -nG)"
  echo "(jeśli /dev/kvm jest, a emulator mówi 'permission denied': $SUDO usermod -aG kvm \"\$USER\" i zaloguj się ponownie)"
fi

# --- 7b. dowiązanie /home/user (domyślnie; --no-home-link wyłącza) ----------------------------------
# tools/gate.sh i briefy w tools/workflows mają /home/user na sztywno (OPS2 to odpina); do tego czasu dowiązanie
# sprawia, że te same ścieżki działają na każdym koncie. Preset, nie reguła: --no-home-link.
if [ "$DO_HOMELINK" = 1 ]; then
  log "7b. dowiązanie /home/user -> \$HOME"
  if [ "$HOME" = "/home/user" ]; then
    echo "HOME to już /home/user - nic do zrobienia"
  elif [ -e /home/user ]; then
    if [ "$(readlink -f /home/user 2>/dev/null)" = "$(readlink -f "$HOME")" ]; then
      echo "/home/user już wskazuje na $HOME"
    else
      warn "/home/user już istnieje i nie jest Twoim katalogiem - nie ruszam (sprawdź: ls -ld /home/user); gate.sh będzie pisał ccache tam"
    fi
  else
    echo "(dlaczego: tools/gate.sh i briefy mają /home/user na sztywno; wyłączenie: --no-home-link)"
    run $SUDO ln -s "$HOME" /home/user || warn "dowiązanie /home/user -> $HOME nie powstało (brak sudo?); zrób ręcznie: sudo ln -s $HOME /home/user"
  fi
fi

# --- 8. plik środowiska (PATH z Pythonem 3.11 itd.) ----------------------------------------------
# tools/gate.sh bierze `which python3`, więc venv z Pythonem 3.11 musi być na początku PATH.
log "8. plik środowiska $ENV_FILE"
ENV_BODY="# wygenerowane przez tools/bootstrap_dev_vm.sh ($(date +%F)); możesz edytować
export PATH=\"$VENV/bin:\$PATH\"             # python3 = $PY_MM (jak w CI)
export CCACHE_DIR=\"\${CCACHE_DIR:-\$HOME/.ccache}\""
if [ "$DO_ANDROID" = 1 ]; then
  ENV_BODY="$ENV_BODY
export ANDROID_HOME=\"$ANDROID_HOME_DIR\"
export ANDROID_SDK_ROOT=\"$ANDROID_HOME_DIR\"
export PATH=\"\$ANDROID_HOME/cmdline-tools/latest/bin:\$ANDROID_HOME/platform-tools:\$PATH\""
fi
if [ "$DRY" = 1 ]; then
  echo "+ [dry-run] zapisałbym do $ENV_FILE:"; printf '%s\n' "$ENV_BODY" | sed 's/^/    /'
else
  mkdir -p "$(dirname "$ENV_FILE")"
  printf '%s\n' "$ENV_BODY" > "$ENV_FILE"
  echo "zapisano $ENV_FILE"
fi
SRC_LINE="[ -f \"$ENV_FILE\" ] && . \"$ENV_FILE\"   # chatadhd-dev-env"
if [ "$DO_BASHRC" = 1 ]; then
  if grep -qF "chatadhd-dev-env" "$HOME/.bashrc" 2>/dev/null; then
    echo "plik .bashrc już ma wpis chatadhd-dev-env"
  else
    run bash -c "printf '%s\n' '$SRC_LINE' >> '$HOME/.bashrc'"
  fi
else
  echo "Aby venv z Pythonem 3.11 był w PATH przy każdym logowaniu, dopisz do ~/.bashrc:"
  echo "    $SRC_LINE"
  echo "(albo uruchom skrypt z --bashrc; na razie: . $ENV_FILE)"
fi

# --- 9. podsumowanie i ostrzeżenia (to nie jest build) -----------------------------------------------
log "9. podsumowanie"
echo "procesory: $(nproc)   pamięć: $(free -g 2>/dev/null | awk '/^Mem:/ {print $2" GB"}')"
df -h "$HOME" | tail -1 | awk '{print "dysk ($HOME): razem " $2 ", wolne " $4 " (" $5 " zajęte)"}'
free_kb="$(df -Pk "$HOME" | awk 'NR==2 {print $4}')"
if [ "${free_kb:-0}" -lt $((20 * 1024 * 1024)) ]; then
  warn "mniej niż 20 GB wolnego: jeden katalog buildu dev to 4-6 GB, ccache do $CCACHE_SIZE, Android SDK kilkanaście GB (patrz docs/ops/PRZEPROWADZKA_GCP_2026-10-23.md, §2)"
fi
if [ "$HOME" != "/home/user" ] && [ ! -e /home/user ]; then
  warn "tools/gate.sh ma na sztywno /home/user (CCACHE_BASEDIR, CCACHE_DIR) - przy HOME=$HOME ccache pisze poza Twoim katalogiem; uruchom bez --no-home-link albo: sudo ln -s $HOME /home/user (OPS2 odepnie ścieżkę)."
fi
if [ "${#ADVISORIES[@]}" -gt 0 ]; then
  echo
  echo "Uwagi z tego biegu (${#ADVISORIES[@]}; żadna nie zatrzymała skryptu):"
  for a in "${ADVISORIES[@]}"; do echo "  - $a"; done
fi
if [ "$DRY" = 1 ]; then
  echo "(tryb --dry-run: wersje narzędzi nie są wypisywane; procesory, pamięć i dysk powyżej dotyczą tej maszyny)"
else
  for t in cmake g++ clang ninja ccache git; do printf '%-8s %s\n' "$t" "$(command -v "$t" >/dev/null 2>&1 && "$t" --version 2>&1 | head -1 || echo 'BRAK')"; done
  printf '%-8s %s\n' "python3.11" "$("$VENV/bin/python" --version 2>&1 || echo 'BRAK')"
  printf '%-8s %s\n' "node" "$(node --version 2>&1 || echo 'BRAK')"
fi

cat <<NEXT

Skrypt NIE uruchomił builda. Następny krok (ręcznie, po '. $ENV_FILE'):
  cd "$REPO/loom"
  cmake --preset dev -DLOOM_BUILD_SERVER=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DPython3_EXECUTABLE="\$(which python3)"
  cmake --build --preset dev --parallel "\$(nproc)"
  # pełna bramka (40-90 min na maszynie 4-rdzeniowej; uruchom w tle z limitem 2 h, np. w tmux):
  #   tools/gate.sh "$REPO" vm1
Zmierz i zapisz: ile trwa zimny build, ile ciepły (ccache), ile bramka. Bez tych liczb żadna z wartości w dokumencie przeprowadzki nie jest pomiarem.
NEXT
