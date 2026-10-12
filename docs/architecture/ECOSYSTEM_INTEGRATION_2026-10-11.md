# Integracja ekosystemu — synteza audytu, priorytety migracji, plan cyklu (T9, S2)

Napisane 2026-10-12 w sesji S2 (`docs/reports/THREADS_2026-10-11.md` §3 T9, §5 P-F zadanie A, §9;
nazwa pliku zgodnie z tabelą §9). Definicja paradygmatu, do której ten plan się odwołuje, jest
w `EVERYTHING_IN_THE_GRAPH_2026-10-11.md` (dalej: EITG, niezmienniki G1–G10). Briefy dla agentów
Sonnet są w `docs/reports/t9-briefs/`. Dokument nadrzędny: `DUCH_GRAFU_2026-10-11.md`.

## 0. Cel, wejścia, zakres

**Cel.** Zadanie A promptu P-F: (1) priorytety migracji danych do profili per repozytorium
z uzasadnieniem wpływu na użytkownika, (2) definicja paradygmatu (→ EITG), (3) plan integracji
ekosystemu w cyklu rozważenie → implementacja → optymalizacja → iteracja z briefami (zakres,
pliki, kryterium).

**Wejścia** (sekcja 4 `THREADS`, z korektą lokatorów):

| Wejście | Gdzie naprawdę jest |
|---|---|
| audyt ekosystemu 2026-10-09 (`README.md`, `REPORT.md`, `backlog.md`, `backlog.json`, `findings.jsonl`, `relationships.json`, `data-graph-runtime-ui.md`, `pass4/REPORT.md`, `pass4/TASK.md`, `pass4/findings.jsonl`, `pass4/backlog.json`, raporty per repo) | gałąź `origin/gpt/ecosystem-audit-2026-10-09`, katalog `docs/reports/ecosystem-audit-2026-10-09/`; **nie ma go na gałęzi roboczej**; `pass4/backlog.md` nie istnieje — `backlog.md` leży w korzeniu audytu |
| inwentarz danych w kodzie (`thread-1…11.md`, `migration-status.md`, `README.md`) | gałąź `origin/gpt/data-profiles-2026-10-04`; na gałęzi roboczej jest tylko `thread-12.md` |
| `CLAUDE.md`, `AGENTS.md`, `OWNER_REQUIREMENTS` R1–R53, `GRAPH_ENTRY_PARTIES_ECONOMY`, `LOOM_CONCEPTUAL_MODEL`, `INDEX.md`, `THREADS`, `DUCH_GRAFU`, `ECOSYSTEM.md` | gałąź robocza |

Nie czytano transkryptów, dowodów ZIP, kodu produktu poza sprawdzeniem istnienia ścieżek
(`loom/data/*`, strażniki, generatory), ani repozytoriów innych niż `chatadhd`. Oceny innych
repozytoriów pochodzą wyłącznie z audytu i są tak oznaczone.

**Czym ten dokument nie jest.** Nie jest nowym audytem (`THREADS` §7 p. 1: czytanie robią agenci
z gotowymi lokatorami), nie zmienia kodu, nie uruchamia agentów, nie wykonuje migracji.

## 1. Synteza audytu — cztery przebiegi czytane przez filozofię

### 1.1 Liczby z mianownikami

