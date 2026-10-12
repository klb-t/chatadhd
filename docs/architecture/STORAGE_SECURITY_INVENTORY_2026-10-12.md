# Inwentarz: gdzie leżą dane i klucze dziś (T5a, 2026-10-12)

Wątek T5a (WP-F, część 1), wymaganie R51. Stan kodu na gałęzi `claude/t5-storage-crypto`
(bazuje na `claude/chataddhd-cpp-loom-core-IRGRN`). Tylko odczyt kodu; nic nie uruchamiano
na prawdziwych danych. Lokatory `plik:linia` dotyczą tej gałęzi.
Skróty: **Py** = aplikacja Kivy (`engine/`, `core/`), **C++** = `loom/src/`, **dir** = katalog
danych (`~/.chatadhd/`, Android `/storage/emulated/0/Documents/ChatADHD/`, `CHATADHD_DATA`).
`nie zweryfikowano` = nie dało się sprawdzić w kodzie, powód podany.

## 0. Wnioski od razu

| # | Ustalenie | Waga | Dowód |
|---|---|---|---|
| F1 | **Szyfrowanie nie chroni żadnych danych.** Py: `core/crypto.py` jest tylko eksportowane (`core/__init__.py:2`); żaden moduł `engine/`, `gui/`, `main.py` go nie wywołuje (grep `crypto` = 0). C++: `CryptoVault` szyfruje wyłącznie tekst podany przez ABI (`loom_crypto_encrypt`), a `setup` zapisuje tylko znacznik weryfikacji hasła (`crypto.cpp:280-296`). DB, BlobStore, memory.json, eksporty, FTS są zawsze jawne. | wysoka | `CLAUDE.md` mówi „Optional AES-256-GCM zero-knowledge”; kod tego nie robi na danych |
| F2 | **Android: szyfrowanie wyłączone w ogóle** (`LOOM_WITH_OPENSSL=OFF`, `loom/android/app/src/main/cpp/CMakeLists.txt:22`; `loom_crypto_status` → `available:false`, `loom/android/README.md:198-206`) **a dir leży we współdzielonym storage** (`MainActivity.kt:91-99`, `AndroidManifest.xml:9-20`, `MANAGE_EXTERNAL_STORAGE`). `chmod 600` na tym woluminie jest bez efektu (nie zweryfikowano: brak urządzenia), więc `secrets.json` z kluczami API jest czytelny dla każdej aplikacji z „All files access”. | wysoka | świadoma decyzja (przeżywa odinstalowanie, `AndroidManifest.xml:9-16`), ale bez ostrzeżenia w UI (nie zweryfikowano UI) |
| F3 | **Py zapisuje `secrets.json` z oknem jawności:** `tmp.write_text` (domyślne umask, zwykle 644) → `replace` → dopiero potem `chmod 600` (`engine/config.py:58-65`); błąd `chmod` jest połykany (`config.py:88-91`). C++ robi to poprawnie: `open(...,0600)` przed zapisem (`loom/src/util/fs.cpp:49`), `config.cpp:185,268`. | średnia | |
| F4 | **Klucz Google Speech w URL** (`engine/providers.py:180`); wyjątek `requests` zawiera pełny URL i trafia do `ProviderError(f"...{e}")` (`providers.py:217-218`) → UI i `LOGBUF`. | średnia | pozostałe wywołania używają nagłówka `Authorization` |
| F5 | **Logi mogą zawierać treść rozmów:** Py loguje na DEBUG do stdout i bufora w pamięci `LOGBUF` (`main.py:24-28`, `gui/base.py:96-103`): treść odpowiedzi modelu (`chat_engine.py:358` pierwsze 100 znaków fragmentu SSE), ciało błędu API do 500 znaków (`chat_engine.py:290-292`, bywa echem promptu). Brak pliku logu (katalog `logs/` tworzony, nieużywany: `paths.py:87`, `runtime.cpp:369` tylko podaje ścieżkę). Nie znaleziono logowania nagłówków ani kluczy (grep `Authorization` → tylko budowa żądań). | średnia | |
| F6 | **Pliki pochodne mają jawny tekst i szersze uprawnienia niż sekrety:** `chatadhd.fts.db` (indeks FTS5 całego tekstu wiadomości: `fts.cpp:165-170`), `memory.json` (`memory_engine.cpp:111`, `owner_only=false`), `models.json` (`providers.cpp:39` `write_file`, domyślne umask), BlobStore chmod 444 (czytelny dla wszystkich lokalnych użytkowników: `provenance.cpp:104`, plik tworzony `write_file` z umask). Sam dir tworzony bez ograniczenia trybu (`config.cpp:44-51`, `engine/paths.py:79-88`). | średnia | |
| F7 | **Sync GitHub wypycha jawny tekst do zdalnego repo,** domyślnie `*.py *.md *.json *.txt`, wykluczenie tylko `secrets.json` (`engine/github_sync.py:45-49`, `loom/data/runtime/github.pack`); jeśli `local_path` = dir danych, idą `config.json`, `memory.json`, `models.json`, `crypto.json`, `github_sync.json`. Zdalne repo nie jest szyfrowane. | średnia | |
| F8 | **Serwer HTTP:** domyślnie `127.0.0.1` bez uwierzytelniania (`server/src/main.cpp:36-40`, `app.cpp:250-259`); ujawnia `/api/secrets` (tylko lista kluczy i `has`; zapis przez POST: `app.cpp:600-618`). Token opcjonalny (`LOOM_SERVER_TOKEN`). Przy `--host 0.0.0.0` bez tokenu wszystko jest otwarte. | średnia | |
| F9 | Do dostawców wysyłany jest **jawny tekst** (CLAUDE.md „Security Notes”): OpenRouter (chat, modele, semantyka), Anthropic Batch, Groq/Google (głos), GitHub, dostawcy embeddingów. Nie ma rejestru „co wysłano” per kategoria (EC-E05 zakłada go). | informacja | tabela §4 |
| F10 | W repo nie znaleziono wzorców kluczy (`sk-or-v1-`, `sk-ant-`, `ghp_`, `github_pat_`, `AIza`) ani plików `*.pem`/`*.key`/`secrets*` pod kontrolą wersji (`git grep`, 3 ZIP-y dowodowe: 0 trafień). Fixture’y w `loom/tests/fixtures` są syntetyczne (nie zweryfikowano treści każdego). | dobra | |

