# Indeks wątków — 2026-10-05 wieczór

**Od 2026-10-05 ~18:00 integrację prowadzi Claude.** Wątki GPT skończyły tokeny;
właściciel: „GPT już nam nie pomoże, przyjmij jego wyniki”. Poprzednia wersja tego
indeksu (integrator 9, kolejka i wszystkie dowody) jest w
[`283ad6a`](https://github.com/klb-t/chatadhd/blob/283ad6a/docs/reports/INDEX.md)
oraz [integrator-intake-2026-10-05](integrator-intake-2026-10-05/README.md).

## Przyjęte na `main` 2026-10-05 wieczorem

Kolejność commitów na `main` (każdy przyrost to oryginalne commity autora po
rebase; zmiany merytoryczne integratora wymienione wprost):

| Wątek | Przyrost | Źródło autora | Uwagi integracji |
|---|---|---|---|
|11| pierwszy: dane → profile runtime | `22873e0` (rebase integratora 9: `215563c`) | bez zmian |
|6| pierwszy: kanał wektorów semantycznych w katalogu | `d9b29c5` | tylko mechanizm; domyślna jakość neutralna 31/45, 0 FP; DEV 33/45 nie promowany; blind nie czytany |
|5| drugi: presety i warstwy importu | `f494931` | jeden konflikt importów w `loom/cli/tests/test_cli_smoke.py` (dwie linie `import`) |
|4| drugi: normalizacja native GraphPacket | `097859a` | bez zmian |
|6| drugi: wagi termów z efektywnego packa | `dab76df` | bez zmian |
|7| wyniki badań modelowych (etapy 1–5a, rachunek) | `b0b02c1` | bez zmian; tylko nowe pliki w `docs/research` i `loom/tools/structure` |
|3| drugi: presety celu i rozumowania jako dane | `2312d2c` (gotowy `4115267`) | bez zmian |
|12| drugi: dane prezentacji onboardingu | `374bab7` | bez commitów-powtórek pierwszego przyrostu (już na `main`); konflikt tylko w dokumentach (`STATE.md` zostaje z `main`) |
|8| higiena repo, CI, seeding jako dane | `ced8f5a` (implementacja `57ad9bb`) | bez zmian; ASan nadal niewykonany (patrz niżej) |
|1| pełny caller semantyczny + adapter W2 | `50e6bb9` | **naprawa Claude**: ponowienie jako nowa próba W2 (opis poniżej) |
|10| drugi: UI metod, podglądy zapytań, tryby czatu | `2fcb822` | bez zmian; same 9 własnych commitów (powtórki 1 i 12 pominięte) |

### Naprawa wątku 1 (regresja ponowienia)

Rejestr W2 jest idempotentny po `operation_id`; caller wyprowadzał ID z
`run|chunk|request_hash`, więc ponowne wywołanie po nieudanej próbie dostawało
z powrotem stary rekord `unresolved`/`completed` i zwracało `blocked`
(3 przypadki / 8 asercji `test_knowledge_semantic` przy odbiorze). Teraz każda
wysyłka jest osobną operacją: próba 0 zachowuje dawne ID (potwierdzenie zostaje
przy nim), a dopiero gdy ta operacja jest już `unresolved`, `completed` albo
`cancelled`, nowe jawne wywołanie dostaje `<id>#1`, `#2`… Oczekujące
potwierdzenie i odmowa zostają przy swojej operacji; wznowienie nadal nigdy nie
wysyła ponownie. Nieznane rezerwacje wcześniejszych prób zostają (nie zerujemy).

### Poprawki integracyjne (Claude, jeden commit po całym stosie)

Pierwsza pełna bramka na czubku stosu dała 143/146. Trzy błędy wynikały ze
styku przyrostów albo środowiska, nie z logiki autorów:

- `unit.test_runtime_profile`: drugi przyrost 3 dodał packi
  `loom/data/runtime/{chat_reasoning,context_goal_cues}.pack` w formacie
  `loom.runtime_profile/1`, a osadzenie rejestru 11 było sprzed nich.
  Przegenerowano `runtime_profiles_embedded.inc` (`gen_runtime_profiles.py`).
  Wszystkie generatory z `--check` są zielone.
- `research.seeding`: `sum()` na floatach jest kompensowane od CPython 3.12,
  a CTest używa 3.11, więc replay zapisanych rankingów V4 różnił się na
  ostatnich cyfrach (u autora 3.12+ przechodził). `float_sum()` w
  `loom/tools/seeding/prototype.py` odtwarza definicję 3.12; testy przechodzą
  na 3.10–3.13 bez zmiany oczekiwanych danych.
- `research.structure`: zestaw urósł do ~1300 przypadków (narzędzia wątku 7),
  ~70 s sam. Dostaje teraz ścieżkę do `loom_candidate_graph_native_tool`
  (4 dotąd pominięte przypadki wykonania native rzeczywiście się wykonują),
  a tylko jego limit zawieszenia wynosi 300 s. Żaden przypadek, próg ani
  asercja nie zmieniły się.

## Bramka

Czubek stosu `670945f` (kod identyczny z tym commitem dokumentacji),
GCC 13.3 Debug, SQLite vendored, serwer ON, Python 3.11:

- **CTest 146/146 PASS** (229 s, `--parallel 4`). Strażnik `verify_ctest.py`:
  145 pozycji wykonanych, 1 jawnie niewykonana (opcjonalna skala katalogu),
  **923 przypadki native / 32 739 asercji, 1876 przypadków Python, 0 pominięć**.
- Build web PASS.
- Wątek 1, jego własne fikstury na zbudowanym kernelu: W2 **6/6** scenariuszy
  (m.in. to samo ID operacji po potwierdzeniu, brak ponownej wysyłki przy
  wznowieniu, idempotentny blok), integracja promptów **15/15 przypadków,
  233 asercje**.
- Pierwsza bramka przed poprawkami integracyjnymi: 143/146 (opis wyżej).
- Dowody: [claude-670945f-evidence.zip](integrator-intake-2026-10-05/claude-670945f-evidence.zip)
  (SHA256 `e3bfd67b295792dff2f09d8bfb30683de14488072724e2bfdec149c439b8c8ba`):
  manifest, JUnit, log CTest, wynik strażnika, build web, log pierwszej bramki,
  wyjścia fikstur, skrypt bramki.
- Bramkę wykonano raz na czubku całego stosu, nie osobno po każdym przyroście;
  pośrednie commity na `main` nie mają własnej bramki.
- 0 płatnych wywołań; nie czytano `eval/real-holdout-key` ani korpusu blind.

## Czego nie ma na `main` (pełne na gałęziach, nie na widoku)

- **7a/7b** (`gpt/research-jev-validation-2026-10-05`,
  `gpt/research-prompt-preferences-2026-10-05`): protokoły i narzędzia pod
  prawdziwe eksporty właściciela; 0 płatnych wywołań, brak wyników. Do użycia,
  gdy eksporty będą zaimportowane lokalnie.
- **Negatywy**: wszystkie gałęzie `archive/**` (integrator, precyzja, UI, W4,
  W5, W8, badania). Nic nie usunięto.
- **Dowody autorów** (ZIP-y, pokwitowania) zostają tam, gdzie je zostawili;
  na `main` są te, które były w ich commitach.

## Ograniczenia, które zostają

- **ASan (8)**: pełny CTest pod ASan+UBSan nie był wykonany ani przez autora,
  ani teraz. GCC i Clang (autor) oraz GCC (Claude) są zielone.
- **Chromium E2E (10)**: autor raportował 84 fixture / 20 native UI / 16 E2E
  PASS; niezależna powtórka integratora 9 była zablokowana przez środowisko
  (`socket()` niedozwolone). Teraz uruchomiono tylko build web.
- **Wątek 7**: etapy 1–5a z rachunkiem są na `main`; sprawdzian na nowych
  danych (24 rozmowy / 96 pytań) i 12 powtórek silnych metod nie zostały
  wykonane; nic nie poszło na prawdziwych eksportach. Wydano 0,70 USD
  z 5 USD według rachunków generacji.
- Przyjęty przyrost nie zamyka całego wymagania; otwarte prace poniżej.

## Otwarte prace (stan po tej integracji)

Inwentarz danych w kodzie (wątek 11, `c21e664`): 695 grup w 559 plikach;
[podział na wątki](https://github.com/klb-t/chatadhd/tree/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code).

| Wątek | Grupy | Co dalej |
|---|---|---|
|1|127|Wersje metod i promptów w grafie; dalsze ścieżki analizy; presety z badań 7 jako dane metod.|
|2|5|Startup/config/path/model/log/worker na danych; te same domyślne wartości.|
|3|51|Domyślne pack/profile rejestru, legacy/resume; selektor respektuje warstwy i wykluczenia 12 (teraz, gdy 11 jest na `main`).|
|4|27|Dalsze KB pack/store i profil grafowy.|
|5|52|Dalsze rejestry/warstwy importu razem z 11/12.|
|6|31|Nowe strojenie tylko na DEV; archiwum/alias/sketch/score jako dane; blind już użyty raz.|
|7|9|Sprawdzian na nowych danych, potem prawdziwe eksporty właściciela (protokoły 7a/7b).|
|8|1|ASan; dalsza macierz CI bez luzowania strażnika.|
|10|81|Most UI/HTTP/nawigacja z onboardingiem 12, „co aplikacja wie”, retencja.|
|11|213|Kolejne podsystemy profili; jedno źródło usage (2).|
|12|—|Konsumenci warstw, retencja, nawigacja razem z 3/10/11.|

## Zasady integracji od teraz

Claude integruje: świeży fetch, rebase na `main`, pełny build GCC Debug
(`-DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON`), pełny CTest ze
strażnikiem wykonanych przypadków (`verify_ctest.py`), build web, `main`
tylko fast-forward. Nie czytamy `eval/real-holdout-key`; blind użyty raz.

## Zadanie B — 2026-10-09, osobna gałąź produktu

[Raport DATA / GRAPH / ENGINE](data-graph-engine-2026-10-09.md): d92d7cc / ddcaeaf / 24c9fc3, testy zakresowe i E2E; pełna bramka dev z jawną blokadą research.structure. Nie zmienia statusu historycznej integracji Claude’a ani main.

### B: external resources — 2026-10-09

[Raport B](data-graph-engine-2026-10-09.md): ad6269a5/b20c0d8a na gałęzi produktu; ZIP resource graph wspólne dla widoku i zadania, external profile w real runtime, uncertain syntax/value_ref. Aktualne scoped ASan7/7 i E2E16/16; full dev145/146, guard REJECT przez 8 fixture errors C. R42 inventory checkoutu pozostaje otwarte; generator proof dokładny, allowlist unchanged. Nie jest to przyjęcie na main ani zakończenie całego R15/R20/R21.


## 2026-10-09 — kontynuacja B: integracja C/D/E i produktu

Aktualny stan zastępuje historyczne wpisy B powyżej: [raport B](data-graph-engine-2026-10-09.md), [manifest źródło→przyjęcie→zależności](data-graph-engine-2026-10-09-integration.json). Produkt373f0774: rejestr/importer/copy, operator zasobów i retencja, D mapper, E w App przez R40, domain inventory, A2 reset i CH-011 native/Python/UI. Pełny dev157/157 guard PASS; niezależny CH-011 C API24/24, A2 reset/removal2/2. Full ASan/Clang/web/JNI są następną serialną macierzą, wcześniejszy App17/17 pozostaje historią. Ordinary link/context/adoption i część batch/legacy nadal otwarte. Watchdog WD-003624/624 opublikowany. Odblokowane Android setup gates wykonane; main niezmieniony.
