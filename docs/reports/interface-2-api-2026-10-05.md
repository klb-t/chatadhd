# Wątek 10 — UI nowych API, 2026-10-05

Gałąź: `gpt/interface-2-2026-10-04`. **Kod UI zakończony i wypchnięty. Odbiór wstrzymany: pełny mixed CTest120/121, regresja analizatora W1+W2.** Wszystkie bramki UI są zielone.0 płatnych/zdalnych wywołań modeli; tylko publiczne fixture i lokalne atrapy.

## Koniec sesji i punkt kontynuacji

**Commit ukończonego kodu UI:** `760ddc37be32a1162f81f39977389c03b208fb1c`. Jest gotowy do przeglądu; odbiór całości na main jest wstrzymany przez opisany niżej CTest120/121. Końcowy commit dokumentacyjny dodaje wyłącznie ten handoff oraz poprawny browser flag w instrukcji powtórzenia; bez dalszych zmian kodu/testów i bez nowej pracy.

**Archiwum jest już publiczne:** `archive/2026-10-05/interface-2-api-negatives`, commit `51c4ae5ce053cf49e5e1447e7445a44cd8556116`. [README/pełne źródła i replay](https://github.com/klb-t/chatadhd/tree/51c4ae5ce053cf49e5e1447e7445a44cd8556116/docs/archive/interface-2-api-negatives-2026-10-05). XZ6,691,128 bytes, SHA256 `686247ba906877df9bc27eb2a88bc818ef9626f54e6f5b559988dfef63d66957`;1628 plików/287,834,799 zachowanych bajtów, każdy człon sprawdzony strumieniowo przeciw SHA manifestu. Cztery historyczne, niezacommitowane wersje methods-ui.mjs nie mają oryginalnych bajtów; ograniczenie jawne, pełne odpowiedzi i zweryfikowane replay źródeł produkcyjnych zachowane. Nie przedstawiamy ich jako exact full-source replay.

**Następna osoba zaczyna tutaj:**

1. Fetch i sprawdzenie aktualnego fixu wątku1 dla attempt identity w `extract/semantic.cpp:798`, wraz z przekazaniem1/2 na końcu tego raportu. Sam approval/wyzerowanie unknown receipt nie jest poprawką. Izolowane odtworzenie i pełny negatyw są w archiwum.
2. Dołożenie oficjalnej poprawki1 albo rebase po jej odbiorze, z zachowaniem W1/W12 pinów i wspólnego kontraktu3/4. Nie edytować cudzych zakresów. Pozostałe luki API są wyszczególnione poniżej i nie blokują ukończenia obecnego kodu UI.
3. Pełny build/CTest/web na zamrożonych inputs po zmianie zależności; jeżeli zielone, uaktualnić gotowość dla9. Nie powtarzać już zielonych bramek UI bez nowej zmiany/failure. Nie scalać negative archive na main.

## Baza i zależności

- Main: `e4109df7e4af22b461def5f7d62e268d9b9a8825`; integrator: `66da570d3b5379492e128d940ad474467082c59f` (przyjęte3+4 i5). Świeży fetch: integrator `0a81480` dokłada tylko receipt5, bez zmiany API. Przeczytano aktualny INDEX i przekazanie10.
- W1: `50e6bb9f80b0cd855e4dd1efedaf3399cac5e4e6`,8 commitów dołożonych bez zmiany implementacji.
- W12: `3c0bc36552ef9851f1174946cfb108549aae3228`,7 commitów dołożonych bez zmiany implementacji.
- Kanoniczny kontrakt3/4: `loom/src/packet/METHOD_GRAPH.md`, `loom.method_graph/1` / `loom.method_run_trace/1`. Golden: `docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json`, SHA256 `8db8175c3b70c3947711ddf5daee5073b99bc5a5114ce0075e06e51932cec54e`.
- Poprzedni gotowy przyrost `fa7538d650da2f4ad37f5ff9254b60a6ee72938f` zachowany na `archive/2026-10-05/interface-2-before-api-intake`. Rebase na bazę integratora jest liniowy, bez merge commitów. Publiczny rebased stack: `3d7e5fd2246ffd94f803ec405dd371cf6a7a8f32`.

## Co działa

**Metody w grafie:** Methods przegląda natywne profile, metody, wersje, parametry, presety i nested/signed kombinacje. Edycja tworzy nową wersję i osobny native accept; aktywacja wymaga readback/CAS konfiguracji. Filtr „Results from this version” podąża po rzeczywistych produced-by/run Claims. Nowa wersja nie przejmuje starych wyników. Native próba:22 entities/35 Claims/22 sources →23/39/24; cały receipt_json identyczny przed/po odmowie overwrite i po restarcie.3 wyniki starej wersji,0 nowej. Opaque JSON zachowuje kolejność DTO, tokeny liczb i nieznane pola.

**Czat grafowy:** off oraz3 grafowe tryby z danych `loom/web/src/profiles/presets/graph-chat.json`. Definicje method/recipe/prompt/version są zapisywane w grafie przed osobną aktywacją. Kontrolka jawnie pokazuje globalny zakres istniejącego3; czyta ustawienia dopiero po otwarciu szczegółów,0 żądań przy montowaniu/przywróceniu. Recorded reply zachowuje tekst, schema error, źródła i natywną kompilację. Rozwiń/popraw sprawdza hash zapisanej kompilacji, adresuje fragment i wpisuje edytowalną prośbę do kompozytora. Nie wykonuje samoczynnego wysłania.

**Ekspercka analiza:** W1 catalog/resolve, pełny provider body jako exact bytes i server prepared handle, podgląd bez HTTP/wpisu fikcyjnego wykonania. Nowe prompt/schema/preset versions przechodzą przez MethodRegistry/GraphPacket. Execute wymaga dokładnego przygotowania, native method binding i admission W2; attempt marker zużywany przed transportem. Pierwsza odpowiedź zachowana przed parse/accounting, także błędna/binarna. Pochodzenie modelowe tylko dla rzeczywistej treści modelu. Potwierdzenie ×10 wiąże oryginalny receipt; unknown outcome pozostaje inspection, bez retry. Strict input odrzuca duplicate keys. Panel mieści16 kontrolek w drawer420px i phone390px; exact1099 bytes zapytania zachowane w obu widokach.

**Onboarding/R40:** App podłącza `OnboardingPanel` i `WhatAppKnows` W12 przez jeden stabilny user-bound host. Jawny Open/Create/Unload, zapis w browser settings tylko identity; profil/historia/defaults/prywatność w native DB. Formularze, osobny review, scenariusz, warstwy, overrides i exclusions korzystają z W12. Stable layer keys, event identity/source_refs i exact outer int64 revision jako decimal string zachowane. Konflikt/utrata odpowiedzi wymaga reload/inspection, bez automatycznego powtórzenia mutacji. W12 raw snapshot jawnie raportuje brak RuntimeProfile W11.

**HTTP:** `/api/onboarding`, `/api/methods`, read-only `/api/methods/chat-settings`, `/api/analysis` i adresowanie fragmentów `/api/graph-reply` pożyczają istniejący statyczny Runtime przez autorytatywną deklarację context. Nie tworzą drugiej DB/runtime ani nowych C ABI exports. LoomApi capabilities są optional; JNI bez implementacji raportuje unavailable. Config hash+mutex serializuje współpracujące zapisy w tym procesie HTTP, nie obiecuje CAS przeciw innemu procesowi.

## Liczby przed i po

Przed: historyczny receipt fa7538d —108/108 CTest,659 native cases/24465 assertions,1276 Python/0skip,84 fixture,20 nativeUI,16 E2E,web PASS. Nie przypisujemy go nowej mieszanej bazie.

| Bramka obecnego przyrostu | Wynik |
|---|---|
| Pełny GCC/Werror/vendored build | PASS |
| Końcowy TypeScript/Vite build | PASS |
| Pełny mixed CTest |120/121;761 native cases (758PASS/3FAIL),29103 assertions (29095PASS/8FAIL),1322 Python/0skip |
| Niezmienione wcześniejsze fixture |84/84 |
| Wcześniejsze native UI |20/20 |
| Wcześniejsze native E2E |16/16 |
| Metody — real native/store/config/restart |14/14 |
| Czat grafowy — real ChatEngine/fragmenty |14/14;4 lokalne fake provider calls |
| Analiza —4 browser +1 C++ native fixture |5/5 |
| Onboarding HTTP — native DB/CAS/restart |8/8 |
| Host W12 —15 pure +6 React/LoomHttpApi |21/21 |
| Whole-App native + geometria drawer/phone |8/8;51 real HTTP responses;0 provider/external/page errors |

Końcowa binarka SHA256 `c65181cb064cf5326b0006bddf31f2244d369e1b6607e20cab6b559447475c2f`. Poszczególne końcowe próby UI mają zgodne before/after source/binary/bundle hashes. Podczas pełnego CTest zmieniły się tylko app.cpp/method bridge/server binary; kernel/test inputs i pozostałe5binarek były identyczne, a testy serwera118/119 wykonały już c651. Nie przedstawiamy całego tego negative receipt jako frozen run. Odziedziczony opt-in `unit.test_catalog_scale` wykonał0cases i nie jest policzony jako pokrycie. Pełny Clang/vendored pozostaje macierzą8; wcześniejsze unused [this] naprawione i Clang18 proof retained w poprzednim przyroście.

## Negatywy i odtworzenie

[RESULTS + archiwa pozytywne](interface-2-api-2026-10-05-evidence/RESULTS.json) zawierają pełne HTTP/receipts, komendy, source manifests, źródła harnessów i screenshoty. [Instrukcja powtórzenia](interface-2-api-2026-10-05-evidence/README.md).

Pełne negatywy pozostają osobno na `archive/2026-10-05/interface-2-api-negatives`: budowa j4/OOM i pusty artefakt po przerwaniu, pierwsze baseline reads, zachowany test braku opcjonalnego W11, test calibration failures, real config DTO reorder, niespójna aktywacja selekcji, Analysis receiver, clipping oraz pełny CTest/JUnit/LastTest i isolated repro. Oryginalne źródła lub zweryfikowane replay patches są opisane hashami; rekonstrukcje jawnie oznaczone. Żadnych testów nie usunięto ani progów nie poluzowano.

## Czego API jeszcze nie dostarcza

Publiczny per-call context_execution/immutable Chat.resume i składanie dowolnego dynamicznego GraphPacket z dowolną historią wiadomości pozostają luką3. Obecne presety zachowują istniejący graph memory/knowledge context; nie deklarują dodatkowej kompozycji, której nie ma w API. Handle analizy jest process-local, znika na restart i nie jest trwałym TaskEngine.

Model interview W12 wymaga dedykowanego privacy-filtered/W2-bound completion transportu. Host go nie udaje przez ordinary chat; formularze/review/layers działają. RuntimeProfile W11 nie jest w tej bazie; dedykowany status jest w raw snapshot, bez projekcji w obecnych widokach W12.

[Publiczny kontrakt TaskEngine](interface-2-task-engine-adapter-2026-10-04.md) opisuje realne4 operacje list/get/cancel/global recovery. Brakuje generic submit/checkpoint-write/complete/public pause/resume-one/TaskRecord CAS; workflow.task.execute pozostaje unavailable. Global recovery nie jest wznowieniem pojedynczego workflow ani potwierdzeniem jego ukończenia.

## Do wątku N

- **Do wątku1:** jedyny bloker pełnej bramki: `test_knowledge_semantic.cpp`252/255/256/319/354/355/358/362. Isolated13cases/125assertions ponawia10/117PASS,3/8FAIL. `extract/semantic.cpp:798` nadaje nowej jawnej próbie ten sam operation_id z run|chunk|wire-request_hash;905–906 zostawiają unknown tokens/cost, więc policy353 zachowuje unresolved, a258–263 zwraca poprzedni receipt. Odmowa caller880–886 jest poprawna. Opcje max_proposals/response_bytes zmieniają cache key829–830, ale nie attempt identity. Nadać nowej jawnej próbie osobne ID i zachować je przez preview/confirmation/checkpoint/resume, bez zmiany cache/candidate identity i anty-double-dispatch839–844.
- **Do wątku2:** zachować unknown/unresolved accounting i odmowę ponowienia tej samej operation_id; nie odblokowywać starego receipt ani nie osłabiać strażnika dla testu. Korekta nowej tożsamości należy do1.
- **Do wątku3:** StageContext knowledge.h101/109 ma run/resume checkpoint, bez osobnego attempt ID — uzgodnić interoperacyjny identyfikator z1. Graph guard nie jest przyczyną regresji. Dostarczyć per-call context_execution/immutable Chat.resume oraz pełną konfigurowalną kompozycję grafu do dowolnej historii.
- **Do wątku4:** publiczne C ABI/JNI odpowiedniki użytych source-private bridges, dokładne JSON/receipts/fragment addressing i transakcyjne expected_rows. Nie utożsamiać strukturalnego bindingu z dowodem wykonania.
- **Do wątku8:** opt-in catalog_scale nadal0cases; utrzymać jawny guard i pełną macierz Clang/vendored bez luzowania progów.
- **Do wątku12:** model interview completion wymaga rzeczywistego transportu z effective privacy, W2, provider/request_token i exact prepared CAS. Rozszerzyć projekcję RuntimeProfile capability, jeśli status ma być widoczny jako kontrolka.
- **Do wątku9:** kod10 gotowy, **odbiór WSTRZYMANY do naprawy1/2 i ponownego pełnego CTest**. Kolejność1 →12 →10, dokładne źródła powyżej; main/STATE/README/INDEX nie edytowano. Żadne pozostałe API luki nie są zamaskowane fikcyjnym adapterem. Negatywnej gałęzi archiwalnej nie scalać na main.
