# Wątek 12 — onboarding, profil użytkownika, warstwy domyślnych

Baza: `main` `30ad7d37337d6641cb7714b03e9feff0e6e25d25`.
Gałąź: `gpt/onboarding-2026-10-04`. Testy offline, zapisane odpowiedzi i dane
syntetyczne; zero wywołań dostawców i kosztu modeli. Ostatni fetch potwierdził
niezmieniony main; INDEX przeczytany ponownie. W12 nie aktualizuje main, STATE
ani INDEX i nie zmienia cudzych katalogów.

Status: przyrost W12 wypchnięty; pełny CTest bieżących źródeł **112/112 PASS**,
185.29 s, exit 0. GCC 13.3, Debug `-g0 -O0`, WERROR, shared, CLI, server,
bundled SQLite. Wszystkie stare progi zachowane. Testowane źródła odpowiadają
`40907ad28f3244bfe508da647273d2e1d3bcf1b3`; późniejsze commity dokumentacyjne
nie zmieniają kodu. [Dowody](onboarding-2026-10-04-evidence/README.md).

## Dostarczony zakres

- `ProfileSession` tworzy od razu profil z osobowymi polami `unknown`.
  Zachowuje `declined` i `never`; niezależne `question_disposition` zachowuje
  zakaz dopytywania także po dopuszczonym wnioskowaniu. `never` wymaga jawnego
  ponownego otwarcia przez użytkownika.
- Rozmowa i formularz zapisują do jednego stanu. Review kandydata i potwierdzenie
  całej części są odrębne. Można pominąć, przerwać, wznowić i powtórzyć część.
  Pytania, kolejność, prompty, kategorie, schemat odpowiedzi i metoda są w
  `loom/data/onboarding/scenario.pack`, z edycją ekspercką jako nową wersją metody.
- Prywatność kontroluje zapis, szczegółowość, wrażliwość, dostawców, wnioskowanie
  i pozyskiwanie wyłącznie z wypowiedzi. Retencja obejmuje pełną/metadata/brak
  historii, konfigurowalne klucze metadanych i liczbę zdarzeń.
  **Automatyczne wygasanie według czasu nie jest zaimplementowane.**
- `user_stated`, `form`, `model_inferred` i review pozostają akwizycją, niezależnie
  od natywnych Origin/EvidenceClass. Zwykła rozmowa przesyła `propose`; tryb
  zapisu to ustawienie `ask`/`candidate`/`automatic`. Kreator wymaga review.
  Usunięcie czyści też nieustrukturyzowane kopie w podsumowaniach;
  `purge_history:true` usuwa historię pola.
- `DefaultLayers` rozstrzyga dowolne klucze preferencji, metod, promptów, zgód
  i profili. Nadpisanie, wyłączenie i wykluczenie mają odrębne znaczenie.
  Trwały znacznik ID przeżywa aktualizację, zniknięcie i powrót wpisu. Nowe ID
  w wykluczonym obszarze stosują `proposal`/`direct`; direct nie przywraca starego
  wykluczonego ID. Resolution wyjaśnia warstwę, źródło i powód obowiązywania.
- `OnboardingStore` utrwala profil, warstwy i Entity/Claim/Observation w jednej
  transakcji Database/KnowledgeStore. Od razu zasiewa typy, metody, presety
  i założenia aplikacji. Istniejący `profiles/self.json` jest definicją sondy
  aplikacji, **nie faktem o projektach użytkownika**; pliku nie zmieniono.
  Migracja dotyczy tylko nowego stanu onboarding. Istniejące rozmowy, rozszerzenia
  i obce runy wiedzy pozostają nienaruszone.
- Archiwa mogą dosiać `propose` z `model_inferred`, prawdziwymi `source_refs`
  i opcjonalnym `method_execution`. W12 nie duplikuje ekstraktora W3/5.
  Receipt wiąże wynik z rzeczywistą metodą/runem; instalacja metody nie tworzy
  pozorowanego wykonania ani potwierdzonej treści.
- `loom/web/src/onboarding/` eksportuje kreator, formularz, widok wiedzy,
  edycję/usuwanie z historią, prywatność i warstwy. Zapisy i transport używają
  wstrzykiwanych adapterów; bez drugiego magazynu w przeglądarce i wywołań na mount.

## Weryfikacja i granice

