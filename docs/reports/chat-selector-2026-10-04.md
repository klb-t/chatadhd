# W3 — selektor w czacie, 2026-10-04

Gałąź: `gpt/chat-selector-2026-10-04`; baza po rebase: `main` na
`ba6eaf6244d2d4136fbb3534bc0d45a4900eeda1`. Kolejność integracji: **W2 + W4 → W3**.
Rebase przyjął wymagania R39–R41; zmienił dokumentację, a źródła produkcyjne i testowe
pozostały identyczne ze zweryfikowanym stanem W3 na bazie `7282437`.
Nie przypisuję dokumentacyjnemu rebase nowego rerunu.
**Płatne wywołania: 0.** Testy używają danych syntetycznych i zapisanych odpowiedzi;
nie czytano holdoutów ani prywatnych archiwów.

## Co działa

Podłączono rzeczywisty adapter embeddingów przez ProviderRegistry: wybór modelu,
manifestu i capability `embedding/embed`, walidacja odpowiedzi, cache po rzeczywistych
bajtach oraz tożsamości modelu/dostawcy/endpointu/opcji/konta. Cache zachowuje wymiar
i pierwszy znany raportowany model także pomiędzy instancjami. Alias wymaga danych;
brak raportu pozostaje nieznany. Podgląd działa offline. Pierwsza surowa odpowiedź,
również błędna lub częściowa, trafia do BlobStore/ProvenanceStore przed parsowaniem;
awaria zapisu blokuje cache. Nie ma automatycznych ponowień ani zmiany dostawcy.

`send()` używa konfigurowanego `type_goal_with_model`; model, temperatura, próg
i budżety są ustawieniami. Typowanie i embedding przed zdalnym wywołaniem wymagają
autoryzacji oraz UsagePolicy z W2: estymacji, potwierdzenia ×10, pomiaru i pokwitowań.
Brak W2 zatrzymuje wywołanie. Trwałe zajęcie ID chroni przed ponowną wysyłką po awarii;
nieznane tokeny/koszt pozostają `null`. Pokwitowanie wiąże dokładne HTTP identity,
więc zmiana endpointu, konta lub body po pauzie blokuje wznowienie.

Pamięć per węzeł, graf wiadomości i ContextEngine składają się w jeden selektor:
stable → project → goal, jeden prompt i wspólny budżet z nagłówkami/ostrzeżeniami.
Deduplikacja zachowuje widoki, scores/ranks, źródła, hashe i zakresy. Zasięg grafu
i szczegółowość są niezależne; odrzucenia i brakujące przesłanki pozostają jawne.
API raportuje `selector_channels`, `goal_typing_capability`, `execution_usage`;
czat zapisuje `context_trace.knowledge_context` i `context_trace.unified_context`.
Nieznany kanał jest `unavailable`.

MethodRegistry korzysta z natywnych Entity/Claim/Observation i GraphPacketStore,
bez drugiej bazy ani katalogu metod w C++. Są **5 kerneli**: lexical, TF-IDF, regex,
BM25, graph_pool; **5 fuzji**: sum/max/min/mean/RRF; ponadto jawnie związane instrumenty
natywne/wektorowe i executory `context_fusion`/`context_selection`. Jev oraz słabe/mocne
modele selektora wymagają rzeczywistego instrumentu i definicji; nie są udawane jako
dostępne. Wagi ze znakiem, progi, kolejność nakładek i DAG kombinacji pochodzą z danych.
Powtórzone ścieżki nie znikają, brak pomiaru nie staje się zerem. Fuzja wpływa na wybór
po deduplikacji, przed punktowaniem/budżetem.

Pomiar jest nowym węzłem z realnymi krawędziami do run/version/parameter-set;
nie zmienia pochodzenia zastanej treści. Profil może dostarczyć także wersje całej
fuzji i końcowego wyboru. Bez nich pokwitowanie etapu jest jawnie niedostępne.
Fusion ma pełny hash wejścia; selection ma dokładny hash przechwyconej puli,
lecz pełny hash pozostaje `null` przez późniejsze odczyty zależności z bazy.