| Co | Liczba | Mianownik i zastrzeżenie |
|---|---|---|
| repozytoria | 7 (6 publicznych + `ar-drafty`) | pełność w granicach uprawnień połączenia GitHub, nie dowód nieistnienia innych |
| ustalenia pass1 (`findings.jsonl`) | 65: 48 naruszeń, 8 mechanizmów, 4 niepewne, 5 naprawionych | 5 naruszeń dotyczy zachowanego standalone `loom` i nie są backlogiem aktywnego kernela |
| ustalenia pass4 (`pass4/findings.jsonl`) | 27 wierszy: 15 nowych naruszeń, 6 rozszerzonych ID, 5 mechanizmów, 1 niepewne | pass2 i pass3 (19 + 9 według `THREADS`) nie były w zakresie sekcji 4; ich ID pojawiają się w pass4 jako rozszerzone (A2-C-*, A3-*) |
| pokrycie semantyczne | 299 / 1027 plików z nazwanymi zakresami; **728 bez** | to nazwane funkcje i linie, nie zweryfikowane pliki |
| dane w kodzie (inwentarz 2026-10-04) | 695 grup / 559 plików / 39 496 kandydatów | audyt sam: „nie używać 695 grup jako nowego backlogu”; wątek 11 (213 grup): 187 przeniesione, 19 częściowe, 5 zachowany format, 1 zachowany algorytm, 1 przekazane |
| pozostałe grupy `open` w inwentarzu | wątek 1: 127 (prompty, metody), wątek 3: 51 (kontekst), wątek 5: 52 (import), wątek 10: 81 (HTTP/UI), wątek 4: 27, wątek 6: 31, wątek 7: 9 | tylko `chatadhd`; statusy sprzed integracji 2026-10-05 i gałęzi B — część już przeniesiona |
| strażnik literałów R42 | mechanizm gotowy (41 testów); pełny audyt repo **negatywny**: 68 558 niesklasyfikowanych, 153 zablokowane (`STATE.md`) | kandydaty leksykalne ≠ przejrzane grupy; nie są automatycznie zakazanymi danymi |

### 1.2 Osiem wzorców zamiast 92 ustaleń

Ustalenia z pięciu aktywnych repozytoriów (cztery języki: C++/Python/TypeScript, TypeScript,
Kotlin, Python+Kotlin, Kotlin) układają się w osiem wzorców. Kolumna G wskazuje niezmiennik EITG §3.

| Wzorzec | Co się dzieje | Ustalenia | G |
|---|---|---|---|
| **A. Dane istnieją, konsument ich nie czyta** | encja/plik/profil jest, zmiana danych nie zmienia zachowania | LEM-001, EA-AGEDS-002/003, CKP-A-003/011, CH-003/004, WD-003, A4-WD-001, 19 pozycji `partial` wątku 11, trzy domeny bootstrapu „gotowe dane, okablowanie oczekuje” | G6, G10 |
| **B. Polityka w kodzie** (R42 wprost) | progi, wagi, prompty, adresy, modele, słowniki w źródle | CH-001/002/007, WD-001/005/009/010, CKP-A-001/002/005/007, EA-AGEDS-001/004/005/009, LEM-002, LOOM-001/004, DIC-0009…0014 | G6 |
| **C. Druga pamięć** | stan trwały poza grafem, bez węzła i roli | WD-004, EA-AGEDS-007, CH-009, LEM-007, EA-AGEDS-006, CKP-A-006, A4-CH-MOD001 | G1, G5 |
| **D. Utrata pochodzenia albo surowych danych** | wynik nie wskazuje metody/wersji/parametrów; surowa odpowiedź znika; tożsamość zależy od kolejności kluczy | LEM-003, CH-006, A4-WD-002, A4-WD-003, A4-D-001, A4-C-001/002, A4-CKP-001/002, A4-B-RES001 | G2, G3, G8 |
| **E. Destrukcyjne albo ciche fallbacki** | błąd staje się sukcesem, brak migracji kasuje, nieznane pole blokuje znane | LEM-005, CH-011, LOOM-002/005, CKP-A-002, A4-CH-MOD001, A3-IMP-CH004, A3-WEB-001, LOOM-003 | G3, G7 |
| **F. Strażnik nie na zwykłej ścieżce** | ×10 i prywatność działają w przygotowanej odpowiedzi, nie w zwykłym czacie i workerze | CH-004, CH-005 | G10 |
| **G. Tekst UI w kodzie** | etykiety, komunikaty, motywy | WD-006, EA-AGEDS-008, LEM-006, CH-008, CKP-A-004 | G6 |
| **H. Granica bezpieczeństwa** | TOCTOU symlink, poświadczenia bez tożsamości operacji, wyciek przez pola pomocnicze | A4-D-002, A4-LEM-001/002, A2-C-001/004 | G6 |

