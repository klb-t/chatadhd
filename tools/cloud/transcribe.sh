#!/usr/bin/env bash
# Transkrypcja nagrań przez Gemini na Vertex AI (projekt VM, konto serwisowe VM; koszt idzie na konto rozliczeniowe
# projektu, więc schodzi z kredytów, jeśli je obejmują). Bez kluczy: token z metadata servera / gcloud.
#   transcribe.sh PLIK… [--model M] [--location L] [--project P] [--prompt TEKST] [--out KATALOG]
# Presety (wszystko zmienialne): model gemini-3.5-flash, lokalizacja global, wynik obok pliku jako PLIK.txt.
# Prompt domyślny: dosłowna transkrypcja w języku nagrania, znaczniki czasu co akapit, rozróżnienie mówców.
# Pliki do ~15 MB idą inline; większe przez Cloud Storage (bucket gs://<projekt>-transcribe, tworzony raz;
# plik po transkrypcji zostaje — usuń --keep=no, gdy nie chcesz trzymać kopii).
set -euo pipefail
MODEL="${TRANSCRIBE_MODEL:-gemini-3.5-flash}" LOC="${TRANSCRIBE_LOCATION:-global}"
PROJECT="${TRANSCRIBE_PROJECT:-$(gcloud config get-value project 2>/dev/null)}" OUT="" KEEP=yes
PROMPT="${TRANSCRIBE_PROMPT:-Przepisz to nagranie dosłownie, w języku nagrania. Rozróżnij mówców (Mówca 1, Mówca 2…), dodaj znacznik czasu [mm:ss] na początku każdego akapitu. Niezrozumiałe fragmenty oznacz [niezrozumiałe]. Bez komentarzy i streszczeń.}"
files=()
while [ $# -gt 0 ]; do case "$1" in
  --model) MODEL="$2"; shift 2;; --location) LOC="$2"; shift 2;; --project) PROJECT="$2"; shift 2;;
  --prompt) PROMPT="$2"; shift 2;; --out) OUT="$2"; shift 2;; --keep=*) KEEP="${1#--keep=}"; shift;;
  -h|--help) sed -n '2,9p' "$0"; exit 0;; *) files+=("$1"); shift;; esac; done
[ ${#files[@]} -gt 0 ] || { sed -n '2,9p' "$0"; exit 2; }
host=aiplatform.googleapis.com; [ "$LOC" = global ] || host="$LOC-aiplatform.googleapis.com"
url="https://$host/v1/projects/$PROJECT/locations/$LOC/publishers/google/models/$MODEL:generateContent"
mime_of() { case "${1,,}" in *.mp3) echo audio/mpeg;; *.wav) echo audio/wav;; *.m4a|*.mp4|*.aac) echo audio/mp4;;
  *.ogg|*.opus) echo audio/ogg;; *.flac) echo audio/flac;; *.webm) echo audio/webm;; *) file -b --mime-type "$1";; esac; }
for f in "${files[@]}"; do
  mime="$(mime_of "$f")"; size=$(stat -c %s "$f")
  [ -n "$OUT" ] && mkdir -p "$OUT"
  dest="${OUT:-$(dirname "$f")}/$(basename "$f").txt"
  req="$(mktemp)"
  if [ "$size" -le 15000000 ]; then
    python3 - "$f" "$mime" "$PROMPT" >"$req" <<'EOF'
import base64,json,sys
f,mime,prompt=sys.argv[1:]
print(json.dumps({"contents":[{"role":"user","parts":[{"inlineData":{"mimeType":mime,"data":base64.b64encode(open(f,"rb").read()).decode()}},{"text":prompt}]}]}))
EOF
  else
    bucket="gs://$PROJECT-transcribe"; gcloud storage buckets describe "$bucket" >/dev/null 2>&1 || gcloud storage buckets create "$bucket" --location EU --uniform-bucket-level-access >/dev/null
    obj="$bucket/$(date +%s)-$(basename "$f")"; gcloud storage cp -q "$f" "$obj"
    python3 -c 'import json,sys;print(json.dumps({"contents":[{"role":"user","parts":[{"fileData":{"mimeType":sys.argv[2],"fileUri":sys.argv[1]}},{"text":sys.argv[3]}]}]}))' "$obj" "$mime" "$PROMPT" >"$req"
  fi
  resp="$(mktemp)"
  curl -s -m 900 -H "Authorization: Bearer $(gcloud auth print-access-token)" -H 'Content-Type: application/json' "$url" --data-binary @"$req" >"$resp"
  rm -f "$req"; [ "$KEEP" = no ] && [ -n "${obj:-}" ] && gcloud storage rm -q "$obj"
  python3 - "$resp" "$dest" <<'EOF'
import json,sys
d=json.load(open(sys.argv[1]))
if "error" in d: sys.exit("błąd: "+d["error"].get("message",""))
text="".join(p.get("text","") for p in d["candidates"][0]["content"]["parts"])
open(sys.argv[2],"w").write(text.strip()+"\n")
u=d.get("usageMetadata",{}); print(f"{sys.argv[2]}  (tokeny: wejście {u.get('promptTokenCount')}, wyjście {u.get('candidatesTokenCount')})")
EOF
  rm -f "$resp"
done
