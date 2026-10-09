# PASS4 — niezależny odbiór i testy przekrojowe, 2026-10-09

Przebieg kontynuuje opublikowany `e29109852e456dd66493b75193bc5882df65ffe2` i payload pass3 `1c77e16ffcf3797e1877ca4a815c3acdd455a07a`. Nie znaleziono późniejszego stanu A przy rozpoczęciu. Historyczne pass1–3 pozostają nienaruszone. Wykonano sześć etapów pakietu w dostępnym zakresie: odbiór B–E, nowe metamorfizmy, rzeczywiste granice modułów, testy nieznanych/niepełnych struktur, kolejne ścieżki pięciu repozytoriów i pakiety odbioru. Brakujące połączenia produktu pozostały jawnymi bramkami, bez implementowania ich przez audytora.

**Potwierdzono 15 nowych naruszeń i rozbudowano dowody 6 wcześniejszych ID.** Rejestr zawiera także 5 dopuszczalnych mechanizmów i 1 niepewną lukę kontraktu. [findings.jsonl](findings.jsonl) i uporządkowany [backlog.json](backlog.json) zachowują repo/SHA, linie, dowód, wpływ, wymaganie, alternatywy, konsumentów, test i ryzyko migracji. Nie ustanawiano minimalnej liczby błędów. Pakiety nie są wiadomościami wysłanymi sesjom produktowym.

## Przypięte źródła

| Repo/gałąź | SHA badany w pass4 |
|---|---|
| ChatADHD main | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` |
| B: data-graph-engine | `387afe08587f179d47c013a2ea518ff4b68e36bc` |
| C: thread7-real | `21ffc5e0769d53e4dbc507a2c57d8385e638b3f1` |
| D: resource-graph | `1d3d133154f213733b7a69af863613cec2dd8ca2` |
| E: graph-perspectives | `3eaac2953c3ee0d01d085e595d68edc091c284f2` |
| Watchdog-JH16 main | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` |
| Custom-Keyboard-Pro / IO Matrix main | `192f820f2d8c653768237e1bff3a1a6d950d69c0` |
| AGEDS main | `9c1d513bc19d177bd324d7506a21fbab98c2e268` |
| LEM-Workbench main | `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456` |

Gałęzie B–E pobrano jawnie. E pojawiło się po początkowym odkryciu i zostało odebrane, więc dawny brak E nie jest aktualną blokadą. Porównania B2/C2, instrukcje, zakresy i ograniczenia są w raportach modułów oraz [baselines.json](baselines.json). Pięć main bez zmian. Nie odświeżano gałęzi bezczynnie ani nie deklaruje się odbioru przyszłych commitów.

## Odbiór zmian i granic

| Granica | Niezależny wynik |
|---|---|
| B: CH-RES-N001/N002 | **Częściowo.** Nowa projekcja link/copy zachowuje role, gałęzie i rodziców. Dawne zwykłe wejścia rozmów nadal zawodzą. Pozostały problem to podłączenie konsumenta, nie obowiązek kopiowania lub materializacji wszystkich wiadomości. |
| B: wspólny odczyt/profil | Native preview, TaskEngine, zapis i reopen istniejącego DB przechodzą. Dwa jawne pliki profilu zmieniają rzeczywisty SemanticAnalyzer. Edycja pola grafu → resolver → workflow runtime pozostaje bez kontraktu. |
| C: credential handoff | **Naprawione w badanym zakresie.** Poprawione TMPDIR, 10/10 wskazanych testów autora plus niezależne odmowy Git/symlink, bez wyłączania ochrony lokalizacji. |
| C → native B → MethodRegistry | Realny store/reopen/replay oraz receipt-backed registry przechodzą. Raw wejście registry nadal odrzuca równoważny packet (A3-DISC-001). Brak wykonawcy jest poprawny, nie jest regresją ani zgodą na wysyłkę. |
| D: referencja na żądanie | Dołączenie bez odczytu i bez pól oraz selektywna projekcja przez istniejący native store działają. Nie udowodniono pełnej domenowej równoważności importu D, dispatch katalogu B ani trwałego wznowienia indeksu. Publiczne discover() odwołuje się do nieopublikowanego modułu. |
| D → native B → E | Rzeczywisty zapis/reopen/replay przed selekcją E działa po ID grafowym. Mapowanie selector/source_version D → ObjectRef E jest **BLOCKED** brakiem połączenia. Audyt nie dopisał adaptera. |
| E: resolver/perspektywy | Realny DefaultLayers → compiler → adapter → selector; dwa profile i exclusion po nowym procesie/pack update przechodzą. Oddzielne próby izolują widoczność, membership analizy i hostową odmowę uprawnień. Nie dowodzą native ACL. |
| E: UI i profile | Brak produkcyjnego hooka UI na przypiętym SHA. Znane pole zewnętrznego profilu jest czytelne headless, ale unknown field blokuje także znane pole (częściowy A3-WEB-001). |

