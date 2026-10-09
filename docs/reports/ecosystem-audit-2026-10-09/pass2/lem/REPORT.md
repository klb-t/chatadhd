# LEM-Workbench: drugi etap weryfikacji, 2026-10-09

Baza `main@1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456`. Pierwszy raport i jego receipts zachowano. Kod produktu niezmieniony. Nie uruchamiano płatnych modeli ani CI.

**Najnowszy stan po rzeczywistych testach Room: A 8 PASS; B 10 FAIL, 3 PASS, 1 BLOCKED; blokada sieci PASS.** Trzy istniejące testy projektu przechodzą osobno. Wcześniejszy BLOCKED migracji w receipt hostowym został rozstrzygnięty rzeczywistym testem Room jako FAIL akceptacji. Pozostały BLOCKED dotyczy pełnego resolvera surowych artefaktów. Wiersze poniżej zachowują chronologię dwóch zamkniętych zestawów dowodów.

Uruchomiono rzeczywisty Kotlin konsumentów, nie kopię algorytmu w Pythonie: `ResearchViewModel → ResearchRepository → ExperimentRunner → ResearchAgent → Retrofit/Moshi/OkHttp`, z przechwyconym transportem oraz powrotem wyniku do DAO. Osobno wywołano rzeczywisty `AppDatabase.getDatabase` z rejestratorem wywołań buildera Room.

**Reprodukcja A: 7 PASS. Akceptacja B: 8 FAIL, 2 PASS, 2 BLOCKED. Kontrola blokady sieci: PASS.** Proces bramki zwrócił 1. PASS A potwierdza odtworzenie dotychczasowego błędu; nie oznacza poprawności produktu. Szczegółowe wyniki: `receipts/results.json`; hashe 13 źródeł i zależności: `receipts/receipt.json`.

| Ustalenie | Nowy dowód wykonania | Stan akceptacji |
|---|---|---|
| LEM-001 | Dwie osobne instancje repozytorium z jedynym zapisanym configiem 256/query albo 1024/classification; oba requesty rzeczywistego konsumenta mają wymiar 768 i brak taskType. Parametr epochs nie ma jawnej dyspozycji, wynik nie wskazuje configID. | Trzy bramki FAIL |
| LEM-003 | Dwa kandydaty odpowiedzi kończą jako hash przyciętego pierwszego tekstu; brak referencji do surowej odpowiedzi. Porażka drugiego embeddingu gubi pierwszy output i requestedDimension w rekordzie. | Dwie bramki FAIL; odczyt artefaktu po restarcie BLOCKED |
| LEM-004 | Pierwsza przechwycona strona zawiera nextPageToken. Agent wykonuje jeden request, zwraca wyłącznie model strony 1. | FAIL |
| LEM-005 | Rzeczywisty AppDatabase wybiera fallbackToDestructiveMigration bez addMigrations. | Builder FAIL; rzeczywista migracja v1→v2 BLOCKED |
| LEM-008 | Timeout oznacza INSTRUMENT_FAILED. Po usunięciu syntetycznego klucza request nie wychodzi nawet do przechwyconego transportu. | Dwie wąskie bramki PASS |
| LEM-009 — nowe | Błąd transportu z U+0000…U+001F przechodzi przez rzeczywisty runner. Ścisły dekoder JSON odrzuca 30/32 payloadów rawMetrics; poprawnie escaped są LF/CR. | FAIL |

W bazie nie istnieje API wyboru aktywnego ExperimentConfig. Test LEM-001 zapisuje sole config i wywołuje istniejący model-name entrypoint. Nie przypisuje aplikacji nieistniejącego przełącznika profilu. B musi doprowadzić wybrany profil do tego konsumenta lub jawnie odrzucić nieobsługiwaną konfigurację; nowy publiczny interfejs wymaga cienkiego adaptera wejściowego w niezależnym teście.

LEM-009 dotyczy błędnego mechanizmu serializacji. R42 wyjątki 2/4 pozwalają trzymać gramatykę JSON i escaping w kodzie. Rekomendacją jest prawidłowy encoder, a nie wyniesienie reguł JSON do profilu. Pełne pola nowego ustalenia znajdują się w `findings.jsonl`; starsze ID mają dodatkowe dowody w `finding-updates.json`.

Zakres hostowy nie dowodzi działania Androida: Context/SharedPreferences i lifecycle są jawnymi fixtures, DAO przechowują dane w pamięci, Room builder jest rejestratorem wywołań. Prawdziwe są źródła powyższych konsumentów, korutyny/Flow i serializacja transportu. JVM odmawia wszystkich połączeń socketowych; interceptor nie wywołuje `chain.proceed`. Użyto wyłącznie oznaczonego syntetycznego nie-klucza. Testowe odpowiedzi nie są pomiarami naukowymi LEM.

