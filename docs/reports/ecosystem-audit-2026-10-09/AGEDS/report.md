# AGEDS — audyt filozofii, 2026-10-09

Baza: `klb-t/AGEDS`, publiczne `main`, **9c1d513bc19d177bd324d7506a21fbab98c2e268**. Nie zmieniono produktu, schematów ani głównych indeksów. To wynik audytu bieżącego zlecenia, a nie nowa architektura konkurencyjna wobec R15/R33/R40/R41.

## Wynik i mianowniki

**9 potwierdzonych naruszeń**, **2 dopuszczalne mechanizmy**, **1 już naprawione**. Naruszenia oznaczają rozbieżność z bieżącym wymaganiem właściciela; nie oznaczają, że poprzednie zadanie AGEDS deklarowało już pełną implementację tych reguł.

Inwentaryzacja: **370/370 śledzonych plików** w podanym drzewie. Wspólny skaner sprawdził **76/76 kwalifikujących się plików kodu**, 11 113 linii, generując 16 588 kandydatów do przeglądu. Kandydaci nie są liczbą błędów. Skaner używa AST dla Pythona i heurystyk dla pozostałych języków, nie dowodzi podłączenia danych.

Analiza semantyczna objęła wskazane zakresy **20/63 plików powierzchni produktu**. Lista plików, zakresy linii, hashe i pozostały mianownik są w `inventory.json` oraz `coverage.json`. Nie twierdzę, że pozostałe 43 pliki są zgodne. Powierzchnia produktu obejmuje 61 plików kategorii `product` skanera oraz dwa zasoby Androida (`AndroidManifest.xml`, `styles.xml`) sklasyfikowane przez skaner jako `data`.

| Klasa ręcznego spisu | Pliki | Zakres |
|---|---:|---|
| Produkt i jego zasoby | 63 | Serwer, core Kotlin, Android |
| Testy i fixtures | 100 | Oddzielone od R42 dla produktu |
| Zachowana historia | 112 | Dokumenty i koordynacja archiwalna; nie są aktywnym backlogiem |
| Dokumentacja | 45 | Aktualne instrukcje i kontrakty |
| Koordynacja i receipts | 14 | Stan/odbiór, bez przypisywania wyników historycznych tej sesji |
| Build, tooling, konfiguracja | 28 | Oddzielny zakres |
| Vendor / bootstrap wygenerowany | 4 | Gradle wrapper |
| Prototyp badawczy | 4 | Niepodłączony on-device ASR |

## Najistotniejsze ustalenia

| ID | Klasyfikacja | Konkretny skutek |
|---|---|---|
| EA-AGEDS-001 | Naruszenie | Model/device/compute type są podłączone przez env, ale VAD i word timestamps wymusza kod adaptera. Receptura nie jest profilem danych. |
| EA-AGEDS-002 | Naruszenie | Job nie przypina żądanego modelu/receptury. Dwa zgłoszenia tego samego artefaktu dają jeden job, którego priorytet zostaje zmieniony. |
| EA-AGEDS-003 | Naruszenie | Silnik konsumuje limity, CLI część udostępnia, lecz Android i HTTP zawsze uruchamiają default. |
| EA-AGEDS-004 | Naruszenie | Słowniki nagłówków, rozpoznawanie numerów i nazw plików to dane domenowe w kodzie. Hipotezy są jednak oznaczone `unverified`; nie wykazano promocji do faktu. |
| EA-AGEDS-005 | Naruszenie | Android nadaje priorytet według kolejności wyboru. Dodatkowe wagi istnieją w funkcji, ale ten caller ich nie włącza. |
| EA-AGEDS-006 | Naruszenie | Graf dowodów istnieje; ustawienia env, endpointy, profile, receptury i warstwy defaultów nie mają wykazanego mapowania do grafu i jego konsumentów. |
| EA-AGEDS-007 | Naruszenie | Wykluczenia zaznaczenia są pamięciowe; ponowne załadowanie seeda przywraca defaulty. Brakuje oddzielnego trwałego `never`. |
| EA-AGEDS-008 | Naruszenie | Teksty UI i fallback endpointu są w Kotlinie. Ręczna zmiana endpointu faktycznie dochodzi do klienta HTTP. |
| EA-AGEDS-009 | Naruszenie | Ranking BM25, tokenizer, snippet i sufity wyszukiwania to stała polityka kodu; przekroczenia są jawnie zgłaszane. |
| EA-AGEDS-010 | Dopuszczalny mechanizm | Statusy lease, identyfikator schematu i SHA-256 spełniają wyjątki R42 3/1/2. Nie usuwać fencing ani kontraktu jako „danych”. |
| EA-AGEDS-011 | Już naprawione | `defaultPresetIds → priorityPresetIds → selectPriority → activePresets → selectedPhones` jest podłączone. Dawnego zadania usuwania nazw grup nie otwierać ponownie. |
| EA-AGEDS-012 | Dopuszczalny mechanizm | Upload wymaga jawnego przycisku z pokazanym zakresem i docelowym serwerem. Sam skan nie oznacza automatycznego wysłania wszystkich plików. |

Pełne lokatory repo/SHA/plik/linie, dowody, wpływ, alternatywy, obiekty docelowe, konsumenci, testy akceptacyjne, ryzyka i zależności są w `findings.jsonl`. Opisy docelowych obiektów są rekomendacją audytora, nie cytatem ani przypisaną właścicielowi nową decyzją. Nie zakładają jednej bazy ani jednego schematu z Loom.

## Macierz wymagań