Nie znaleziono nowego równoległego trwałego store/resolvera/workflow engine w sprawdzonych rozszerzeniach. Istniejące różne reprezentacje B i D nie zostały uznane za automatycznie połączone. [Mapa data → graph → runtime → UI](data-graph-runtime-ui.json) pokazuje rzeczywiste połączenia i luki. [Kolejka odbioru B–E](acceptance-queue.json) podaje dokładne testy, SHA i brakujące bramki.

## Potwierdzone nowe problemy

- **B:** A4-B-RES001 — relokacja tych samych 7 bajtów i jawny rescan pozostawiają nieosiągalny stary locator, także przy force.
- **C:** A4-C-001 — wersja źródła w przygotowaniu i manifest payera mogą się rozjechać; A4-C-002 — kolejność kluczy planu zmienia identyfikatory. Queue słusznie odmawia ponownego dodania, nie wykonano duplikatu. Nowy builder nadal przepuszcza syntetyczne pomocnicze canaries i diagnostykę (A2-C-001/004); brak twierdzenia o ujawnieniu realnej treści prywatnej w publikacji C.
- **D:** A4-D-001 — opcje parsera zmieniają wynik bez zmiany tożsamości receptury; A4-D-002 — deterministyczny wyścig symlink omija local_roots. Test otwiera wyłącznie syntetyczne pliki audytu.
- **E/ChatADHD:** A4-E-001 — deklaracja nieużywanego adaptera innego źródła usuwa ostrzeżenie aktywnego planu; A4-CH-MOD001 — cache modeli traci metadata zależnie od poprawnego opakowania i usuwa uszkodzone źródło; A4-CH-VIEW001 — GraphView narzuca domenowy filtr widoczności bez wejścia polityki. Nowa próba samego whitespace potwierdza stary próg parsera A3-IMP-CH004.
- **Watchdog:** A4-WD-001 — schema usuwa wybraną metodę; A4-WD-002 — manifest/eksport ponownej analizy gubią braki i flagi wejścia; A4-WD-003 — sprzeczne obserwacje rozstrzyga niejawne last-wins.
- **IO Matrix:** A4-CKP-001 — kolejność kluczy mapy zmienia stabilność capture; A4-CKP-002 — eksport pomija rzeczywisty settleMillis. Rozszerzono CKP-A-012 o opóźnione wklejanie i fallback do zmienionego celu.
- **LEM:** A4-LEM-001 — dwa requesty jednego eksperymentu używają różnych syntetycznych poświadczeń po zmianie ustawienia; A4-LEM-002 — spóźniona odpowiedź starego rejestru wraca po clear/rekey. Nie mierzono realnych kont ani kosztu.

AGEDS dostarczył 25 dodatnich kontroli nowych ścieżek: uporządkowane i wersjonowane referencje, jawna niepełność, wznowienie pagera i odrzucanie spóźnionych zdarzeń audio. Brak nowego naruszenia nie został zastąpiony sztuczną hipotezą. R42 zastosowano do decyzji i konsumentów; sześć wyjątków nie stało się ani polowaniem na literały, ani zwolnieniem całych plików.

## Wykonane bramki

| Zestaw | PASS | FAIL | BLOCKED | Uwaga |
|---|---:|---:|---:|---|
| B native, 23 kryteria | 18 | 3 | 2 | 3 PASS reprodukcji; 45 procesów Runtime |
| C, 28 kryteriów | 22 | 6 | 0 | 7 PASS reprodukcji; credential 10 przypadków wewnątrz jednej bramki |
| D, 32 kryteria | 22 | 2 | 8 | Te same kryteria sprawdzone z main i B4, nie 64 niezależne testy |
| E4, 20 kryteriów | 15 | 2 | 3 | 2 PASS reprodukcji; finalny oracle po peer review |
| Python ChatADHD, 14 | 11 | 3 | 0 | 3 PASS reprodukcji |
| Istniejące reducery/SSR, 12 | 9 | 0 | 3 historyczne | Jedna bramka braku E zastąpiona nowym PASS; nie jest bieżącą blokadą |
| IO Matrix host, 23 | 17 | 6 | 0 | 6 PASS reprodukcji |
| AGEDS host, 25 | 25 | 0 | 0 | Kontrakty, nie Android/device |
| LEM host, 9 | 6 | 3 | 0 | 3 PASS reprodukcji, 2 kontrakty i kontrola sieci |
| Watchdog, 11 | 8 | 3 | 0 | 3 PASS reprodukcji; rzeczywisty export handler, bez middleware/HTTP |

