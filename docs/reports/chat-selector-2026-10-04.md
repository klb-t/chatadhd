# W3 — selektor w czacie, 2026-10-04

Gałąź: `gpt/chat-selector-2026-10-04`. Baza: aktualny `main`,
`161cc22dfb84fe863389d6b90323bd44516a68dc`. Integracja: wątek 9, **W2 → W3**.
Płatne wywołania: **0**. Nowe regresje: dane syntetyczne; bez czytania holdoutów.

## Wykonane

- Rzeczywisty adapter tekstowych embeddingów wybiera nazwany model i manifest
  przez ProviderRegistry, wymaga capability `embedding/embed`, waliduje wektory i indeksy
  odpowiedzi OpenAI-compatible. Brak automatycznych ponowień lub zmiany dostawcy.
- Kanał wektorowy: cache po SHA-256 rzeczywistych bajtów i tożsamości modelu,
  dostawcy, endpointu, opcji i konta. Zdalny cache trwały; wstrzyknięty dostawca
  współdzieli cache między instancjami, izoluje wymianę dostawcy. Podgląd jest offline.
- Surowa pierwsza odpowiedź embeddingu trafia do BlobStore i ProvenanceStore,
  także przy HTTP/JSON/vector error lub częściowym timeout. Ślad zawiera referencje;
  awaria zapisu blokuje cache. Typowanie zachowuje też częściową odpowiedź wyjątku transportu.
- `send()` uruchamia konfigurowane `type_goal_with_model`: model, próg,
  temperatura i budżety są ustawieniami. Licznik prób wspólny dla projekcji planu;
  pierwsza odpowiedź i jej pochodzenie zachowane, confidence nie staje się faktem.
- Przed zdalnym typowaniem celu i embeddingiem działa API UsagePolicy z W2: szacunek, potwierdzenie
  ×10, pomiar i pokwitowania. Brak W2 zatrzymuje wywołanie. Wyłączne, trwałe zajęcie
  ID zapobiega ponownemu wysłaniu po awarii przed rozliczeniem; nieznany koszt to null.
- Wspólny selektor: pamięć per węzeł, graf wiadomości i wynik ContextEngine,
  stable → project → goal, jeden prompt i budżet obejmujący nagłówki i ostrzeżenia.
  Deduplikacja zachowuje alternatywne widoki, scores/ranks, źródła, hashe i zakresy.
  Zasięg grafu i szczegółowość niezależne; odrzucenia i brakujące przesłanki jawne.
- API: `selector_channels`, `goal_typing_capability`, `execution_usage`;
  w czacie `context_trace.knowledge_context` i `context_trace.unified_context`.
  Nieznany kanał otrzymuje unavailable. Ślady pozostają też w metadanych wiadomości.
- ActiveTask sprawdza rzeczywiście zużytą pamięć również ponad 16 000 znaków.
  Jawny błędny run/goal nadal zwraca błąd, także w konfigurowanym żądaniu.

## Liczby przed i po

| Miara | Przed | Po |
|---|---:|---:|
| Embeddingi i typowanie modelowe w produkcyjnym czacie | niepodłączone | podłączone; wykonanie wymaga autoryzacji i W2 |
| Wiadomości kontekstu z pamięci, grafu i KB | do 3 osobnych | 1 w nowym presecie |
| Syntetyczny batch: 3 wejścia / 2 unikalne teksty | brak tej ścieżki | 1 wywołanie; powtórzenie z cache: 0 nowych |
| Pełne CTest | 106/108 + 2/2 ponowione | 108/108, jeden przebieg; 407,06 s |
| Nowe regresje W3 bez W2 | 0 | 46/46; 464/464 assertions |
| Nowe regresje W3 z rzeczywistym W2 | 0 | 49/49; 536/536 assertions |
| Płatne wywołania tej pracy | 0 | 0 |

