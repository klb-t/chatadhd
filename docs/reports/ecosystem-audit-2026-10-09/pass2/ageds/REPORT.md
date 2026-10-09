# AGEDS — drugi przebieg audytu, 2026-10-09

Baza `main`: `9c1d513bc19d177bd324d7506a21fbab98c2e268`. Pierwszy raport i receipts pozostają niezmienione. Nowe narzędzia są wyłącznie w katalogu audytora. Nie zmieniono produktu, schematów, głównych indeksów ani koordynacji projektu.

Zamknięte pakiety obejmują upload i przydział sprawy, HTTP→queue→worker→zapis wyniku, lease/retry, skan przez HTTP i rzeczywisty silnik Kotlin, import WhatsApp, wersje corpus seed, trwałość cache, cytat→eksport→inertne SQLite→odczyt. Nie oznacza to pełnego audytu każdej funkcji AGEDS.

## Wynik i rozdzielenie bramek

| Nowy niezależny zestaw | Reprodukcja | Akceptacja produktu |
|---|---:|---|
| Python, real FastAPI/SQLite/worker | 15/15 PASS obserwacji; część to kontrole poprawnego mechanizmu, nie błędy | 9 PASS, 6 FAIL |
| Kotlin — metody upload/selection oraz realny cache/gate | 2 PASS odtworzeń | 2 PASS, 1 FAIL, 1 brak kontraktu profilu kolejki |
| Kotlin — pełny CorpusStore i realny codec | 1 PASS odtworzenia | 1 PASS, 1 FAIL |
| Kotlin — pełny scan engine i parsery | 1 PASS odtworzenia | 2 PASS, 1 brak kontraktu globalnego budżetu |

**PASS odtworzenia problemu nie jest PASS produktu.** Wszystkie cztery runnery zwracają obecnie kod 1. Dwa brakujące kontrakty akceptacyjne oznaczono `BLOCKED_MISSING_CONTRACT`; nie udajemy wykonania nieistniejącego interfejsu wyboru strategii. B otrzymuje dokładne oczekiwanie oraz odtworzenie aktualnego konsumenta. Dla proponowanych pól `case_id` i `date_order` kontrakt jest jawnie nazwany w testach; jeśli B wybierze równoważne API, zmienia się wyłącznie adapter wejścia testowego.

Dodatkowo uruchomiono istniejące bramki środowiskowe: **429 testów Python + 588 subtests PASS** (6,37 s) i **85 testów JS PASS**. To zachowane bramki autora, nie 514 nowych niezależnych dowodów naprawy. JUnit/log są obok raportu.

## Potwierdzone ustalenia

- **A2-AG-001:** HTTP przyjmuje nieobsługiwane receptury/limity i je pomija. To granica walidacji, powiązana z EA-AGEDS-002/003.
- **A2-AG-002:** importer WhatsApp ma jedną politykę day-first. Ujawnia niejednoznaczność i zachowuje źródło, lecz nie umożliwia alternatywnego profilu interpretacji.
- **A2-AG-003:** upload nie przekazuje wyboru sprawy do `ensure_source`; wybiera pierwszy rekord case. Nie jest to twierdzenie o przełamaniu autoryzacji — API auth pozostaje jawną granicą wdrożenia.
- **A2-AG-004:** `CorpusStore` przyjmuje i po otwarciu cache odtwarza `schemaVersion=999` jako normalny typed seed; brak bramki zgodności wersji. Surowe unknown fields pozostają w cache.
- **A2-AG-005:** `SourceScanLimits` nie obejmuje globalnego zatrzymania na 10000 wierszy/50000 komórek/4000000 znaków. Próba rzeczywistego silnika potwierdza rozjazd limitu per-file i efektywnego globalnego budżetu; częściowe pokrycie jest poprawnie ujawnione.