Nowe testy natywne: 18 profilu, 11 grafu, 8 warstw i 14 persistence — 51
przypadków. Pokrywają świeży profil, zapisany kreator, declined/never, wykluczenie
po aktualizacji, restart, niezmienność wcześniejszych danych, atomowość błędów,
retencję kopii, wersje metod, importowany wynik i spóźnioną odpowiedź.
UI: 17 przypadków offline i build Vite 85 modułów przeszły.

| Bramka końcowa | Rzeczywiście wykonane |
|---|---|
| CTest | 112/112 PASS, 185.29 s, 0 failure/skip statuses |
| Native | 710 przypadków / 27 169 assertions, 0 failures |
| W12 w native | 51 przypadków / 2 704 assertions |
| Python unittest | 1 276 przypadków, 0 skips, w 25 entries |
| Dodatkowe smoke | server.smoke i cli.smoke PASS, bez mianownika unittest |
| UI | 17/17 PASS; tsc + Vite PASS, 85 modułów |
| Generator | `gen_onboarding_pack.py --check` PASS |

Liczniki pochodzą z nieuciętego LastTest.log, nie z wnioskowania na podstawie
bazy. Osobny `loom_tests --count` potwierdza 710. Dawny opt-in
`unit.test_catalog_scale` wykonał 0 przypadków/0 assertions; jego pojedynczy
powolny test ok. 1 GB nie był włączony i nie jest wliczony do 710.
Po publikacji sprawdzono 335 wejść natywnych: zmienione/brakujące 0;
mapa źródeł SHA256 `dbe6532d6482e64b3feab17014e350297cbd80071fcff37360e31b98f21ee487`.

Adapter osobno zlinkowano z **rzeczywistym** loaderem W11
`d3488a6bee016d1ea5e6e286dbc7d3a0249761fc`: 8 przypadków/58 assertions;
na main bez loadera 8/51, z jawnym unavailable. Rzeczywisty rejestr W3
`03c670caa6ee3d8ac2c478186548114f8e83927f` wczytał i rozwiązał natywny
method_profile W12. To zgodność deklaracji, nie wykonanie modelu.

Niezależny reproducer na rzeczywistej bibliotece potwierdził privacy authority,
redakcję po wymianie reguł, CAS przy błędnym settings, migrację legacy preferencji
i trzy bindingi, w tym dwie wersje jednej metody. Początkowy negatyw CTest
wynikał z fixture zmieniającej pack bez podbicia revision; poprawiono fixture,
bez zmiany asercji i progów. Zachowano też błąd typowania nowej asercji C++
i przerwane przebiegi starszych źródeł; nie są odbiorem końcowym.

Nie ma nowego C ABI/HTTP route ani wpięcia w App.tsx. W10 wykonuje bridge
i nawigację; W3 używa policy gate w produkcyjnym selektorze/writerze. W11 nie
jest jeszcze na main. Nie wykonano płatnych rozmów; W7 pozostaje właścicielem
tej walidacji/budżetu. API: [native](../../loom/src/onboarding/README.md),
[UI](../../loom/web/src/onboarding/README.md).

## Do wątku 9

Właściciel **przeniósł onboarding i R39–R40 z 10 do 12**. Proszę poprawić INDEX;
W10 ma wpięcie UI W12 w App/nawigację i istniejący transport. W12 nie przejmuje
plików W3/4/10/11. Przyjąć po rebase i świeżej wspólnej bramce, tylko przez
integratora. Mixed harnessy nie zastępują odbioru po integracji. RuntimeProfile
foundation W11 można przyjąć osobno; po scaleniu odbudować, aby adapter W12
wybrał gałąź z rzeczywistym loaderem.

## Do wątku 10

W12 jest właścicielem onboarding i R39–R40, zgodnie z poleceniem właściciela;
W10 wpina `OnboardingPanel` i `WhatAppKnows`. Kontrakt i helpery:
`loom/web/src/onboarding/types.ts`, `native-snapshot.ts`, `controller.ts`.
Jeden stabilny adapter: `getSnapshot`, `dispatch`, `dispatchLayer`, `modelRequest`,
`completeModelRequest`, `ingestModelReply`, `saveScenario`. Formularz zapisuje
te same akcje/review co rozmowa. Bridge C++:
`open/read/apply/update_pack/model_request/policy_decision`.

