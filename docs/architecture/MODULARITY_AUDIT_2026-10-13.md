# Audyt modułowości — P5 (2026-10-13)

Cel właściciela: moduły **dowolnie łączalne, wymienialne, mieszalne**. Audyt statyczny (bez zmian kodu): skany `make_unique`,
łańcuchy `if (== "literał")`, adresy/modele w źródle, ścieżki wywołań `/chat/completions`, C ABI. Dowody: `plik:linia` w `loom/`.
Zakres: `loom/{include,src,server,cli,web,android}`, Python `engine/`, `core/` (5,9k linii, powiela C++).

## 1. Mapa modułów

Wybór: **R** = rejestr/fabryka, **D** = dane (pack/profil), **W** = na sztywno.

| Obszar | Rola | Szew publiczny | Wybór dziś | Zależni |
|---|---|---|---|---|
| Runtime (`src/runtime.cpp:159-281`) | korzeń kompozycji | `runtime.h` | **W** (28 × `make_unique`, stały graf) | wszystko |
| Profile (`model/runtime_profile.cpp`, 19 domen) | warstwowe dane + `explain` | `runtime_profile.h` | **D** | media, storage, graph_*, knowledge… |
| Magazyny (`storage/`) | bajty → backend | `StorageBackend`, `BackendRegistry` | **R+D** (wzór) | BlobStore, server, CLI |
| Dostawcy (`providers/providers.cpp`) | manifesty możliwości | `ProviderRegistry::find/can` | **D**, ale prawie nieużywany | tylko `graph_reply.cpp:509`, `embedding.cpp` |
| LLM chat (`chat/`, `semantic/`, `worker/`, `archive/pipeline`, `context_engine`) | wołanie modelu | brak (`HttpTransport` tylko niżej) | **W** (6 kopii URL+nagłówków) | czat, analiza, kontekst |
| Batch (`chat/batch_api.cpp`) | tani tryb LLM | `SemanticBatchAPI` | **W** (`type=="anthropic"`) | worker |
| Media ASR/OCR (`media/`) | mowa/obraz → tekst | `AsrProvider`, `OcrProvider` | **R lokalny** + `D` (`asr_order`) | importer, voice |
| Embeddingi (`providers/embedding.cpp`, `resolve/vectors.cpp`) | wektory | `EmbeddingProvider`, `VectorSpace` | **R+D**, adapter tylko `openai` | selector, resolve, kontekst |
| Selector (`search/selector.cpp`) | 3 warstwy szukania | `SelectorEngine(tier, embedder)` | **W** (tier=int) | pamięć, grafowa pamięć |
| Metody kontekstu (`context/method_*`) | kanały kandydatów + fuzja | `MethodRegistry`, `CandidateChannel` | **D** + `if` po capability | czat, UI metod |
| Importer (`import/`) | formaty → wiadomości | `ConversationImporter` | **W** (łańcuch `fmt==`), presety D | CLI, server, web |
| Analizator semantyczny (`semantic/`, `extract/`) | encje/tematy | `SemanticAnalyzer`, `Lexicons` | **D** (reguły w packu) | graf, worker |
| Worker semantyczny | tryb regex/llm/batch | `SemanticWorker` | **W** (bool `use_llm`, string `mode`) | graf |
| Graf (`graph/`, `kb/`, `packet/`) | węzły, wpisy, pakiety | `graph_entry.h`, `GraphPacketStore` | **D** (providery wpisu) | UI, kontekst |
| GitHub (`github/`) | synchronizacja VCS | `loom_github_sync` | **W** | UI |
| Krypto (`crypto/`) | koperty, sygnatury | `crypto_suite.h` | **R** (backend) | magazyny |
| Zdarzenia/zadania (`core/`) | bus, `TaskEngine::register_handler` | `event_bus.h`, `tasks.h` | **R** | większość |
| C ABI (`capi/`, 116 symboli) | granica | `loom.h` | stałe symbole | CLI, server, JNI |
| Server (`server/src/app.cpp`, 71 tras) | REST/SSE | HTTP | **W** (trasa = funkcja) | web |
| Web (`web/src/api/loom-api.ts`) | klient | `LoomApi` (http/jni) | **R** (2 transporty) | komponenty |
| Android | WebView + JNI | most | **W** | — |
| Python `engine/`,`core/` | pierwszy klient | — | **W**, duplikat C++ | Kivy |

