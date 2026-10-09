# CH-003: niezależny konsument Runtime — main kontra B

Ten sam `probe.cpp` jest kompilowany przeciw niezmienionemu `libloom_core.a` main albo B. Nie odtwarza analizatora, resolvera, GraphEngine, bazy ani workera. Tworzy prawdziwy `Runtime` z `start_workers=false`, uruchamia live graph / ręczny drain i sprawdza materializowane krawędzie oraz metadane. Istniejący `ScriptedTransport` przechwytuje każdą próbę LLM i zwraca syntetyczny timeout. Brak realnych credentiali i brak płatnej sieci.

```bash
python3 run.py --repo /path/to/main --sha 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 \
  --build-dir /path/to/main-build --scratch /tmp/audit-semantic-main --output /tmp/main-receipt.json
python3 run.py --repo /path/to/B --sha ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8 \
  --build-dir /path/to/B-build --scratch /tmp/audit-semantic-B --output /tmp/B-receipt.json
```

Wymaga g++ z C++20 i ukończonego buildu vendored SQLite/OpenSSL: `libloom_core.a`, `libloom_sqlite3_amalgamation.a`, `libloom_miniz.a`. Biblioteki/OpenSSL muszą odpowiadać hostowi. Runner weryfikuje SHA checkoutu i zapisuje hashe źródła i biblioteki. Wiązanie biblioteki ze źródłowym SHA pochodzi z osobnego receipt buildu; runner tego nie udaje.

32 kontrole: dwa profile, live/worker, regex/LLM fallback, źródło hash, błąd packa/pending/reopen/recovery oraz powrót do builtin przez usunięcie packa i pustą jawną nakładkę — przed i po restarcie. Restart sprawdzany jest przez nowy obiekt Runtime i tę samą bazę. Poprzednie węzły mogą pozostać jako historia: test wymaga braku nowych krawędzi dla nieaktywnych reguł, nie niszczenia poprzednich wyników.

Exit 1 oznacza FAIL akceptacji produktu. Porównanie main/B rozstrzyga, które błędy odtworzono, naprawiono albo wprowadzono. Żaden oczekiwany FAIL nie jest zamieniany na PASS. Obsługiwane metadane live na main znajdują się w `semantic.analyzer_profile_hash`; test akceptuje ten istniejący kontrakt oraz nowy root-level hash, bez wymuszania migracji lokalizacji.
