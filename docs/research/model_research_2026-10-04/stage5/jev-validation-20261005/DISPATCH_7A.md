# 7C → 7A: 192 zamrożone pierwsze operacje

Paczkę wykonuje tylko prowadzący 7A, na istniejącym wspólnym prywatnym rejestrze
programu `thread7-new-key-2026-10-04-eur5`. Nie tworzyć nowego ledgeru ani jego kopii.
Nie powtarzać wcześniejszych 720 operacji. Ta paczka ma dokładnie 192 nowe ID,
96 dla każdego przepisu, 24 nowe rodziny; planowana rezerwa wynosi 0,192 USD.

`../new-label-dispatch-binding-v1/BINDING_RECEIPT.json` dokumentuje całą zmianę:
wyłącznie manifest `/programme_id` i odpowiadające trzy pola configu scorera.
Body, ID, kolejność operacji, model/provider, parametry, progi, gold i instrukcje
nie zmieniły się. Oryginały pozostają w `../new-label-preparation-v1` i
`../new-label-scoring-v1`. Zależności i dokładne requesty przechodzą walidację.

Manifest wykonawcy SHA256:
`e0dea1c45f0306cd4e7253fa4989bd9d1007a8c4878989bb80a98d7c850bea26`.
Konfiguracja scorera SHA256:
`4cfc08a2afbe4e98204232eda45d4cafd81f6a12f738fd4d7c5456bbc9f328ce`.
Scorer pozostaje dokładną kopią helpera
`51a68636c569362e6777865467e6eace09ff9d79c7f7613a53a3132d7a782d13`.

Z katalogu repo, po publikacji commitu preparacji i sprawdzeniu aktualnych
cen/FX oraz stanu wspólnego ledgeru, 7A używa istniejącego runnera. Własne
prywatne ścieżki 7A przekazuje przez zmienne środowiskowe; nie zapisuje ich w repo:

```sh
python -m loom.tools.structure.research_programme_runner run \
  --policy docs/research/model_research_2026-10-04/stage5/new-label-preparation-v1/operator-policy.json \
  --manifest docs/research/model_research_2026-10-04/stage5/new-label-dispatch-binding-v1/manifest.json \
  --evidence "$PROGRAMME_EVIDENCE_PATH" \
  --private-dir "$EXISTING_PROGRAMME_LEDGER_PATH" \
  --key-file "$EXISTING_PROGRAMME_KEY_FILE"
```

Zmienne oznaczają istniejące artefakty prowadzącego, nie nowe puste pliki.
`run` nie jest uruchamiany przez 7C. Jeśli bramka zatrzyma collection, zachować
pierwsze próby i braki; rozliczać tylko przez istniejący mechanizm bez retry.
Koszt rzeczywisty pochodzi wyłącznie z rachunków generacji.

Po collection 7A publikuje bezpieczny bundle przez
`loom.tools.structure.programme_results_v1.normalize(manifest, existing_ledger)`.
Oczekiwane miejsce: `collection-v1/NORMALIZED.json` obok tego pliku, plus osobny
receipt z SHA256 bundle, liczbą wykonanych prób i stanem kosztów. Nie kopiować
prywatnych envelope HTTP, bindingu konta, klucza ani rejestru do repo.

Scoring publicznych pierwszych odpowiedzi, wykonywany przez 7C:

```sh
python docs/research/model_research_2026-10-04/stage5/new-label-dispatch-binding-v1/score_first_only.py \
  --config docs/research/model_research_2026-10-04/stage5/new-label-dispatch-binding-v1/configuration.json \
  --manifest docs/research/model_research_2026-10-04/stage5/new-label-dispatch-binding-v1/manifest.json \
  --bundle docs/research/model_research_2026-10-04/stage5/jev-validation-20261005/collection-v1/NORMALIZED.json \
  --gold docs/research/model_research_2026-10-04/stage5/new-label-corpus-v1/gold.json \
  --config-sha256 4cfc08a2afbe4e98204232eda45d4cafd81f6a12f738fd4d7c5456bbc9f328ce \
  --bundle-sha256 "$PUBLISHED_NORMALIZED_BUNDLE_SHA256" \
  --output docs/research/model_research_2026-10-04/stage5/jev-validation-20261005/collection-v1/SCORE.json
```

SHA bundle ma pochodzić z receipt prowadzącego. Braki pozostają w mianownikach:
96/arm i 48/arm/język. Konflikt prawdopodobieństw jest osobnym rodzajem błędu,
nie trzecią dostępną etykietą. Wyniki nowych danych raportować osobno od starych
80/96 i 89/96. Pełne negatywy zostają w archiwum, bez automatycznej adopcji.