| Wymaganie | Pokrycie w zbadanych ścieżkach | Dowód / luka |
|---|---|---|
| R42: dane poza kodem, sześć wyjątków | Częściowe | Seed jest danymi; ASR, wagi, limity, słowniki, UI i ranking wymagają pracy. Wyjątki kontraktu/standardu/lifecycle uzasadnione osobno. |
| Alternatywy jako strategie/profile | Częściowe | Adapter ASR zastępowalny programowo; wybór endpointu działa. Kolejka/ASR/parser/search nie mają kompletnych profili. |
| R15: wszystkie używane dane w grafie | Niepełne | Relacje dowodowe/runs/jobs są; ustawienia i profile poza mapowaniem. |
| R40: default / override / disable / never | Niepełne | Seed ma defaulty i wybór ręczny, ale brak trwałej warstwy wykluczeń i wyjaśnienia źródła wartości. |
| R41: metody, wersje, parametry, produced_by | Częściowe | Run zapisuje metadane ASR; metoda/receptura nie jest przypiętym profilem do zlecenia. |
| R33: eksperymenty opt-in, scope/frequency/volume/budget | Brak wykazanej implementacji | To jest kolejka zwykłych transkrypcji, nie system eksperymentów. Nie stwierdzono nieautoryzowanych eksperymentów ani płatnych wywołań. |
| Basic kompletny preset; Advanced/Expert te same dane | Brak wykazanej implementacji | W badanych UI istnieją konkretne wybory, nie pełny model współdzielonych workflow/presetów. |
| Ustawienie → konsument | Mieszane | Env ASR i endpoint: działa. Scan limits Android/API: pominięte. Seed groups: działa. |
| Jawność epistemiczna | Działa w zbadanej części | Source observations mają `unverified`; raw/ASR/adnotacja/cytat rozdzielone. Nie stanowi to oceny każdego parsera. |

## Relacje projektów i gałęzie

AGEDS jest samodzielnym Evidence Workbench. `ECOSYSTEM.md` określa ChatADHD jako możliwy interfejs, iOmatrix jako partnera dwukierunkowego, Loom/LEM jako możliwe źródła metod reprezentacji, WatchDog jako partnera metod pochodzenia. Są to kierunki koncepcyjne: nie utożsamiać repozytoriów ani nie deklarować wdrożonych adapterów. Prototyp JNI zachowano w `research/prototypes/on-device-asr`, bez działającej integracji runtime.

| Gałąź | SHA | Ahead / behind względem main | Ocena |
|---|---|---|---|
| `codex/ageds-completion-20261002` | `469b1fba3b3dd70df66805874827c9ae09e3237f` | 0 / 3 | Przyjęta |
| `codex/ageds-foundation-20260930` | `b22881eb5a60f662fb6e14153e1fbe338d6ab687` | 0 / 70 | Przyjęta |
| `codex/ageds-night-20261001` | `b6abe86ec1cdcd4ca8669be359ddb25422a51cc2` | 0 / 14 | Przyjęta |
| `fix/standalone-android` | `87a91627cf4f3c5ebd0aad886edd4a632cd50dc7` | 4 / 78 | Commity nie są przodkami main, ale handoff i commit squash `1e71cf8b0163ba194c81e3c713138eac725f7319` wskazują przyjęcie funkcjonalności. Nie jest to nowa kolejka czterech zadań. |

Brak reklamowanych gałęzi B/C w AGEDS w tym odczycie. Niezależny odbiór ich przyrostów w chatadhd należy do raportu zbiorczego na konkretnych SHA; ten raport nie przypisuje im żadnej naprawy AGEDS.

## Weryfikacja i wznowienie

Uruchomiono **6/6 lokalnych probes**, bez sieci, realnego modelu ani płatnego CI. Sprawdzają wymuszone opcje adaptera, domenową klasyfikację, rzeczywiste limity skanu, deduplikację kolejki, brak konfiguracji w eksporcie i twardy limit wyszukiwania. Pierwsza próba importu workera zatrzymała się na braku `anyio`; receipt zachowano. Końcowy probe adaptera wykonuje niezmieniony AST jego klasy i funkcji pomocniczych, z atrapą faster-whisper. Nie jest to test całego workera ani realnego ASR. `pytest` również nie jest zainstalowany; nie przypisuję sobie dawnych 429 testów z handoffu. Android/JVM/browser/device nie były uruchamiane.

Odtworzenie ze wskazanego checkoutu:

```sh
python path/to/AGEDS/audit-tools/semantic_probes.py --repo path/to/AGEDS-checkout --output receipt.json
python path/to/scanner/scan.py --repo path/to/AGEDS-checkout --repo-name klb-t/AGEDS --revision 9c1d513bc19d177bd324d7506a21fbab98c2e268 --base-branch main --visibility public --output scan-output
```

Punkt wznowienia: najpierw zapinać recepturę ASR/job i budżety do konsumentów (001–003), potem kolejkę/defaulty/graf (005–007). Dalszy audyt semantyczny: `server/app/importers/*`, pozostałe fragmenty `scanner.py`, `read_pages.py`, `verified_*`, `archive.py`, `exchange*`, Android `CitationWorkspace.kt`, `SourceScanEngine.kt`, parsers i moduły JS. Ich kandydaci są już w `scan/candidates.jsonl`; nie trzeba ponownie odkrywać repo ani odtwarzać dawnego backlogu.

Artefakty: `coverage.json`, `inventory.json`, `findings.jsonl`, `decisions.json`, `flow-map.json`, `backlog.json`, `branches.json`, `scan/`, `audit-tools/semantic_probes.py` oraz oba receipts. Publikację checkpointu wykonuje koordynator zadania A; ten podaudyt nie wykonuje samodzielnego push.