Cztery tryby odpowiedzi: `off`, `answer_as_graph`, `text_plus_JSONgraph`,
`separate_model_afterwards`. Używają rzeczywistego kompilatora W4 i jego store/CAS.
Wire, tekst modelu, projekcja i kandydat grafu mają oddzielne źródła; SSE zapisuje
chunk przed parserem/anulowaniem. Błąd grafu zachowuje czytelny tekst. Requested/reported
model są rozdzielone. Strukturalna projekcja i akceptacja nie ustanawiają prawdy treści.

## Liczby i dowody

| Miara | Pierwszy etap | Końcowy stan |
|---|---:|---:|
| Nowe przypadki W3 | 46/46, 464 asercje, bez W2 | 121/121, 1604/1604, rzeczywiste W2+W4 |
| Pełny CTest, niezmienione progi | 108/108, 407,06 s | 108/108, 276,11 s |
| Tryby odpowiedzi grafowej | 0 | 4 |
| Płatne wywołania | 0 | 0 |

Końcowy runner: 0 pominięć, 397,54 s. Czas przebiegu nie dowodzi przyspieszenia;
nie wykazano jakości na żywych modelach ani prawdziwych archiwach. Bazowy pierwszy
CTest miał 106/108 i dwa timeouty; niezmienione ponowienia przeszły. Zachowano
659 natywnych przypadków/24 465 asercji oraz 1276 Python, w tym FFI 18.
Istniejący `unit.test_catalog_scale` wykonuje 0 przypadków — do W8. Nowe `.verify.cc`
są uruchamiane osobnym runnerem, jeszcze poza CMake/CTest. Nie usunięto testów
ani nie zwiększono timeoutów. Budżet promptu to szacunek Unicode, nie tokenizer:
`token_codepoints_per_token` jest edytowalny, preset 4.