Trzy obserwacje, których nie ma w audycie:

1. **Wzorzec A jest ważniejszy niż B.** Audyt liczy B jako najliczniejszy, ale z inwentarza wątku 11
   wynika, że 187 z 213 grup już przeniesiono, a mimo to CH-003/CH-004 pozostają: dane są, konsument
   nie czyta. Migracja, która kończy się na pliku danych, nie zmienia nic dla użytkownika. Dlatego
   każdy brief T9 kończy się na **konsumencie i teście A/B na przechwyconym żądaniu**, nie na packu.
2. **Wzorce C, D, E są niewidoczne, dopóki nie zabolą.** Polityka w kodzie (B) ogranicza, ale nie
   niszczy. Druga pamięć (C) i destrukcyjny fallback (E) niszczą bez ostrzeżenia (LEM-005 kasuje
   bazę, WD-004 gubi preferencje, A4-CH-MOD001 kasuje cache). To przesuwa je nad B w priorytetach §2.
3. **Pięć repozytoriów, te same wzorce, więc wspólna jest metoda, nie kod.** Audyt sam ma już
   postać briefu (`target_data_graph_object`, `consuming_paths`, `acceptance_tests`,
   `migration_risk`); briefy T9 ją przyjmują i dopisują tylko kolejność i niezmiennik G.

### 1.3 Co nie jest backlogiem

- **Naprawione** (nie otwierać): CH-012 (reset modelu), CH-013 (presety rozumowania), WD-008
  (dostawcy i budżet asystenta), CKP-A-008 (parametry AI), EA-AGEDS-011 (defaulty grup korpusu).
- **Dopuszczalne mechanizmy** (R42 kategorie 1–6; nie „polować na literały”): CH-014, WD-007,
  CKP-A-009/010, EA-AGEDS-010/012, LEM-008, LOOM-007, A4-B-M001, A4-D-M001, A4-E-M001, A4-CKP-M001,
  A4-AG-M001.
- **Standalone `loom`** (LOOM-001…006): zachowany baseline zgodności; nic z niego nie migruje.
  Odpowiedniki w aktywnym kernelu: LOOM-001 (próg 20 znaków) jest już danymi (DIC-0019
  `graph_ingest.pack#/defaults/min_input_codepoints`, DIC-0013 `open` w wątku 1 dla LLM); LOOM-002
  (ciche kolizje) sprawdza B-CH-03; LOOM-005 (bezwarunkowa wersja schematu) należy do T5/T7.
  Jedyna praca: węzeł lineage w samomodelu (B-ECO-01) i wzmianka w README standalone, że nie otwiera
  nowszych formatów (rekomendacja LOOM-005).
- **695 grup** jako liczba: martwa. Żywe są: ustalenia audytu, pozycje `partial` wątku 11 i grupy
  `open` wątków 1/3/5/10, które wchodzą do briefów B-CH-02/04/05 jako materiał, nie jako cel.
- **`ar-drafty`**: prywatne (`THREADS` §6); w grafie wyłącznie węzeł z kopertą wskazującą prywatny
  raport.

### 1.4 Czego audyt nie może powiedzieć i jak plan sobie z tym radzi

- **728 ścieżek bez nazwanych zakresów** (ChatADHD 332, Watchdog 201, IO Matrix 177, AGEDS 18).
  Plan nie udaje, że je zna. Faza iteracji (§3) konsumuje `coverage-index.json` audytu porcjami,
  a dwa strażniki (literały R42, druga pamięć G5) łapią nowe przypadki niezależnie od tego, czy
  ktoś je przejrzał.
- **Android, urządzenie, pełne E2E** nie były uruchamiane w żadnym repo. Każdy brief rozróżnia
  „test hosta” od „test urządzenia” i nie zalicza jednego za drugie.
