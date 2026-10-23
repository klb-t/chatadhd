#!/usr/bin/env bash
# =============================================================================
# vm_oneliner.sh - JEDNA KOMENDA na świeżą VM (Ubuntu 24.04), wklejana przez ssh jako zwykły użytkownik z sudo:
#
#   curl -fsSL https://raw.githubusercontent.com/klb-t/chatadhd/claude/chataddhd-cpp-loom-core-IRGRN/tools/vm_oneliner.sh | bash -s -- --android --emulator --bashrc
#
#   (klon staje na tej samej gałęzi; po scaleniu do main zmienią się adres i domyślna gałąź; flagi są opcjonalne i idą
#    do tools/bootstrap_dev_vm.sh: --android (Java 17 + Android SDK/NDK), --emulator (test KVM), --bashrc (dopisz środowisko),
#    --dry-run (tylko pokaż), --help).
#
# Co robi, po kolei (każdy krok sprawdza, czy już zrobiony; można uruchamiać wiele razy):
#   1. apt: git, curl, ca-certificates, tmux, gh (GitHub CLI, do logowania i pushów)
#   2. klonuje repozytorium do ~/chatadhd (publiczne, HTTPS) albo robi w nim `git fetch`
#   3. uruchamia ~/chatadhd/tools/bootstrap_dev_vm.sh z przekazanymi flagami (kompilatory, cmake/ninja/ccache,
#      Python 3.11, Node 22, Playwright/Chromium, opcjonalnie Android) - skrypt jest w repo i tam ma pełny opis
#   4. instaluje Claude Code natywnym instalatorem (https://claude.ai/install.sh -> ~/.local/bin/claude), dopisuje PATH
#   5. wypisuje trzy kroki, których skrypt NIE może zrobić bez człowieka (logowanie Claude, logowanie GitHub,
#      pierwsze uruchomienie Remote Control z pytaniami Enable/Trust)
#
# Źródła komend Claude Code (dokumentacja code.claude.com, odczyt 2026-10-23): instalator natywny `curl -fsSL
# https://claude.ai/install.sh | bash` (setup.md), logowanie przez ssh = `claude auth login` drukuje URL, kod wklejasz
# w terminalu (authentication.md, troubleshoot-install.md), Remote Control = `claude remote-control --name NAZWA` w
# katalogu projektu, pokazuje sesję w aplikacji Claude i na https://claude.ai/code; trzymać w tmux po rozłączeniu ssh
# (remote-control.md). Token z `claude setup-token` NIE wystarcza do Remote Control - potrzebne pełne `claude auth login`.
#
# NIEPRZETESTOWANE na prawdziwej VM (stan 2026-10-23): sprawdzona tylko składnia. Zacznij od `--dry-run`.
# =============================================================================
set -euo pipefail
LOG="${CHATADHD_BOOTSTRAP_LOG:-$HOME/chatadhd-bootstrap.log}"
if [ -z "${CHATADHD_ONELINER_LOGGING:-}" ]; then
  # cały przebieg także do pliku (do wklejenia, gdy coś nie wyjdzie): tail -60 ~/chatadhd-bootstrap.log
  export CHATADHD_ONELINER_LOGGING=1
  exec > >(tee -a "$LOG") 2>&1
fi

REPO_URL="${CHATADHD_REPO_URL:-https://github.com/klb-t/chatadhd.git}"
REPO_DIR="${CHATADHD_REPO_DIR:-$HOME/chatadhd}"
# Domyślnie ta sama gałąź, z której pobrano ten skrypt (tam leżą oba skrypty; na main ich jeszcze nie ma) - to
# odwzorowuje układ koordynatora (/home/user/chatadhd na tej gałęzi). Inną gałąź: CHATADHD_BRANCH=... przed bash.
BRANCH="${CHATADHD_BRANCH:-claude/chataddhd-cpp-loom-core-IRGRN}"
RC_NAME="${CHATADHD_RC_NAME:-chatadhd-vm}"