## 2. Kryteria i ocena (0–3)

Operacyjnie „dowolnie łączalne/wymienialne/mieszalne” = 6 testów (po jednym na kolumnę):

| Kod | Kryterium | Test operacyjny |
|---|---|---|
| I | Interfejs | konsument bierze abstrakcję, nie klasę konkretną |
| R | Rejestr | nowa implementacja = jeden wpis, zero edycji konsumenta |
| D | Wybór z danych | zmiana packa/ustawienia zmienia wybór bez rekompilacji |
| K | Kompozycja | kilka naraz: chain / fallback / ensemble / mirror |
| S | Zakres (R47) | wybór per aplikacja/użytkownik/rozmowa z regułą konfliktu i `explain` |
| G | Graf (G1–G10) | implementacja i wybór są węzłami; przebieg ma `produced_by` |

3 = spełnione, 2 = częściowo, 1 = ślad, 0 = brak. Dowód przy każdej ocenie < 3.

| Obszar | I | R | D | K | S | G | Dowód (oceny < 3) |
|---|---|---|---|---|---|---|---|
| Magazyny | 3 | 3 | 3 | 2 | 2 | 2 | K: mirror/replika jest, brak ensemble `storage_service.cpp`; S: kategoria, nie rozmowa; G: lokatory częściowo; `gdrive` „planned” `storage_backend.cpp:73` |
| Profile | 3 | 2 | 3 | 1 | 2 | 3 | R: domeny z `gen_runtime_profiles.py`, nowa = regeneracja `.inc`; K: brak składania profili |
| Dostawcy (manifesty) | 2 | 2 | 3 | 1 | 1 | 1 | konsumuje tylko `graph_reply.cpp:509` i embedding; `providers.cpp:412` `if (m.id=="openrouter")`; sekrety po stałych kluczach `providers.cpp:192` |
| LLM chat | 0 | 0 | 1 | 0 | 1 | 1 | 6 kopii `/chat/completions`: `chat_engine.cpp:1215`, `semantic_llm.cpp:126`, `semantic_worker.cpp:712`, `batch_api.cpp:43`, `archive/pipeline.cpp:286`, `context_engine.cpp:581`; `get_string("api_key")` w 6 miejscach; domyślny URL `config.cpp:199`, `context_engine.cpp:566`; nagłówki `HTTP-Referer` w 3 |
| Batch | 0 | 0 | 0 | 0 | 1 | 1 | `batch_api.cpp:214` `type=="anthropic"`, URL `:96,:167` |
| Media ASR/OCR | 3 | 1 | 2 | 2 | 1 | 1 | R: fabryka lokalna `media_providers.cpp:361-365`, OCR tylko `ocr_space` `:380`; K: kolejność jest (`asr_order`), brak ensemble; osobny od `ProviderRegistry` |
| Embeddingi | 3 | 2 | 2 | 1 | 1 | 2 | adapter tylko `openai` `embedding.cpp:41`; schematy auth w stałym zbiorze `:44` |
| Selector | 2 | 0 | 1 | 0 | 1 | 1 | tier jako `int`, `selector.cpp:104`; brak rejestru; nie korzysta z `MethodRegistry` |
| Metody kontekstu | 3 | 1 | 3 | 3 | 2 | 3 | R: `method_channels.cpp:271-284` łańcuch `if (capability==…)`, jedna lista dwa razy (`context_candidates.cpp:62`, `context_engine.cpp:1589`) |
| Importer | 1 | 0 | 2 | 1 | 1 | 2 | `importer_core.cpp:126-141,560-568` formaty w `if`; `export_archive.cpp:165-650` ~20× `prov=="openai"/"anthropic"`; `catalog/scan.cpp:76-90`, `extract/extract.cpp:126` `"chatgpt"/"claude"` |
| Analizator/ekstrakcja | 3 | 1 | 3 | 1 | 2 | 2 | reguły to dane; brak łączenia analizatorów (jeden `Impl`, `analyzer.cpp:167`) |
| Worker semantyczny | 1 | 0 | 1 | 0 | 1 | 2 | `semantic_worker.cpp:850` `use_llm ? "llm":"regex"`, tryb batch gałęzią `:244`, `knowledge/engine.cpp:296`, `archive/pipeline.cpp:156` |
| Graf/wpis | 2 | 2 | 3 | 2 | 2 | 3 | `graph_entry.cpp:255` `kind=="local"` |
| GitHub/VCS | 1 | 0 | 1 | 0 | 1 | 1 | `capi_media.cpp:129-135` akcje magazynu przez `loom_github_sync`; `github_sync.h` konkretna klasa |
| Krypto | 3 | 2 | 2 | 1 | 1 | 2 | — |
| Runtime (korzeń) | 1 | 0 | 0 | 0 | 0 | 0 | `runtime.cpp:167-259` stały graf, brak „wstrzyknij/zastąp moduł” poza HTTP |
| C ABI | 2 | 1 | 1 | 1 | 0 | 0 | 116 symboli = stała lista; nadużycie `loom_github_sync` i `loom_media_*` jako worek JSON |
| Server | 1 | 0 | 0 | 0 | 1 | 0 | `app.cpp` 71 ręcznych tras, `svr_.Get("/api/storage/...")` `:745-758`; brak tras z rejestru |
| Web | 3 | 2 | 2 | 1 | 2 | 1 | transport podmienialny; komponenty 1:1 do paneli |
| Python | 0 | 0 | 0 | 0 | 0 | 0 | `engine/providers.py:78,152,258`, `engine/batch_api.py:122,170`, `engine/config.py:16` — powielone adresy i klasy; wymaga decyzji o losie |