Baza: 659/659 przypadków natywnych; 1276 przypadków Python w 25 zestawach,
w tym structure 859 i contracts 209. Pierwszy przebieg bazy: dwa niezmienione
timeouty 60 s podczas współbieżnej kompilacji; ponowienia 48,90 s i 34,91 s.
Końcowy pełny przebieg zachowuje te liczby: natywne 24 465/24 465 asercji,
Python wszystkie OK; FFI 18/18 jest już wliczone w 1276. Structure: 41,14 s,
contracts: 41,32 s czasu CTest. Jawne PYTHONUNBUFFERED=1 służyło obserwowalności.
Wcześniejszy końcowy snapshot miał 106/108 (dwa timeouty), a jego retry 1/2;
diagnostyczne 859/859 obejmowało testy, lecz czas 54,099 s nie obejmował discovery.
Istniejący `unit.test_catalog_scale` wybiera 0 przypadków — zadanie W8.
Nie usunięto testów, nie zwiększono timeoutów, nie poluzowano progów.
Budżet promptu to jawny szacunek ceil(liczba punktów Unicode / token_codepoints_per_token),
z edytowalnym presetem 4; nie pomiar tokenizera. Wartości ułamkowe są obsługiwane,
a przepełnienie reprezentacji jest błędem, nie zerowym oszacowaniem.
Nie mierzono jakości na prawdziwych archiwach ani przyspieszenia czasowego;
oszczędności wywołań dotyczą deterministycznych atrap.

## Ustawienia i przekazanie API do UI

Zapisz obiekt `context_execution` przez istniejący mechanizm config / C API.
Historyczny przepis pozostaje domyślny. Minimalny preset offline:

```json
{"unified":{"enabled":true,"memory_max_chars":0},
 "request":{"budget_tokens":4000},
 "goal_typing":{"enabled":false,"calls_authorized":false},
 "embedding":{"enabled":false,"calls_authorized":false}}
```

Wynik `ContextEngine.build` i ślad czatu zwracają `selector_channels`:
id, available, source/reason, last_retrieval_status i last_retrieval_reason.
Dla wykonania wektorowego są też configured_enabled, calls_authorized,
usage_policy_dependency i provider_execution_ready. `available` nie oznacza zgody
na płatne wykonanie. `goal_typing_capability` opisuje osobno typowanie,
a `execution_usage` zawiera decyzje i pokwitowania instrumentów.

Wykonanie wymaga jawnych enabled + calls_authorized w danej sekcji.
Embedding: provider_id, model, channel_id, limit, min_score, timeout_ms,
request_options, estimated_resources, input_tokens_per_byte, cost_usd_per_input_token.
Typowanie: model, base_url, confidence_threshold, temperature, max_requests,
max_input_bytes, max_output_tokens, timeout_ms, max_response_bytes, estimated_cost_usd.
Oczekiwane zużycie: estimated_response_bytes i estimated_output_tokens (domyślnie null).
Maksymalne budżety są oddzielne od szacunku; nie wywołują fałszywego ×10 po
krótkiej odpowiedzi. Powtórzenie tych samych ustawień i jawny szacunek ×10 mają regresję.
Wartości to presety; walidacja reprezentacji/liczb nie jest limitem wydatków.
Brak ceny/tokens pozostaje null; deklarowany szacunek nie jest pomiarem.

Obie sekcje: `usage.baseline_key`, `usage.resume_operation_id`. Po
requires_confirmation pokaż pokwitowanie i wróć z dokładnym ID oraz
`usage.confirmation = {"receipt_id":"…","approved":true,"ref":"…"}`.
Bajty i szacunek muszą pozostać te same; nowy batch/projekcja potrzebuje własnej
decyzji. Ruchoma baza i mnożnik pochodzą z `loom_usage_policy` W2.

Unified: budget_tokens, token_codepoints_per_token, source_priority, include_knowledge, section_labels,
memory_max_chars, legacy_relation_hops, legacy_max_messages, legacy_max_visited,
legacy_detail_chars, legacy_include_inactive, legacy_include_current_conversation,
legacy_seed_ids. Wyłączenie bieżącej rozmowy jest edytowalnym presetem. Zero oznacza pełną
pamięć, brak limitu wiadomości/odwiedzin, pełny tekst w odpowiednich polach;
dla budget_tokens — pusty kontekst. Opcje czatu osobno kontrolują pamięć, graf i historię.

## Niewykonane i granice zakresu