- **Repozytoria już się przesunęły.** `STATE.md`: Watchdog 644/644 po A4-WD001 (`6be9aed`)
  i A4-WD003 (`f2b6b89`), WD-003/001/011 z pełnymi przebiegami; gałąź B `6d1231bd`; T1–T8 w toku.
  Dlatego **każdy brief zaczyna od ponownego sprawdzenia lokatora na bieżącym SHA** i kończy
  statusem w słowniku audytu: naprawione / przeniesione / zamaskowane / nadal wadliwe /
  niezweryfikowane.

### 1.5 Co już należy do T1–T8 (nie briefujemy ponownie)

| Ustalenie | Wątek |
|---|---|
| CH-004, CH-005 (strażnik ×10 i prywatność na zwykłej ścieżce) | T6 |
| CH-011 (błąd = `analysed`), masowe ponowienie, tryb ponawiania jako dane | T2 (natywnie), T7 (Python) |
| A4-CH-MOD001 (cache modeli), A2-CH-001 (`memory.json`), A2-CH-004 (`github_sync`) | T7 |
| A4-D-002 (TOCTOU symlink), raport ekonomii R50 | T4 |
| R44 nierozpoznane → węzły; duplikaty importu; A3-IMP-CH004 (próg białych znaków) jako przypadek testowy | T1 |
| widok wejściowy R45, dane ontologii, zalążek zerowy | T3 |
| inwentarz magazynów i kluczy, projekt obszaru „Magazyn i bezpieczeństwo”, kryptografia | T5 |
| porządki GitHub, README repozytoriów | T8 |

Uwagi do przekazania S1 (nie wymagają decyzji): T1 powinien objąć A3-IMP-CH004 jako przypadek
negatywny progu; A4-B-RES001 (relokacja w `catalog/scan.cpp`) dotyka tego samego kodu co T1
i T4 — brief B-CH-06 ma to jako zależność; T5 może przyjąć schemat węzła magazynu z EITG §6.

### 1.6 Krytyka wejść (na życzenie właściciela)

1. **Kolejka audytu porządkuje według klasy ryzyka** (granice → dane zmieniają działanie →
   receptury → graf i UI → baseline). Plan §2 porządkuje według **wpływu na użytkownika**. Obie
   są poprawne; różnią się w dwóch miejscach: CKP-A-012 (hipoteza: późna odpowiedź edytuje inne
   pole) i LEM-005 (kasowanie bazy) idą u mnie na szczyt, bo klawiatura jest używana codziennie,
   a kasowanie jest nieodwracalne; CH-008 (etykiety web) idzie w dół, bo web nie jest codzienną
   powierzchnią właściciela (telefon jest).
2. **„FAIL” w pass4 to reprodukcja wady, nie odsetek awarii produktu.** Audyt mówi to wprost; plan
   nie używa tych liczb jako miary jakości.
3. **„chatadhd” w audycie miesza aplikację Python i kernel Loom.** Właściciel używa codziennie
   aplikacji Python na Androidzie (`THREADS` T7, DUCH §6), więc ustalenia po stronie Python
   (CH-001 `engine/config.py`, CH-010 `engine/semantic_llm.py`, CH-011, A4-CH-MOD001, A3-IMP-CH004)
   mają większy wpływ, niż sugeruje ich liczba. Briefy chatadhd mają osobną kolumnę „lustro
   Python”.
4. **Nazwy docelowe audytu** (`NarrativeRecipe`, `RoutingStrategy`, `SchedulePolicy`, …) mogą być
   odczytane jako nowe typy do zaimplementowania. Nie są: zbiory otwarte żyją w danych (I6), więc
   nazwa jest kluczem w packu, nie klasą w Kotlinie czy C++. Briefy wymagają „najpierw istniejący
   mechanizm”.