## 1. Dane w katalogu danych (dir)

Rozwiązanie ścieżki: `engine/paths.py:42-76` / `loom/src/core/config.cpp:53-84` (override → `CHATADHD_DATA`
→ katalog z `.chatadhd_data` → pierwszy kandydat). Nazwy plików: `config.cpp:86-101`.

| Kategoria | Lokalizacja | Format | Kto czyta | Szyfr. dziś | Klucz | Sync | Lokator (zapis / odczyt) |
|---|---|---|---|---|---|---|---|
| Rozmowy, wiadomości, węzły, linki, proweniencja, KB | `dir/chatadhd.db` | SQLite WAL, schema_version 4 | każdy z prawami do dir (644 przy domyślnym umask) | brak | n/d | tylko przez kopię pliku / GitHub sync gdy dir w `local_path` (nie `.db`: wzorce include nie obejmują `*.db`) | Py: `main.py:89`, `engine/db.py:24,45`; C++: `core/config.cpp:89`, `db/db.cpp:303-305` |
| WAL / SHM | `dir/chatadhd.db-wal`, `-shm` | SQLite | jak wyżej; zawiera świeże, niescommitowane do pliku głównego strony = ten sam jawny tekst | brak | n/d | brak | tryb WAL: `engine/db.py:45`, `db/db.cpp:303` |
| Indeks FTS (kopia tekstu) | `dir/chatadhd.fts.db` | SQLite FTS5, tabele `loom_fts`, `loom_fts_docs` | jak wyżej | brak | n/d | brak | `core/config.cpp:90`, `db/fts.cpp:151,165-170`, `runtime.cpp:170` |
| Ledger zużycia | `dir/usage-policy.sqlite` | SQLite; `usage_operations/_samples/_events` (JSON `record`/`payload`) | jak wyżej; zawiera identyfikatory operacji, zużycie, nie ma tekstów rozmów (nie zweryfikowano pól `payload` w każdej ścieżce) | brak | n/d | brak | schemat `policy/usage_policy.cpp:227-237`; otwarcia: `extract/semantic_usage.h:34`, `worker/semantic_worker.cpp:583`, `chat/chat_engine.cpp:666`, `chat/graph_reply.cpp:429`, `import/import_usage.cpp:35`, `capi/capi_packet.cpp:124,133`, `capi/capi_config.cpp:20`, `context/context_goal_usage.h:70`, `context/provider_vector.cpp:630`; wzorzec nazwy: tylko C++ (Py nie ma ledgera) |
| Cache wektorów embeddingów (pochodne treści) | `dir/context_embedding_cache/<klucz>.json` + `representations.sqlite` | JSON `loom.embedding_cache/2` (wektor, model) / SQLite | tylko właściciel (0600: `provider_vector.cpp:233,275`) | brak (embedding da się częściowo odwrócić; nie szacowano) | n/d | brak | `context/provider_vector.cpp:683,222-236,272-277` |
| BlobStore (surowe źródła, artefakty) | `dir/blobs/<h0:2>/<h2:4>/<sha256>` + `blobs/tmp/` + tabela `loom_blobs` w DB | bajty bez zmian | wszyscy lokalni (chmod **444**) | brak | n/d | brak; adresowanie po treści (dedup) | `include/loom/provenance.h:20-56`, `core/provenance.cpp:84-110`; ścieżka `core/config.cpp:100`; `runtime.cpp:176` |
| Sekrety | `dir/secrets.json` | JSON (`api_key`, `anthropic_batch_key`, `github_token`, klucze dostawców) | właściciel (600), **Android: nie zweryfikowano** (F2) | brak (jawny JSON) | n/d (sam jest magazynem kluczy) | wykluczony ze sync (`github.pack`, `github_sync.py:48`) | Py: `engine/config.py:53-91`, `main.py:88`; C++: `core/config.cpp:185,267-276`, `util/fs.cpp:45-73`; HTTP: `server/src/app.cpp:600-618` |
| Konfiguracja | `dir/config.json` | JSON | jak DB (644) | brak | n/d | tak, jeśli dir w `local_path` (`*.json`) | Py: `engine/config.py:93-` (`Config`), `main.py:87`; C++: `config.cpp:224` |
| Drzewo pamięci | `dir/memory.json` | JSON, atomowy zapis; **trafia do promptu** | 644 | brak | n/d | jak config | Py: `engine/memory_engine.py:120-144`, `main.py:93`; C++: `memory/memory_engine.cpp:105-113` (`owner_only=false`) |
| Cache modeli | `dir/models.json` | JSON (id, nazwa, cena); bez treści użytkownika | 644 | brak | n/d | jak config | Py: `engine/models.py:105-111`; C++: `providers/providers.cpp:35-40,142-183` |
| Stan sync GitHub | `dir/github_sync.json` | JSON (repo, gałąź, `local_path`, wzorce; token jest w `secrets.json`) | 644 | brak | token: `secrets.json` | n/d | C++: `github/github_sync.cpp:150-185,561`; Py: `engine/github_sync.py:362-422` |
| Sejf szyfrowania | `dir/crypto.json` | JSON `EncryptedBlob` ze znacznikiem weryfikacji (nie dane) | właściciel (0600: `crypto.cpp:276`) | tak (sam znacznik) | hasło w pamięci procesu (`password_`) po `unlock`; nigdzie nie utrwalone | **trafia do sync GitHub** (`*.json`) | `runtime.cpp:208`, `crypto/crypto.cpp:255-354`, `include/loom/crypto.h:62-82` |
| Eksporty | `dir/exports/` | katalog tworzony; pliki z `import/exporter.cpp:129` (`atomic_write`, 644) | 644 | brak | n/d | brak | `config.cpp:49,97`, `import/exporter.cpp:129`; Py: katalog tworzony `paths.py:87`, zapisy Py nie zweryfikowane (nie znaleziono wywołania w `engine/`) |
| Załączniki | `dir/attachments/` | katalog tworzony; Py `messages.attachments` trzyma **ścieżki do oryginałów**, nie kopie | zależy od oryginału | brak | n/d | brak | `paths.py:87`, `config.cpp:49`, `chat_engine.py:197-232`; w C++ tylko pole w katalogu (`catalog/catalog.cpp:33`). Czy cokolwiek kopiuje do `attachments/`: nie zweryfikowano |
| Logi | brak pliku; stdout/stderr + bufor `LOGBUF` (500 wpisów, RAM) | tekst | proces / terminal | brak | n/d | brak | Py: `main.py:24-28`, `gui/base.py:77-103`; C++: `util/log.cpp:77,89` (stderr domyślnie włączony, `data/runtime/util.pack:19`), sink ABI `capi/capi_core.cpp:114-117`; `logs/` nieużywany. Zawartość: F4, F5 |
| Pakiety (packet) | **brak trwałego magazynu**: obliczane w pamięci, wynik zwracany przez ABI; flaga `canonical_store_written` w raportach | JSON | n/d | n/d | n/d | n/d | `packet/packet.cpp:740`, `capi/capi_packet.cpp`; dowody odtwarzalności w repo (§3) |
| Wyjście archiwum / wiedzy (out_dir) | katalog wskazany przez użytkownika | Markdown/JSON | wg uprawnień katalogu | brak | n/d | zależnie od katalogu | `archive/pipeline.cpp:891,913` (`owner_only=false`), `knowledge/engine.cpp:228,668`, `materialize/materialize.cpp:636` |

