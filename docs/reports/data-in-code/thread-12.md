# Wątek 12 — osobny inwentarz R40/R42 (2026-10-05)

Punkt odniesienia to własny pierwszy W12 `3c0bc36552ef9851f1174946cfb108549aae3228`; baza drugiego przyrostu to integrator `66da570d3b5379492e128d940ad474467082c59f`. Oryginalna gałąź pozostaje nietknięta. Lokacje tabeli są przypięte do **pierwszego** W12, a nie do późniejszych numerów wierszy.

R42 odczytano z rzeczywistej gałęzi Claude `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`: [wymaganie na 3cd5848](https://github.com/klb-t/chatadhd/blob/3cd5848/docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md), wiersze 483–515. Kryterium właściciela brzmi: **“could someone want to change it without changing the algorithm?”** Użytkownik widzi słownik mechanizmu wyłącznie przez tłumaczenia w danych; brak lub błąd packa daje jawny błąd albo osadzone dane wygenerowane z packa, bez ręcznych ukrytych defaults. W11 jest właścicielem automatycznego guarda i uzasadnionej allowlist. Wcześniejszy przegląd wyłącznie gałęzi GPT nie znalazł R42; nie obejmował tej aktualnej gałęzi Claude.

Oryginalne [data-in-code na c21e664](https://github.com/klb-t/chatadhd/tree/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code) ma raporty thread-1…thread-11, bez thread-12. INDEX na `66da570`, wiersze 40–53, podaje dla 12 `nowy`: odebrać onboarding foundation oraz uzgodnić consumers/retencję/nawigację z 3/10/11. Nie ma starej puli numerowanych grup 12 do dopisania. **W12-DIC-* jest osobną przestrzenią identyfikatorów; nie zmieniamy ani nie zwiększamy historycznej liczby 695.**

Zakres tego przyrostu: dane tekstów UI, języki, presety renderowania i tokeny prezentacji; sześć objaśnień warstw; usunięcie ręcznie powtórzonego presetu RuntimeProfile przez istniejące bindings. Węzeł `presentation.onboarding` korzysta z istniejącego R40, nakładek i trwałych wykluczeń. Nie wprowadzamy nowej retencji czasowej ani zmian zgód. Osiem wybranych grup ma stan `migrated_reviewed`: implementację sprawdzono w gotowych źródłach. Pozostają trzy jawne miejsca niewyniesione oraz dwie grupy kontraktu/diagnostyki oczekujące na guard W11. Suplement jest ograniczonym przeglądem wybranych kolejnych grup, nie pełnym audytem literałów. To nie zamyka całego R42 ani pełnych bramek integratora.

Bieżące dane drugiego przyrostu mają `loom/data/profiles/user.pack` revision **3** i wpis `presentation.onboarding` / `onboarding.presentation/v1` revision **2**. To fakty źródłowe; nie oznaczają wykonanej migracji ani ukończonej bramki native.

| ID | Grupa i źródło pierwszego W12 | Docelowe dane / kontrakt | Stan |
|---|---|---|---|
| W12-DIC-0001 | Teksty kreatora, formularza i prywatności — `loom/web/src/onboarding/OnboardingPanel.tsx:47–64,76–96,114–167,178–236` | `loom/data/onboarding/ui.pack#/locales` | migrated_reviewed |
| W12-DIC-0002 | Teksty widoku wiedzy i edytora warstw — `loom/web/src/onboarding/WhatAppKnows.tsx:15–35,46–71,80–102` | `loom/data/onboarding/ui.pack#/locales` | migrated_reviewed |
| W12-DIC-0003 | Komunikaty kontrolera i walidacji formularza — `loom/web/src/onboarding/controller.ts:6,67,88–107,154–158` | `loom/data/onboarding/ui.pack#/locales` | migrated_reviewed |
| W12-DIC-0004 | Presety renderowania i początkowego widoku — `loom/web/src/onboarding/OnboardingPanel.tsx:20–21,55,72–85,137–143,158,176` | `loom/data/onboarding/ui.pack#/defaults` | migrated_reviewed |
| W12-DIC-0005 | Tokeny wizualne CSS — `loom/web/src/onboarding/onboarding.css:1–16` | `loom/data/onboarding/ui.pack#/defaults/presentation_tokens` | migrated_reviewed |
| W12-DIC-0006 | Tłumaczenia stanów, pochodzenia i warstw — `loom/web/src/onboarding/OnboardingPanel.tsx:47,76,186–195` | `loom/data/onboarding/ui.pack#/locales` | migrated_reviewed |
| W12-DIC-0007 | Sześć objaśnień resolvera warstw — `loom/src/onboarding/layers.cpp:157–158,167–168,173–174,180–184,187–189,192–196` | `loom/data/onboarding/ui.pack#/locales/*/layer.*` | migrated_reviewed |
| W12-DIC-0008 | Drugi preset defaults RuntimeProfile — `loom/data/profiles/user.pack:167–213` | `loom/src/onboarding/gen_onboarding_pack.py + user.pack#/runtime_bindings` | migrated_reviewed |
| W12-DIC-0009 | Proza decyzji prywatności — `loom/src/onboarding/profile.cpp:206–237` | `przyszły wspólny katalog identyfikatorów polityki; kontrakt W3/W12` | remaining |
| W12-DIC-0010 | Diagnostyka native i walidacji snapshotu — `loom/src/onboarding/profile.cpp:41–67,240` | `W11: guard/allowlist R42 category 6; W10: mapowanie publicznych błędów` | retained_diagnostic_pending_guard |
| W12-DIC-0011 | Wcięcie natywnego tekstu dokumentu scenariusza — `loom/src/onboarding/store.cpp:105–106` | `przyszły preset formatu inspekcji` | remaining |
| W12-DIC-0012 | Nazwy kontraktu, schematy i słownik mechanizmu — `loom/include/loom/onboarding.h:9–31` | `R42 categories 1/3; przyszła allowlist W11` | retained_contract_pending_guard |
| W12-DIC-0013 | Etykiety prezentacyjne natywnej projekcji grafu — `loom/src/onboarding/graph.cpp:267–275,284–285,303–304,312–313,401–403,444–445` | `przyszłe dane etykiet projekcji grafu; uzgodnienie kontraktu W3/W12` | remaining |

Szczegóły, przypięte zakresy wierszy i zastrzeżenia każdego ID znajdują się w [thread-12.json](thread-12.json). Testy i zapisane odpowiedzi są fixture, nie kodem produktu. `builtin.inc` jest artefaktem generowanym z kanonicznych danych; zawarta w nim proza nie jest ręcznym presetem. Wcięcie native `store.cpp:106`, proza `privacy_decision`, etykiety projekcji grafu i native diagnostyka pozostają jawnie odróżnione od wybranego zakresu UI/warstw. W `graph.cpp` etykiety `Built-in defaults`, `User overrides`, `Type definitions` i sufiksy `prompt`, `parameters`, `recipe`, `preset`, `version` są tekstem prezentacyjnym pola `Entity.label`: można je zmienić bez zmiany algorytmu, więc grupa 0013 pozostaje danymi do wyniesienia. Nazwy ról, schema IDs, identyfikatory przekazane z danych oraz delimitery serializacji należą do odrębnej grupy kontraktu 0012; nie zostały zaliczone do 0013. Ten przyrost nie zmienia produkcyjnego `graph.cpp`.

## Niezmieniony prerequisite pierwszego W12

Bieżąca baza integratora nie zawierała kolejkującego się pierwszego foundation. Żeby drugi przyrost był kompletny i testowalny niezależnie od kolejki odbioru, na nowej gałęzi liniowo odtworzono wyłącznie siedem niezmienionych commitów własnego W12. Każdy został wypchnięty; stara gałąź nadal wskazuje `3c0bc36`. Opublikowany tip prerequisite to `aa8872421d7f0a727ae3b986592ab7c1c956d787`, na bazie `66da570`. Nie użyto merge ani nie przejęto plików innych wątków. Ten prerequisite jest dziedziczeniem, nie nową migracją drugiego przyrostu.

| Oryginał | Replay na nowej bazie | Pliki zmienione w commicie | Kontrola blobów |
|---|---|---|---|
| `67e2124ecae25d841e611b629268987372b658fd` | `b9bf62135483eb1a132831570e4038a6741c6e30` | 1 | listy ścieżek i wszystkie SHA blobów identyczne |
| `a8640c9b375c5f207dcc2429cbbe646fa8bb452c` | `ce529710d33da95aa88229c286ac27e01a6094e2` | 5 | listy ścieżek i wszystkie SHA blobów identyczne |
| `306c6a763c8945402d80f490c183c3b04e51e91a` | `8331a4991b4537dc21cc892ef36ea50fe8276763` | 4 | listy ścieżek i wszystkie SHA blobów identyczne |
| `8fd3cbcceb1b24f8d6a91ad14d53a2cf22ea5f93` | `54ca5a972549fd64c1e8bf405f626319f544b58b` | 10 | listy ścieżek i wszystkie SHA blobów identyczne |
| `cfd1bda901c221de307a671e0a42aac2e694240b` | `96ee59f4c5dfc753b4f517b91adaf00366460f24` | 7 | listy ścieżek i wszystkie SHA blobów identyczne |
| `40907ad28f3244bfe508da647273d2e1d3bcf1b3` | `6f213a85a1f3cc755ea41644b54b5e4d109724e8` | 2 | listy ścieżek i wszystkie SHA blobów identyczne |
| `3c0bc36552ef9851f1174946cfb108549aae3228` | `aa8872421d7f0a727ae3b986592ab7c1c956d787` | 71 | listy ścieżek i wszystkie SHA blobów identyczne |

Autor suplementu niezależnie porównał `git diff-tree` oraz SHA blobów dla każdej zmienionej ścieżki w każdej parze. To dowód niezmienionego zakresu prerequisite, bez ponownego przypisywania historycznego CTest nowej bazie. Pełne późniejsze bramki pozostają osobnym odbiorem.

## Warunki odbioru

- Domyślny katalog zachowuje dotychczasowe renderowanie i sześć angielskich objaśnień; dodatkowy język i nakładka faktycznie zmieniają wynik bez zmiany algorytmu.
- Uszkodzenie lub brak wymaganych danych daje jawny wynik błędu. Wyłączona/wykluczona prezentacja nie odzyskuje zdań angielskich; jej brak nie zmienia native zgód ani nie blokuje core policy_decision.
- Po aktualizacji packa trwałe wykluczenie `presentation.onboarding` pozostaje aktywne; prezentacja nie staje się ukrytym drugim magazynem preferencji.
- RuntimeProfile.defaults powstaje z kanonicznych entries/bindings; dowód zmiany kanonicznego wpisu i kontrola generacji mają wykrywać rozjazd.
- Liczby CTest i wynik guarda dopisujemy wyłącznie po rzeczywistym wykonaniu na docelowej bazie, bez luzowania progów.

Autor inwentarza wykonał niezależne syntetyczne sprawdzenie pure-helper: **12 przypadków / 47 asercji PASS**, bez zmian hashy źródeł podczas przebiegu. [Dowód i zachowany negatyw błędnego założenia rozmiaru katalogu](../onboarding-2-2026-10-05-evidence/independent-presentation/README.md) oddzielają helpery od native/React DOM/CTest. Aktualny katalog ma **153 wiadomości EN i 153 PL**. Pełny CTest i pełny guard R42 nie są tu deklarowane; osobny autor publikuje dowód parytetu native.

## Do wątku 9

Raport jest suplementem nowego W12, nie rekonstrukcją brakującego oryginalnego thread-12 ani odbiorem pierwszego `3c0bc36`. Aktualny INDEX przypisuje 12 foundation oraz uzgodnienia. Przy odbiorze zachować starą gałąź i odrębne dowody pierwszego/drugiego przyrostu.

## Do wątku 10

Został potwierdzony twój przydział mostu UI/HTTP/nawigacji, what-app-knows i retencji w INDEX:51. Konsumuj efektywny katalog/prezentację z native, z null/jawnym brakiem przy suppression; bez lokalnego fallbacku English i bez raw failure.message w zwykłym UI. Wpięcie poza loom/web/src/onboarding pozostaje u 10.

## Do wątku 3

Selektor oraz zapis/sending nadal korzystają z OnboardingStore::policy_decision i aktualnego revision. Wyłączenie prezentacji nie jest wyłączeniem prywatności. W12 nie edytuje context/chat ani nie deklaruje produkcyjnego podłączenia tych konsumentów.

## Do wątku 11

R42 na 3cd5848 przypisuje ci automatyczny guard literałów z kategorią i powodem każdego wpisu allowlist. W12-DIC-* to nowy suplement poza bazowymi 695 grupami. Rozróżnij generowane dane, kontrakty, lifecycle i diagnostykę od UI/prozy; nie uznawaj pozostałych reason/messages za migrowane przez ten przyrost. Loader RuntimeProfile nie jest edytowany; usuwana jest wyłącznie własna ręczna kopia presetu w profilu onboarding.