## 3. Ustalenia (wg wpływu na użytkownika)

| # | Ustalenie | Lokator | Dlaczego narusza | Najmniejsza poprawka | Ryzyko |
|---|---|---|---|---|---|
| 1 | **Nie da się zmienić ani zmieszać dostawcy LLM.** Jeden `base_url` + jeden `api_key`; 6 kopii wywołania | tabela §2, wiersz LLM | brak portu; nie ma „model A czat, model B analiza, Anthropic bezpośrednio, lokalny Ollama”; R47 per rozmowa niemożliwy | jeden `LlmPort` (`complete/stream`) nad `ProviderRegistry::find("llm","chat.completions")`; 6 miejsc woła port; adres, nagłówki, sekret z manifestu | średnie: czat i strumień; testy przechwyconego żądania |
| 2 | **Dostawcy to martwe dane**: manifesty istnieją, kod i tak czyta stałe klucze | `providers.cpp:192,412` | wzorzec A (dane są, konsument nie czyta) | jak #1 + sekret z `auth_secret` | niskie |
| 3 | **Brak operatorów kompozycji.** Jest kolejność ASR, fuzja metod; brak chain/fallback/ensemble/mirror jako wspólnych | `media_providers.cpp:372`, `method_channels.cpp:249` | „mieszalne” działa tylko w dwóch miejscach, różnie | `Composite<Port>` z 4 operatorami, wybór z profilu | średnie |
| 4 | **Importer: format i dostawca eksportu w `if`.** Nowy format = edycja 3 plików | `importer_core.cpp:126-568`, `export_archive.cpp:165+` | brak rejestru formatów; nazwy platform w kodzie (R42) | `FormatRegistry` (detect+body) i tabela `provider→handlery`; presety już są | średnie: dużo kodu, testy zgodności |
| 5 | **Dwa światy możliwości**: `MediaProviders` ma własną fabrykę, obok `ProviderRegistry` i `BackendRegistry` | `media_providers.cpp:361`, `providers.h:96`, `storage_backend.h:80` | trzy rejestry o tym samym kształcie | jedno `AdapterRegistry<Port>` (fabryka po `adapter`), które trzy istniejące wywołują | niskie |
| 6 | **Worker/batch/selector: tryb jako string lub bool** | `semantic_worker.cpp:850`, `batch_api.cpp:214`, `selector.cpp:104` | nie da się dodać trybu ani połączyć dwóch | tryby jako wpisy rejestru, wybór z profilu `worker` | niskie |
| 7 | **ABI i server jako worek**: akcje magazynu wchodzą przez `loom_github_sync`; 71 tras ręcznie | `capi_media.cpp:129`, `app.cpp:745` | dostęp do modułu zależy od historii, nie od adresu (G1) | jeden `loom_invoke(port, action, json)` + trasy generowane z tabeli akcji | wysokie: kontrakt ABI, `test_abi_compat` |
| 8 | **Korzeń kompozycji sztywny**: `Runtime` tworzy 28 obiektów na stałe | `runtime.cpp:167-259` | brak wstrzykiwania portów w testach i w wdrożeniach (serwer vs telefon) | `RuntimeParts` z domyślnymi fabrykami, nadpisywalnymi z profilu | średnie |
| 9 | **Lista nazw w `if` mimo packów**: `tfidf`, `regex`, `local`, `openai`, `claude` | `method_channels.cpp:271`, `graph_entry.cpp:255`, `embedding.cpp:41` | wzorzec B | tabela capability → fabryka (patrz #5) | niskie |
| 10 | **Python = druga implementacja** powielająca adresy i klasy | `engine/providers.py`, `batch_api.py` | dryf; zmiana dostawcy w dwóch miejscach | decyzja: zamrozić Python (CLAUDE.md: zgodność nie jest wymagana od 2026-09-29), nie refaktorować | brak |

Zostawione bez ruchu (mocne): `StorageBackend`+`BackendRegistry`, `RuntimeProfile`, `MethodRegistry` + `CandidateChannel`, `EventBus`, `LoomApi` (http/jni).

## 4. Wzorzec docelowy: jedna funkcja `Port` na istniejących klockach

Nie nowy mechanizm — uogólnienie `BackendRegistry` (fabryka po `kind`) + `ProviderRegistry` (możliwości, `find`) + `RuntimeProfile` (wybór z zakresów).

| Element | Z czego | Rola |
|---|---|---|
| **Port** (typowany interfejs) | `StorageBackend`, `AsrProvider`, `EmbeddingProvider` | `LlmPort`, `AsrPort`, `OcrPort`, `EmbedPort`, `FormatPort`, `VcsPort`, `BatchPort`, `SearchTierPort` |
| **Rejestr adapterów** `AdapterRegistry<Port>` | `BackendRegistry` | `adapter` (string z manifestu) → fabryka; planned/implemented jak dziś |
| **Możliwości** | `ProviderManifest.capabilities` | `find(resource, capability, constraints)` wybiera kandydatów |
| **Wybór** | `RuntimeProfile` domena `ports` + zakresy R47 (aplikacja → użytkownik → rozmowa) | który adapter dla której roli; `explain` gratis |
| **Kompozycja** | `asr_order`, fuzja metod, `placement` | `chain` (potok), `fallback` (kolejność), `ensemble` (równolegle + fuzja), `mirror` (zapis wielokrotny) |
| **Graf** | `graph_entry` providers, `produced_by` | adapter, profil wyboru i każdy przebieg to węzły; sekret tylko jako odniesienie (G6) |

Zasada szwu: konsument zna tylko `Port` i rolę (`"chat.default"`, `"analysis.fast"`), nigdy `base_url`/model/sekret.
Kolejność wdrożenia: najpierw port tam, gdzie boli (LLM), operatory kompozycji wyprowadzić z dwóch istniejących wystąpień, nie projektować z góry.

## 5. Plan: fale (jeden agent Sonnet, po kolei)

| Fala | Brief | Zakres | Zależy od |
|---|---|---|---|
| 1 | `B-MOD-01-llm-port` | `LlmPort` + adapter z manifestu; 6 miejsc woła port; test przechwyconego żądania | — |
| 1 | `B-MOD-02-adapter-registry` | `AdapterRegistry<T>`; media, embedding, batch na nim | 01 |
| 2 | `B-MOD-03-port-profile` | domena profilu `ports`: rola → adapter, zakresy R47, `explain` | 01 |
| 2 | `B-MOD-04-composition` | `chain/fallback/ensemble/mirror` na portach | 02, 03 |
| 3 | `B-MOD-05-format-registry` | importer: `FormatRegistry`, tabela dostawców eksportu | 02 |
| 3 | `B-MOD-06-worker-search-modes` | tryby workera, batch, selector tier jako wpisy rejestru | 02, 03 |
| 4 | `B-MOD-07-runtime-parts` | `RuntimeParts`, wstrzykiwanie portów | 02 |
| 4 | `B-MOD-08-abi-server-invoke` | `loom_invoke` i trasy z tabeli akcji; koniec nadużycia `loom_github_sync` | 07 |
| 5 | `B-MOD-09-graph-addressing` | adaptery, wybory i przebiegi jako węzły (G1–G6) | 03 |

Python: bez refaktoru (ustalenie 10). Każdy brief: pełna brama, wpis w `INDEX.md`, decyzja i sposób cofnięcia.