- Publiczny ProviderRegistry nie ma metody embed; wdrożono źródłowy adapter
  `providers::provider_embed`. Publiczna deklaracja w `loom/include/loom/providers.h`,
  per-request execution API i migracja jego kontraktu wymagają właściciela nagłówków/API.
- Cache identyfikuje żądany model i manifest; `response.model` pozostaje w surowym
  źródle, bez porównania z żądanym modelem. Capability dotyczy usługi, nie dowolnego
  modelu. Kontrakt sprawdzono w [oficjalnej dokumentacji](https://openrouter.ai/docs/api/api-reference/embeddings/submit-an-embedding-request); bez próby live.
- W2 nie ma jeszcze na bazowym main. Test integracyjny używa rzeczywistych źródeł
  W2 `34cc920dd3cdb0c0fca0a514569b19111429583f`, bez włączania ich do gałęzi W3.
  Integrator musi ponowić wspólny build po dołożeniu W2.
- Istniejące generowanie głównej odpowiedzi czatu nie otrzymało tu strażnika;
  ten raport potwierdza rozliczanie instrumentów selektora, nie wszystkich wywołań
  aplikacji. Pozostałe transporty wymagają oddzielnego podłączenia UsagePolicy.
- Stary adapter natywny bez execution scope zachowuje sentinel 1 / 256 000 B /
  4096 tokens / 60 s i pozostaje poza nowym strażnikiem W2. Nowa ścieżka czatu
  przyjmuje większe ustawienia i przechodzi przez strażnika. Migracja starego
  publicznego kontraktu i jego testów jest poza zakresem; bramki nie zostały poluzowane.
- Wspólny selektor budżetuje już wybraną pulę ContextEngine; nie otwiera ponownie
  odrzuconych kandydatów KB. Wspólnego optimum nie dowiedziono. Inne stare limity
  z LIMITS_AND_WIRING pozostają do migracji u właścicieli odpowiednich ścieżek.
- Wstrzyknięty dostawca odpowiada za własne rozliczanie; runtime także wymaga jego
  jawnej autoryzacji. Trwałe zajęcie operacji zweryfikowane na Linux/POSIX;
  niewspierane platformy raportują unavailable przed wysłaniem.
- Rejestracja nowych testów w CMake/CI i presety/schemat ustawień należą do ich
  właścicieli. W3 dodaje uruchamialny runner w swoim zakresie. Bez zmian UI, ABI,
  STATE, README, profili, cudzych zakresów i main.

## Odtworzenie i dowody

```sh
cmake -S loom -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG=-g0 -DCMAKE_CXX_FLAGS_DEBUG=-g0 "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=gold -Wl,--no-map-whole-files" -DLOOM_WERROR=ON -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON -DLOOM_USE_SYSTEM_SQLITE=OFF
cmake --build build -j2
env -u PYTHONPATH -u TMPDIR PYTHONUNBUFFERED=1 ctest --test-dir build --output-on-failure -j1 --no-tests=error
python3 loom/src/chat/verify_selector.py build
python3 loom/src/chat/verify_selector.py build --usage-policy-ref 34cc920dd3cdb0c0fca0a514569b19111429583f
```

Dowody oraz pierwsze nieudane przebiegi: sąsiedni `chat-selector-2026-10-04/`.
Końcowy kod: `0833f78ec32cc01f57546fab94e2743092f91108`. Snapshot przed konfigurowaniem
estymatora: `1159b8975c9d76b2afddcc9c40bcd077b9f261b6`; dowody pre-estimator.
Patch i manifest odtwarzają pełny wcześniejszy snapshot W3 na wskazanym main;
końcowe manifesty przypinają źródła, hash rdzenia i dokładne polecenia.
Regresje ujawniły walidację signed/unsigned i stary obiekt dostawców w build;
audyt poprawił znikające scores/ranks i brak jawnej autoryzacji wstrzyknięcia.
Zachowano też nieudaną współbieżną kompilację oraz log braku miejsca. Naprawa
artefaktów i końcowy Debug build z `-g0` nie zmieniły asercji, optymalizacji,
testów ani progów; ograniczono symbole debug wobec kwoty dysku środowiska.
Końcowy link użył zainstalowanego GNU gold z no-map-whole-files po OOM GNU ld;
źródła, flagi kompilacji i hash rdzenia pozostały identyczne.
