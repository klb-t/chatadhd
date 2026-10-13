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

Aktualny checkpoint osobnej gałęzi B: runtime **6d1231bd**, audit metadata
`02714d5b`; main bez zmian. [Raport B](data-graph-engine-2026-10-09.md) i
[manifest źródło→przyjęcie→zależności](data-graph-engine-2026-10-09-integration.json)
zastępują historyczne wpisy B powyżej. P4a działa w rzeczywistym ChatView/headless
przez istniejący operator; źródło→kontekst→ordinary chat/egress nadal jest otwarte.

Pełne dev 160/160, Clang 160/160, ASan 159/159, guard PASS; catalog_scale jawnie
opt-in/niewykonany. Świeży web: 15 komend + build/receipt, JNI 1/1 i generatory PASS.
R42 inventory nadal nierozliczone (68558 unclassified, 153 blocked, 0 stale); nie jest
bramką runtime. Watchdog A4-WD001/003: złożony lint/build/test 644/644
PASS na `f2b6b89`, wcześniejsze 630/636 zachowane historycznie. AGEDS request pin
wymaga odzyskania źródłowego SHA/handoff, bez ponownej implementacji. Setup
draft revision 8 zapis/readback, publikacja/nowa instancja pending. Następnie P4b.1 shared
ContextEngine/R40 preparation; P4b.2, P5 i generic discovery nadal otwarte.
Release/TSan/device/live-quality niewykonane, płatnych modeli/CI zero.

## 2026-10-12 — S1 (orkiestrator Opus): wątki T7 → T8

Stan kolejki, meldunek dzienny i decyzje: [wp-queue](wp-queue-2026-10-10.md). Plan: [THREADS_2026-10-11](THREADS_2026-10-11.md).

| Wątek | Commity | Brama |
|---|---|---|
| T7 dane w aplikacji Python | `514e249`, `7ea4ecb` | CTest 161/161, guard PASS (160+1 opt-in), web 15/16 — `test:chat-context` czerwony również na bazie `e777592` (P-1) |
| T6 strażnik ×10 wszędzie | `d4542a7`…`940b070` | CTest 163/163, guard PASS (162+1 opt-in), web 17/18 (P-1) |
| P-1 czat obok panelu wiedzy na telefonie | `3b251c6` | 7/7 dotkniętych skryptów web |
| T1 import: nierozpoznane do grafu + duplikaty | `854868d7`…`da57134d` | CTest 164/164, guard PASS (163+1 opt-in), web 17/17 |
| T2 worker: ponawianie, zator, historia prób, kolejność kontekstu | `8044626`, `c8203a1`, `dbeac2d` | CTest 164/164, guard PASS (163+1 opt-in), web 18/18 |
| T3 widok wejściowy grafu wg osi | `b7e4a35`…`559ffd0` | CTest 165/165, guard PASS (164+1 opt-in), web 19/19 |
| T4 raport ekonomii + TOCTOU `access.py` | `25b4437`…`1251333` | CTest 166/166, guard PASS (165+1 opt-in), web 19/19 |
| T5 magazyn i kryptografia PQ (inwentarz, projekt, preset hybrydowy) | do `097adff2` | CTest 169/169, guard PASS (168+1 opt-in), web 19/19 |

Decyzje techniczne S1 są zapisywane w `wp-queue` (sekcja „Decyzje S1”).

## 2026-10-12 — S2/S3 (Fable) scalone na gałąź roboczą przez S1