Końcowe zależności: W2 `5a73a360f44626333ce6510f26261c32239de73c`,
W4 `1377e20c71f200b01416c7c87f06c3ffdafb02b6`; wcześniejsze dowody używały W2
`910a1d6b3764f82c78e39c5b0adca796ba1168e5` i W4 `14eccaf679c3897c82b252ca8504f35f653d93f0`.
**Jeden wspólny kontrakt W3/W4:**
[METHOD_GRAPH.md](https://github.com/klb-t/chatadhd/blob/1377e20c71f200b01416c7c87f06c3ffdafb02b6/loom/src/packet/METHOD_GRAPH.md).
Producent golden: 1/1, 97/97 asercji; rzeczywiste prepare → sprawdzona atrapa HTTP →
kompilator → bind → accept/restart/read/replay/retry. Dwa niezależne wykonania natywnego
konsumenta CABI sprawdziły po **3 wyniki, 19 Entity, 30 Claim, 17 Observation**.
Konsument nie przypisuje sobie wykonania producenta (`producer_execution_verified=false`).
[Odtworzenie producenta](chat-selector-2026-10-04-evidence/golden-producer/README.md),
[konsumenta](chat-selector-2026-10-04-evidence/golden-consumer/README.md),
[CTest](chat-selector-2026-10-04-evidence/golden-native/ctest.log).

Wejściowy fixture pozostał bajtowo niezmieniony; brakujące żądanie HTTP dostarcza
jawna nakładka danych z wersją/hashem przepisu. Projekcja zapisanej odpowiedzi
zmienia tylko stamp pakietu i kodowanie JSON, co test porównuje i rejestruje.
Dwa eksporty różni jedynie ID źródła raw_response i zależne ślady/hashe: kontrakt,
model, parametry, dokładne body i wyniki są identyczne. Pełne drugie wejście
i pokwitowanie zachowano bezstratnie w gzip.

Pierwszy etap zachowano na
[archive/gpt/chat-selector-pre-methods-2026-10-04](https://github.com/klb-t/chatadhd/tree/1c4c8a1025be67f8794823df093a8f2508fedc25/docs/reports/chat-selector-2026-10-04.md),
stan przed rebase na `archive/gpt/chat-selector-before-r39-rebase-2026-10-04`.
Pełne negatywne snapshoty, manifesty, logi i odtworzenie Werror/fixture/SSE pozostają na
[archive/gpt/chat-selector-methods-negative-2026-10-04](https://github.com/klb-t/chatadhd/tree/785dd52c9949eb78ee021da35113af71f6378fe9/docs/reports/chat-selector-2026-10-04-evidence).

## Ustawienia i granice

[Szczegółowy handoff API](chat-selector-2026-10-04-evidence/API.md) opisuje pola
wywołań; format grafu pozostaje wyłącznie w powyższym wspólnym kontrakcie W4.

`context_execution.method_registry`: `enabled/profile/receipt_ids/selection/run_context`,
`capability_bindings.<id>.actual_parameters`, `graph_blend_operation`, `diversity`,
`result_methods.{fusion,selection}`. Liście wymagają `limit/min_score`; kolejność to
`parameter_layers`; fusion wymaga `operation/signal`, RRF także `rrf_constant`.
Role grafu: `parameter_set_version/uses_parameter_set/uses_combination`.
Result stage wybiera jedną bezpośrednią wersję executora, z `fusion` absent/null;
nie wykonuje promptu/przepisu. Niezgodne lub niezużywane parametry są odrzucane.
Prywatne API: `MethodRegistry::{load,resolve,prepare,bind_results,accept}`; integracja
ContextEngine zapisuje resolution/result graph w `ContextSet.goal.params.method_registry`.

`context_execution.graph_reply`: `mode/profile/receipt_ids/selection/run_context/base_packet`,
`host/apply_policy/admission`, transport główny i `postprocess_transport/provider_manifests`.
`recipe.definition.parameter_bindings` wiąże controls przed hashami, `request_bindings`
finalny packet/request po rejestracji. `recipe.request` zawiera model/messages/stream;
`output_binding` — graph_pointer/text_pointer. Transport wymaga `calls_authorized`,
`usage_estimate`; automatic — jawnego `store_request/explicitly_accepted` i CAS.
Ślad: `loom.chat_graph_reply/1`; fragment: `chat::graph_reply_fragment(result,address)`.

Nowe pack/presety i publiczne ABI/HTTP są poza zakresem. Bez kompletnego profilu ścieżka
nie ma zastępczego katalogu. Zwykły/off transport czatu nadal jest historyczny;
stary publiczny adapter typowania bez scope zachowuje 1/256 000 B/4096 tokens/60 s.
Nie zadeklarowano rozliczenia wszystkich operacji ani optimum całego korpusu.

## Do wątku N

- **2:** podłączyć konfigurację UsagePolicy; zachować semantykę input_bytes i `null`
  kosztów/tokenów. Lokalne registry/packet/retrieval/fusion potrzebują pomiarów CPU/RAM
  i wspólnej ruchomej bazy, nie nowych limitów.
- **4:** powtórzyć opublikowany golden własnym buildem, zaktualizować status producenta;
  zachować parameter-set/combination edges i `candidate_packet` z preview.
- **9:** po W2/W4 ponowić CTest i build web; zarejestrować runner oraz przydzielić
  publiczne ProviderRegistry::embed, ABI/HTTP registry/graph_reply/fragment i immutable
  PreparedRequest/resume. Ponowny Chat.send tworzy nowe run/turn IDs i nie wznawia
  starego potwierdzenia ×10; niskopoziomowe wznowienie dokładnego request działa. PR #12.
- **10:** podłączyć rzeczywiste settings/trace; klucz nie autoryzuje dodatkowego modelu.
  Fragmenty mają local_id/node_id i UTF-8/codepoints, nie UTF-16. Własne kombinacje
  zapisywać jako natywne wersje grafu; UI pozostaje poza W3.
- **11:** dostarczyć/generować kompletne pack/profile dla context/embedding/goal_typing/
  graph_reply wraz z vocabulary, precedence, result_methods i bindings. RuntimeProfile
  nie dostarcza jeszcze domen W3. DIC-0332–0382 pozostają niemigrowane; wykazać zgodność
  default results po podłączeniu. `loom/src/search/selector.cpp::index` wymaga walidacji
  liczności/wymiarów/finite embeddingów i należy do tego zakresu.
