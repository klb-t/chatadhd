# Dokładny punkt wznowienia

Pracuj dalej wyłącznie na `gpt/ecosystem-audit-2026-10-09`; pierwszego przebiegu ani tego zamkniętego pakietu nie nadpisuj. Nowe dowody zapisz w następnym katalogu etapu. Root manifest i publication-receipt określają opublikowany zestaw. Nie zmieniaj produktu, STATE/INDEX ani plików B/C.

Ostatnie sprawdzone main: `baselines.json`. Ostatnie B: `384c5e1686cd3a58a7d89a8a6813a18c764f697d`; C: `b9b503f62bb8e8e00c94ab7401e1cd9579129af3`. Nie używaj starego „brak gałęzi C”. Wyniki ddca/6988 pozostają historyczne. Nie odświeżaj gałęzi bez celu.

1. Otwórz `resume-queue.json`, `backlog.json`, `coverage-index.json` oraz odpowiedni pakiet odbioru. Pierwsze nieodblokowane kontrakty nie blokują kolejnych pozycji kolejki. Wykaz811 plików bez zakresów jest jawny; nie trzeba regenerować skanu literałów.
2. Utwórz osobny checkout konkretnego nowego SHA. Dla napraw powtórz ten sam audit runner, z rzeczywistymi konsumentami i przechwyconym transportem. Pole `--sha` jest wymagane; zależności/kompilowane biblioteki także muszą odpowiadać źródłom. Zależne binaria nie mogą być niejawnie pożyczone z innego SHA.
3. Uruchamiaj katalog narzędzi:

```sh
python tools/ecosystem-audit-2026-10-09/pass2/run_index.py --list
python tools/ecosystem-audit-2026-10-09/pass2/run_index.py --validate
python tools/ecosystem-audit-2026-10-09/pass2/run_index.py --jobs NOWE_ZADANIA.json --output-dir NOWE_DOWODY
```

`NOWE_ZADANIA.json` to niepusta lista `{ "suite": "chat-python", "args": ["--repo", "CHECKOUT", "--sha", "SHA", "--phase", "acceptance", "--output", "NOWY_RECEIPT.json"] }`. Każdy suite ma własne `--help`; dokładne argumenty i zależności są w per-module test-index/README. Indeks wykonuje następne zadania także po FAIL; exit1 nie staje się PASS. Osobne A reprodukcji nie jest akceptacją B.

4. CH004/005 pełna akceptacja wymaga istniejącego kontraktu producenta user/revision/category i operation/estimate/approval; brakujących danych nie dopisuj fikcyjnie w harnessie. CH006 pełny candidate/ask wymaga rzeczywistej ścieżki, nie truthy metadanych. Cpayer trzeba wiązać z dokładnym request SHA, nie tylko operation_id. W Bsemantic sprawdź także usunięcie i pustą nakładkę bez restartu.
5. Pozostałe bramki środowiska: browser/socket, LSan/proc, właściwe JDK21/SDK37 AGEDS, urządzenie/Keystore/IME, raw artifact resolver i pełny publiczny backup profilu+warstw+workflow. Nie powtarzaj udanych kompilacji wyłącznie dla liczby testów. Publiczne raporty nie mogą zawierać prywatnych źródeł ani sekretów; stosuj syntetyczne canaries.
6. Po zamknięciu modułu zapisz receipts, klasyfikację, funkcje/linie i nieprześledzone wywołania; commit/push własnej gałęzi, potem kolejny odblokowany moduł. Standalone loom pozostaje historyczny.

W środowisku tej sesji checkouts były w `audit-work/continuation/checkouts/{main,B,B2,C,C2}`; natywne buildy w `audit-work/continuation/scratch/native/{main-build,B-build}`. To ścieżki robocze, nie wymóg odtworzenia — trwałym źródłem są opublikowane skrypty, przypięte SHA i receipts.