Zachować **zewnętrzny** revision jako CAS; revision profilu to inny licznik.
Przed transportem W2 preflight; tylko oczekiwany wzrost ×10 wymaga potwierdzenia.
`calls_authorized:false` oznacza przygotowanie, nie zgodę na wykonanie.
Do modelu kontroler przepuszcza: prompt, section, questions, context, candidates,
policy, reply_schema. Provider jest jawnym wyborem; token, CAS, graf i metadata
metody zostają lokalnie. Host wiąże odpowiedź z oryginalnym provider/token przed
native ingest. Nie dodawać surowego profilu. Zmiana polityki/scenariusza w innym
widoku też unieważnia request. Pause działa w trakcie transportu; spóźnione
odpowiedzi są ignorowane/odrzucane. Szczegóły i wszystkie envelopes w README UI.

## Do wątku 3

Autorytatywne API: `OnboardingStore::policy_decision(user, request)`.
Request: `{op:ask|store|send|infer,category,field?,provider?,detail?,sensitivity?,
provenance?}`. Response: allowed/reason, reguła, retencja, efektywna warstwa
i snapshot_revision. Selekcja, zapis i wysyłka sprawdzają odpowiedni op oraz
revision przy wykonaniu. `profile.privacy` to retained inspection, nie zgoda:
wyłączona/wykluczona/brak/proposal odmawia. Privacy nodes mają `applicable`
i `layer_status`. Wolne `privacy_decision` zakłada już aktywną złożoną politykę.

Graph method_profile używa natywnych version/recipe/prompt/parameter/preset
entities, exact bytes/hash i data-defined selection precedence. Seed nie tworzy
run; odebrana odpowiedź tworzy candidate→method_version i candidate→run,
z inference/review. Receipt hashuje canonical received reply; raw provider-byte
hash/measurement są jawnie unavailable. Historyczne wykonane wersje nie wracają
do aktywnego registry. Kilka wersji jednego ID jest dozwolone; wybór kreatora
używa prompt/recipe/parameter hash, revision i capability.

Dosiew: `target:profile`, `op:propose`, field/value/id/time,
`provenance:model_inferred`, rzeczywiste source_refs i opcjonalny
`method_execution` z exact method_profile, run_id, method_version_id,
request_token i response_sha256. Wynik nadal candidate; obserwacje importu
nie zmieniają się. W3/5 wykonuje istniejące metody zgodnie z polityką.

## Do wątku 11

Jeden `DefaultLayers` rozstrzyga wszystkie klucze, następnie
`runtime_profile_values(definition,layers,bindings)` wywołuje rzeczywiste
`RuntimeProfile::from_definition/with_values`. Bez własnego schema loadera;
W11 plików nie edytowano. `runtime_definition` i RFC6901 `runtime_bindings`
są danymi packa. Suppressed values są usuwane przed loader validation;
invalid required schema daje jawny błąd, bez powrotu do presetu.

Zmiany loadera nie są niezbędne. Do integracji pozostaje rejestracja domeny
onboarding i ewentualny publiczny bridge. Trwałe wykluczenie pozostaje metadata
ID w warstwach; RFC6902 remove nie chroni przed nowszym packiem. Na main
inspection raportuje `available:false`; native onboarding działa, bez udawania
walidacji W11. Mixed8/58 dowodzi adaptera, nie całego wdrożenia W11.

## Do wątku 2

W12 nie duplikuje usage policy ani nie wykonuje wywołań. Host W10/3 przed
modelem/ekstrakcją używa Waszego preflight z danych. Nie wprowadzono nowych
limitów ani potwierdzeń poza oczekiwanym ×10.

## Do wątku 4

Projection używa istniejących DTO i KnowledgeStore, bez zmian kb/packet.
Scoped `loom_onboarding_*` tables i per-user run odświeżają się atomowo.
Bez immutable GraphPacket receipt z prywatnymi snapshotami, którego delete
nie potrafiłby usunąć. Obserwacje importu i obce runy są zachowane.
Czasowa retencja i produkcyjny konsument reguł wymagają dalszego wspólnego
przyrostu z tym samym policy API.

## Do wątku 5

Dosiew to inferencyjne propozycje z Waszych obserwacji i receipts metod.
Bez drugiego importera/modyfikacji archiwów. Infer/store i never sprawdzane
przed zapisem.

## Do wątku 8

Nowe `.cpp` i testy rejestrują się przez istniejące CMake glob/discovery;
bez zmian CMake i C ABI. `.pack` używa deterministycznego
`gen_onboarding_pack.py --check`, poza KB JSON manifestem. Wszystkie stare
progi/przypadki zachowane. Dowód zawiera źródła z hashem, inventory, liczniki
i negatywy. Powtórzyć macierz po integracji; ten GCC przebieg nie jest
odbieraniem Clang/ASan.
