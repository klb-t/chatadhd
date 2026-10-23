#!/usr/bin/env bash
# Przechodzi seryjnie przez konta dostawców: co już podpięte — pomija; reszta: link do założenia konta, link do
# klucza, ukryte wklejenie (bez echa), zapis, sprawdzenie. Uruchamiasz w terminalu VM (np. przycisk SSH w konsoli GCP):
#   bash ~/chatadhd/tools/cloud/login_all.sh [nazwa…]     # bez nazw: wszyscy z listy; z nazwami: tylko oni
#   bash ~/chatadhd/tools/cloud/login_all.sh --status      # tylko stan, bez pytań
# Lista PROVIDERS niżej to przykłady (otwarta): dopisz linię, a skrypt ją obsłuży. Klucze bez własnego CLI trafiają do
# ~/.config/chatadhd/keys.env (prawa 600, format ENV), CLI z własnym logowaniem (gh, aws, az, hf, wandb) używają swoich
# plików. Nic nie jest wysyłane nigdzie poza samym dostawcą przy sprawdzeniu.
set -uo pipefail
KEYS="$HOME/.config/chatadhd/keys.env"; mkdir -p "$(dirname "$KEYS")"; touch "$KEYS"; chmod 600 "$KEYS"
export PATH="$HOME/.local/bin:$PATH"
b=$'\e[1m'; g=$'\e[32m'; y=$'\e[33m'; r=$'\e[31m'; n=$'\e[0m'

# name | kind | signup URL | key URL | ENV var (kind=key) | hint
# kind: key = paste an API key; cli = provider CLI flow (handled in login_cli/check_cli)
PROVIDERS=(
  "github|cli|https://github.com/signup|https://github.com/login/device||kod z ekranu wpisz na stronie"
  "gcp|cli|https://console.cloud.google.com/freetrial|https://console.cloud.google.com/billing/credits||VM ma konto serwisowe; kredyty widać pod linkiem"
  "aws|cli|https://portal.aws.amazon.com/billing/signup|https://console.aws.amazon.com/||zaloguj się do konsoli, skopiuj pokazany kod"
  "azure|cli|https://azure.microsoft.com/free/|https://login.microsoft.com/device||wpisz kod z ekranu, zaloguj kontem Microsoft"
  "huggingface|cli|https://huggingface.co/join|https://huggingface.co/settings/tokens/new?tokenType=write||Create token → kopiuj"
  "wandb|cli|https://wandb.ai/signup|https://wandb.ai/authorize||klucz widać od razu po zalogowaniu"
  "lambda|key|https://cloud.lambda.ai/sign-up|https://cloud.lambda.ai/api-keys/cloud-api|LAMBDA_API_KEY|Generate API key; pokazuje się raz"
  "openrouter|key|https://openrouter.ai/sign-up|https://openrouter.ai/settings/keys|OPENROUTER_API_KEY|Create Key"
  "anthropic|key|https://console.anthropic.com/|https://console.anthropic.com/settings/keys|ANTHROPIC_API_KEY|Create Key"
  "openai|key|https://platform.openai.com/signup|https://platform.openai.com/api-keys|OPENAI_API_KEY|Create new secret key"
  "gemini|key|https://aistudio.google.com/|https://aistudio.google.com/app/apikey|GEMINI_API_KEY|Create API key (konto Google)"
  "groq|key|https://console.groq.com/|https://console.groq.com/keys|GROQ_API_KEY|Create API Key"
  "together|key|https://api.together.ai/signup|https://api.together.ai/settings/api-keys|TOGETHER_API_KEY|klucz na stronie ustawień"
  "replicate|key|https://replicate.com/signin|https://replicate.com/account/api-tokens|REPLICATE_API_TOKEN|Create token"
  "runpod|key|https://www.runpod.io/console/signup|https://www.runpod.io/console/user/settings|RUNPOD_API_KEY|API Keys → Create"
  "modal|key|https://modal.com/signup|https://modal.com/settings/tokens|MODAL_TOKEN|New token (id:secret w jednej linii)"
  "cloudflare|key|https://dash.cloudflare.com/sign-up|https://dash.cloudflare.com/profile/api-tokens|CLOUDFLARE_API_TOKEN|Create Token"
)

have_key() { grep -q "^$1=." "$KEYS"; }
put_key() { local tmp; tmp="$(mktemp)"; grep -v "^$1=" "$KEYS" >"$tmp" || true; printf '%s=%q\n' "$1" "$2" >>"$tmp"; mv "$tmp" "$KEYS"; chmod 600 "$KEYS"; }
get_key() { ( set -a; . "$KEYS"; eval "printf %s \"\${$1:-}\"" ); }