5. **Sekcja 4 `THREADS` wskazała dwa nieistniejące lokatory** (`pass4/backlog.md`,
   `thread-11.md` na gałęzi roboczej) — tabela w §0 koryguje. Plan z nieaktualnymi lokatorami
   byłby wzorcem D zastosowanym do dokumentacji.

## 2. Priorytety migracji danych do profili — per repozytorium

### 2.1 Kryteria (preset, nie reguła; właściciel może je przestawić)

| Oś | Pytanie | Skala |
|---|---|---|
| **U1 szkoda** | czy użytkownik traci dane, pieniądze albo prywatność, zanim zauważy | nieodwracalna / odwracalna / brak |
| **U2 siła R42** | czy właściciel chciałby to zmienić bez zmiany algorytmu (test R42) | na pewno / prawdopodobnie / rzadko |
| **U3 częstość** | jak często powierzchnia jest używana | codziennie (telefon) / tygodniowo / badawczo |
| **U4 odblokowanie** | ile dalszych pozycji czeka na tę | dużo / kilka / nic |
| **U5 koszt** | pliki, testy, ryzyko migracji istniejących danych | S / M / L |

Priorytet jest porządkowy (P0 > P1 > P2 > P3), uzasadniony słownie, nie wynikiem sumy. P0 = szkoda
nieodwracalna albo codzienna powierzchnia z utratą; P1 = „dane nie zmieniają działania” na ścieżce
używanej przez właściciela; P2 = receptury i polityki wersjonowane; P3 = pokrycie grafowe, UI,
higiena tożsamości.

### 2.2 Co znaczy „profil per repozytorium”

Minimalny kształt, wspólny tylko co do **ról**, nie co do schematu (`ECOSYSTEM.md`: różne grafy
nie muszą mieć tej samej semantyki):

| Rola w profilu | `chatadhd` dziś | Inne repo (propozycja: użyć istniejącego katalogu danych repo) |
|---|---|---|
| magazyny (węzły klasy `Storage`, EITG §6) | brak → T5 | brak we wszystkich |
| definicje ustawień + domyślne + warstwy + wykluczenia | `loom/data/runtime/*.pack`, `profiles/user.pack`, DefaultLayers | Watchdog `config/*.json` (częściowo), IO Matrix `SettingsSchema` (kod), AGEDS env/seed, LEM brak |
| katalog komunikatów i języków | `onboarding/ui.pack` | Watchdog ma dane PL/EN nawigacji; reszta brak |
| metody, receptury, prompty z wersją | `prompts/`, `philosophy/`, MethodRegistry | brak (prompty w kodzie we wszystkich) |
| katalog dostawców i modeli | `runtime/model.pack`, `runtime/net.pack` | IO Matrix `assets/providers.json` + LEGACY w kodzie; reszta w kodzie |
| samoopis (co repo jest, co umie, jakie ma magazyny) | `profiles/self.json` | brak; zalążek: wpis w B-ECO-01 po stronie `chatadhd` |
| koperta odniesienia `loom.ecosystem_ref/1` | — | — (wspólna, EITG §7) |

Decyzja: **nie narzucamy układu `loom/data` innym repozytoriom.** Każde repo trzyma profil tam,
gdzie już trzyma dane; wspólna jest koperta i sześć kategorii R42. Dzięki temu brief dla IO Matrix
nie wymaga od agenta znajomości Loom.

### 2.3 `chatadhd` (kernel Loom, aplikacja Python, web)