say()  { printf '\n==> %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1; }

if [ "$(id -u)" = 0 ]; then
  echo "Uruchom jako zwykły użytkownik z sudo, nie jako root (Claude Code i ccache mają działać na Twoim koncie)." >&2
  exit 1
fi

say "1/5 pakiety podstawowe (git, curl, tmux, gh)"
# Świeża VM po pierwszym starcie zwykle robi automatyczne aktualizacje (unattended-upgrades) i trzyma blokadę dpkg
# przez kilka minut (zmierzone 2026-10-23 na VM właściciela: dwa biegi padły na "Could not get lock"). Czekamy,
# zamiast się wywracać; apt dostaje też własny limit czekania na blokadę (DPkg::Lock::Timeout).
APT_LOCK_WAIT="${CHATADHD_APT_LOCK_WAIT:-900}"   # sekund; preset
wait_for_apt() {
  local t=0
  while sudo fuser /var/lib/dpkg/lock-frontend /var/lib/dpkg/lock /var/lib/apt/lists/lock >/dev/null 2>&1; do
    [ "$t" = 0 ] && echo "   czekam na blokadę apt/dpkg (najpewniej unattended-upgrades po pierwszym starcie VM; zwykle 1-5 min, limit ${APT_LOCK_WAIT}s): $(pgrep -af 'unattended-upgr|apt|dpkg' | head -3 | tr '\n' ';')"
    sleep 5; t=$((t+5))
    [ "$t" -ge "$APT_LOCK_WAIT" ] && { echo "   blokada apt trzymana ponad ${APT_LOCK_WAIT}s - próbuję mimo to (apt sam poczeka do limitu)"; break; }
  done
}
command -v fuser >/dev/null 2>&1 || true   # bez psmisc pętla wyżej nic nie widzi; wtedy ratuje limit apt poniżej
wait_for_apt
APT="sudo apt-get -o DPkg::Lock::Timeout=$APT_LOCK_WAIT"
$APT update -qq
$APT install -y -qq --no-install-recommends git curl ca-certificates tmux gh psmisc

say "2/5 repozytorium -> $REPO_DIR (gałąź $BRANCH)"
if [ -d "$REPO_DIR/.git" ]; then
  git -C "$REPO_DIR" fetch -q origin
else
  git clone -q "$REPO_URL" "$REPO_DIR"
fi
git -C "$REPO_DIR" checkout -q "$BRANCH" 2>/dev/null || git -C "$REPO_DIR" checkout -q -b "$BRANCH" "origin/$BRANCH"
git -C "$REPO_DIR" pull -q --ff-only origin "$BRANCH" || echo "(pull --ff-only nie przeszedł - gałąź lokalna ma własne commity; zostawiam)"

say "3/5 środowisko deweloperskie: tools/bootstrap_dev_vm.sh $*   (pełny zapis: $LOG)"
# Bootstrap nie zatrzymuje całości: gdy padnie, dalej instalujemy Claude Code i wypisujemy kroki; na końcu „lekarz”.
bash "$REPO_DIR/tools/bootstrap_dev_vm.sh" "$@" || echo "!! bootstrap zakończył się błędem (szczegóły wyżej i w $LOG) — reszta kroków idzie dalej, lekarz na końcu pokaże, czego brakuje"

say "4/5 Claude Code"
if ! need claude && [ ! -x "$HOME/.local/bin/claude" ]; then
  curl -fsSL https://claude.ai/install.sh | bash
fi
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *)
  if ! grep -q '/.local/bin' "$HOME/.bashrc" 2>/dev/null; then
    printf '\n# Claude Code (instalator natywny)\nexport PATH="$HOME/.local/bin:$PATH"\n' >> "$HOME/.bashrc"
  fi
  export PATH="$HOME/.local/bin:$PATH";;
esac
"$HOME/.local/bin/claude" --version 2>/dev/null || claude --version || echo "(claude nie odpowiada - otwórz nową powłokę albo sprawdź ~/.local/bin)"

say "lekarz: co jest, czego brakuje (BRAK = uruchom komendę z prawej)"
doctor() { local name="$1" cmd="$2" fix="$3"; if command -v "$cmd" >/dev/null 2>&1; then printf '  %-10s %s\n' "$name" "$( { "$cmd" --version 2>/dev/null || "$cmd" -V 2>&1; } | head -1)"; else printf '  %-10s BRAK -> %s\n' "$name" "$fix"; fi; }
doctor node node "sudo apt-get install -y nodejs npm   (albo ponowny bieg tej komendy, gdy nodejs.org było niedostępne)"
doctor npm npm "sudo apt-get install -y npm"
doctor cmake cmake "sudo apt-get install -y cmake ninja-build ccache build-essential"
doctor ninja ninja "sudo apt-get install -y ninja-build"
doctor ccache ccache "sudo apt-get install -y ccache"
doctor g++ g++ "sudo apt-get install -y build-essential"
doctor clang++ clang++ "sudo apt-get install -y clang"
doctor python3 python3 "sudo apt-get install -y python3 python3-venv"
doctor tmux tmux "sudo apt-get install -y tmux"
doctor gh gh "sudo apt-get install -y gh"
doctor claude claude "curl -fsSL https://claude.ai/install.sh | bash ; export PATH=\$HOME/.local/bin:\$PATH"
[ -f "$HOME/.config/chatadhd-dev-env.sh" ] && echo "  środowisko: . $HOME/.config/chatadhd-dev-env.sh   (nowa powłoka wczytuje je sama przy --bashrc)"
[ -d "$REPO_DIR/loom/web/node_modules" ] && echo "  web: node_modules są" || echo "  web: node_modules BRAK -> cd $REPO_DIR/loom/web && npm ci && npx playwright install --with-deps chromium"

say "5/5 to zostaje dla Ciebie (trzy kroki wymagają wklejenia kodu z przeglądarki telefonu/laptopa):"
cat <<MSG

  a) Zaloguj Claude Code (wydrukuje adres; otwórz go na telefonie, skopiuj kod, wklej tu):
       claude auth login
     sprawdzenie:  claude auth status --text   oraz   claude doctor   (ma potwierdzić Remote Control)

  b) Zaloguj GitHub, żeby sesja mogła pushować (też kod z przeglądarki):
       gh auth login --web --git-protocol https && gh auth setup-git

  c) Uruchom Remote Control w tmux (przeżyje rozłączenie ssh), odpowiedz raz na pytania Enable/Trust:
       tmux new-session -d -s rc -c "$REPO_DIR" 'claude remote-control --name $RC_NAME'
       tmux attach -t rc        # odpowiedz y/y; Ctrl+B potem D odłącza, sesja dalej działa
     Sesja pojawi się na https://claude.ai/code i w aplikacji Claude (karta Code) jako „$RC_NAME”.

  Pierwszy build jądra (ccache zimny, kilkadziesiąt minut), potem bramka:
       cd "$REPO_DIR/loom" && cmake --preset dev -DLOOM_BUILD_SERVER=ON && cmake --build --preset dev --parallel 4
       cd "$REPO_DIR" && GATE_OUT=/tmp/gate-vm bash tools/gate.sh "$REPO_DIR" vm

  Opis całej przeprowadzki: $REPO_DIR/docs/ops/PRZEPROWADZKA_GCP_2026-10-23.md
MSG