check_cli() { case "$1" in
  github) gh auth status >/dev/null 2>&1 && gh api user -q .login 2>/dev/null;;
  gcp) gcloud config get-value account 2>/dev/null;;
  aws) aws sts get-caller-identity --query Arn --output text 2>/dev/null;;
  azure) az account show --query user.name -o tsv 2>/dev/null;;
  huggingface) hf auth whoami 2>/dev/null | head -1;;
  wandb) python3 - <<'EOF' 2>/dev/null
import netrc,os
try: print("ok" if netrc.netrc(os.path.expanduser("~/.netrc")).authenticators("api.wandb.ai") else "")
except Exception: pass
EOF
  ;; esac; }
check_key() { local v; v="$(get_key "$2")"; [ -n "$v" ] || return 1; case "$1" in
  lambda) curl -sf -u "$v:" https://cloud.lambda.ai/api/v1/instance-types >/dev/null && echo ok;;
  openrouter) curl -sf -H "Authorization: Bearer $v" https://openrouter.ai/api/v1/key >/dev/null && echo ok;;
  anthropic) curl -sf -H "x-api-key: $v" -H "anthropic-version: 2023-06-01" https://api.anthropic.com/v1/models >/dev/null && echo ok;;
  openai) curl -sf -H "Authorization: Bearer $v" https://api.openai.com/v1/models >/dev/null && echo ok;;
  gemini) curl -sf "https://generativelanguage.googleapis.com/v1beta/models?key=$v" >/dev/null && echo ok;;
  groq) curl -sf -H "Authorization: Bearer $v" https://api.groq.com/openai/v1/models >/dev/null && echo ok;;
  together) curl -sf -H "Authorization: Bearer $v" https://api.together.xyz/v1/models >/dev/null && echo ok;;
  replicate) curl -sf -H "Authorization: Bearer $v" https://api.replicate.com/v1/account >/dev/null && echo ok;;
  cloudflare) curl -sf -H "Authorization: Bearer $v" https://api.cloudflare.com/client/v4/user/tokens/verify >/dev/null && echo ok;;
  *) echo "zapisany (bez sprawdzenia)";; esac; }

login_cli() { case "$1" in
  github) gh auth login --web --git-protocol https && gh auth setup-git;;
  gcp) echo "  VM używa konta serwisowego; osobiste logowanie: gcloud auth login --no-launch-browser";;
  aws) aws configure get region >/dev/null 2>&1 || aws configure set region us-east-1; aws login --remote;;
  azure) az login --use-device-code --allow-no-subscriptions >/dev/null;;
  huggingface) read -rsp "  wklej token HF (niewidoczny): " t; echo; hf auth login --token "$t" --add-to-git-credential >/dev/null;;
  wandb) read -rsp "  wklej klucz W&B (niewidoczny): " t; echo; wandb login --relogin "$t" >/dev/null 2>&1;;
  esac; }

status_only=0; only=()
for a in "$@"; do [ "$a" = --status ] && status_only=1 || only+=("$a"); done
declare -a summary
for line in "${PROVIDERS[@]}"; do
  IFS='|' read -r name kind signup keyurl env hint <<<"$line"
  if [ ${#only[@]} -gt 0 ] && [[ ! " ${only[*]} " == *" $name "* ]]; then continue; fi
  if [ "$kind" = cli ]; then who="$(check_cli "$name")"; else who="$(check_key "$name" "$env")"; fi
  if [ -n "$who" ]; then summary+=("${g}✔${n} $name: $who"); echo "${g}✔${n} $name: $who"; continue; fi
  if [ "$status_only" = 1 ]; then summary+=("${y}·${n} $name: brak"); continue; fi
  echo; echo "${b}── $name${n}  ($hint)"
  echo "   konto:  $signup"
  echo "   klucz:  $keyurl"
  read -rp "   [Enter]=podpinam  s=pomiń  q=koniec: " ans
  case "$ans" in q) break;; s) summary+=("${y}·${n} $name: pominięty"); continue;; esac
  if [ "$kind" = cli ]; then login_cli "$name"; who="$(check_cli "$name")"
  else read -rsp "   wklej klucz $env (niewidoczny): " v; echo; [ -n "$v" ] && put_key "$env" "$v"; who="$(check_key "$name" "$env")"; fi
  if [ -n "$who" ]; then summary+=("${g}✔${n} $name: $who"); echo "   ${g}✔ działa${n}"
  else summary+=("${r}✘${n} $name: nie przeszło sprawdzenia"); echo "   ${r}✘ sprawdzenie nie przeszło${n} (klucz zapisany, można powtórzyć: $0 $name)"; fi
done
echo; echo "${b}Podsumowanie${n}"; printf '  %s\n' "${summary[@]}"
echo "Klucze bez CLI: $KEYS (600). Dopisanie dostawcy: jedna linia w PROVIDERS."