## 2. Poza katalogiem danych

| Kategoria | Lokalizacja | Format | Kto czyta | Szyfr. dziś | Klucz | Sync | Lokator |
|---|---|---|---|---|---|---|---|
| Model embeddingów Py | cache `sentence-transformers` (zwykle `~/.cache/huggingface`) | wagi modelu, bez danych użytkownika | użytkownik OS | n/d | n/d | brak | `core/selector.py:33,64`; ścieżka cache: nie zweryfikowano (zależy od biblioteki) |
| Przeglądarka (loom/web) | `localStorage`: `loom.application.views.v1`, `loom.application.workflow.v1:<[view,profile,rev]>` (+ `:checkpoint`), zapisane perspektywy `SAVED_KEY`, `STORAGE_KEY` Knowledge, preferencje motywu (`App.tsx:26-36`) | JSON | każdy skrypt tej samej origin; profil przeglądarki | brak | n/d | brak (tylko ta przeglądarka) | `web/src/components/ApplicationProfiles.tsx:24,115,198,346`, `KnowledgeWorkbench.tsx:70,85,90`, `profiles/workflow-checkpoint.ts:13-47`, `App.tsx:26-36` |
| Przeglądarka: token serwera | `sessionStorage[TOKEN_KEY]` (znika z kartą) | string | skrypty origin | brak | n/d | brak | `web/src/api/loom-http.ts:99-111` |
| IndexedDB / cookies | nie używane (grep `indexedDB`, `document.cookie` w `web/src` = 0) | n/d | n/d | n/d | n/d | n/d | brak |
| Android: dir danych | `/storage/emulated/0/Documents/ChatADHD/` (wspólny storage, `MANAGE_EXTERNAL_STORAGE`) | jak §1 | wszystkie aplikacje z „All files”, menedżery plików, klienty sync | **brak** (OpenSSL off) | n/d | dowolny klient sync użytkownika (zamierzone) | `MainActivity.kt:91-99`, `AndroidManifest.xml:9-20`, `LoomNative.kt:34`; `allowBackup=false` (`AndroidManifest.xml:26`) |
| Android: magazyn kluczy systemu (Keystore) | nie używany (grep `Keystore`, `EncryptedSharedPref` = 0) | n/d | n/d | n/d | n/d | n/d | brak |
| Zdalne repo GitHub | `github.com/<repo>@<gałąź>` przez Contents API | pliki z `local_path` wg include/exclude | każdy z dostępem do repo; GitHub | brak (jawne) | token w `secrets.json` | dwukierunkowy (domyślnie), rozstrzyganie konfliktów w kodzie | Py: `engine/github_sync.py:45-49,133-148,396,422`; C++: `github/github_sync.cpp:205-243,307-330,410,532-590`; domyślne wzorce `data/runtime/github.pack` |
| Dowody badań w repo (ZIP) | `loom/src/catalog/tests/profile_policy/results/2026-10-05/evidence.zip` (530 KB, 61 plików), `loom/tools/structure/retrieval_exploration_v1/actor_projection_v1/first_evidence.zip` (23 KB, 19), `.../native_source_free_v1/first_evidence.zip` (306 KB, 343) | ZIP w git | każdy z dostępem do repo | brak | n/d | git | skan wzorców kluczy: 0 trafień (F10); treść osobista poza wzorcami: nie zweryfikowano |
| Fixtury eksportów / importu (ZIP, DB) | `loom/tests/fixtures/{exports,import,eval}/*.zip`, `import/*.db` | syntetyczne | repo | brak | n/d | git | `loom/tools/gen_import_fixtures.py:231-232`; prawdziwość „syntetyczne” wg generatora, nie sprawdzano każdego |
| Ledger programu badawczego | `<katalog programu>/ledger.sqlite3` | SQLite | właściciel katalogu | brak | n/d | brak | `loom/tools/structure/research_programme_runner.py:200`, `programme_results_v1.py:171` |
| Przekazanie poświadczeń (separate programme) | `<session_dir>/private.pem` (RSA-3072, PKCS8 **bez** hasła), `session.json`, `openrouter-new.key`; HTML w ścieżce poza git | PEM / JSON / tekst | właściciel (katalog 0700, pliki 0600, `O_EXCL|O_NOFOLLOW`; wymuszane odczytem) | RSA-OAEP-SHA256 (etykieta = metadane) + AES-256-GCM (AAD = metadane); klucz końcowy zapisany jawnie | `private.pem` na dysku, kasowany po użyciu | brak (jednorazowe, poza git: `credential_handoff.py:27-33`) | `loom/tools/structure/credential_handoff_v2.py:25-26,29-35,99-117,120-166`; v1: `credential_handoff.py:141-191`; schemat `loom.credential_envelope/2`, algorytm `RSA-OAEP-256+A256GCM` |