**T9 (S2, gałąź `claude/t9-ecosystem-synthesis`, PR #15, tylko dokumenty):**
[EVERYTHING_IN_THE_GRAPH](../architecture/EVERYTHING_IN_THE_GRAPH_2026-10-11.md) — paradygmat „wszystkie dane
systemu są w grafie” jako indeks z pochodzeniem nad magazynami, niezmienniki G1–G10, zgodność z R50/R51, koperta
`loom.ecosystem_ref/1`. [ECOSYSTEM_INTEGRATION](../architecture/ECOSYSTEM_INTEGRATION_2026-10-11.md) — synteza audytu
w osiem wzorców, priorytety migracji per repo (U1–U5), fale W1–W4, cykl rozważenie → implementacja → optymalizacja →
iteracja z regułą stopu, decyzje D-E1…D-E9. [t9-briefs](t9-briefs/README.md) — 20 briefów dla Sonnet z zależnościami
od T1–T8. Korekta lokatorów THREADS §4: audyt na `origin/gpt/ecosystem-audit-2026-10-09`, `thread-11.md` na
`origin/gpt/data-profiles-2026-10-04`.

**T10/T14 (S3, gałąź `claude/t10-edge-cases`, PR #14, tylko dokumenty):**
[EDGE_CASES](../architecture/EDGE_CASES_2026-10-11.md) — 89 przypadków z niezmiennikami i relacjami
metamorficznymi, brakujące abstrakcje A1–A23, 20 MR, mapowanie na T1–T14.
[ACCEPTANCE_TESTS 2026-10-11](../architecture/ACCEPTANCE_TESTS_2026-10-11.md) — ścieżki P1–P7 z telefonu, odporność
na ASR, ślad `loom.acceptance_trace/1`, kryteria 18–31. Raport: [t10-edge-cases-2026-10-11](t10-edge-cases-2026-10-11.md).
Decyzja właściciela **D3** (2026-10-12): wszystkie dostępne klucze; dane rzeczywiste dozwolone i preferowane tam, gdzie
jest wybór; korpusy syntetyczne zostają jako kontrola — zapisana w `OWNER_REQUIREMENTS`, `AGENTS.md`, `LOOM_CONCEPTUAL_MODEL` §7.3.

## 2026-10-12 — T8: integracja, PR-y, gałęzie

`main` = gałąź robocza = `230fea8` (fast-forward z `2b5e9a1`, po zielonej bramie całości: CTest 169/169, guard PASS, web 19/19).

**PR-y (stan po T8):**

| PR | Gałąź | Stan | Co dalej |
|---|---|---|---|
| #16 | `claude/chataddhd-cpp-loom-core-IRGRN` | scalony (fast-forward `main` 15:37 UTC) | gałąź robocza = `main` |
| #15 | `claude/t9-ecosystem-synthesis` | scalony na gałąź roboczą 13:41 UTC | — |
| #14 | `claude/t10-edge-cases` | scalony na gałąź roboczą 13:41 UTC | — |
| #13 | `gpt/research-jev-validation-2026-10-05` | zamknięty: zastąpiony przez T12 + D3 | treść na archiwum |
| #12 | `gpt/chat-selector-2026-10-04` | zamknięty: treść na `main` (64/69 plików identycznych, reszta rozwinięta) | — |
| #11 | `gpt/repo-hygiene-2026-10-04` | zamknięty: 25/25 commitów na `main` jako równoważne łatki | — |
| #10 | `gpt/usage-policy-2026-10-04` | zamknięty: kod na `main`; raport i dowody tylko na archiwum | — |
| #7 | `gpt/ecosystem-agent-research-2026-10-01` | zamknięty: zastąpiony przez T9 (`ECOSYSTEM_INTEGRATION`) | treść na archiwum |
| #1–#6, #8, #9 | — | zamknięte wcześniej | — |

**Gałęzie:** 37 zakończonych gałęzi (`gpt/*`, `claude/t9…`, `claude/t10…`, `claude/wp-a-send-gate`, `wip/worktree-agent-a342…`)
skopiowano pod `archive/2026-10-12/<nazwa>`; każda kopia wskazuje ten sam commit (sprawdzone). **0 usuniętych treści.**
Starych nazw nie udało się zdjąć: proxy git tej sesji odrzuca usuwanie refów (HTTP 403), więc stare nazwy nadal są widoczne
obok kopii archiwalnych. Na miejscu celowo: `main`, gałąź robocza, `eval/real-holdout-key` (nie czytamy). Lokalne gałęzie wątków
S1 (`claude/t1…t7…`) nie były wypychane — ich commity są na `main`.

## P4: magazyny jako wtyczki (2026-10-13, branch `claude/p4-storage-plugins`)

Decyzje techniczne (odwracalne; wszystko jest danymi):
- **Interfejs:** `loom/storage_backend.h` (put/get/stat/list/remove/probe po kluczach względnych; tożsamość = sha256),
  rejestr rodzajów `BackendRegistry` (id → fabryka). Rodzaje: `local`, `s3` (SigV4 po `HttpTransport`; MinIO/R2/B2 – styl ścieżkowy),
  `webdav`; `gdrive` zadeklarowany jako `planned` (wymaga przepływu OAuth z odświeżaniem — pozycja otwarta).
- **Dane:** magazyn = wpis `storages.<id>` z `backend:{kind,...}` w `policy/storage_security.pack`; użytkownik dopisuje własne w
  `<data>/profiles/storage_security.pack`. Szablony `s3_example`, `webdav_example`, `gdrive_example` mają `configured:false`.
  Poświadczenia tylko w `secrets.json` pod `<secret_ref>_access_key/_secret_key` lub `_user/_password/_token`; nigdy w wynikach ani logach.
- **Rozmieszczenie:** `placement.<kategoria>` = kolejność odczytu; role `primary|replica`; domyślnie bez zmian (`local_dir`, układ
  `blobs/aa/bb/<sha>` bajt w bajt). Zapis: kworum 1 (`sync_rules.write_quorum`), niedostępna replika degraduje, nie blokuje.
- **BlobStore** przechodzi przez rejestr tylko przy nie-domyślnym rozmieszczeniu; lokalny katalog zostaje układem roboczym
  (hydratacja przy odczycie; nie-lokalny `primary` = zapis tylko zdalny). Odczyt z repliki jest weryfikowany sha256.
- **SQLite zostaje lokalne** (WAL przez sieć/klienta synchronizacji jest niebezpieczny); kopia: `storage backup` (VACUUM INTO → dowolny magazyn).
- **Migracja:** `storage copy|move --category C --to S`: kopia → weryfikacja sha256 na celu → dopiero potem przełączenie
  rozmieszczenia (nakładka profilu); błąd = wycofanie skopiowanych obiektów, rozmieszczenie bez zmian; źródło kasuje tylko `--delete-source` (z `move`).
- **Proweniencja:** link `blob:<sha> -stored_in-> węzeł(kind=storage)` w tabelach `nodes`/`links` (schemat bez zmian).
- **Powierzchnie:** akcje `storage.*` w istniejącym wejściu JSON `loom_github_sync` (zero nowych eksportów), `/api/storage/*`,
  `loom storage ...`, provider `storage.probe` (sieciowe magazyny nie są kontaktowane z widoku wejścia → `storage_backends_unprobed`).
- Cofnięcie: usuń `profiles/storage_security.pack` (przywraca lokalne rozmieszczenie; dane w innych magazynach zostają).
- Otwarte: Google Drive; wieloczęściowy upload S3 dla bardzo dużych obiektów (dziś cały obiekt w pamięci); `encrypt_at_rest` per magazyn (T5b).

## P6: pakiet przez GitHub Actions + F2 (2026-10-13, branch `claude/p6-android-package`)

Decyzje techniczne (odwracalne):

- **Workflow `.github/workflows/package.yml`:** tylko `workflow_dispatch` (minuty płaci właściciel). Dwa zadania: `android`
  (artefakt `chatadhd-android-apk`) i `server` (artefakt `chatadhd-server-linux-x64`). Domyślnie jedna ABI `arm64-v8a` i rdzeń
  `Release` (telefon; `x86_64` dla emulatora i `Debug` dla szybkiej budowy to wejścia workflow). Wersje NDK/`compileSdk` czytane
  z `app/build.gradle.kts`; CMake z SDK: najnowszy 3.x (projekt wymaga ≥ 3.24). Cofnięcie: usuń plik workflow.
- **Podpis APK:** *debug*, ale klucz przypięty (sekret `ANDROID_DEBUG_KEYSTORE_B64`, bez niego cache Actions), bo świeży klucz
  na każdym runnerze blokuje aktualizację, a odinstalowanie kasuje teraz prywatne dane. **Release z Twoim keystore: otwarte.**
- **Serwer:** `deploy/install.sh --package DIR` buduje tym samym kodem co instalacja (jedna funkcja `build_all`), a
  `--from-tarball` instaluje gotowe archiwum (bez kompilatora i node); `deploy/verify-package.sh` uruchamia serwer z archiwum i go
  odpytuje. Budowa na Ubuntu 24.04 ⇒ cel potrzebuje glibc ≥ 2.38. Cofnięcie: instalacja ze źródeł działa bez zmian.
- **F2, lokalizacja danych = ustawienie** (`SharedPreferences "storage"`): nowa instalacja `private` (`getFilesDir()`), istniejące dane
  w `Documents/ChatADHD` zostają + pasek ostrzeżenia + „Przenieś do prywatnego katalogu”; `shared`, `app_external` i dowolna ścieżka
  zawsze do wyboru. Przeniesienie: kopia → SHA-256 każdego pliku → przełączenie → stara kopia zostaje do ręcznego usunięcia.
  Cofnięcie: wybierz poprzednią lokalizację w ekranie „Lokalizacja danych”.
- **F2, klucze API:** `secrets.json` pieczętowany kluczem AES-GCM z Android Keystore (`secrets.json.keystore`); jawny plik tylko
  chwilowo (rdzeń czyta go raz przy starcie i zapisuje cały przy `set_secret`; sprawdzone na prawdziwym rdzeniu). Jawny plik wygrywa
  (migracja, awaria), brak klucza nie kasuje pliku (`.unreadable`). Opcja wyłączalna. Zaszyfrowanie reszty danych: nie (poza zakresem).
- Otwarte: pierwsza kompilacja Kotlina i uruchomienie na urządzeniu; release signing; wznawianie przerwanego kopiowania.

## P7 fala 1: jedna akcja „użyj modelu” (2026-10-13, branch `claude/p7-use-model-action`)

Zgodnie z `docs/architecture/GRAF_HUB_KOD_SILNIK_2026-10-13.md`: jedno wejście `llm::use_model()` (`loom/use_model.h`,
`src/llm/`) zamiast kopii wywołania modelu. Decyzje techniczne (odwracalne; wszystko jest danymi):

- **Żądanie = dane:** `models` (jeden, albo lista wariantów), `mode` (`single` / `sequential` zwraca wynik każdego wariantu /
  `first_success` zatrzymuje się na pierwszym udanym), `parameters` (uporządkowane, dopisywane po `model` i `messages`),
  kontekst (gotowe `messages` albo odwołanie rozwiązywane przez `Services::resolve_context`), `prompt`, wybór dostawcy,
  haki strumienia, token anulowania. Warianty tylko na wniosek; bez nich zachowanie jak dotąd (te same bajty żądania).
- **Dostawca z ustawień:** klucz `llm_provider` (preset `openrouter`) → manifest w `ProviderRegistry` (wbudowane + opcjonalna
  tablica `provider_manifests` w config). Manifest daje adres (`base_url`, albo ustawienie wskazane w `metadata.base_url_setting`),
  sekret (`auth_secret`, schemat `auth_scheme`), nagłówki i **adapter** (`metadata.adapter`). Kod wołający nie zna już ścieżki
  `/chat/completions`, nagłówków auth ani nazw sekretów; zniknął `if (m.id == "openrouter")` z `providers.cpp`.
- **Adaptery** (`AdapterRegistry`, rodzina → fabryka; nowa rodzina = jeden `register_adapter`): `openai_chat` (OpenAI-zgodne,
  w tym OpenRouter) i `anthropic_messages` (Messages + Message Batches jako operacje `BatchSubmit/Status/Results` tego samego adaptera).
- **Bezpiecznik ×10 w każdym wywołaniu:** każde wydające wywołanie przechodzi przez wspólną bramkę (`UsageGate` nad
  `chat::ModelUsageGuard`) przed wysłaniem i jest rozliczane po; wstrzymane wywołanie wraca jako `State::Held` i **nic nie wychodzi**.
  Łańcuch wariantów zatrzymuje się na wstrzymanym (nie obchodzi pytania do właściciela). Własne przepływy zostały za tym samym
  interfejsem: strojenie celu (`GoalTypingGate`), odpowiedź grafowa (pożyczona bramka), ponowne wysłanie workera.
- **Proweniencja:** każdy wynik ma rekord `loom.model_use/1` (model zażądany/zgłoszony, dostawca, adapter, punkt końcowy bez
  sekretów, parametry + ich sha256, sha256 żądania i wyjścia, status, użycie) – w wyniku, w `Services::on_provenance`, a w
  szacunku dla księgi tam, gdzie szacunek wyprowadza akcja.
- **Zmigrowane miejsca (po jednym commicie, bajty żądania te same – testy „golden” dodane przed migracją):** analiza semantyczna
  (`semantic_llm`), współbieżny batch i natywne Message Batches (`batch_api`), udoskonalanie elementów archiwum
  (`archive/pipeline`), strojenie celu (`context_engine`), batch workera (zgłoszenie/odpytanie/wyniki) i bramka ponownego wysłania
  (`semantic_worker`), czat (`chat_engine`, strumień i bufor, ścieżka grafowa).
- **Świadome różnice (domyślnie niewidoczne):** adres jest zawsze bez końcowego `/` (strojenie celu go nie obcinało);
  `enabled()` analizy semantycznej wymaga teraz rozwiązywalnego dostawcy (pusty `base_url` = wyłączona); wstrzymanie bramką w
  analizie semantycznej to pauza (wynik regex, bez zliczania błędu), a w `batch_api` zwraca „brak id” z ostrzeżeniem – **nie ma
  jeszcze ekranu potwierdzenia dla tych kohort** (czat i worker go mają).
- **Modalności (doprecyzowanie właściciela, 2026-10-13):** `use_model` nie zna czatu ani tekstu. Wejścia i wyjścia to **typowane
  części** (`text`, `json`, `image`, `audio`, `video`, `embedding`, `file_ref`; odwołują się do węzłów grafu / blobów); wiadomość
  czatu to jeden z kształtów wejścia. Duże wyjścia (media) trafiają przez `OutputStore` do `BlobStore` (rozmieszczenie z rejestru
  magazynów P4) jako blob + węzeł grafu `kind=blob` + rekord artefaktu z proweniencją; w wyniku jest tylko odwołanie.
- **Możliwości jako dane:** adapter deklaruje domyślnie, co przyjmuje / zwraca / jakie ma tryby (`stream`, `batch`); warstwy danych
  nadpisują pola po kolei: manifest dostawcy (`metadata.capabilities`) → config `model_capabilities` → `ModelSpec::capabilities`.
  Niezgodność (np. wejście audio do `openai_chat`, obraz z modelu tekstowego, strumień dla obrazów, brak magazynu wyjść) jest
  odrzucana **przed wysłaniem** z podanym powodem (`Errc::Unsupported`) – nigdy po cichu z innym modelem. W `first_success`
  odrzucony wariant jest raportowany, a łańcuch idzie dalej.
- **Jeden rodzaj bytu: akcja** (`loom/data/runtime/actions.pack`, profil `actions`, schemat wartości `loom.action_catalogue/1`).
  Akcja jest *atomowa* (zrealizowana w kodzie/adapterze: `use_model`, `save`, `share`) albo *złożona* (kroki innych akcji jako
  dane); oba kształty są takie same: `id`, `kind`, `status` (`implemented|planned`), `steps` (złożona), typowane `inputs`/`outputs`,
  `requires` (wolne cechy elementu wejściowego), `effects` (`reads_only`, `writes`, `sends_off_device`, `costs_money`,
  `irreversible`, `touches_private_data`). Sześć zmigrowanych miejsc to akcje złożone z krokiem `use_model`: `chat.send`,
  `semantic.analyse`, `semantic.batch`, `batch.chat`, `archive.refine`, `goal.typing`; deklarowane (planned): `graph_native.reply`,
  `jev.edge_classify`, `input.route_intent`, `script.call`, `agent.call`, `media.generate_image|video|music`, `save`, `share`.
  Wołający podaje id akcji (`ModelRequest::action`), proweniencja je zapisuje; nieznana, `planned` albo niebędąca wołaniem modelu
  akcja jest odrzucana przed wysłaniem. Nową akcję dopisuje się nakładką `<dane>/profiles/actions.pack` (dane, nie kod). Dopasowywanie
  elementów do akcji i silnik reguł: fala 2 (nic z tego jeszcze nie istnieje).
- **Adapter obrazów (dowód kształtu):** `openai_images` (`/images/generations`, `b64_json` albo `url`), sprawdzony wyłącznie na
  fałszywym transporcie (bez sieci i płatnych wywołań); wyjście obraz → blob + węzeł. Akcja `media.generate_image` zostaje `planned`,
  dopóki nikt jej nie woła. Wyjście `json` to tekst sparsowany (z tolerancją płotków ```), błędny JSON = osobny błąd.
- **Przypięcia:** wpis `generated_outputs` w `loom/data/validation/product_literals.pack` odświeżony (był nieaktualny już na
  `a047df88`: brakowało trzech paczek, hasze generatora/wyjścia się nie zgadzały); strażnik wylicza teraz `verified_generated_files: 1`.
- Cofnięcie: przywróć poprzednie commity miejsc wywołania (każdy osobno); `use_model` można zostawić – nic innego go nie woła.
  Usunięcie `actions.pack` (i wpisu w `.inc` generatorem) wyłącza tylko sprawdzanie nazw akcji – bez `action` w żądaniu nic się nie zmienia.
- **Koszt bramki dla wołań masowych (zmierzone, do decyzji):** każde wołanie przez wspólną bramkę dopisuje do księgi
  `usage-policy.sqlite` ok. 9,5 KB i w buildzie debug trwa ok. 175 ms (200 wołań = 1,9 MB, 35 s). Dla czatu to nic, ale dla
  `semantic.analyse` i `batch.chat` (jedno wołanie na wiadomość) daje ok. 1 GB księgi na 100 tys. wiadomości. Wybór: zostawić (jak
  teraz, zgodnie z „każde wołanie przechodzi przez bramkę”), bramkować całe przebiegi jednym wpisem (tak jak batch i ponowne wysłanie
  workera), albo współdzielić otwarty uchwyt księgi (zmiana w `ModelUsageGuard`). Nie podjąłem tej decyzji za Ciebie.
- Otwarte (nie zmigrowane w tej sekcji; dalsze miejsca patrz „P7 fala 1, reszta” niżej): ekran potwierdzenia ×10 dla analizy/batcha;
  wpisanie wyboru dostawcy do węzłów grafu (fala 2); katalog akcji nie ma jeszcze dopasowania ani reguł (fala 2); wywołanie obrazów
  nie ma jeszcze żadnego wołającego ani ekranu.

## P7 fala 1, reszta: pozostałe miejsca wywołania modelu i zwijanie księgi zużycia (2026-10-13, branch `claude/p7b-use-model-rest`)

- **Zmigrowane (po jednym commicie, test „golden” bajtów żądania przed migracją):** `extract/semantic.cpp` (akcja
  `knowledge.propose_semantics`; własna pętla księgi semantycznej i punkt kontrolny siedzą w `make_function_gate`), trasa analizy
  serwera (`analysis.run`: `prepare` przez `prepare_call`, `execute` przez `execute_call` za bramką ze wspólnego strażnika; surowe
  bajty odpowiedzi i rozliczenie zostają w trasie), postprocess odpowiedzi grafowej (`graph_native.reply` jest teraz `implemented`),
  embeddingi (`embedding.embed`: adapter `openai_embeddings`, wektor to część typowana `embedding`, a nie osobna ścieżka; polityka
  wektorów admituje sama, więc `make_admitted_gate`) i odczyt katalogu modeli `/models` (operacja `ListModels` adaptera `openai_chat`).
  Ścieżki domyślne (`/chat/completions`, `/embeddings`, `/models`, `/v1/messages`…) żyją już tylko w adapterach
  (`Adapter::default_endpoint`); `grep -rn "chat/completions\|/embeddings\|Authorization" loom/src` pokazuje adaptery
  (`src/llm`) oraz adaptery innych rodzajów: ASR/OCR (`media_providers.cpp`, multipart, Groq/Google/OCR.space), magazyny S3/WebDAV i GitHub.
- **Dodatki do żądania:** `body_bytes` (dokładne bajty ciała), `wire_headers` (kompletna receptura nagłówków w kolejności; uwierzytelnienie
  trasy dopisane na końcu, jeśli receptura go nie niesie – to zachowuje kolejność nagłówków każdego miejsca), `make_function_gate`,
  `make_admitted_gate`; `apply_auth` nie wysyła pustego nagłówka dla trasy bez poświadczenia (analiza bez klucza jak dotąd: bez nagłówka).
- **Świadome różnice:** analiza i wnioskowanie semantyczne biorą sekret i schemat z dostawcy z ustawień (`llm_provider`, preset
  `openrouter` = ten sam `api_key`), a nie z wpisanej nazwy sekretu; odpowiedź 2xx inna niż 200 jest błędem HTTP w tych miejscach (wcześniej
  przechodziła); w embeddingach przy kilku błędach naraz kolejność zgłoszenia jest inna (błędy wiersza przed niezgodnością liczby), a
  rekord proweniencji niesie kształt i sha256 wektora zamiast wartości.
- **Zwijanie księgi zużycia** (zmierzone: 122 operacje = 4,5 MB z WAL, po zwinięciu 127 KB; ok. 37 KB/operację w teście, bo log WAL
  dubluje strony): ustawienie z presetem w danych – profil `usage_compaction` (`data/runtime/usage_compaction.pack`, nakładka
  `<dane>/profiles/usage_compaction.pack`), potem klucz config `usage_ledger_compaction`, potem jawne `override` komendy.
  Preset: `mode: auto`, `older_than_days: 30`, `statuses: [completed]`, `fold_samples_beyond_window: true`, `reclaim_space: true`,
  `auto_interval_hours: 24`; `off` i `manual` dozwolone. **Nie** wchodzi do `loom_usage_policy` (paczka `usage_policy.pack` zostaje
  przypięta, a opcje polityki są związane z paragonami potwierdzeń – ustawienie porządkowe nie może unieważniać oczekujących paragonów).
  Zwinięcie: rekord i zdarzenia ukończonej operacji znikają, zostaje nagrobek (id, kohorta, status, sha256 szacunku – ponowne żądanie
  tego samego id odpowiada „completed”, inne szacunek = konflikt), a próbki poza oknem bazowym (`baseline_window`) sumują się w jeden
  wiersz na kohortę i zasób (`usage_fold_samples`); baseline w oknie jest identyczny co do bitu, a dla `baseline_window: null` próbek
  się nie zwija. Nigdy nie są zwijane: `allowed`, `unresolved`, `requires_confirmation`, `denied`, operacje z decyzją właściciela
  (`confirmation`) i ich paragony. `compact` jest akcją `usage.compact` w `actions.pack`; wejścia: `loom_usage_policy_json`
  (`compact`, `compaction_settings`; `dry_run`, `as_of`, `override`), CLI `loom usage compact|compaction`, automatycznie przy starcie
  runtime z workerami (tylko gdy księga istnieje; raz na `auto_interval_hours`). Test: decyzje ×10 (łącznie z paragonami) przed/po
  identyczne na zasianej księdze, plik maleje, potwierdzenia nietknięte (`tests/test_usage_compaction.cpp`).
  Cofnięcie: `loom config set usage_ledger_compaction '{"mode":"off"}'` (albo `manual`); usunięcie nagrobków nie jest potrzebne – tabele są addytywne.
  Ograniczenie: po zwinięciu poszerzenie `baseline_window` uzupełnia brakującą część okna średnią zwiniętych próbek (przybliżenie).
- Otwarte: ASR/OCR (`media_providers`) jako wejścia typowane `audio`/`image` use_model (potrzebuje ciała multipart w `CallSpec`); ekran
  potwierdzenia ×10 dla analizy kontraktowej; automatyczne zwijanie działa przy starcie (długo działający serwer zwija po restarcie,
  nie w tle); jedyny nieprzechodzący przypadek zestawu W3 (`chat graph wiring: explicit off…` w `chat_graph_integration.verify.cc`, poza ctest;
  110 przypadków, 109 przechodzi) dotyczy ścieżki czatu, której ta gałąź nie zmieniała (katalog `context_execution_claims` powstaje przy
  wysyłce przez wspólną bramkę z fali 1) – nie sprawdzałem go na gałęzi bazowej.

## S2 / R3 — jedno wejście „wykonaj akcję” (2026-10-13)

- `loom_invoke(ctx, port, action, json)` w C ABI (addytywnie) + JNI; porty `storage`, `github`, `actions/list` (katalog akcji
  z opisami: wejścia z `default`/`edit`, wymagane cechy, flagi skutków, trasa HTTP). Trasy `/api/storage/*` serwera z katalogu.
  `loom_github_sync` robi tylko GitHub; `storage.*` tam to przestarzały alias przekazujący do `loom_invoke`.
  Akcje magazynu w `data/runtime/actions_storage.pack` (schemat `loom.action_catalogue/1`), tymczasowo — do wchłonięcia przez
  `actions.pack` z S1/P7. Raport: `docs/reports/S2_R3_2026-10-13.md`.
  Cofnięcie: revert scalenia R3 na `claude/s2-parallel`; stare wejścia nie zostały usunięte.

## S2 / R2 — opis każdego pola ustawień: wartość domyślna i zasada edycji (2026-10-13)

- Dane `data/runtime/settings_fields.pack`: cechy pola (typ, czy ma domyślną, opis, `sensitive`) + **zasady** w ogólnym
  kształcie {zakres, cel = pole, akcja `edit`, skutek} (GRAF_HUB_KOD_SILNIK §5). Brak zasady = edycja fakultatywna (R43).
  Etykiety: allow→optional, deny→read_only, require_value→required, require_confirmation→requires_confirmation; inne skutki
  przechodzą jako otwarte napisy. Domyślne wartości czytane z rzeczywistych domyślnych, nie kopiowane.
- `loom_settings_fields` (C ABI, JNI), `GET /api/settings/fields`, `loom config fields`, tabela w ustawieniach web.
  Sekrety opisane, wartości nigdy nie zwracane. Blokuje zapis tylko `deny` (dziś wyłącznie `_config_version`).
- Test kompletności w obie strony względem rzeczywistych domyślnych. Raport: `docs/reports/S2_R2_2026-10-13.md`.
  Cofnięcie: usunąć zasadę z pliku danych (lub nakładka `profiles/settings_fields.pack`); revert scalenia R2.

## P8 / fala 2 — które akcje pasują do którego elementu (2026-10-13)

Wg `docs/architecture/GRAF_HUB_KOD_SILNIK_2026-10-13.md` §5–§7, §9. Gałąź `claude/p8-traits-rules`.

- **Cechy na klasach** (`data/graph/classes.pack`, addytywnie, flaga kalibracji zostaje): `traits` dziedziczone po `parent`; id klasy
  jest też własną cechą (stare `requires: ["message"]` znaczy „jest wiadomością”); węzeł dodaje cechy własne. Klasy `content` i `form`
  (§6), podklasy postaci wg modalności (tekst, obraz, dźwięk, wideo, muzyka, ruch, kod, tabela), relacja `expresses` (postać → treść,
  waga = wierność), klasy nazwane dotąd w `requires` (conversation, message, prompt, edge…). Grupy = nazwane pęki cech albo flaga skutku
  albo oś (`groups`); lista otwarta. Rzutowane do profilu `graph_entry` (`ontology.traits|groups|relations`).
- **Wymagania akcji** (`actions.pack`): `requires`, `produces`, `content_op` (preserve|condense|expand|restyle|evaluate|combine|none),
  `form_op` (read|express|none), `cost` (względny, 1 = jedno zwykłe wywołanie modelu, nie pieniądze), `confidence`; wypełnione dla
  wszystkich wpisów, plus akcje kalibracyjne `planned` (transcribe, describe_image, ocr, summarise, translate, elaborate, parody,
  critique, combine, depict, narrate) i wpisy `actions.for|explain|propose_chain`. Akcje magazynu i zużycia wymagają cech `storage` /
  `usage_ledger` (wcześniej `[]` = pasowały do każdego elementu).
- **Zasady** (`data/runtime/action_rules.pack`, domena `action_rules`): jeden magazyn i jeden kształt {zakres, cel, akcja, skutek} — ten
  z S2/R2. **Decyzja:** osobny plik zasad, zasady pól ustawień przeniesione z `settings_fields.pack`, `loom_settings_fields` pyta
  silnik (klasa `settings_field`, akcja `edit`, zakres `installation` – nazwy w danych); wyjście bajt w bajt jak przed zmianą (test z
  wzorcem). Kontener = obiekt po id (nakładka dodaje/zmienia/wyłącza regułę, `disabled:true`); zasady fabryczne mają prawdziwe zakresy
  (`engine`, `app_preset`), `global` zostaje aliasem `installation`. Cele: klasa, cecha, pęk, węzeł, `any`, skrót `settings_field`,
  akcja/grupa akcji; wybór akcji: id, `*`, `{group}`, `{flag}`. Skutki: allow (domyślny, R43), confirm, deny (z powodem i
  inicjatorem), default_action. Konflikty: wszystkie reguły z `scopes.pack` (most_specific_wins, locked_by, first_defined, merge,
  intersection, min, max, weighted_vote, ask, by_context); rządzi ta, którą nazwie najogólniejsza pasująca zasada, inaczej domyślna
  łańcucha. Zasada ×10 zablokowana (`locked` + `locked_by`) przed niższym zakresem.
  Cofnięcie: przenieść `rules` z powrotem do `settings_fields.pack` i czytać je w `src/core/settings_fields.cpp` (jeden commit);
  nakładka `profiles/settings_fields.pack` z `rules` nie jest już poprawna → `profiles/action_rules.pack`.
- **Silnik** (`include/loom/action_rules.h`, `src/actions/`): `actions_for(element, zakres)` → akcje ze statusem (applicable /
  needs_confirmation / denied + powód / default) i wyjaśnieniem (które cechy, skąd, która zasada); `explain`; `propose_chain`
  (ekstrapolacja §5): łańcuchy akcji dorabiających brakującą cechę, tylko kroki dokładające cechę potrzebną do celu, każdy krok pod
  zasadami zakresu, ranking `rank_by` (dane) z pewności/kosztu/długości, progi z presetów `extrapolation` (max_length 3, max_cost 10,
  min_confidence 0,25, max_results 5; `generous` bez granic; `off` wyłącza; nadpisanie per wywołanie, `null` = bez granicy), a gdy
  nic się nie mieści – `beyond_bounds` (czego by to wymagało). Tylko propozycje, nic nie jest wykonywane.
- **Wejścia:** `loom_invoke("actions", "for"|"explain"|"propose_chain", json)` (bez nowych eksportów C), `POST /api/actions/for|explain|
  propose_chain` (z tabeli akcji, jak magazyn), `loom actions list|for|explain|chain`. Testy: `tests/test_action_rules.cpp`.
- Otwarte: element po samym id węzła (`m_…`/`c_…` → klasa; dziś element podaje wołający); progi ekstrapolacji per zakres (dziś preset
  aktywny + nadpisanie w wywołaniu); wykonanie zaakceptowanego łańcucha (interpreter akcji złożonych); akcje kalibracyjne są
  `planned` – bez implementacji; dopasowanie poleceń z okienka konwersacji (§8) i emergentne grupy (R55) korzystają z tego silnika.
## S2 / R4 — widok wejściowy grafu: prawdziwe liczniki + testy w przeglądarce (2026-10-13)

- Liczniki interfejsów, metod i perspektyw policzone z rzeczywistych źródeł (dotąd „nie wiem”): metody = byty `method:method`
  w profilach; perspektywy = ustawienia obszaru `graph/perspectives` w pakiecie warstw domyślnych; interfejsy = nowy rejestr
  `data/graph/interfaces.pack` (C ABI z `loom.h`, trasy HTTP z serwera **i z katalogu akcji**, polecenia CLI), generator
  `tools/gen_graph_interfaces.py` (`--check`). Testy porównują liczniki z niezależnie policzonym źródłem.
- e2e: `test:graph-entry-browser` (panel na prawdziwym serwerze, nakładka ukrywa obszar), `test:usage-pending-browser`
  (blok ×10: potwierdź / odrzuć). Raport: `docs/reports/S2_R4_2026-10-13.md`.
  Cofnięcie: usunąć wiązanie `interfaces` w `entry.pack` albo `hidden:true` w nakładce; revert scalenia R4.

## S2 / R1 — rejestr formatów importu i dostawców eksportu, platformy jako dane (2026-10-13)

- `importer_core`: jeden wpis `ImportFormat` na format (rozszerzenia, rozpoznanie treści, mime, ciało importu); nowy format =
  jeden wpis (test `test_import_format_registry.cpp`). `export_archive` nie zna nazw dostawców: `ProviderRegistry`, OpenAI
  i Anthropic we własnych plikach. Nazwy platform (chatgpt/claude/…) w presecie `data/presets/import.pack` (`platforms`),
  API `include/loom/platforms.h`. `grep '== "openai"\|== "claude"' loom/src` = 0. Wyniki importu bez zmian.
- Raport: `docs/reports/S2_R1_2026-10-13.md`. Cofnięcie: revert trzech commitów R1 (kolejno) i regeneracja `import_presets.inc`.
- Otwarte: rejestr dostawców jest globalny (testy muszą go przywracać); nowa platforma o znanej strukturze = wpis presetu,
  nowa struktura = adapter w kodzie.

## S2 / R5 — magazyn Google Drive i wysyłanie S3 w częściach (2026-10-13)

- Google Drive jako kolejny magazyn w rejestrze magazynów: OAuth, `client_id`/`client_secret`/`refresh_token` jako sekrety,
  token dostępu tylko w pamięci, odświeżany przed wygaśnięciem i raz po 401. Płaska przestrzeń nazw (nazwa pliku = klucz,
  jeden folder). Adresy, folder, próg wysyłki wznawialnej — ustawienia (presety).
- S3: duże obiekty w częściach (`multipart_part_size` preset 8 MiB, `multipart_threshold`, <0 wyłącza); błąd = przerwanie
  wysyłki; jedyny limit to 10 000 części z protokołu (część rośnie, by się zmieścić). Małe pliki bez zmian.
- Testy tylko na atrapach (bez sieci). Raport: `docs/reports/S2_R5_2026-10-13.md`. Cofnięcie: revert scalenia R5.
- Otwarte: brak próby na prawdziwym Google/S3; obrócony `refresh_token` nie jest zapisywany z powrotem.

## S2 / R6 — tryby analizy i warstwy wyszukiwania jako dane (2026-10-13)

- Tryby `regex`/`llm`/`batch` to wpisy rejestru; kolejność w `worker.pack` (`analysis_modes`, domyślnie batch, llm, regex);
  dwa tryby mogą dzielić jeden przebieg. Tryb `llm` przez istniejące `use_model`. Wyszukiwanie: lista warstw zamiast numeru,
  pierwsza używalna odpowiada, awaria przekazuje do następnej; stary numeryczny konstruktor jako adapter.
- Domyślne zachowanie bez zmian. Raport: `docs/reports/S2_R6_2026-10-13.md`. Cofnięcie: revert scalenia R6 lub kolejność w profilu.

## S2 / R7 — drobne zaległości po R2–R4 (2026-10-13)

- `loom_invoke("actions","list")` uwzględnia nakładkę użytkownika `profiles/actions.pack` (`hidden:true` ukrywa wpis na liście;
  trasy serwera nadal z wbudowanego katalogu — to okablowanie kodu). Pomoc CLI zawiera `config fields`. Rejestr interfejsów
  ma też `jni` (eksporty mostka) i `web` (wywołania API aplikacji web); liczba interfejsów z rejestru, nie wpisana na sztywno.
- Raport: `docs/reports/S2_R7_2026-10-13.md`. Cofnięcie: revert scalenia R7.
- Otwarte: wywołania web spoza wzorca `"VERB","/api/..."` (import, eksport, wysyłka plików) poza rejestrem; skróty w
  `data_domains.pack` bez generatora (liczone skryptem jednorazowym przy każdej zmianie).


## P9 — wykonywanie akcji złożonych i „wszystko z okienka konwersacji” (R54) (2026-10-13, branch `claude/p9-composite-routing`)

Wg `docs/architecture/GRAF_HUB_KOD_SILNIK_2026-10-13.md` §5, §8, §9. Decyzje techniczne (odwracalne; wszystko jest danymi):

- **Element po samym id** (`include/loom/action_element.h`, dane `runtime/element_resolution.pack`): `m_`, `c_`, `vg_`, `n_`, `l_`, skrót blobu
  (64 hex albo `blob:`/węzeł `kind=blob`), klucz ustawienia → klasa, cechy własne (typ MIME blobu → klasa i cechy) i atrybuty dla zasad.
  Ciąg będący id klasy zostaje klasą; ciąg bez pasującej postaci przechodzi bez zmian (silnik zgłasza `class_unknown` jak dotąd); id znanej
  postaci, którego nie ma w magazynie → `not_found`, nigdy zgadywanie. `actions.for|explain|propose_chain` (C ABI, HTTP, CLI) przyjmują goły id.
- **Interpreter** (`include/loom/action_run.h`, `src/actions/run.cpp`): kroki złożonej akcji to dane (`steps` w `actions.pack`: `id`, `action`, `with`,
  `when`, `variants`, `variant_mode` = `all`|`first_success`, `on_failure`); złożone w złożonych są spłaszczane do jednego planu (cykl i głębokość
  odrzucane), części typowane (`llm::Part`) płyną z kroku do kroku (`inputs: [{from: input|<krok>, variant?}]`, domyślnie wynik poprzedniego
  kroku), `$arg` i `$text` w `with`, warunki `trait|step|output|arg|not|all|any`. Złożona akcja z niepustym `consumer` (czat, analiza semantyczna…)
  jest wykonywana przez swojego konsumenta i interpreter ją odrzuca; interpretowane są te z pustym `consumer` (np. `summarise`, teraz `implemented`
  z krokiem `use_model` i promptem w danych). **Każda akcja planu przechodzi przez zasady** zakresu *przed* startem: `deny` zatrzymuje cały plan z
  powodem i inicjatorem reguły, `confirm` wstrzymuje przebieg z pokwitowaniem (`arr_…`, wiąże plan i bramki; odpowiedź bez właściwego
  pokwitowania jest odrzucana), wstrzymanie ×10 wstrzymuje krok z pokwitowaniem strażnika (`usage_…`) i nie jest nigdy pokryte potwierdzeniem
  z propozycji ani `confirmed:true`. `gate_as` na akcji dolicza zasady innych nazw (`settings.set` jest też bramkowane jako `edit`, więc działają reguły
  pól ustawień). Wynik kroku zapisuje rekord `loom.action_step/1` (decyzja, skróty wejść i wyjść, proweniencja wywołań modelu `loom.model_use/1`), zdarzenie
  `action.step` trafia do dziennika zdarzeń; każdy checkpoint przebiegu to zdarzenie `action.run` (dokument `loom.action_run/1`), więc pauza przeżywa
  restart. Anulowanie: token między krokami i wewnątrz wywołania modelu, `actions.cancel` dla wstrzymanego przebiegu. Łańcuch z `propose_chain` jest
  przyjmowany jako `chain` i idzie tym samym interpreterem; łańcuch z akcją tylko zadeklarowaną (`planned`) nie rusza (nic nie jest wysłane).
- **Wykonawcy** (`src/actions/runner.cpp`, `Runtime::actions()`): `use_model` (przez `llm::use_model` i wspólną bramkę ×10; model z kroku albo
  `default_model`), akcje z portem w katalogu (`storage.*`, `actions.*`), `settings.set`/`settings.get` (typ wartości wg pola, `check_config_write`,
  walidacja polityki zużycia i `semantic_retry_mode` jak dotąd). Reguła `deny:settings.set:secret` (inicjator `engine`) nie wpuszcza poświadczeń do
  rozmowy; przebieg na poświadczeniu nigdy nie zapisuje podanych wartości i nie może czekać na odpowiedź.
- **Wejścia:** `loom_invoke("actions", "run"|"confirm"|"cancel"|"status"|"preview")`, `loom_invoke("chat", "route")`, `POST /api/actions/run|preview|confirm|cancel|status`,
  `POST /api/chat/route`, `loom actions run|preview|confirm|cancel|status|route` (nowe flagi `--propose --yes --no --list` w `cli.pack`), API web `actions`.
- **Rozpoznanie zamiaru** (R54; `include/loom/chat_route.h`, `src/actions/route.cpp`, dane `runtime/intent_routing.pack` + `phrases` na akcjach katalogu):
  `input.route_intent` jest `implemented`. Każde wejście okienka (`loom_chat_ex`, `loom chat`) przechodzi przez `handle()` przed wysłaniem. Metoda to ustawienie
  (`intent_routing_method`; preset **heuristic**; `model` na życzenie; `off`): *heuristic* = lokalnie i za darmo — jawna składnia (`/summarise`, `/set k v`, `/run akcja on id`),
  wzorce (`ustaw k=v`, `set k to v`, `show k`) z danych, leksykon fraz akcji (PL+EN, polskie litery zwijane, słowo kończące się `*` to przedrostek, fraza = wszystkie słowa w
  pierwszych 12 tokenach, jedno w pierwszych 4); *model* = jedno wywołanie `use_model` (akcja `input.route_intent`, katalog akcji w prompcie, odpowiedź `json`) pod tą samą bramką
  ×10 — wstrzymanie wraca jako `usage_pending` z `stage: route_intent` i jest potwierdzane polem `route_usage_confirmation` (nie `usage_confirmation` czatu), a błąd modelu jest
  zgłaszany w `issues` i decyduje metoda lokalna. **Polecenie wymaga akcji ORAZ elementu** (id w tekście albo słowo odnoszące się do rozmowy/wiadomości; „streść mi, jak
  działa TCP” bez elementu to wiadomość). Wiadomość idzie dalej ścieżką czatu z tym samym żądaniem co przedtem (test „golden”); wejście z załącznikami, `usage_confirmation`,
  wyszukiwaniem w sieci itd. jest wiadomością z definicji (`message_request_fields`). Polecenie: `actions.for` na elemencie, a gdy brakuje cechy — łańcuch z `propose_chain`;
  odpowiedź **w rozmowie** (wiadomość użytkownika + wiadomość asystenta z `metadata.action_proposal`): co zostanie wykonane, na czym, szacunek (koszt względny, liczba wywołań
  modelu, dokładna liczba bajtów wysyłanych do modelu; koszt w pieniądzach **nieznany**, nie zero), co zasady każą potwierdzić albo dlaczego nie można (zasada z powodem i
  inicjatorem, akcja `planned`, brak cechy, brakujące argumenty). Teksty PL/EN są w danych. „tak/yes” (słowa z danych, cała wiadomość) jako odpowiedź na propozycję będącą
  ostatnią rzeczą powiedzianą wykonuje zapisany przebieg (z pokwitowaniem z metadanych), „nie/no” go odrzuca; starsza propozycja nie jest wykonywana po cichu, ale można ją potwierdzić
  jawnie `actions.confirm`. Ustawienie `intent_routing_run`: **propose** (preset), `read_only` (od razu, gdy plan tylko czyta i zasady o nic nie pytają) albo `always` (od razu; zasady
  i ×10 nadal pytają). Wiadomości ruchu poleceń (`metadata.routed`, `action_proposal`, `action_run`) nie wchodzą do treści rozmowy podawanej akcjom (np. streszczeniu) i nie są analizowane
  semantycznie (`semantic_status = done` ustawiane przy zapisie), ale zostają w historii wysyłanej do modelu kolejnych zwykłych wiadomości jak każda wiadomość asystenta.
- **Web:** blok propozycji (`ActionProposalBlock`) z przyciskami potwierdź/odrzuć wg wzorca `usage_pending`; teksty w `conversation_view.pack` (`presentation.action_proposal`, pl/en,
  `gen_onboarding_pack.py --check` czysty), logika w skanowalnym leksykalnie `context/action-proposal.ts`, test jednostkowy z renderem bloku (`npm run test:action-proposal`).
- Nowe ustawienia (opisane w `settings_fields.pack`, golden zaktualizowany): `intent_routing_method`, `intent_routing_run`, `intent_routing_model`.
- Cofnięcie: `loom config set intent_routing_method off` wyłącza rozpoznawanie (każde wejście jest wiadomością); `input.route_intent` można zadeklarować `planned` nakładką
  `profiles/actions.pack` (wtedy ścieżka modelu odmówi, heurystyka dalej działa — wyłącz metodą); interpreter nie jest wołany przez nic poza `actions.run`/routerem,
  więc revert commitów `feat(chat)`/`feat(actions)` tej gałęzi wystarcza; dane nowych profili (`element_resolution`, `action_run`, `intent_routing`) można usunąć razem z `.inc`
  generatorem.
- Otwarte: wyniki tekstowe nie są jeszcze zapisywane jako węzły grafu z krawędzią `derived_from` (wynik jest w dokumencie przebiegu i w rozmowie); `usage.compact` i akcje
  `planned` (transkrypcja, obraz…) nie mają wykonawców (plan z nimi nie startuje, propozycja mówi dlaczego); szacunek w pieniądzach wymaga cennika modelu; leksykon fraz jest
  kalibracją (fałszywe polecenia kosztują jedno „nie”); przebieg trzyma wyniki kroków w dokumencie (duże wyniki wypadałoby składować w blobach); wejście ze skryptu/agenta
  (`script.call`, `agent.call`) i równoległe kroki z §3 nadal nie istnieją.
