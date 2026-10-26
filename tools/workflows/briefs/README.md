# Briefy pakietów (args dla `tools/workflows/single-package.js`)

Kopie briefów S1 z katalogu roboczego sesji (scratchpad), zapisane w repo 2026-10-23 przed przeprowadzką
(R68): po zmianie maszyny sesja traci scratchpad, a kolejne pakiety mają czytać karty + `git log` + te briefy.
Uruchomienie: `Workflow({scriptPath: 'tools/workflows/single-package.js', args: <zawartość pliku>})`.
Reguły właściciela, do których briefy odsyłają, są w `CLAUDE.md`; jego słowa pod `spec:<id>` (`tools/spec.py show`).

## Pakiet kontekstu, `spec:` i zapis biegu (IW-1, IW-2; 2026-10-26)

Brief może nazwać pakiet kontekstu (`"context_pack": "podwykonawca"`, pakiety w `tools/routines/context_packs.json`
— przykłady) i pozycje spec (`"spec": ["R90"]`). Przed startem, 0 tokenów:

```
python3 tools/context_pack.py fill-brief <brief.json> -o <wypełniony.json>   # context_prefix + context_pack_hash
python3 tools/context_pack.py check-brief <wypełniony.json>                  # uwagi: brak spec:, nieznany numer, nieaktualny hash
RID=$(python3 tools/run_record.py start --routine package.run --param id=<id> --context-pack-hash <hash> --brief-file <brief.json>)
```

`single-package.js` zaczyna prompt od `context_prefix` bajt w bajt (ten sam pakiet = ten sam prefiks; hash w karcie),
dopisuje `spec:` i — gdy jest `run_id` — każe agentowi zapisywać kroki biegu. Po biegu:
`python3 tools/run_record.py import-card <karta.json> --run-id $RID`. Pierwszy taki brief: `iw3_brief.json`.