## 3. Kryptografia, która istnieje

| Element | Parametry | Gdzie | Uwagi |
|---|---|---|---|
| Szyfrowanie danych (Py) | AES-256-GCM, nonce 12 B, sól 32 B, PBKDF2-HMAC-SHA256 600 000 iteracji; JSON `{v,ct,n,s,i}` | `core/crypto.py:35-67,93-113` | tylko gdy zainstalowano `cryptography`; singleton `crypto` nieużywany (F1) |
| Szyfrowanie danych (C++) | to samo, identyczny format; `iterations` czytane z bloba | `loom/src/crypto/crypto.cpp:1-250`; stałe `kKdfIterations`, `kSaltBytes`, `kNonceBytes` | wszystko za `LOOM_HAVE_OPENSSL` (CMake `AUTO`, `loom/CMakeLists.txt:34,106`); Android: wyłączone; AAD brak (nie zweryfikowano w `encrypt`) |
| Sejf | `crypto.json` = blob znacznika; `status()` zwraca `kdf:"pbkdf2-sha256"`, `cipher:"aes-256-gcm"` | `crypto.cpp:255-354`, `include/loom/crypto.h:62-82` | brak rotacji, brak wielu kluczy, brak opieki nad kluczem poza hasłem w pamięci |
| ABI | `loom_crypto_status/setup/unlock/lock/encrypt/decrypt` | `capi/capi_media.cpp:38-` | kto wywołuje z UI: nie zweryfikowano |
| Wymiana kluczy / podpisy | RSA-OAEP-3072 (v1, v2); **brak** podpisów, brak ML-KEM/ML-DSA/Argon2/X25519 | `credential_handoff*.py` | `ECOSYSTEM`/R51 wymagają zamiany |
| Biblioteka | system OpenSSL **3.0.13** (bez ML-KEM/ML-DSA/Argon2) | `openssl version` w środowisku | `loom/third_party` nie zawiera libki kryptograficznej poza SQLite/miniz |
| TLS | cpp-httplib + OpenSSL (desktop); Android: transport z Kotlina (`LoomHttp.kt`) | `loom/CMakeLists.txt:106-`, `android/README.md:185-200` | |