[results-index.json](results-index.json) wskazuje finalne receipts i kategorie. Nie podajemy łącznego procentu PASS produktu: reprodukcja PASS potwierdza wadę. Wczesne błędy harnessu zachowano i wyłączono z końcowego rozliczenia. Odrzucono podejrzenie błędu samej relokacji C — pierwszy test zmieniał także serializację planu; izolowana relokacja przechodzi. Korekta SSR selektora wierszy i rozdzielenie testu uprawnień E są opisane w peer review.

## Pokrycie i środowisko

| Repo | Przed pass4 | Nowe pliki z zakresami | Teraz / mianownik | Bez nazwanych zakresów |
|---|---:|---:|---:|---:|
| ChatADHD | 94 | 6 | 100/432 | 332 |
| Watchdog | 54 | 11 | 65/266 | 201 |
| IO Matrix | 47 | 9 | 56/233 | 177 |
| AGEDS | 39 | 6 | 45/63 | 18 |
| LEM | 24 | 0 | 24/24 | 0 plików, nadal luki zachowań |
| Historyczny standalone loom | 9 | 0 | 9/9 | 0; bez nowego skanu/backlogu aktywnego kernela |
| Łącznie | 267 | 32 | **299/1027** | **728** |

To nazwane funkcje/linie, **nie całkowicie zweryfikowane pliki**. Przyrost do main przyznano tylko przy zgodności badanych bajtów pliku z przypiętą bazą. Nowe moduły D/E i skrypty badawcze C mają osobne mianowniki. [coverage-index.json](coverage-index.json) zawiera pełną listę pozostałych ścieżek i odsyła do nieprześledzonych wywołań; [coverage-validation.json](coverage-validation.json) sprawdza zakresy na właściwych SHA.

Odblokowano miejsce przez usunięcie odtwarzalnych obiektów i porzuconych plików kompilatora, zachowując archiwa i receipts. Wykonano rzeczywisty selektywny native build B4 z manifestem 9 obiektów i 110/110 eksportów C ABI; nie jest to pełny nowy CMake/CTest ani sanitizer. Dodano wariant odbioru świeżego pełnego buildu, ale go nie wykonano. Kotlin/JVM i realne biblioteki transportowe działały na hoście; Android, preferences/lifecycle/DAO i urządzenie mają jawne granice fixtures. Nie powtarzano starego Room ani blokady emulatora bez nowego warunku.

**Chromium działa:** świeże uruchomienie i odczyt DOM PASS, więc dawne EPERM nie uzasadnia dalszego pomijania przyszłego UI. Nie zastępuje to brakującego komponentu E. Fixture nie jest rozmową użytkownika, pomiarem modelu ani badaniem klinicznym.

## Artefakty i następny odbiór

[Wykonywalny indeks](../../../../tools/ecosystem-audit-2026-10-09/pass4/test-index.json) ma runner `run_index.py` przyjmujący checkout/SHA i jawne zależności; instrukcje w [REPRODUCE.md](REPRODUCE.md). Indeks przetestowano trzema własnymi testami i rzeczywistym uruchomieniem Watchdog; FAIL produktu pozostaje kodem 1. Pakiety modułowe publikowano kolejno na własnej gałęzi z `[skip ci]`. Produkt, schematy, główne STATE/INDEX i main pozostają bez zmian. Prywatne repo nie były nowymi wejściami tego publicznego pakietu; ich wcześniejsze ograniczenia pozostają w prywatnym zakresie.

Dokładny punkt wznowienia: [RESUME.md](RESUME.md). Najpierw konkretny naprawiony SHA dla bramek bezpieczeństwa, tożsamości i provenance, potem brakujące połączenia D/B/E; równolegle kolejne nieprześledzone ścieżki z mianownika. Nie ponawiać obecnych przebiegów bez nowego kodu, warunku lub hipotezy.
