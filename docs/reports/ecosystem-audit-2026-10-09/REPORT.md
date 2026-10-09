# Audyt filozofii ekosystemu klb-t — 2026-10-09

Etap A zamknięty dla opisanych zakresów. Odnaleziono 7 dostępnych repozytoriów (6 publicznych, 1 prywatne); publicznie zapisano 65 ustaleń: **48 naruszeń, 8 dopuszczalnych mechanizmów, 4 niepewne i 5 już naprawionych**. Pięć naruszeń dotyczy zachowanego standalone loom; nie są automatycznie backlogiem aktywnego kernela. Prywatnych ustaleń nie włączono do tych liczników.

Raport ocenia aktualne main na niezależnych SHA, nie dawną kolejkę. Kod produktu, wspólne schematy, główne STATE/INDEX i cudze gałęzie pozostały niezmienione. Nie uruchomiono modeli ani płatnego CI. Publikacja jest na `gpt/ecosystem-audit-2026-10-09`; checkpointy zapisano w trakcie prac.

## Zakres i mianowniki

| Repo / bazowa gałąź | SHA main | Wpisy drzewa | Skan kodu / kwalifikowane | Semantycznie: pliki z zakresem / produkt | Naruszenia / mechanizmy / niepewne / naprawione |
|---|---|---:|---:|---:|---:|
| [chatadhd](chatadhd/report.md) / main | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` | 3158 | 619/621 | 24/432 | 11 / 1 / 1 / 2 |
| [Watchdog-JH16](Watchdog-JH16/report.md) / main | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` | 603 | 282/284 | 25/266 | 9 / 1 / 0 / 1 |
| [Custom-Keyboard-Pro](Custom-Keyboard-Pro/report.md) / main | `192f820f2d8c653768237e1bff3a1a6d950d69c0` | 461 | 252/252 | 28/233 | 7 / 2 / 2 / 1 |
| [AGEDS](AGEDS/report.md) / main | `9c1d513bc19d177bd324d7506a21fbab98c2e268` | 370 | 76/76 | 20/63 | 9 / 2 / 0 / 1 |
| [LEM-Workbench](LEM-Workbench/report.md) / main | `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456` | 66 | 27/27 | 24/24 | 7 / 1 / 0 / 0 |
| [loom](loom/report.md) / main | `0b23fa64c1de955c947349feef7bb67c05763f97` | 21 | 9/9 | 9/9 | 5 / 1 / 1 / 0 |
| [ar-drafty — raport prywatny](https://github.com/klb-t/ar-drafty/blob/754aba77e6d3bbc3d6c8ddb8edfe75379378e147/docs/reports/ecosystem-audit-2026-10-09/report.md) / main | `d8af2dd022dacb2f068074158d83e566813592d6` | w raporcie prywatnym | w raporcie prywatnym | w raporcie prywatnym | niepublikowane tutaj |

Pełna paginacja połączonego GitHub: owner=klb-t, offset 0/100, po 100 pozycji → 7/0; dodatkowy spis bez owner → 7/0. To pełność **w granicach uprawnień połączenia**, nie dowód nieistnienia innych repo. Receipt: [discovery.json](discovery.json).

Publiczne drzewa: **4679 wpisów**. Skan: **1265/1269 kwalifikowanych plików kodu**, 4 kwalifikowane pliki wyłączone regułą ochrony treści. Razem 299582 kandydatów, nie naruszeń. JSON, zasoby, testy, vendor, pliki generowane, dokumentacja i zachowana historia są osobnymi klasami w inwentarzach. Skan nie czyta całej ich zawartości i nie analizuje całej historii Git. Blind/holdout, prywatne korpusy, sekrety i request bodies badań pozostają poza odczytem.

Pokrycie semantyczne dotyczy wymienionych plików/zakresów, nie wszystkich ich metod. Dla chatadhd to 24 pliki produktu + 1 plik danych + 10 dokumentów w głównym manifeście; źródła użyte przez testy mają dodatkowe hashe w receipts. Klasyfikację ścieżkową skanera skorygowano jawnie w repo reports: np. ExperimentRunner.kt to produkt LEM, nie samodzielne badanie. Różne mianowniki manualne i skanera nie są zamieniane. [coverage-index.json](coverage-index.json) i `*/coverage.json` wskazują dokładne listy; `*/scan/inventory.jsonl.gz` zawierają każdy wpis drzewa.

## Najważniejsze potwierdzone zachowania

- **chatadhd:** zwykłe ścieżki chat/semantic worker nie mają tego samego guardu co explicit prepared reply (CH-004/005); legacy analiza używa stałego promptu/prefixu i bezpośrednio dopisuje inferencje (CH-002/006); błąd przetwarzania może oznaczyć rekord jako przeanalizowany (CH-011). Nie twierdzimy, że cały nowy MethodGraph/GraphPacket ma te wady.
- **Watchdog:** templateId nie wybiera treści, request params giną w adapterze, a missingPolicy=exclude nie wyklucza brakującego wiersza (WD-001/002/003). Reguła ochrony liczb pomija znak (WD-011), lecz wynik nadal jest PROPOSED — nie ma dowodu automatycznej akceptacji tej inferencji.
- **Custom-Keyboard-Pro / IO Matrix:** built-in task wygrywa z personalnym task o tym samym ID, uszkodzony katalog uruchamia ręczny fallback, część Policy Matrix nie steruje plannerem (CKP-A-001/002/003).
- **AGEDS:** job nie przypina pełnej receptury, selekcja ma zaszyty priorytet, a ScanLimits mimo konsumenta w silniku nie przechodzą przez zbadane Android/HTTP entrypointy (EA-AGEDS-002/003/005).
- **LEM-Workbench:** ExperimentConfig nie steruje runnerem, wyniki nie zachowują pełnego raw evidence, registry pomija paginację, a brak migracji Room wybiera destrukcyjny fallback (LEM-001/003/004/005).
- **Standalone loom:** natywnie odtworzono niespójny licznik/kolejkę, ciche konflikty danych i zmianę nieznanej wersji schematu. Repo jest zachowanym compatibility baseline, nie tożsamym z aktywnym `chatadhd/loom`.

## Co działa i czego nie otwieramy ponownie

CH-012: model użytkownika nie jest ponownie resetowany do Haiku, a zmiana modelu resetuje licznik błędów. CH-013: runtime layers i generowanie z danych działają w przetestowanym helperze. WD-008: konfiguracja providerów oraz opt-in/budżet asystenta są konsumowane. CKP-A-008/010: parametry zadań i kaskada ustawień mają realne odczyty. EA-AGEDS-011: defaultPresetIds jest danymi i ma konsumenta. To scoped dowody; nie certyfikat całej aplikacji.

## Kryterium R42 i rozdzielenie twierdzeń

Źródłem jest [zapis wymagań na bazowym SHA](https://github.com/klb-t/chatadhd/blob/9e20f99ab27e7cd45e1892f83bf60fbe60db3de9/docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md) oraz bieżące zlecenie. Dokładny test: **could someone want to change it without changing the algorithm?** Jeśli tak, to dane/profil/ustawienie/preset. Sześć wyjątków: (1) nazwy kontraktu kod↔dane, (2) stałe standardów zewnętrznych, (3) własny słownik mechanizmu — dla użytkownika tłumaczony danymi, (4) kontraktowe formaty serializacji, (5) minimalny bootstrap do odnalezienia danych, (6) diagnostyka deweloperska. Nie dodano siódmego wyjątku. Mechanizm matematyczny sam jest algorytmem; próg/wybrana metoda badania pozostaje polityką. Testy i fixtures nie są produktem. Wyjątek jest uzasadniany lokalnie, nigdy nazwą całego pliku. Brak packa oznacza jawny błąd lub embedded pack wygenerowany z tych samych danych, nie ręczny ukryty default.

W findings rozdzielono źródło wymagania, obserwację, wpływ, interpretację oraz rekomendację. `owner_quote=null` znaczy brak cytatu, nie brak wymagania. Potwierdzenie ścieżką źródłową oznaczono oddzielnie od wykonanego testu; hipotez nie doliczono do naruszeń. Docelowe obiekty grafu są rekomendacją odwzorowania na istniejące R15/R33/R40/R41, nie nową architekturą. Rozpoznane alternatywy mają statusy; nie każde rozszerzenie jest już wdrożone.

## Wykonana weryfikacja

- Skaner: 12/12 testów PASS; wszystkie 6 skanów tym samym hashem skanera/reguł. Kandydaty mają stabilne ID, SHA pliku, regułę i zakres, bez treści źródłowej.
- chatadhd: 10/10 prób rzeczywistych jednostek Pythona z atrapami DB/HTTP i blokadą transportu; native layers 9/9 przypadków, 168/168 asercji; generator roundtrip 13 ciągów PASS. To nie pełny serwer/UI E2E.
- Watchdog: lint/typecheck, 53 istniejące testy i 6 prób audytowych PASS; transport modeli atrapowany.
- AGEDS: 6 prób PASS; początkowy brak zależności zapisany i usunięty w środowisku testowym.
- loom: db_compat_test, event_bus_test i 6 natywnych reprodukcji PASS, z istniejącym SQLite 3.47.2 z chatadhd o zapisanych hashach; dodatkowe SQL probes. Nie wykonano CMake/CTest/sanitizerów.
- CKP: walidator 3 profili PASS (wyłącznie struktura), testy Kotlin/Android zablokowane brakiem kompilatora. LEM: pełna analiza 24 plików Kotlin, bez uruchomienia Android/Gradle.
- Gate raportów: 65/65 unikalnych ID, 212/212 poprawnych strukturalnych lokatorów na SHA, 105 odrębnych plików źródłowych; 0 błędów/ostrzeżeń. To kontrola integralności raportu, nie dowód poprawności produktu.

Testy audytowe często potwierdzają wadliwe bieżące zachowanie. Ich PASS nie oznacza, że naprawa jest wykonana. Receipts i komendy są przy każdym repo. Pełnego CMake/CTest/Kivy, browser E2E, Android/device i całej macierzy uprawnień nie uruchomiono.

## Gałęzie i sesje B/C

Aktualny INDEX z 2026-10-05 odróżnia przyjęte W11/W6 i pozostałe integracje od przygotowania badawczego. [branch-review](chatadhd/branch-review.md) przypina cztery gałęzie badawcze: `0f52ac5`, `8e9d31a`, `ffe6443`, `62d1b05`; ich przejrzane nierównoważne przyrosty są w docs/tools, nie w runtime produktu. Intake jest przodkiem preparation, więc nie liczymy go jako drugiego niezależnego pakietu. Pełna [macierz integracji](chatadhd/branches-integration.md) obejmuje 134 refs: 26/26 dozwolonych niearchiwalnych divergent sprawdzono patchowo, 101 archiwalnych zinwentaryzowano tylko przez ancestry, chroniony ref wyłączono. Pełne metadane są w `chatadhd/branches-integration.json.gz`; 5 gałęzi jest całkowicie patch-equivalent. Pozostałe różnice są jawnie rozdzielone od statusu przyjęcia w INDEX. Archiwalne refy pozostają historią, nie automatycznym backlogiem.

B/C sprawdzone po zamknięciu zakresów o **2026-10-09 04:46:03 UTC**: obie wskazane gałęzie nie były reklamowane przez GitHub; polecenie zakończyło się kodem 0 z pustym wynikiem. **0 niezależnie zweryfikowanych poprawek B/C**, ponieważ brak dostępnego SHA. Nie jest to twierdzenie o nieistnieniu pracy nieopublikowanej. [bc-review.json](bc-review.json) podaje dokładne polecenie i warunek wznowienia. Nie odświeżano ich bezczynnie.

## Artefakty i dokładny punkt wznowienia

- [findings.jsonl](findings.jsonl): ujednolicone 65 ustaleń z plikami/liniami, SHA, dowodami, alternatywami, konsumentami, testami, ryzykiem i zależnościami; oryginalne pełne raporty w katalogach repo.
- [Macierz wymagań](requirements-matrix.md), [rejestr decyzji](decisions.jsonl), [mapa data → graph → runtime → UI](data-graph-runtime-ui.md), [kolejność backlogu](backlog.md), [relacje projektów](relationships.json).
- [Skaner i instrukcja](../../../tools/ecosystem-audit-2026-10-09/README.md) oraz [komendy testów](REPRODUCE.md) są odrębnym narzędziem audytu. Duże surowe candidates są odtwarzalnym wynikiem pośrednim: SHA-256/rozmiar i sposób odtworzenia zapisano w `*/scan/retention.json`; nie wstawiono ich do Git. Wszystkie findings, inventory i podsumowania zapisano.
- [Prywatny raport ar-drafty](https://github.com/klb-t/ar-drafty/blob/754aba77e6d3bbc3d6c8ddb8edfe75379378e147/docs/reports/ecosystem-audit-2026-10-09/report.md) pozostał w prywatnym repo na tej samej nazwie gałęzi; publicznie tylko identyfikator, baza i status publikacji.

Wznowienie implementacji: najpierw CH-004/005 — offline fixture zwykłego ChatEngine send oraz semantic worker z deny/opt-in, przechwycenie transportu, porównanie tej samej konfiguracji z prepared reply. Następnie rzeczywiste konsumenty WD-001/002/003, CKP-A-003, EA-AGEDS-003 i LEM-001; kryteria są w backlogu. CKP-A-012 wymaga najpierw opóźnionej odpowiedzi po zmianie fokusu — pozostaje hipotezą. Kompletna macierz eksperymentów i Basic/Advanced/Expert wymaga osobnego roundtrip testu, nie samej encji JSON.

Wznowienie dalszego audytu: z inwentarzy wybierać nieprześledzone zakresy chatadhd (408/432 plików bez zadeklarowanego przeglądu w głównym manifeście), Watchdog (241/266), CKP (205/233), AGEDS (43/63). W LEM i standalone loom wszystkie wymienione źródła produktu przejrzano, lecz runtime Android/full integration pozostaje luką. Nie używać dawnych 695 grup data-in-code jako nowego backlogu. Gdy B/C dostarczą konkretny SHA, wykonać tylko związane testy akceptacyjne i oznaczyć: naprawa / przeniesienie / maskowanie / nadal niepewne.