## 4. Co wychodzi do dostawców (jawny tekst)

| Kanał | Co | Klucz | Lokator |
|---|---|---|---|
| OpenRouter chat | cały kontekst: prompt systemowy, drzewo pamięci, graf, historia, bieżąca wiadomość, załączniki (obrazy base64, tekst plików) | `api_key` | Py `engine/chat_engine.py:144-292`; C++ `chat/chat_engine.cpp:1217` |
| Analiza semantyczna LLM | tekst wiadomości | `api_key` | `engine/semantic_llm.py:158`; `semantic/semantic_llm.cpp:128`; `extract/semantic.cpp:860` |
| Anthropic Batch | wiadomości w partiach | `anthropic_batch_key` | `engine/batch_api.py:297`; `chat/batch_api.cpp:44` |
| Lista modeli | brak treści | `api_key` | `engine/models.py:130`; `providers/providers.cpp:202` |
| Głos / OCR | audio, obrazy | Groq / Google (klucz w URL: F4) | `engine/providers.py:102,180`; `media/media_providers.cpp:144` |
| Embeddingi | tekst jednostek | wg manifestu dostawcy | `providers/embedding.cpp:121-122`; `chat/graph_reply.cpp:545` |
| Archiwum / kontekst | tekst do modelu | `api_key` | `archive/pipeline.cpp:287`; `context/context_engine.cpp:582` |
| GitHub | pliki z `local_path` | `github_token` | wiersz §2 |

Rejestr wysyłek per kategoria, czas i hash payloadu: **nie istnieje** (ledger zużycia liczy zużycie, nie treść ani kategorie; EC-E05).

## 5. Luki, których nie dało się zamknąć

| Pytanie | Powód |
|---|---|
| Uprawnienia plików na Androidzie (FUSE/sdcardfs) i widoczność `secrets.json` dla innych aplikacji | brak urządzenia; wniosek z dokumentacji platformy |
| Czy UI C++/web kiedykolwiek woła `loom_crypto_encrypt` na danych użytkownika | przeszukano `src`, `cli`, `server`: brak; web/Android nie przeszukiwano do końca |
| Czy Py zapisuje do `exports/` i `attachments/` | brak wywołań w `engine/`; `gui/` przeszukano tylko po słowach kluczowych |
| Czy `payload` w `usage_events` kiedykolwiek zawiera tekst użytkownika | nie czytano wszystkich ścieżek zapisu |
| Zawartość fixture’ów i dowodów pod kątem danych osobistych | skan tylko wzorców kluczy |
| Czy AEAD w C++ używa AAD | nie czytano ciała `encrypt` (linie 120-180) szczegółowo |
