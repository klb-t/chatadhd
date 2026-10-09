# PASS4 — punkt wznowienia

PASS4 zamknięty w przypiętym zakresie. Autorytatywny ostatni stan to ten katalog, nie historyczny główny REPORT.md ani pass2/pass3. Własna gałąź: `gpt/ecosystem-audit-2026-10-09`. Commity modułów i weryfikacja drzewa będą w PUBLICATION.json; nie zmieniać historycznych receipts.

1. Przeczytać REPORT.md, acceptance-queue.json, backlog.json i coverage-index.json; jawnie fetch B/C/D/E oraz własną gałąź. Obecne piny są w baselines.json, E_after_start zastępuje początkowy brak E. Nie wykonywać starych testów bez różnicy kodu/warunku/hipotezy.
2. Po zmianie D: A4-D-002 bezpieczny open wiążący uprawnienie z obiektem (także wariant podmiany rodzica), potem A4-D-001 wersja efektywnej receptury. Zachować zero-read attach i obecne dodatnie kontrole. Nie modyfikować produktu w A.
3. Po zmianie B: CH-RES-N001/N002 odbierają zwykłych konsumentów bez narzucania materializacji; A4-B-RES001 relokacja; A3-DISC-001 raw wejście z tym samym packetem C4 oraz receipt-backed sentinel. Świeży build z udokumentowanego SHA; nie używać starego .so tylko dlatego, że się ładuje.
4. Po zmianie C: A4-C-001/002 oraz public builder/diagnostyka A2-C-001/004. Credential fixture naprawiony: nie otwierać go jako starej blokady. A2-C-002/003 są niezmienionymi wcześniejszymi otwartymi problemami, nie nowymi odkryciami pass4.
5. Po zmianie E: source-specific capability; known/unknown profile fields; rzeczywiste D selector/source_version→E ObjectRef; produkcyjny UI. Chromium jest dostępny, ale hooka nie było na `3eaac295…`. Zachować izolowane E4-06a/b i native resolver/exclusion. Pole→workflow runtime pozostaje konkretnym brakującym kontraktem.
6. Pozostałe repo, bez czekania na B–E: CKP realny CallEngine EOF/timeout i MIME pliku niepochodzącego z mikrofonu; Watchdog trwałość przypiętych źródeł manifestu i prawdziwy HTTP/autoryzacja; AGEDS AndroidRangeAudio/SourceScanUi oraz niedostępne źródła; LEM fizyczna trwałość ustawień przy rekey/clear i brak publicznego raw-artifact resolvera. Wszystkie ścieżki bez zakresów wymieniono w coverage-index.json. W ChatADHD priorytetem modele/cache → UI i konfiguracja do kolejnych wykonawców, nie historyczny standalone loom.

Braki środowiska odróżniać od kontraktu: nie wykonano device/OEM, pełnego nowego B4 CTest/sanitizer ani crash-resume indeksowania. Kotlin host, native selektywne testy oraz browser-environment wykonano. Nie powtarzać obecnych wyników wyłącznie w celu zwiększenia licznika.

Publiczne dane i fixtures są syntetyczne lub już publiczne; nie czytać prywatnych archiwów do tuningu ani nie publikować fragmentów. Zero płatnych wywołań/CI. Publikacja pakietów nie oznacza wysłania wiadomości innym sesjom. Nie wymagać ich reakcji, aby kontynuować własny zakres.