Dokładne SHA, linie, hashe zakresów, wpływ, alternatywy, konsumenci, migracja, ryzyko i kryteria znajdują się w `findings.jsonl` i `handoff-B.json`. Ustalenia są interpretacjami audytora wobec jawnych wymagań; nie przypisują właścicielowi nowych intencji.

## Mechanizmy, których nie należy usuwać

Timeout realnego workera daje `failed`, bez transkrypcji; retry zachowuje dawny run. Stara lease nie publikuje po przejęciu. Upload nie kolejkuje ASR automatycznie. Nieprawidłowe metadata JSON jest odrzucane. Skan bez dozwolonych korzeni i traversal są blokowane. Nieprzyjęte/stare zapisy cache nie niszczą poprzednich bajtów. Zmiana endpointu podczas uploadu nie przekierowuje zamrożonej partii, a odznaczony URI nie trafia do transportu. Inertny eksport/import SQLite zachowuje kanoniczne bajty i rozszerzenia, nie wznawia jobs; cytat nadal wskazuje dawną wersję. Rehashed błędna wersja, kolizja ID i brak referencji są odrzucone przez rzeczywisty validator.

To nie wyjątek dla dowolnych stałych. Nazwy kontraktów/stanów (R42.1/R42.3), SHA-256/HTTP/SQLite limity reprezentacji (R42.2/R42.4) i diagnostyka programistyczna (R42.6) mogą pozostać mechanizmem; arbitralne wolumeny i polityki wymagają oddzielnej konfiguracji. Minimalnego bootstrapu (R42.5) nie użyto jako uzasadnienia dla wyboru sprawy, dat ani limitu retencji.

## Przyrost pokrycia i granice

Z 63 plików produktu: pierwszy przebieg obejmował wybrane zakresy 20; teraz doszły **wybrane zakresy 19 nowych**, łącznie **39/63**, **24 bez zakresów semantycznych**. `coverage.json` wymienia je wszystkie, funkcje i przedziały, oraz nieprześledzone wywołania. Kompilacja parserów i zielone testy nie powiększają automatycznie licznika semantycznego. Między przebiegami ten SHA produktu się nie zmienił.

Host Kotlin działa: kompilator 2.1.20, Java 17; codec 1.8.0. Pełne źródła store/cache/scanner są niezmienione; selected VM methods są wyciągane dokładnie z checkoutu, ich hashe trafiają do receipt. Android UI/Context/ContentResolver albo provider są atrapami infrastruktury. To nie Android/device i nie pełny lifecycle. Natywny JNI AGEDS pozostaje niepodłączonym prototypem, nie alternatywną działającą ścieżką ASR.

Rzeczywiście ponowiono pinned Gradle: pobranie 9.7.0 zakończyło `Network is unreachable`; zachowano log. JDK21/SDK37/ADB nie są dostępne. Nie obniżono pinów projektu, żeby ogłosić jego build zielonym. Browser gate ma osobny log próby; sandbox blokuje sockety. ASGI i JS nie zastępują browser E2E. Żaden model nie został pobrany ani odpłatnie wywołany.

## Następny punkt pracy

B: najpierw wersja `CorpusStore` i jawne odrzucenie nieobsługiwanych operacyjnych pól HTTP, następnie przypięta receptura jobów. Profil kolejki i globalnych budżetów skanu wymaga uzgodnionego rzeczywistego wejścia, żeby zamknąć dwa brakujące testy akceptacyjne. Nie przejmować kodu produktu w audycie.

Dalszy audyt: 24 wymienione pliki, zwłaszcza UI cytatów/scanu i pełne parsery XLS/XLSX/WAV, `CitationSelection.requireMatchingCreated`, rzeczywisty Android SAF/lifecycle/transport i pełny independent exchange consumer. Przejrzane fragmenty większych plików nadal mają jawnie nieprześledzone gałęzie. Brak nowego SHA AGEDS B nie blokuje tych prac. Root osobno ocenia B/C w chatadhd.
