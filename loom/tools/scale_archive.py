#!/usr/bin/env python3
"""Pad a ChatGPT-style export with procedurally varied noise up to a target
size, for scale-testing Loom's Archive Intelligence pipeline against
multi-GB archives (OWNER_REQUIREMENTS_2026-09-26.md R1: "import selektywny
trzeba umozliwic, bo mam archiwa wielo GB" -- catalog without importing,
select, import only what matters).

By default it starts from the committed, hand-authored synthetic corpus
(tests/fixtures/eval/synthetic_dev/chatgpt_export.zip), so the exact
ground_truth.json for that corpus stays checkable against the padded
archive: the real 34 ChatGPT-side signal/noise/trap conversations are
written first, byte-identical, then the file is padded with procedurally
generated noise conversations (life admin, cooking, health, other people's
code, small talk -- the same categories as the hand-authored noise, just
generated instead of authored) until it reaches --target-gb.

Streaming: conversations are generated and written one at a time to the
output file (never held in memory as a single list), so this scales to
arbitrary --target-gb on constant memory.

Deterministic: --seed fixes a random.Random() stream; the same seed and
target size always produce byte-identical output.

Usage:
    python3 loom/tools/scale_archive.py --target-gb 2 --out /tmp/big_export/conversations.json

NEVER COMMIT THE OUTPUT of this script (it is explicitly excluded by
.gitignore -- see the note at the bottom of this file / the root
.gitignore's "Loom synthetic corpus scale-test output" entry).
"""
from __future__ import annotations

import argparse
import json
import pathlib
import random
import sys
import zipfile

DEFAULT_BASE_ZIP = (pathlib.Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "eval" /
                     "synthetic_dev" / "chatgpt_export.zip")

# ── procedural noise vocabulary (deliberately NOT the corpus's own hand-picked
# entities -- this is bulk filler, not additional ground truth) ──────────
CATEGORIES = ["life_admin", "cooking", "health", "other_peoples_code", "small_talk", "weather", "shopping"]

TOPICS = {
    "life_admin": ["podatki", "ubezpieczenie samochodu", "termin w urzedzie", "przeglad techniczny",
                   "platnosc za prad", "odnowienie dowodu", "zwrot paczki", "umowa najmu"],
    "cooking": ["przepis na zupe", "co na obiad", "piekarnik nie grzeje", "zakwas chlebowy",
                "marynata do kurczaka", "przepis na ciasto", "gotowanie ryzu", "grillowanie warzyw"],
    "health": ["bolacy kark", "wizyta kontrolna", "recepta", "bezsennosc", "bol glowy",
               "szczepienie", "wynik badania", "fizjoterapia"],
    "other_peoples_code": ["review PR kolegi", "stary skrypt PHP", "legacy Java", "cudzy config Dockera",
                            "review frontend w Vue", "skrypt bash znajomego", "stara baza Access"],
    "small_talk": ["plany na weekend", "nowy serial", "koncert w miescie", "pogoda", "urlop",
                   "remont lazienki", "nowy telefon", "ksiazka ktora czytam"],
    "weather": ["prognoza na jutro", "burza wieczorem", "upal w tym tygodniu", "pierwszy snieg"],
    "shopping": ["lista zakupow", "promocja w sklepie", "zwrot butow", "prezent urodzinowy"],
}

OPENERS = ["Hej, {t}, wiesz cos na ten temat?", "{t} -- trzeba to ogarnac w tym tygodniu.",
           "Szybkie pytanie o {t}.", "Mysle o {t}, co radzisz?", "{t}? kompletnie zapomnialam o tym."]
REPLIES = ["Jasne, powiedz wiecej.", "Ogarnijmy to razem.", "Masz juz jakis plan?",
           "To zalezy od kilku rzeczy.", "Moge pomoc, daj szczegoly."]
FOLLOWUPS = ["No dobra, dzieki.", "Spoko, zajme sie tym pozniej.", "Ok, zapisuje sobie.",
             "Chyba wiem juz co robic.", "Dzieki, to wystarczy na teraz."]
MODEL_SLUGS = ["gpt-4o", "gpt-4.1", "gpt-4o-mini", "o3-mini", "gpt-5"]