| P | Ustalenia / grupy | Dlaczego tu (U1–U5) | Mechanizm docelowy (istniejący) | Brief |
|---|---|---|---|---|
| P0 | CH-004/005, CH-011, A4-CH-MOD001, A2-CH-001/004, A4-D-002, R44, A3-IMP-CH004 | utrata danych i kosztów na codziennej powierzchni | — | **T1–T8** (§1.5) |
| P1 | CH-001 (startowe domyślne w C++ i Python) | codzienna aplikacja; `config.pack` „gotowe dane, okablowanie oczekuje”; U4: każdy inny preset startowy czeka na to | RuntimeProfile domena `config`, DefaultLayers; Python czyta ten sam pack przez generator (osadzony pack z danych) | B-CH-01 |
| P1 | CH-002, CH-003, DIC-0009…0014 i reszta `open` wątku 1 | drugi tani model (R24) to historia „nigdy nie widziałem sensownych wyników”; prompt i limity 3000/800/5 w kodzie; U2 na pewno | MethodRegistry + `prompts/` z hashem; jeden efektywny snapshot analizatora per zadanie (CH-003) | B-CH-02 |
| P1 | CH-006, CH-010 (admission i scalanie w legacy ingest) | jakość grafu R3; inferencja zapisywana jak obserwacja (G9) | `graph_ingest.pack` (istnieje: DIC-0015…0020) + polityka admission `ask`/`candidate`/`auto` + MergePolicy; `produced_by` na relacji | B-CH-03 |
| P2 | CH-007 + 51 grup `open` wątku 3 (kontekst) | wagi historii i znaczniki instrukcji = polityka promptu; po T2 (kolejność kontekstu) | wersjonowany przepis renderera kontekstu w `context/` i `policy/`; preset `legacy` bajt w bajt | B-CH-04 (po T2) |
| P2 | CH-008, CH-009, A4-CH-VIEW001, A3-WEB-001, A4-E-001 + 81 grup wątku 10 | druga pamięć (`localStorage`), filtr widoczności bez polityki; web nie jest codzienną powierzchnią → P2 nie P1 | perspektywa, sesja workflow, checkpoint jako węzły (`profiles/graph_perspective.pack` istnieje); `localStorage` jako zadeklarowany cache; katalog komunikatów web | B-CH-05 (po T3) |
| P3 | A4-C-001/002, A2-C-001/004, A3-DISC-001, A4-D-001, A4-B-RES001 | higiena tożsamości i pochodzenia w modułach B/C/D/E; narzędzia badawcze, nie codzienna ścieżka | kanoniczna serializacja planu, parametry parsera w tożsamości metody, relokacja = nowy lokator + `stale` | B-CH-06 (po T1, T4) |
| — | CH-015 (opt-in eksperymentów) | hipoteza; należy do badań | — | T11/T12 |

### 2.4 `Watchdog-JH16` (według audytu: API + UI + usługi badawcze; powierzchnia badawcza)

Najpierw sprawdzić na bieżącym `main`: WD-001/003/011 i A4-WD-001/003 (`STATE.md` wskazuje
przebiegi 624/630/636 i 644/644 po poprawkach) — status „naprawione” musi być potwierdzony
lokatorem, nie wpisem w STATE.

| P | Ustalenia | Dlaczego | Mechanizm docelowy | Brief |
|---|---|---|---|---|
| P1 | WD-002 (`missingPolicy=exclude` zachowuje brakujący wiersz), A4-WD-002 (manifest ponownej analizy gubi braki i pochodzenie) | **zły wynik liczbowy po cichu** w narzędziu badawczym = szkoda niewidoczna; G2/G8 | `MethodStep.missingPolicy` konsumowany przez `ratio`; maska wykluczeń w ślad; manifest z pochodzeniem wejścia | B-WD-01 |
| P2 | WD-005 (routing suite/kwantyl/Wilson), WD-009 (prefiks 24 000 + literalny prompt), WD-003 (parametry żądania, sprawdzić) | koszt i jakość; U2 na pewno | profile `RoutingStrategy`/`ContextConstructionPolicy` jako wpisy w istniejącym `config/`, z hashem; obecne zachowanie jako preset | B-WD-02 |
| P3 | WD-004 (`localStorage` cicho wraca do default), WD-006 (teksty Setup w TSX), WD-010 (tylko `coalesce`) | druga pamięć, UI, scheduler; odwracalne | preferencje = ustawienia konta z warstwą źródła; katalog komunikatów PL/EN już istnieje dla nawigacji; `misfirePolicy` + `catchUpMax` jako dane | B-WD-03 |

