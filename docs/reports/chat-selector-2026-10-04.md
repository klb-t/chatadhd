# W3 — selektor w czacie, 2026-10-04

Gałąź: `gpt/chat-selector-2026-10-04`. Baza: aktualny `main`,
`7282437b1c88933977f64b3468b9f42f7b400494`. Integracja: wątek 9, **W2 + W4 → W3**.
Poniższe pierwotne wyniki odnoszą się do pierwszego etapu; rozszerzenie opisano dalej.
Pierwotne siedem commitów zachowano na `archive/gpt/chat-selector-pre-methods-2026-10-04`;
kontynuacja jest liniowo przeniesiona na świeży main, bez zmiany dokumentów integratora.
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

## Pierwszy etap i archiwum

Szczegółowy wcześniejszy raport, ustawienia unified/embedding/goal_typing, błędy
infrastruktury i dowody przed rozszerzeniem są na
[`archive/gpt/chat-selector-pre-methods-2026-10-04`](https://github.com/klb-t/chatadhd/tree/1c4c8a1025be67f8794823df093a8f2508fedc25/docs/reports/chat-selector-2026-10-04.md).
Zachowano odtwarzalne źródła; obecne drzewo zawiera tylko wybrane dowody.

## Wspólny kontrakt 3/4 — bramka właściciela

Jeden plik formatu, używany przez oba raporty:
[`loom/src/packet/METHOD_GRAPH.md`](https://github.com/klb-t/chatadhd/blob/1377e20c71f200b01416c7c87f06c3ffdafb02b6/loom/src/packet/METHOD_GRAPH.md).
Obejmuje loom.method_graph/1, loom.method_run_trace/1 i
loom.method_graph_fixture/1. W3 nie tworzy drugiej definicji protokołu.
Aktualny wspólny fixture z tej gałęzi przechodzi już rejestr/bind/native store;
pełny eksport po rzeczywistym wykonaniu W3 i niezależny consumer W4 są w toku.
**Odbiór wstrzymany do zapisanego dowodu tego pełnego przejścia.**

## Rozszerzenie W3 według polecenia 1–11

Zweryfikowane liczby tego przyrostu odpowiadają wypchniętemu `6e59577`.
Doprecyzowanie compiler-input SHA i eksport wspólnego golden są bieżącą
kontynuacją; ich dodatkowe wyniki zostaną dopisane po niezależnym consumerze W4.

- `MethodRegistry` czyta rzeczywiste Entity/Claim/Observation z profilu oraz pokwitowań
  GraphPacketStore, zapisuje przez ten sam store. Definicje, prompty, przepisy, presety
  i kombinacje mają jawne wersje i hashe; odrzucone/driftujące wiersze blokują wykonanie.
  Efektywne parametry są osobną wersją węzła: prawdziwe krawędzie version/run →
  parameters oraz run → combination, dokładne definition_records i trwałe źródła.
  Wymagane role słownika: parameter_set_version, uses_parameter_set oraz
  uses_combination dla użytej kombinacji; brak danych zatrzymuje rejestrację.
  Nie ma drugiej bazy ani katalogu metod/presetów w C++.
- Operacje wykonawcze: lexical, TF-IDF, regex, BM25, graph_pool oraz jawnie związane
  instrumenty natywne/wektorowe. Jev i modele selektora nie są udawane: wymagają
  dostarczonych definicji i rzeczywistego instrumentu, inaczej są unavailable.
  Wagi ze znakiem, progi i kolejność nakładek pochodzą z danych. Dowolne DAG kombinacji
  zachowują powtórzone ścieżki; sum/max/min/mean/RRF mają jawne parametry i brak pomiaru
  nie jest zerem. Wynik kombinacji trafia po deduplikacji, przed wspólnym punktowaniem.
- Pomiar wyszukania jest nowym węzłem, a nie zmianą pochodzenia zastanej treści.
  Rzeczywiste krawędzie łączą go z przebiegiem i konkretną wersją metody.
  Hash wejścia obejmuje query, uporządkowany corpus, run/pack, pulę grafu i zużyte
  parametry. Instrument wstrzyknięty musi dostarczyć binding rzeczywistych parametrów;
  fikcyjne/niezużywane ustawienia są odrzucane. Fusion i końcowy wybór mają także
  własne realne result/run/version/parameter-set edges, gdy profil dostarcza
  result_methods. Brak definicji jest jawnie unavailable. Ujemne wagi i odwrócenie
  kolejności są objęte regresją. Nie relabeluję starych treści jako nowo wytworzonych.
  Fusion ma pełny hash wejścia; selection ma dokładny hash przechwyconej puli, lecz
  pełny hash pozostaje null ze względu na późniejsze odczyty zależności z bazy.
- Cztery tryby: `off`, `answer_as_graph`, `text_plus_JSONgraph`, `separate_model_afterwards`.
  Używają rzeczywistego W4, bez kopii kompilatora. Kontekst to jawny base_packet oraz
  wiązania przepisu; złożenie odpowiedzi wymaga jednego końcowego żądania, inne
  kompozycje są jawnie unsupported. Kandydat albo automatic przez natywny store/CAS.
  Pochodzenie treści zawsze model; strukturalna projekcja nie oznacza prawdy treści.
- Surowy wire, tekst modelu, projekcja JSON i kandydat grafu mają oddzielne źródła.
  SSE zapisuje chunk przed parserem/anulowaniem; błędny schemat zachowuje tekst.
  Poprawne pole tekstowe text+JSON jest czytelne nawet przy błędzie grafu.
  Requested/reported model i brak raportu są rozróżnione; fragmenty używają W4
  `local_id` albo `node_id`, zakresów UTF-8/codepoints i oryginalnej kompilacji.
- Cache embeddingów trwale wiąże wymiar i pierwszy znany raportowany model także
  pomiędzy wywołaniami/instancjami; nie dopisuje potwierdzenia do starych nieznanych
  wpisów. Alias raportowanego modelu wymaga danych. Tożsamość sprawdzana przed i po
  natywnym embed oraz po bramce; raport oddziela readiness/live/cache/auth.
- Pokwitowanie typowania celu wiąże teraz pełne HTTP identity (opaque hash endpointu,
  uporządkowanych nagłówków/konta, body, timeout/stream). Zmiana endpointu/konta po
  pauzie ×10 nie pozwala wysłać; przywrócenie dokładnego żądania pozwala na jedną próbę.
  Surowy wynik jest zapisany przed parsowaniem także dla rozliczenia usage.

| Sprawdzenie rozszerzenia | Przed: etap pierwotny | Po |
|---|---:|---:|
| Prywatne regresje bez W2/W4 | 46 / 464 asercje | 92/92; 834/834 |
| Prywatne regresje rzeczywiste W2 + W4 | brak wspólnego przebiegu | 120/120; 1504/1504 asercji |
| Pełne CTest, niezmienione testy/progi | 108/108; 407,06 s | 108/108; 303,45 s |
| Operacje natywne / fuzje z parametrami | 2 kanały / stały sygnał rank | 5 / 5 |
| Tryby odpowiedzi grafowej w ChatEngine | 0 | 4 |
| Rzeczywiste wydatki / wywołania płatne | 0 / 0 | 0 / 0 |

Czas CTest jest czasem tego przebiegu, nie dowodem przyspieszenia algorytmu.
Nowe `.verify.cc` pozostają poza CMake/CTest; pełny runner wykonuje je osobno.
Nieudany Werror build, błąd fixture optional oraz błędna atrapa SSE: pełne source
snapshots, manifesty, logi i skrypt odtworzenia wyłącznie na
[`archive/gpt/chat-selector-methods-negative-2026-10-04`](https://github.com/klb-t/chatadhd/tree/785dd52c9949eb78ee021da35113af71f6378fe9/docs/reports/chat-selector-2026-10-04-evidence).
Zweryfikowano zgodność zdalnych bajtów; wybrane drzewo zawiera osobne zielone dowody.
Właściwe źródła zależności: W2 `910a1d6b3764f82c78e39c5b0adca796ba1168e5`,
W4 `14eccaf679c3897c82b252ca8504f35f653d93f0` (oba pozostają na swoich gałęziach).
Nowe reguły parameter-set/combinations pochodzą z tego dokładnego kontraktu W4.
Po szturchnięciu właściciela pobrano również W4 1377e20 i przeczytano świeży
INDEX na main; uzupełnienie wspólnego artefaktu producer→native verifier trwa.

## API do podłączenia przez właścicieli

`context_execution.method_registry` przyjmuje `enabled`, pełny `profile` z natywnymi
DTO/vocabulary/selection, `receipt_ids`, `selection`, `run_context` i
`capability_bindings.<id>.actual_parameters`, `graph_blend_operation`, `diversity`.
Parametry liścia wymagają `limit/min_score`; native operations wymieniają pozostałe
zużywane klucze. Selection wymaga danych `parameter_layers`; fusion wymaga
`operation/signal`, RRF również `rrf_constant`. Żadnego presetu zastępczego w kodzie.
`recipe.definition.parameter_bindings` wiąże rzeczywiste runtime controls przed
hashowaniem; `request_bindings` po rejestracji wiąże finalny packet i request.
Hash przepisu/instrukcji i hash finalnie wysłanego żądania mają odrębne zakresy.
Prywatny `MethodRegistry::{load,resolve,prepare,bind_results,accept}` jest API silnika.
`result_methods.{fusion,selection}` wymaga jawnych standardowych selection overlays,
każdy z jedną bezpośrednią wersją rzeczywistego executor capability
`context_fusion` / `context_selection`; fusion tego etapu musi być absent/null.
Złożenie upstream ma dowolny DAG, signed weights i jawne fuzje. Wersje etapów nie
wykonują promptów/przepisów; zadeklarowane parametry muszą odpowiadać zużytym.
Brak/empty members raportuje unavailable; nie dziedziczy przypadkiem innych metod.
Raport resolve zawiera każdy liść, parametry, capability oraz przyczynę unavailable;
ContextSet goal.params.method_registry zawiera resolution i rzeczywisty result graph.

`context_execution.graph_reply` przyjmuje `mode/profile/receipt_ids/selection`,
`run_context`, `base_packet`, `host`, `apply_policy`, `admission`, transport główny
oraz `postprocess_transport/provider_manifests`. Kompletny `recipe.request` zawiera
model/messages/stream; output_binding ma graph_pointer i opcjonalny text_pointer.
Transport wymaga calls_authorized, usage_estimate, opcjonalnego confirmation oraz
ustawianego timeout. Automatic wymaga istniejącego store_request z target i jawnym
explicitly_accepted; `selection_scope:all_native_rows` jawnie wylicza zamknięte
wiersze i CAS pod blokadą tej samej bazy. Brak tej polityki zachowuje dokładny CAS
od klienta. Metadane wiadomości/context_trace zawierają `loom.chat_graph_reply/1`.
`chat::graph_reply_fragment(result,address)` używa oryginalnego packet/compilation.
Publiczne ABI/HTTP nie są edytowane w tym zakresie.

## Niewykonane i granice

Domyślny katalog/presety w packu oraz publiczne ProviderRegistry::embed i ABI/HTTP
pozostają u właścicieli tych plików. Nowa ścieżka wymaga kompletnych danych;
nie ma zastępczego katalogu w C++. Jev i modele wyszukania potrzebują instrumentu.
Stary publiczny adapter typowania bez execution scope zachowuje sentinel 1 /
256 000 B / 4096 tokens / 60 s; nowa ścieżka przyjmuje ustawienia i W2.
Guard głównego czatu obejmuje przygotowane tryby grafowe; zwykły/off transport
pozostaje historyczny. Nie zadeklarowano rozliczenia wszystkich operacji aplikacji.
Wspólny selektor budżetuje już wybraną pulę KB; optimum całego korpusu nie zostało
wykazane. Trwałe zajęcie wykonania sprawdzono na POSIX/Linux, inne platformy mogą
raportować unavailable. Środowisko nie jest dowodem jakości żywych modeli.

## Do wątku N

- **Do wątku 2:** zapewnić preset UsagePolicy w danych i podpiąć konfigurację po scaleniu.
  input_bytes oznacza bajty promptu celu, tekstów embeddingu albo pełnego HTTP body
  w guardzie odpowiedzi; kohorty baseline muszą zachować tę samą semantykę.
  Nieznane tokeny/koszt pozostają null; limity wyjścia nie są prognozą kosztu.
  Lokalne registry/packet/retrieval/fusion nie mają jeszcze estymacji ani pomiarów
  CPU/RAM i ruchomej bazy; wymagają wspólnego kontraktu instrumentalnego, nie limitów.
- **Do wątku 4:** kontrakt loom.method_graph/1 i prawdziwy kompilator zostały użyte.
  Przy scalaniu zachować nowe parameter-set i combination DTO/edges oraz preview receipt; wynik preview musi być
  candidate_packet, ponieważ apply przy acceptance=preview zwraca oryginalny packet.
  Adapter wytwarza teraz również grafowe przebiegi całej fuzji/końcowego wyboru
  z wersji dostarczonych przez profil. Oryginalne przebiegi zastanej pamięci pozostają
  do powiązania z ich źródłowymi metodami; nie należy ich wymyślać retrospektywnie.
- **Do wątku 9:** scalić dopiero po W2/W4 i ponowić wspólny CTest oraz build web.
  Przydzielić publiczne ProviderRegistry::embed, ABI/HTTP registry, graph_reply,
  fragment oraz immutable PreparedRequest/resume; nie edytowałem nagłówków/CAPI.
  Nowy Chat.send tworzy nowe run/turn IDs, więc sam ponowny send nie domyka potwierdzenia
  wcześniejszej operacji ×10; niskopoziomowe wznowienie dokładnego request działa.
  Rejestracja runnera w CI/CMake należy do właściciela tych plików. Draft PR #12.
- **Do wątku 10:** użyć powyższych rzeczywistych API/trace i settings. UI pozostaje
  w swoim zakresie. Samo posiadanie klucza nie autoryzuje embeddingu ani dodatkowego
  modelu. Fragmenty nie mają indeksów UTF-16; własne kombinacje należy zapisywać jako
  natywne wersje w grafie, a selection traktować jako wskazanie tych wersji.
- **Do wątku 11:** dostarczyć/generować pack/profile presetów context, embedding,
  goal_typing i graph_reply z natywnymi method/version/prompt/recipe/combination DTO,
  vocabulary (w tym parameter_set_version/uses_parameter_set/uses_combination),
  kolejnością parameter_layers, result_methods.fusion/selection i dokładnymi
  parameter/request_bindings. Dostępny na gałęzi W11 RuntimeProfile/RUNTIME_PROFILES.md
  jest wspólną infrastrukturą, lecz nie dostarcza nowych domen/presetów W3; konsumentów
  trzeba połączyć po uzgodnieniu rzeczywistych danych, nie osadzać katalogu w kodzie.
  Obecny main nie ma takich danych; użytkownik może podać pełną definicję, ale nie
  ma nowego domyślnego katalogu. Brak kompletnego presetu blokuje tę nową ścieżkę.
  DIC-0332–0382: historyczne cue/scoring/prompt/reasoning/attachment defaults nie są
  migrowane tym rozszerzeniem; adapter/wartości w kodzie nie udają migracji do danych.
  Dodać konkretną bazę/nakładkę/generowane osadzenie, następnie dopiąć konsumentów W3
  i dowód identycznych default results. `loom/src/search/selector.cpp::index` nadal
  wymaga wspólnej walidacji liczności/wymiarów/finite embeddingów, jak set_profile;
  przekazanie INDEX „Do3” wskazało plik poza zakresem W3, faktycznie należący do11.