def gen_noise_conv(rng: random.Random, idx: int) -> dict:
    cat = rng.choice(CATEGORIES)
    topic = rng.choice(TOPICS[cat])
    n_turns = rng.randint(2, 5)  # user/assistant pairs
    year = rng.randint(2024, 2026)
    month = rng.randint(1, 12)
    day = rng.randint(1, 28)
    hour = rng.randint(7, 23)
    minute = rng.randint(0, 59)
    import calendar
    import datetime
    base = datetime.datetime(year, month, day, hour, minute)
    base_epoch = float(calendar.timegm(base.timetuple()))

    mapping = {"root": {"id": "root", "parent": None, "children": [], "message": None}}
    prev = "root"
    for i in range(n_turns):
        uid = f"n{idx}-u{i}"
        aid = f"n{idx}-a{i}"
        u_text = (OPENERS[i % len(OPENERS)] if i == 0 else FOLLOWUPS[(i - 1) % len(FOLLOWUPS)]).format(t=topic)
        a_text = REPLIES[i % len(REPLIES)]
        t_u = base_epoch + i * 240
        t_a = t_u + 90
        mapping[prev]["children"].append(uid)
        mapping[uid] = {"id": uid, "parent": prev, "children": [aid],
                        "message": {"id": uid, "author": {"role": "user"},
                                   "content": {"content_type": "text", "parts": [u_text]},
                                   "create_time": t_u, "metadata": {}}}
        mapping[aid] = {"id": aid, "parent": uid, "children": [],
                        "message": {"id": aid, "author": {"role": "assistant"},
                                   "content": {"content_type": "text", "parts": [a_text]},
                                   "create_time": t_a, "metadata": {"model_slug": rng.choice(MODEL_SLUGS)}}}
        prev = aid
    return {"id": f"noise-pad-{idx}", "title": topic, "create_time": base_epoch,
            "current_node": prev, "mapping": mapping}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target-gb", type=float, required=True, help="target conversations.json size, in GB (10^9 bytes)")
    ap.add_argument("--out", type=str, required=True, help="output path for the padded conversations.json")
    ap.add_argument("--seed", type=int, default=1337, help="deterministic RNG seed (default: 1337)")
    ap.add_argument("--base-zip", type=str, default=str(DEFAULT_BASE_ZIP),
                     help="ChatGPT-style export zip to seed the file with (default: the committed synthetic corpus)")
    ap.add_argument("--no-base", action="store_true", help="skip the base corpus, write pure noise")
    args = ap.parse_args()

    target_bytes = int(args.target_gb * 1_000_000_000)
    rng = random.Random(args.seed)

    base_convs = []
    if not args.no_base:
        base_path = pathlib.Path(args.base_zip)
        if not base_path.exists():
            print(f"warning: base zip {base_path} not found; writing pure noise "
                  f"(run loom/tools/gen_synthetic_eval_corpus.py first, or pass --no-base)", file=sys.stderr)
        else:
            with zipfile.ZipFile(base_path) as zf:
                base_convs = json.loads(zf.read("conversations.json"))

    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    written_real = 0
    written_noise = 0
    with open(out_path, "wb") as f:
        f.write(b"[\n")
        first = True
        for c in base_convs:
            if not first:
                f.write(b",\n")
            f.write(json.dumps(c, ensure_ascii=False).encode("utf-8"))
            first = False
            written_real += 1
        idx = 0
        while f.tell() < target_bytes:
            conv = gen_noise_conv(rng, idx)
            idx += 1
            if not first:
                f.write(b",\n")
            f.write(json.dumps(conv, ensure_ascii=False).encode("utf-8"))
            first = False
            written_noise += 1
            if written_noise % 20_000 == 0:
                print(f"  ... {written_noise:,} noise conversations, {f.tell() / 1e9:.3f} GB so far",
                      file=sys.stderr)
        f.write(b"\n]\n")

    size = out_path.stat().st_size
    print(f"wrote {out_path} ({size / 1e9:.3f} GB): {written_real} real (from the synthetic corpus) + "
          f"{written_noise:,} procedurally generated noise conversations, seed={args.seed}")


if __name__ == "__main__":
    main()