Nie nadajemy PASS surowym artefaktom po samym niepustym JSON-ie z referencjami. W bazie brak realnego resolvera/store, więc pełny roundtrip pozostaje zablokowany. Analogicznie wywołanie buildera Room nie dowodzi destrukcji ani zachowania rekordów podczas realnej migracji.

Pierwszy przegląd objął źródła 24/24 plików produktu. Przyrost liczby unikalnych przeczytanych plików wynosi **0**; przyrost dowodów to kompilacja 13 rzeczywistych źródeł i wykonanie opisanych zakresów. Deklaracje modeli/DAO nie są liczone jako wykonanie zapytań Room. `coverage.json` zawiera funkcje, zakresy i nieprześledzone wywołania; nie uruchamiano ponownie skanera kandydatów.

Pakiety odbioru B: `packages-for-B.json`. Wykonywalny indeks: `test-index.json`. Instrukcja odtworzenia: `tools/ecosystem-audit-2026-10-09/pass2/lem/README.md` i runner `run.py --repo <checkout> --sha <commit>`. Wynik `--phase acceptance` pozostaje niezerowy przy FAIL **lub** BLOCKED. Kompilator hostowy jest przypięty do Kotlin 2.2.10; biblioteki i ich hashe zawiera manifest.

Następny punkt odbioru: uruchomić identyczny host zestaw na poprawce B, następnie zweryfikować rzeczywisty Room open/reload v1→v2 i resolver artefaktów. Zmiana sygnatur ma powodować jawną konieczność dostosowania adaptera testu, nigdy pomijanie przypadków.

Ponowne sprawdzenie środowiska odblokowało pełną istniejącą bramkę `testDebugUnitTest`: **PASS**, Gradle 9.3.1, JDK 17.0.20.1+1, Android SDK 36.1, AGP 9.1.1. Wykonano 30 zadań w 255,6 s, skompilowano cały produkt i rzeczywiste implementacje DAO/AppDatabase wygenerowane przez KSP. Trzy istniejące testy (w dwóch klasach) są testami JVM, nie migracją ani Android E2E. Oryginalny błąd połączenia wynikał ze starego GRADLE_OPTS; użycie aktualnego proxy środowiska odblokowało pobranie zależności. Zachowano logi nieudanych prób i PASS w `receipts/environment/`. Nie usuwano reguł ani nie osłabiano bramki.

Następnie wykonano niezależne testy **rzeczywistego Room/KSP i zapytań SQLite** na hostowym Robolectric SDK 28. Historyczny schemat v1 pochodzi z `0c6ea26abecd7c401dd9cb452309741216533c30`; encje ExperimentResult/LedgerEntry są byte-identical z bazą audytu. Room generuje fixture schematu — test nie kopiuje implementacji migracji. Przy otwarciu przez rzeczywisty AppDatabase v2 znikają zarówno historyczny ledger, jak i ExperimentResult. Przy sztucznej nieobsługiwanej wersji 99 otwarcie nie odmawia, a ledger również znika. Reprodukcja A przechodzi, obie akceptacje B kończą się FAIL. **Aktualna ExperimentConfig zachowuje wszystkie wartości po rzeczywistym zamknięciu/ponownym otwarciu Room i wyzerowaniu singletona** — ten odrębny test B przechodzi.

To 4 dodatkowe testy: 1 A PASS, 1 B PASS i 2 B FAIL; Gradle zwraca 1, nie sukces. Ostatni przebieg trwał 27,6 s; XML, surowy log i zebrane obserwacje są w `receipts/room/`. Pierwszy przebieg tych testów pozostał w `receipts/room-first/`. Nie uruchamiano fizycznego Androida, emulatora ani restartu procesu OS. Ostrzeżenie Robolectric o Java 21 dla SDK 36 nie dotyczy wykonanych testów SDK 28 i nie jest dowodem wykonania SDK 36.

Zaległa bramka migracji jest więc rozstrzygnięta: produkt traci rekordy w kontrolowanym realnym przepływie Room. Punkt wznowienia B: implementacja migracji/polityki nieobsługiwanych wersji, następnie `gradle_probe.py --audit-room --phase acceptance` na SHA poprawki, plus weryfikacja urządzenia. Następną brakującą granicą danych pozostaje realny resolver surowych artefaktów i przypięcie wyniku do wersji receptury.

Sam opublikowany wrapper został również wykonany z `--repo`, `--sha` i `--phase acceptance` na świeżym archiwum źródeł: trzy B, jeden PASS i dwa FAIL, exit 1, 17,16 s. Dowód jest w `receipts/room-wrapper/`; nie doliczamy tego powtórzenia jako nowych przypadków. `validation.json` potwierdza poprawność zakresów i brak zmian checkoutu produktu.