### 2.5 `Custom-Keyboard-Pro` / IO Matrix (klawiatura Android; codzienna; tekst użytkownika)

| P | Ustalenia | Dlaczego | Mechanizm docelowy | Brief |
|---|---|---|---|---|
| P0 | CKP-A-012 (hipoteza: późna odpowiedź AI edytuje inne pole; opóźnione wklejanie) — **najpierw test hipotezy** | jeśli prawdziwa: korupcja tekstu użytkownika na codziennej powierzchni; U1 nieodwracalna | wiązanie wyniku z celem edycji (identyfikator pola + wersja treści) | B-CKP-01 |
| P1 | CKP-A-001 (wbudowane zadanie wygrywa z personalnym o tym samym ID; brak wykluczenia), CKP-A-002 (uszkodzony katalog → zaszyte endpointy i modele) | tekst użytkownika idzie do sieci według danych, których nie może nadpisać; U2 na pewno | wersjonowany `TaskRecipe` + warstwy default/user + tombstone wykluczenia; preset zgodności dostawców jako dane albo pack generowany | B-CKP-01 |
| P2 | CKP-A-003 (Policy Matrix nie dociera do runtime), CKP-A-005 (pierwszeństwo źródeł, stratny postprocessing), CKP-A-007 (GoalPrompt w kodzie) | zachowanie odczuwalne co dzień (sugestie), ale odwracalne | profile wag/koszt-nieznany/trasy; `SuggestionPolicy` z zapisem straty; prompt wersjonowany z jednym limitem alternatyw | B-CKP-02 |
| P3 | CKP-A-004 (definicje ustawień z Kotlin), CKP-A-006 (brak adresowalnych deskryptorów), CKP-A-011 (`profiles/*.json` bez konsumenta), A4-CKP-001/002 (stabilność przechwycenia, `settleMillis`) | pokrycie grafowe i tożsamość; odwracalne | definicje ustawień + prezentacja jako dane; deskryptory ustawienie/zadanie/prompt/kontekst/wykonanie; kanoniczna kolejność map | B-CKP-03 |

### 2.6 `AGEDS` (Evidence Workbench; sprawa prawna R9; łańcuch opieki nad dowodem)

| P | Ustalenia | Dlaczego | Mechanizm docelowy | Brief |
|---|---|---|---|---|
| P1 | EA-AGEDS-001 (receptura ASR w kodzie), EA-AGEDS-002 (zadanie nie przypina receptury; dedup po artefakcie), EA-AGEDS-003 (limity skanu nie przez Android/HTTP) | **odtwarzalność dowodu**: transkrypcja bez przypiętej receptury nie jest cytowalna w postępowaniu (R9); G2 | receptura + profil modelu + hash przypięte do zadania; dwa hashe = dwa zadania; limity przez każdy interfejs do silnika i pokwitowania | B-AG-01 |
| P2 | EA-AGEDS-004 (słowniki nagłówków, numery, nazwy plików), EA-AGEDS-005 (priorytet z kolejności i wag), EA-AGEDS-009 (ranking, tokenizer, sufity wyszukiwania) | interpretacja dowodu zmienna bez zmiany algorytmu; hipotezy już `unverified`, więc odwracalne | metoda parsera + słownik + profil formatu z `produced_by` per obserwacja; profil kolejki; profil wyszukiwania z wersją indeksu | B-AG-02 |
| P3 | EA-AGEDS-006 (konfiguracja poza grafem), EA-AGEDS-007 (wykluczenia nie przeżywają seeda), EA-AGEDS-008 (napisy PL i fallback endpointu w Androidzie) | druga pamięć i UI; odwracalne | lokalne encje ustawień/profili/metod z mapowaniem; trwałe `never`; katalog komunikatów + ustawienie endpointu | B-AG-03 |

### 2.7 `LEM-Workbench` (instrument Gemini; badawczy; mały)

