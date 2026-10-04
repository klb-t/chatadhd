# Integrator — 2026-10-04

Przyjęto liniowo dokumentację R39–R41 Claude (`ba6eaf6`) oraz pierwszy przyrost
polityki zużycia wątku 2 (`6930fd2`). Zachowano INTERFEJS/PR9. Pozostałe
implementacje pozostają na swoich gałęziach. [INDEX](INDEX.md) przypina źródła,
statusy wszystkich 11 wątków, inwentarz i konkretne przekazania.

## Co wykonano i liczby przed / po

| Pomiar integratora | Wynik |
|---|---|
| Dokumentacja Claude: rebase, build, pełny CTest, web | 48 linii; 108/108 w 315.71s; web 85 modułów |
| W2: ręcznie inicjalizowane wartości presetu C++ | 6 → 0; kanoniczny dokument danych i generator |
| W2: kontrakty / generator / native source-edit proof | stare 19 zachowane → 25/25; 5/5; 4/4 warianty |
| W2: pełny aktualny CTest / web | 108/108 w 265.33s; web 85 modułów w 2.44s |
| W2: kontrola rzeczywistych przypadków | 107 wykonanych wpisów; 659 native / 24465 assertions; 1276 Python, 0 skips |
| W2: rzeczywista budowa | 129 niepustych obiektów, dokładny zbiór wejść Ninja; hashe obiektów i 7 binariów/archiwów zachowane po testach |
| W4: oryginalny P1 replay | podwójne wykonanie → pojedyncze; drugie executed=false, ledger calls=1/unresolved |
| W5: dwa oryginalne checkpoint reproduktory | historyczny błąd → 2/2 scenariusze, 29/29 niezależnych kontroli |
| W5: nowy OCR/provenance replay | MIME 1/3; dwie błędne ścieżki, 11 kontroli diagnostycznych |
| Nowe płatne wywołania integratora | 0 |

W2 zrebazowano na aktualny main z Claude. Wszystkie 16 wybranych plików
kodu/danych/testów/API są identyczne ze źródłem `5a73a36`. Pełny pierwotny
checkpoint, niewybrane historyczne dowody i ówczesny raport zachowano na
`archive/2026-10-04/usage-policy-before-intake`; końcowy raport autora: `cc6a758`.
Pięć domyślnych ścieżek odczytu, nakładka config.json/loom_usage_policy i restart
przeszły niezależną próbę zmiany danych. Shared/JNI eksport i startup/config
nie są częścią tego przyrostu.

Dwa wcześniejsze pełne przebiegi W2 dały 107/108 przez CLI ELF z trybem 0644.
Przyczyna zmiany trybu między wywołaniami nie została ustalona. Korekta do 0755
wewnątrz jednego nowego pełnego przebiegu zachowała hash i 32182400 bajtów ELF;
ten przebieg dał 108/108. Drugie stdout jest niepełne, jego pełny XML zachowano.
Oba nieudane wyniki są [wyłącznie w archive](https://github.com/klb-t/chatadhd/blob/c1166c51e54ee71727fb74765d448f8ae88fa8a2/docs/reports/integrator-usage-permission-2026-10-04.md).
Zielony wynik jest osobnym pełnym przebiegiem. Nie zmieniono testów, progów ani
timeoutów; istniejący opt-in catalog_scale 0/0 pozostaje jawnie niewykonany.

Dowody: [R39–R41](integrator-requirements-verification-2026-10-04/README.md),
[W2](integrator-usage-acceptance-2026-10-04/README.md),
[replay W4](integrator-packet-fix-2026-10-04/README.md),
[checkpoint W5](integrator-import-fix-2026-10-04/README.md).
Wcześniejsze reprodukcje, timeouty i negatyw katalogowy zachowano w archive.

## Czego nie przyjęto i dlaczego

3/4: rzeczywisty golden, producent 1/97 i pełny 121/1604 oraz oba natywne
konsumery PASS już opublikowano i sprawdzono. Raport 3 jest aktualny; nadal
wymagane potwierdzenie 4, uzgodnienie aliasów modeli / nested combinations i
pełne bramki połączonego źródła. Pierwotny P1 replay 4 niezależnie zamknięty.

5: wznawianie naprawione, ale nowy replay actual importer/OCR z atrapy transportu
wykazał pusty format MIME przy immutable blob w obu ścieżkach provenance.
[Pełny negatywny dowód](https://github.com/klb-t/chatadhd/blob/6e4bf03d6294719de8df4e9477e417d232c7b87b/docs/reports/integrator-import-screenshot-2026-10-04.md)
został wyłącznie w archive. Raport, instrukcja, autor 112/112 i rzeczywisty
2.15GB ENOSPC→resume istnieją; dawnych braków nie przypisujemy obecnemu kodowi.

1 czeka na miejsce w kolejce; 6 ma 14 pominięć DEV; 7 kończy offline i rozliczenie;
8/10 nadal bez poprawki vendored Clang; 11 wymaga pełnych bramek i deduplikacji
usage danych. Loader/CLI oraz poprawka materialize 11 są już opublikowane:
usunięto nieaktualne zarzuty z indeksu. README prezentacyjny czeka na odbiór funkcji.

Kontrole fetch rozdzielają czas committera i obserwację zmiany ref od dokładnego
czasu push, którego Git nie udostępnia. Nie stwierdzono przerwy ≥2h dla 2/3/4.
Żadnego kodu innych wątków nie poprawiano w zakresie integratora. Nie czytano
ślepego korpusu ani real-holdout-key. Wyłącznie dane publiczne i syntetyczne.

## Do wątku N

- **2:** osobny przyrost pięciu grup startup/config oraz ownership header/bootstrap/public export; usage jest już na main.
- **3/4:** W4 potwierdza opublikowany golden i jeden METHOD_GRAPH.md w swoim raporcie. Uzgodnić requested/observed model i nested DAG; potem rebase i pełne bramki. Nie trzeba czekać na wzajemny main.
- **3/11:** domyślne pack/profile, legacy typing i immutable PreparedRequest/resume; checked embedding APIs i injection nakładki. Konkretne luki w INDEX.
- **5:** zachować format obrazu przy immutable bytes, strict mock obu import paths; wersję migracji sprawdzić wewnątrz transakcji; osiem audit regresji do regularnego suite. Potem świeży rebase/full gates/replay.
- **1/7:** prompty, przepisy i datowane oceny jako wersje metod/twierdzenia. W7 plan wszystkich etapów i kosztów, rozliczenie starego $1.098135722; osobny klucz 5€ dopiero od właściciela.
- **6:** nowy przyrost tylko DEV; zachować pierwszy ślepy wynik, bez ponownego strojenia.
- **8/10:** Clang app.cpp:835/839, warunkowo :515 bez W2; pełna świeża macierz. UI R39–R41 i ustawienia/potwierdzenie usage.
- **11:** źródłowe parity i pełne bramki; usage.defaults z kanonicznego dokumentu 2; foundation osobno jeśli potrzebny; warstwy/wykluczenia R40 i media API z 5.
- **9:** następny odbiór według kolejki w INDEX; zachować historię negatywów i wpisy INTERFEJS, README oprzeć na przyjętych funkcjach.