| P | Ustalenia | Dlaczego | Mechanizm docelowy | Brief |
|---|---|---|---|---|
| P0 | LEM-005 (brak migracji Room = kasowanie bazy), LEM-003 (wyniki tracą surową odpowiedź i kroki częściowe), A4-LEM-001/002 (dwa żądania jednego eksperymentu z różnymi poświadczeniami; spóźniona odpowiedź wraca po `clear`/rekey) | **nieodwracalna utrata** i niespójne poświadczenia; małe repo, więc tanie (U5 S) | jawna migracja albo bezpieczny błąd; utrwalenie żądania i wyjścia każdego kroku przed agregacją; tożsamość operacji przypięta do poświadczeń | B-LEM-01 |
| P2 | LEM-001 (ExperimentConfig nie steruje), LEM-002 (receptura i transport w Kotlinie), LEM-004 (rejestr modeli bez paginacji, ukryty filtr) | wzorzec A w czystej postaci; instrument badawczy, więc P2 | preset/`method_version` z obsługiwanymi polami i statusem zdolności; profil transportu; snapshot rejestru z kompletnością | B-LEM-02 |
| P3 | LEM-006 (UI, twierdzenia historyczne, motyw w kodzie), LEM-007 (graf bez konfiguracji, modeli, metod, wyników) | pokrycie; odwracalne | katalog UI; opis grafowy istniejących obiektów przy zachowaniu Room | B-LEM-03 |

### 2.8 `loom` (standalone) i `ar-drafty`

Bez migracji. `loom`: węzeł `preserved_compatibility_predecessor_of` w samomodelu i wzmianka
o nieotwieraniu nowszych formatów (B-ECO-01). `ar-drafty`: węzeł z kopertą do prywatnego raportu;
żadnych ustaleń publicznie.

### 2.9 Fale — kolejność między repozytoriami

Repozytoria są rozłączne, więc fale mówią, co w każdym repo jest pierwsze, a nie że repo czekają
na siebie. W obrębie jednej sesji obowiązuje jeden agent naraz (`CLAUDE.md`, `THREADS` P0).

| Fala | Treść | Kryterium wejścia | Kryterium wyjścia |
|---|---|---|---|
| **W1 — nic nie ginie** | B-LEM-01, B-CKP-01 (test hipotezy CKP-A-012 + 001/002), B-AG-01, B-WD-01, B-CH-01, B-ECO-01 | brief potwierdzony na bieżącym SHA (faza rozważenia); dla `chatadhd`: B-ECO-01 od razu (same nowe pliki), B-CH-01 po T7 | brama repo zielona; test A/B na przechwyconym żądaniu; status audytu zaktualizowany |
| **W2 — dane zmieniają działanie** | B-CH-02, B-CH-03, B-WD-02, B-CKP-02, B-AG-02, B-LEM-02 | W1 danego repo zamknięta | jak wyżej + `produced_by`/hash w wyniku |
| **W3 — graf i interfejs** | B-CH-04 (po T2), B-CH-05 (po T3), B-WD-03, B-CKP-03, B-AG-03, B-LEM-03, B-ECO-02 (strażnik drugiej pamięci) | W2 danego repo; dla `chatadhd` T1–T8 scalone | strażnik G5 zielony w `chatadhd`; katalogi komunikatów konsumowane |
| **W4 — higiena i iteracja** | B-CH-06, porcja ścieżek z `coverage-index.json`, rozważenie mostów (§5) | W3 | brak pozycji P0/P1; właściciel decyduje o dalszym ciągu (pieniądze) |

Mapowanie na kolejkę audytu: grupa 1 (granice, zgody, utrata) ≈ W1; grupa 2 (dane zmieniają
działanie) ≈ W2; grupa 3 (receptury) ≈ W2/W3; grupa 4 (graf i UI) ≈ W3; grupa 5 (baseline) = bez
migracji. Różnice uzasadnione w §1.6 p. 1.
