# K3 — projekt: selektor kontekstu, adresy względne, pole widzenia, mapa preferencji

Etap: projekt (tylko dokument; bez zmian w jądrze, bez budowania, bez wywołań modeli). Pakiet K3-design-context-selector, 2026-10-23, koordynator Fable.

Podstawa: słowa właściciela R67, R69, R70 (korekta w R72), R72 (korekta w R74), R74, R76, R77, R79, R81–R87 (`python3 tools/spec.py show R67 R69 R70 R72 R74 R76 R77 R79 R81 R82 R83 R84 R85 R86 R87`). R78 właściciel sam oznaczył jako przykład abstrakcyjny, którego nie trzeba realizować, a R80 jako etap dalszy, mało pilny — oba służą tu tylko do sprawdzenia, czy abstrakcje je uniosą. Badanie: `docs/architecture/K3_BADANIE_KONTEKST_2026-10-23.md` (dalej „badanie", sekcje A–F). Numery linii odnoszą się do `origin/main` 94872898 (odczyt 2026-10-23); każde zdanie „dziś" jest datowaną obserwacją (R60), ważną do pierwszego commita zmieniającego wskazany plik. Skróty jak w badaniu: `CE` = `loom/src/chat/chat_engine.cpp`, `CEH` = `loom/include/loom/chat_engine.h`, `CC` = `loom/data/runtime/chat_context.pack`, `AR` = `loom/data/runtime/action_rules.pack`, `GM` = `loom/src/graph/graph_memory.cpp`, `SF` = `loom/data/runtime/settings_fields.pack`, `OPS` = `loom/src/actions/ops.cpp`, `CF` = `loom/src/chat/context_format.cpp`.

**Jak czytać ten dokument** (CLAUDE.md „Examples are examples", R58–R60, R83, R84): wszystko konkretne — nazwy wymiarów, znaki składni, liczby, kolejności, listy — to **przykłady** (oznaczone „przykład") albo **presety w danych**. Silnik obsługuje każdą konfigurację, także mało użyteczną; wtedy podpowiada (§1i), nie odmawia. Żaden konkret nie jest wymaganiem właściciela, chyba że stoi przy nim jego cytat. Listy są otwarte; lista zamknięta występuje tylko tam, gdzie wymusza ją rzecz sama, z zapisanym powodem i datą (§5, wiersz D9).

## 0. Całość w jednym akapicie

Jest jedna funkcja `select(ContextQuery) -> ContextSelection` (§1d): bierze **pozycję** (węzeł grafu, dla którego budujemy kontekst — adres `0`), opcjonalne **pole widzenia** po osiach (bezwzględne albo modyfikujące; puste = wnioskowane, §1e), i zwraca zbiór kontekstu + ślad + szacunek kosztu. Wymiary grafu (§1a) i składnia adresu względnego (§1b) są danymi; pozycja i pole widzenia to dwie osobne rzeczy (§1c). Układ wyniku szanuje pamięć podręczną dostawcy (§1f), a ponowienie szerzej/głębiej i zawężenie przy zmianie tematu są przepływami wołanymi przez użytkownika, agenta albo eksperymentatora (§1g). Wszystkie wartości domyślne czyta z **mapy preferencji** — węzłów grafu z bogatymi krawędziami (§1h) — a mało użyteczne konfiguracje zgłasza podpowiedzią (§1i). Dzisiejszy `ChatEngine::build_messages` (`CE:1164`) staje się pierwszym klientem tej funkcji, a bajty żądania bez profilu zostają takie same (§2).

---

## 1. Abstrakcje

### 1a. Rejestr wymiarów jako dane (R74, R76, R77, R84)

Plik: `loom/data/graph/dimensions.pack`, schemat `loom.dimensions/1`. Bez enuma w kodzie: wymiar istnieje, bo jest wpisem w paczce (albo węzłem `kind:"dimension"` w grafie — oba źródła czyta ten sam rejestr). Jądro zna tylko kształt wpisu:

| pole | znaczenie | przykład wartości |
|---|---|---|
| `id`, `label{pl,en}`, `why{pl,en}` | nazwa i tekst dla właściciela | `message`, „Kolejne wiadomości" |
| `codec` | typ współrzędnej — nazwa kodeka z rejestru kodeków (otwarty w warstwie danych: kodek nieznany działa jak `name`, patrz niżej; porównanie natywne wymaga wpisu w kodzie, D9); wbudowane startowe (przykłady, nie zamknięty zestaw): `integer`, `double`, `name`, `datetime`, `range`, `predicate`, `levels` | `integer` |
| `levels[]` | tylko dla `codec:"levels"`: nazwy poziomów hierarchii, od najgrubszego | `["major","minor","patch"]` |
| `origin` | co znaczy `0` | `current` (węzeł bieżący) |
| `order` | jak porównywać współrzędne (jaki kodek, jakie pola bazy) | `{"by":["created","rowid"]}` |
| `walk` | jak dojść do sąsiadów: zapytanie bazy, typ linku, metoda | `{"kind":"db_order","query":"conversation_active"}`; `{"kind":"link","link_type":"version_of"}` |
| `between` | co znaczy punkt między węzłami (R77): `mode` = wpis w rejestrze metod (lista otwarta); przykłady startowe: `nearest`, `interpolate`, `derive` (metoda z `method_registry`); tryb bez zarejestrowanej metody → podpowiedź `between_unavailable` (§1i); wynik pochodny ma pochodzenie `{derived:true, from:[a,b], method, params}` | `{"mode":"nearest"}` |
| `words` | słowa dopuszczone w adresie na tej osi i ich znaczenie (część adresu, R74) | `{"last":"max","first":"min","prev":-1,"next":1,"all":"*"}` |
| `step_from_fov` | z której osi pola widzenia wynika krok ruchu i jaką regułą (R79) | `{"axis":"history.window","rule":"max(1, floor(extent/10))"}` |

Wpisy startowe (przykłady, nie lista): `message` (integer; dziś płaska lista `created,rowid`, `db.cpp:773-788`), `message_version` (integer po `version_group_id`/`version_num`, `db.h:78-79`), `program_version` (levels major.minor.patch — przykład właściciela w R76), `branch` (name; drzewo `parent_id`, dziś nieśledzone przy budowie kontekstu, badanie C/R74), `time` (datetime; `between: interpolate`), `weight` (double; dziś tylko prefiks tekstu, badanie C/R77), `perspective` (name; `perspectives.h:1-12`), `scope` (levels z łańcucha `scopes.pack`: engine → … → conversation → turn), `representation_level` (levels L0/L1/L2 z `guardian.pack:728-766`), `scale` (double; to cecha WIDOKU, nie węzła — R79; wpis istnieje, by pole widzenia mogło ją nieść).

Kodek nieznany temu jądru nie jest błędem: wymiar działa jak `name` (napis porównywany regułą `order`), a rejestr wystawia podpowiedź `codec_unknown` (§1i). Rejestr kodeków jest więc otwarty w warstwie danych; zamknięta jest tylko warstwa porównania natywnego (kodek = funkcja porównania w C++) — powód, data i odwrót w §5 (D9).

### 1b. Adres względny: gramatyka jako preset w danych (R72, R74, R76)

Adres to **dana** (JSON), a tekst jest jedną z jej reprezentacji (R80). Składnia tekstowa leży w `loom/data/graph/address_syntax.pack` (schemat `loom.address_syntax/1`): znaki i słowa są presetem, można mieć kilka składni naraz (np. `default` i `owner_2026-10-23`, przykład), parser wybiera wskazaną, a drukarka drukuje w wskazanej.

Kształt danych:
```
Address = {"steps":[Step...]}                      // złożenie między wymiarami, od zewnątrz do wewnątrz
Step    = {"dim":"message","coord":Coord}          // dim pominięty = wymiar domyślny rodzaju węzła bieżącego
Coord   = {"int":-1} | {"double":0.5} | {"name":"x"} | {"word":"last"} | {"datetime":"2026-10-22T10:00Z"}
        | {"range":[Coord,Coord]} | {"levels":[Coord,...]} | {"pred":"<wyrażenie reguł>"}
        // klucz = nazwa kodeka z rejestru kodeków (§1a, otwarty); powyższe rodzaje to przykłady startowe, nie zamknięta unia
```
Preset `default` (przykład gramatyki; każdy znak jest polem paczki):
```
address := step ('/' step)*          step := [dim ':'] coord
coord   := int | double | '"'name'"' | word | coord'..'coord | coord('.'coord)+ | '['pred']'
int     := ['+'|'-'] digits          // 0 = bieżący, -1 = poprzedni, +1 = następny (R74)
```

| adres (preset `default`) | znaczenie | uwagi |
|---|---|---|
| `0` | węzeł bieżący — odpowiedź, którą właśnie budujemy (§1d) | R74: współrzędna, nie etykieta |
| `-1` | poprzednia wiadomość (najczęściej prompt użytkownika) | pozycja względna, nie preset ważności wiadomości (R72, R74) |
| `-1/version:last` | poprzednia wiadomość, jej ostatnia wersja | złożenie dwóch wymiarów |
| `program_version:-1.last.last` | poprzednia duża wersja, w niej ostatnia mała, w niej ostatnia poprawka | hierarchia (R76) |
| `-3..0` | zakres: trzy ostatnie wiadomości i bieżąca | kodek `range` |
| `time:2026-10-22T10:00Z` | punkt w czasie → węzeł najbliższy albo pochodny wg `between` wymiaru | R77 |
| `branch:prev/message:last` | poprzednia gałąź, jej ostatnia wiadomość | słowa są częścią adresu |
| `-1."00"` | **przykład właściciela** (R74: „przy czym to jest oczywiście tylko przykład syntaksu, niekoniecznie dobry") — w presecie `owner_2026-10-23` czytany jako `-1/version:max.last` | alias, nie forma kanoniczna |

Operacje (nagłówek `loom/include/loom/address.h`, czysta biblioteka bez bazy):
```
Result<Address> parse(std::string_view text, const Syntax&);   std::string print(const Address&, const Syntax&);
Result<NodeSet> resolve(const Address&, const Position&, Walker&);   // krok po kroku: NodeSet -> NodeSet (flat-map)
```
Własności: `parse(print(a)) == a` dla każdego wygenerowanego adresu (round-trip, §3); `print(parse(t))` normalizuje (`-1."00"` → `-1/version:max.last` w presecie `default`). Złożenie `a/b` to flat-map, więc `(a/b)/c == a/(b/c)` (§3); **nie** jest przemienne (`-1/version:last` ≠ `version:last/-1`) — to cecha, nie błąd. Adres poza zakresem (`-5`, gdy są trzy wiadomości) daje pusty zbiór i podpowiedź `address_outside`, nie błąd (R58).

### 1c. Pole widzenia a pozycja (R79)

Dwie osobne rzeczy: **pozycja** = węzeł bieżący (adres `0`, rozwiązany względem kotwicy wołającego); **pole widzenia** = rozciągłość wzdłuż każdej osi. Dzisiejsze `hops`, `nodes`, `window`, `budget_tokens` to przykłady osi (badanie A.2 etap 5b, B.3). Osie są danymi — `loom/data/runtime/context_axes.pack`, schemat `loom.context_axes/1` (odpowiedź na badanie F.10):

| pole osi | znaczenie | przykład |
|---|---|---|
| `id`, `label`, `why` | nazwa dla właściciela | `graph.nodes`, „Ile powiązań z grafu" |
| `drives` | jaką wielkość jądra ustawia (lista) | `["graph.max_nodes"]`; dla `history.window`: `["history.window"]` |
| `codec`, `default` | typ i preset | `integer`, `20` (dziś stała 20, `graph_memory.pack`) |
| `steps` | kroki modyfikujące — te same co dziś w `AR:832-900` (`wider` ×2, `narrower` /2, `deeper` +1, `shallower` −1) | odwołanie do `scoped_values.keys.*.steps` |
| `step_keys` | które klucze kroków tę oś ruszają | `["context.breadth_delta"]` |
| `movement` | jak z rozciągłości wynika krok ruchu po związanym wymiarze (R79: „a z nim związane jest szybkość ruchu") | `{"dim":"message","rule":"max(1, floor(extent/10))"}` |

Dziś mapowanie klucz kroku → wielkość jest zaszyte (`context.breadth_delta` → nodes+window, `context.depth_delta` → hops; `CE:1232-1246`) — staje się polem `step_keys` w danych. Nowa oś (np. `detail.resolution`, `relation.hops` z `context_engine.h:53-93`, `time.span`, `scale`) to wpis w paczce, bez zmiany kodu. Oś, której `drives` jądro nie zna, nie jest błędem: ślad notuje `unapplied` (jak dziś `search`/`kb`, `CE:1616-1619`) i podpowiedź `axis_unapplied`.

### 1d. Funkcja selektora (R67, R69, R70; badanie F.1, F.3, F.8)

Miejsce: nowy obszar jądra `loom/src/context_select/` z nagłówkiem `loom/include/loom/context_selector.h`; `ChatEngine::build_messages` (`CE:1164`) staje się jej klientem, a `GraphMemorySelector::expand` (`GM:200-250`), `ContextEngine::build` (`context_engine.h:53-93`) i drzewo pamięci (`memory_engine.h:103-105`) — dostawcami części. Trzy dzisiejsze selektory o różnych wejściach (badanie C/R69) nie są scalane, tylko opakowane; powód i odwrót w §5 (D1).

```cpp
namespace loom::context_select {
struct Position   { std::string conv_id; std::string anchor_id; Address node; };   // node domyślnie "0" względem kotwicy
struct AxisValue  { std::string axis; Json absolute = nullptr; Json steps = Json::array(); }; // bezwzględna albo kroki [{step:"wider",times:1}]
struct ContextQuery {
  Position position;                       // węzeł, dla którego budujemy kontekst
  std::vector<AxisValue> field_of_view;    // puste = wnioskuj (R67/R69); inaczej per oś bezwzględnie lub modyfikująco
  Json inference = nullptr;                // {mode, ask_level, budget}; mode = nazwa wpisu w context_inference.pack (przykłady startowe: off, cues, model; lista otwarta;
                                           //   nieznana nazwa → podpowiedź i wynik bez wnioskowania); null = preset portu wołającego
  std::string caller;                      // "user", "agent", "experimenter", dowolna nazwa (lista otwarta)
  Json preferences = nullptr;              // wejścia łańcucha zakresów (jak scopes::Binding) do odczytu mapy (§1h)
  Json cache_hint = nullptr;               // skróty klas poprzedniego żądania (§1f); null = brak
  Json options = nullptr;                  // dodatki portu; nieznany klucz = błąd (jak dziś CE:690)
  static Result<ContextQuery> from_json(const Json&);  Json to_json() const;
};
struct ContextSelection {
  Json parts;          // [{class, items:[{address, node, text, provenance}], sha256, bytes}] w końcowej kolejności
  Json trace;          // inferred[], dropped[] z powodami, advisories[], field_of_view efektywne, cache{}
  Json cost;           // {tokens_est, bytes, prefix_common_bytes, model_calls:[{purpose, tokens_est}]}
};
struct Services;       // porty: db, graph, memory, context engine, graph selector, preference map, clock, usage ledger (atrapy w testach)
Result<ContextSelection> select(const ContextQuery&, Services&);
}
```

**Co jest węzłem `0`, zanim odpowiedź istnieje** (badanie F.3): wiadomość asystenta powstaje po odpowiedzi (`CE:2266`), a pytanie jest zapisane przed budową kontekstu (`CE:1859`). Pozycja `0` to **węzeł-szkic odpowiedzi**: domyślnie wirtualny (`{kind:"draft_reply", anchor:<id wiadomości użytkownika>}`, istnieje tylko w `ContextQuery` i w śladzie), `-1` = kotwica (prompt), `-2…` = wcześniejsze aktywne wiadomości. Zapis szkicu jako wiersza `nodes` (`kind:"draft"`) to opcja `context_selector.persist_draft` (preset `false`, więc bajty i wiersze bazy bez zmian); po odpowiedzi ślad wiąże szkic z prawdziwym wierszem asystenta.

**Wołający** (R70 „nie tylko na życzenie użytkownika, ale również agenta, eksperymentatora"): jedno wejście, kilka portów (badanie F.8). Użytkownik: `ChatEngine::send` → `select`. Agent: `agent.cpp:156-168` składa dziś płaską historię; dostaje opcję `agent.context_selector` (preset `off` = dzisiejsze bajty; `on` = ten sam `select` z `caller:"agent"`). Eksperymentator: nowa funkcja C `loom_context_select(ctx, query_json)` (dodatek do `loom.h`, §2) i `POST /api/context/select` — zwracają wybór bez wysyłania; `loom/tools/eval` (Python, nieczytany) może je wołać. Presety per wołający w `loom/data/runtime/context_selector.pack` (`callers.<nazwa>`; nazwy to przykłady, lista otwarta).

**Pierwszeństwo wartości osi** (badanie F.9) to preset w danych (`context_selector.pack`, klucz `precedence`); przykład kolejności domyślnej: argument bezwzględny > kroki modyfikujące > reguła łańcucha zakresów > wartość wnioskowana > preset mapy — inna kolejność to inna dana, bez zmiany kodu. Flagi `include_*` i `sources` profilu (`CEH:102-104`, `CC:33-338`) zostają jako wartości bezwzględne — ręczny wybór zostaje jako jedna z konfiguracji, nie jedyny mechanizm (koordynator, na podstawie R67: argumenty „bezwzględne lub modyfikujące").

### 1e. Wnioskowanie zakresu z ostatnich wiadomości i celu (R67, R72; badanie F.4)

Właściciel (R72): „można by w sumie zrobić że z kilku ostatnich wiadomości ustala zakres kontekstu potrzebny do realizacji celu". Dwie warstwy, obie jako dane w `loom/data/runtime/context_inference.pack` (schemat `loom.context_inference/1`):

| warstwa | wejście | mechanizm | koszt | zapis |
|---|---|---|---|---|
| **wskazówki deterministyczne** (preset `cues`, bez modelu) | wiadomości pod adresami `-1..-k` (k = preset, przykład 3), typ celu z `ContextEngine::type_goal` (klasyfikator wskazówek, `context_engine.h:115-122`, `context_goal_cues.pack`), zadanie aktywne, zbieżność perspektyw („widzi we wszystkich perspektywach co się schodzi", R70: dla każdej perspektywy z `perspectives.h:1-12` liczymy węzły osiągalne z ziaren `-1..-k`; waga zbieżności to dana — preset: liczba perspektyw, w których węzeł występuje; przykład skutku: węzeł obecny w wielu perspektywach jest wyżej) | reguły `[{when:{goal_type:"code_edit"}, set:{"graph.hops":1,"project.levels":["project","branch","module","function"]}, why:{pl,en}}]` (przykład); reguła `when:{}` daje dzisiejsze wartości (2 i 20), więc „wnioskowanie bez trafienia" = dzisiejsze bajty | zero wywołań modelu | `trace.inferred[] = {axis, value, method:"cues", evidence:[adresy, reguła], confidence}` + `trace.inference_id` |
| **wnioskowanie modelem** (opcja, preset `off`) | to samo + tekst ostatnich k wiadomości | jeden model z własnym budżetem (wzór: `ContextEngine` autoryzacja jednej próby, `context_engine.h:88-95`); przed wywołaniem szacunek kosztu; `ask_level` z mapy (§1h) decyduje, czy pytać | widoczny w `cost.model_calls` | jak wyżej, `method:"model"`, odpowiedź surowa w magazynie źródeł, nie w śladzie |

Korekta użytkownika staje się daną (R85): zdanie „szerzej" po odpowiedzi działa jak dziś na następną wiadomość (`route.cpp:745-850`, `OPS:2357`), a dodatkowo dopisuje krawędź do mapy preferencji (`provenance:"correction", refers:<inference_id>`, §1h), więc następne wnioskowanie w podobnym kontekście zaczyna od poprawionej wartości. Każde wnioskowane poszerzenie przechodzi przez tę samą bramkę ×10 co ręczne (`request_guard` `CE:91-129`, potwierdzenie `OPS:2432-2506`) — zastosowanie zasady ×10 z CLAUDE.md także do poszerzeń wnioskowanych jest decyzją koordynatora (odwrót: preset bramki dla wnioskowania; §1i).

### 1f. Układ pod pamięć podręczną dostawcy (R70; badanie E.1, F.5, F.6)

Właściciel (R70): „o ile nie zaburzy to układu to modyfikacje dodaje na końcu, a kontekst jest zbudowany od najuniversalniej potrzebnych i ogólnych danych, przez bardziej szczegółowe – na przykład o projekcie później przez gałąź moduł funkcję jeśli koderkę robimy".

| element | projekt | dziś (R60) |
|---|---|---|
| stabilny prefiks | kolejność klas = preset `order` (`CC:8-31`); nowy preset `general_to_specific` (przykład: system, profile, rules, memory, project[levels…], history, graph); poziomy w klasie `project` (projekt → gałąź → moduł → funkcja to **przykład właściciela**) = lista `project.levels` z danych, każdy poziom to zbiór węzłów z adresu po wymiarze `scope`/grafu, renderowany jako podblok od ogółu do szczegółu | pasma stable → project → goal tylko w `ContextEngine` (`context_engine.h:22-25`); czat ich nie czyta |
| modyfikacje na końcu | `modifications.append` (preset `true`): w klasie elementy w stałym porządku po adresie, nowe dopisane na końcu klasy; wnioskowane poszerzenia trafiają do klas po prefiksie (w `stability` graf jest ostatni, `CC:23-31`); eksmisja od początku (okno/budżet, `CE:1501-1527`) łamie prefiks — opcja `history.evict` (`oldest` = dzisiejsze bajty; przykłady innych: `summarise_oldest`, `keep_anchor`) | ogon `CE:1567-1580`; eksmisja najstarszych |
| pomiar wspólnego prefiksu | skrót `sha256` i bajty **per klasa**; skróty poprzedniego żądania rozmowy czytane z `context_trace` ostatniej wiadomości użytkownika (`CE:1901`, dodatkowe klucze `class_hashes`); `trace.cache = {classes:[{class, sha256, bytes, same_as_previous}], common_prefix_bytes, common_prefix_classes, previous_request}`; UI zamienia heurystykę `prefixStable` (`context-receipt-state.ts:179-185`) na zmierzoną wartość, gdy jest | tylko skrót całości (`CE:1584`, `CF:636`); „stabilny prefiks: tak/nie" to heurystyka statyczna |
| znaczniki dostawcy | `provider.cache_control` per trasa (preset `off` → ciało bajt w bajt jak dziś, test złoty); `on` → adapter dopisuje znacznik na granicy klas wskazanej danymi (dla `anthropic_messages` przykład: znacznik na ostatnim bloku prefiksu; czy czat trafia dziś na gałąź z gotowym ciałem `adapters.cpp:296-303` — nieustalone (odczyt 2026-10-23, §6.2), więc K3-5b najpierw sprawdza trasę) | brak jakiegokolwiek mechanizmu cache (badanie E.1) |
| czas życia cache | `loom/data/runtime/provider_cache.pack`: `{provider, ttl_seconds, source, observed_on, valid_until, reliability}` — **datowane obserwacje** (R60), zebrane w K3-5 najpierw z dokumentacji dostawcy; żadnej stałej w kodzie | brak |
| pomiar zużycia | surowe `usage` dostawcy (z polami cache, gdy są) zapisane w metadanych wiadomości asystenta pod kluczem `usage` (setting `chat_store_usage`, preset `true`) i w księdze zużycia; narzędzie `loom/tools/extract/economics.py:199-205` czyta je zamiast tylko archiwów | `usage` wraca w `ChatResult` (`CE:2298`) i nie jest zapisywane (`CE:2245-2258`) |

### 1g. Ponowienie szerzej/głębiej i zawężenie przy zmianie tematu (R70; badanie F.7)

| pytanie | projekt (wszystko konfigurowalne, nazwy to przykłady) |
|---|---|
| kto ocenia „niewystarczająca odpowiedź" | lista otwarta sędziów w danych `retry.judges`: zdanie użytkownika (dziś: `context.modify` na następną wiadomość, `route.cpp:745-850`), dotknięcie „ponów szerzej" na odpowiedzi (web; ponawia TĘ odpowiedź), reguła agenta (`agent.retry = {when:"answer_insufficient", judge:"self"|"model"|"rule", max_retries, widen:[{axis,step}]}` przykład), eksperymentator (jawne wywołanie portu z nowym polem widzenia) |
| kształt ponowienia | `retry.as` = nazwa kształtu ponowienia z danych (lista otwarta; nowy kształt = nowy wpis); przykłady startowe: `"version"` = nowa wersja odpowiedzi w tym samym `version_group_id` (stara → `status='version'`, `db.cpp:627-634`), `"turn"` = nowa wymiana; preset per wołający (przykład: użytkownik `version`, agent `turn`); ślad wiąże ponowienie z oryginałem i zapisuje różnicę pola widzenia |
| sygnały zmiany tematu | lista otwarta w `topic_change.pack`: adnotacja wątku analizatora (filtr wątków `representation.cpp:419-448`, dziś wyłączony), `goal_type(-1) ≠ goal_type(-2..-k)`, pokrycie ziaren poniżej progu (przykład 0.2), zakończone zadanie aktywne, zdanie użytkownika („nowy temat"), przerwa czasowa; wagi sygnałów i reguła łączenia z `scopes.pack` (`weighted_vote`, `ask`…) |
| „już nie będzie powrotu do tego samego przez czas trwania pamięci podręcznej" (R70) | predyktor jako dane `topic_return`: start = reguła (przykład: `P(return) = f(czas od ostatniej aktywności tematu, ttl z provider_cache.pack)`), później wartości uczone z mapy (jak często użytkownik wracał), zawsze z pochodzeniem; decyzja zawężenia → podpowiedź w paragonie + jedno dotknięcie „wróć do szerokiego"; zawężenie nie pyta ×10 (zmniejsza), ale poszerzenie po nim idzie przez bramkę normalnie |

### 1h. Mapa preferencji = węzeł grafu z bogatymi krawędziami (R81, R82, R85, R86, R87; badanie F.2)

Właściciel: „jedno pokrętło to ja widzę wielowymiarową daną mapowaną coraz większą rozdzielczością na podstawie interakcji z użytkownikiem" (R85); „po prostu węzeł w grafie powiązany z innymi bogatymi krawędziami" (R86).

**Kształt danych** (tabele `nodes`/`links` bez zmian schematu; rodzaje „przez użycie", CLAUDE.md):
```
nodes: {kind:"preference", label:"context.axis.graph.nodes", content:null, metadata:{schema:"loom.preference/1", codec:"integer"}}
links: {src:<preference>, dst:<węzeł kontekstu>, link_type:"valued_in", weight:1.0,
        metadata:{value:20, provenance:<tekst z rejestru pochodzeń — otwarty; przykłady startowe: preset, user, correction, inferred, imported>, source:"chat_context.pack#loom",
                  set_by:"…", date:"…", resolution:{level:"person"}, context:{goal_type:"code_edit"}, refers:"<inference_id>"}}
```
Węzły kontekstu (dst) to dowolne węzły grafu: poziomy łańcucha zakresów (`scopes.pack`: installation → person → units → conversation → turn), rodzaje zadań (typy celu), osoby, rozmowy, interakcje (konkretna wiadomość), perspektywy. Wartość siedzi na **krawędzi**, bo wymiary mapy są krawędziami (R86); węzeł niesie tylko tożsamość klucza i kodek.

| operacja | projekt |
|---|---|
| start („gruba mapa") | osiem `selector_profiles` (`CC:33-338`) zostaje źródłem; przy odczycie rzutowane jako wirtualne krawędzie `provenance:"preset"` (bez zapisu do bazy); `materialize` (preset `on_interaction`: pierwsza interakcja z kluczem zapisuje prawdziwe węzły i krawędzie; przykłady innych położeń: `always`, `never`; lista otwarta) |
| odczyt w potrzebnej rozdzielczości | `read(key, context, resolution) -> {value, density, chain, edges}`: kandydaci = krawędzie, których `dst` leży w domknięciu kontekstu żądania (łańcuch zakresów + typ celu + …); konflikt wg reguł konfliktu z `scopes.pack` (lista otwarta; np. `most_specific_wins` — preset, `weighted_vote`, `min`, `max`, `ask`); `density` = liczba punktów na żądanej rozdzielczości lub drobniejszej |
| region rzadki (R81, R82) | `density < min_density` (próg = preset w danych) → **wyprowadź** (interpolacja między sąsiednimi kontekstami) w granicach swobody albo **zapytaj**. Granice swobody to same węzły `preference` (`freedom.creative_licence`, `freedom.compute`, `freedom.time`, `freedom.cost`, `freedom.trust`… — przykłady właściciela plus dodane, lista otwarta, R83). `ask_level` to **liczba**: próg trudności na skali z danych (przykład skali: 0..1), przy którym pytamy (przykłady położeń: `0` = o wszystko, `1` = o nic, preset `0.5` = standard z R82) — nie enum (R84); trudność decyzji liczona z ważonych kryteriów profilu i zadania (dane); reguła wyprowadź/zapytaj to preset w danych |
| wzrost rozdzielczości (R85) | każda interakcja, np. odpowiedź, poprawka, „szerzej", „nie pytaj", zoom, odrzucenie, = nowa krawędź z pochodzeniem; nic nie jest nadpisywane — starsze krawędzie zostają jako historia (odczyt bierze najnowszą na tej samej rozdzielczości, chyba że reguła mówi inaczej) |
| łańcuch zakresów | `scopes::Binding` (`scope_resolver.h:34-50`) dostaje opcjonalne źródło `preference_node`; `Resolution.chain` (`:52-60`) niesie wtedy pochodzenie krawędzi; poziom `turn` zostaje w `TurnCarrier` |
| kontrolka (R87) | trzy luźno sprzężone rzeczy jako dane: (1) **kontrolka** w `loom/data/runtime/controls.pack` `{id, kind:"knob" (lista otwarta: slider, toggle, field, gesture, voice…), renders:{web:…, android:…}, binds_node}`; (2) **węzeł** `preference`; (3) **wiązanie z polem** = krawędź `drives` → rodzaj wiązania z rejestru (lista otwarta; przykłady:) `{kind:"setting", key:"graph_memory_max_nodes"}` / `{kind:"flow_param", flow:"chat.send", param:"graph.nodes"}` / `{kind:"edge_attr", …}`. Jeden węzeł → wiele kontrolek i pól; `settings_fields.pack` dostaje opcjonalne klucze `control` i `preference_node` (brak = kontrolka domyślna dla `type`, `SF:1272-1301`) |

### 1i. Podpowiedzi R58 i „nie pytaj następnym razem" (badanie D.9, F.11)

Kształt: `reason(code, level, params)` z `view_engine.cpp:156-158` (`{code, level, text{pl,en}, params}`) przeniesiony do wspólnego `loom/include/loom/advisory.h` (view_engine zostaje przy swoim do osobnego sprzątania). Podpowiedź **nigdy nie blokuje**: wybór jest zwracany, podpowiedź leży w `trace.advisories[]` i w paragonie. Kody (przykłady): `fov_empty` (nic nie widać: graf 0, okno 0, pamięć wyłączona), `fov_redundant` (ten sam węzeł z dwu adresów), `address_outside`, `codec_unknown`, `axis_unapplied`, `inference_widened` (z `guard` ×10), `cache_prefix_broken` (z nazwą klasy), `narrowed_on_topic_change`. Każda podpowiedź ma „czy to celowe?" i „nie pytaj następnym razem" = krawędź mapy `preference(advisory.<code>.ask_every) --valued_in--> <zakres>` z wartością liczbową (0 = nigdy, 1 = zawsze, n = co n-ty raz; odwracalne w ustawieniach). Bramka ×10 (`request_guard` `CE:91-129`) dotyczy też poszerzeń wnioskowanych: wynik `guard.asked` ląduje w śladzie, potwierdzenie idzie dzisiejszą ścieżką `OPS:2432-2506`.

---

## 2. Co zostaje bez zmian i dlaczego

| co | dlaczego | jak pilnujemy |
|---|---|---|
| bajty żądania bez profilu selektora oraz profili `legacy`/`loom_legacy`/`compat` | komentarz `CE:817-819` „no profile: the settings as they are (today's request bytes)"; złoty plik `loom/tests/fixtures/golden/chat_request_legacy.json` (badanie E.2) | reguła `when:{}` wnioskowania = dzisiejsze wartości; `persist_draft`, `cache_control`, `agent.context_selector` preset off; testy `test_chat_engine.cpp:134-190,461-501`, `test_context_receipt.cpp:143`, `chat_preset_parity.cpp` zielone w każdym pakiecie |
| schemat bazy `schema_version "4"`, formaty ID/czasu/JSON | wartownik zgodności z Pythonem (`loom/tests/compat/`) — od 2026-09-29 (CLAUDE.md) wartownik regresji, nie ograniczenie nowej pracy; brak migracji w K3 to preset (D15) | preset K3: cały nowy stan to wiersze `nodes`/`links` o nowych `kind`/`link_type` oraz dodatkowe klucze w `messages.metadata` (`context_trace.class_hashes`, `usage`); nowe kolumny i tabele dozwolone przez świadomą migrację schematu (D15) |
| C ABI tylko dodatkowo | `libloom.so` eksportuje dokładnie to, co `loom.h` (`test_abi_compat.py`) | nowa funkcja `loom_context_select` = nowy wpis w `loom.h` + test; `loom_chat_ex` dostaje nowe **opcjonalne** klucze (`context`), opis w komentarzu `loom.h:195-203` |
| `ChatOptions::from_json` odrzuca nieznane klucze (`CE:690`) | kontrakt drutu jest jawny | nowe klucze dodawane wprost i wymienione w `loom.h`; zagnieżdżony `context` ma własny `from_json` z tą samą regułą |
| paragon `loom.context_receipt/1` | UI `ContextReceipt.tsx` czyta /1 | tylko klucze dodatkowe (`field_of_view`, `inferred`, `cache`, `advisories`, `addresses`); zmiana numeru zarezerwowana na niezgodną zmianę |
| `TurnCarrier`, łańcuch zakresów, kroki jako dane, filtr wątków, poziomy L0/L1/L2 | działają i są opisane w paragonie (badanie D.1–D.3, D.10) | selektor je woła, nie zastępuje |

## 3. Testy własnościowe / generatywne (przykłady są konieczne, pełne pokrycie wystarczające)

| # | własność | generator | gdzie |
|---|---|---|---|
| T1 | `parse(print(a), S) == a` dla każdej składni S z paczki | losowe adresy: liczba kroków i poziomów hierarchii z parametru generatora, bez górnej granicy w kodzie (próbka startowa: 1–4), każdy kodek, słowa, zakresy | `tests/test_address.cpp` |
| T2 | `print(parse(t))` jest idempotentne (forma kanoniczna) | teksty z T1 + aliasy (`-1."00"`) | j.w. |
| T3 | łączność złożenia: `resolve((a/b)/c) == resolve(a/(b/c))` na atrapie grafu | losowy graf (wiadomości, wersje, gałęzie), losowe adresy | j.w. |
| T4 | adres poza zakresem → pusty zbiór + podpowiedź, nigdy błąd | offsety większe od rozmiaru | j.w. |
| T5 | kodek nieznany → wymiar działa jak `name` + `codec_unknown` | losowe nazwy kodeków | `tests/test_dimensions.cpp` |
| T6 | monotoniczność pola widzenia dla osi z `monotone: up` (preset osi): większa rozciągłość na jednej osi (reszta stała) daje nadzbiór elementów w tej klasie (przed budżetem); oś o innym kierunku (np. `down` albo kierunek zależny od innej zmiennej, CLAUDE.md „Examples are examples") jest testowana własną regułą kierunku z danych | losowe rozciągłości rosnące i losowe kierunki osi, graf z atrapy | `tests/test_context_select.cpp` |
| T7 | kroki: `wider` potem `narrower` wraca do wartości wyjściowej, gdy reguła kroku jest odwracalna; gdy nie (min. 1), ślad to mówi | losowe ciągi kroków | j.w. |
| T8 | stabilność prefiksu: dopisanie modyfikacji przy `modifications.append` nie zmienia skrótów klas przed punktem dopisania | para żądań różniących się tylko poszerzeniem | j.w. |
| T9 | bajty: `cache_control off`, brak profilu, `persist_draft off` → ciało identyczne ze złotym plikiem | trzy warianty złotego pliku | `test_chat_engine.cpp` |
| T10 | mapa: dopisanie krawędzi nigdy nie usuwa ani nie zmienia pochodzenia istniejących; dla reguły konfliktu `most_specific_wins` (preset): `read` po dopisaniu na drobniejszej rozdzielczości zwraca nową wartość, na grubszej — starą; każda inna reguła konfliktu z `scopes.pack` ma własność opisaną osobno, zgodnie z tą regułą (generator losuje też regułę) | losowe ciągi interakcji | `tests/test_preference_map.cpp` |
| T11 | każda konfiguracja przyjęta: losowe pole widzenia z zerami, duplikatami osi, osiami nieznanymi → `select` zwraca wynik (może pusty) + podpowiedzi, nigdy błąd; błąd tylko dla nieznanego klucza JSON (kontrakt drutu) | fuzzer kształtu `ContextQuery` | `tests/test_context_select.cpp` |
| T12 | podpowiedź nigdy nie blokuje: dla każdego wyniku z `advisories` niepustymi `parts` są obliczone i wysyłalne | wyniki z T11 | j.w. |
| T13 | wnioskowanie `cues` bez pasującej reguły = wynik identyczny z `inference:off` | losowe teksty bez wskazówek | j.w. |
| T14 | ×10: wnioskowane poszerzenie, które przekracza bazę kohorty, zawsze ma `guard.asked == true` | bazy i rozmiary losowe | `test_context_receipt.cpp` |

Nieobjęte (luka, nie „poza zakresem"): prawdziwe trafienia cache dostawcy (test z prawdziwymi wywołaniami jako opt-in `cache_probe`, uruchamiany po potwierdzeniu użytkownika z szacunkiem kosztu, nie w bramce; pomiar w księdze zużycia jako uzupełnienie), jakość wnioskowania modelem (eksperyment, nie test jednostkowy), zachowanie na Androidzie (emulator w bramce); do K3-3 pełny T11 (losowe pole widzenia) — K3-2 ma tylko wersję minimalną; tryby `between` poza `nearest` do czasu podpięcia rejestru metod (§6.4).

---

## 4. Pakiety K3-1 … K3-9 (każdy ≤ ~50 wywołań narzędzi, commit po każdym kawałku, karta wznowienia na końcu)

**Pierwszy pakiet: K3-1 (rejestr wymiarów + gramatyka adresu jako dane, parser/drukarka/rozwiązywanie na atrapie, testy własnościowe).** Powód wyboru zamiast „wyodrębnienia selektora z dzisiejszym zachowaniem bajt w bajt": (1) K3-1 nie dotyka `loom/src/chat` ani złotego pliku, więc jest byte-safe trywialnie (nowy nagłówek, nowa paczka, nowy test), a wyodrębnienie musi przepisać ~500 linii `CE:1164-1650` pod złotymi testami — większe ryzyko i ponad 50 wywołań; (2) K3-1 daje słownik (adres, wymiar, kodek), którym K3-2 opisze w śladzie każdy element historii (`0`, `-1`, …) — bez niego wyodrębnienie i tak musiałoby wymyślić te pojęcia; (3) K3-1 ma gotową, mierzalną akceptację (T1–T5). Wartość dla właściciela: jego przykład `-1."00"` parsuje się i drukuje, co da się pokazać w jednej linii.

Wspólne dla wszystkich pakietów: brief wskazuje na CLAUDE.md zamiast kopiować reguły; agent pracuje w jednym worktree, bramka nie biegnie w tym samym drzewie; teksty PL/EN w paczkach mówią, co kod robi DZIŚ; każdy pakiet kończy się kartą: co zrobione, co nie, jak odwrócić.

| id | warstwa | wejście | wynik (pliki) | akceptacja | karta wznowienia |
|---|---|---|---|---|---|
| **K3-1** rejestr wymiarów + adres | kernel + data | §1a, §1b; `graph_entry.pack:978-1000` (osie wpisu jako wzór etykiet) | `loom/data/graph/dimensions.pack`, `loom/data/graph/address_syntax.pack` (presety `default`, `owner_2026-10-23`), `loom/include/loom/address.h`, `loom/include/loom/dimensions.h`, `loom/include/loom/advisory.h` (kształt `reason`), `loom/src/graph/address.cpp`, `loom/src/graph/dimensions.cpp`, `loom/tests/test_address.cpp`, `loom/tests/test_dimensions.cpp` (doctest, generatory losowe z ziarnem) | T1–T5 zielone; `git diff --stat` nie dotyka `loom/src/chat`, złotych plików ani `loom.h`; `-1."00"` → `-1/version:max.last` w testach; paczki z `label/why` PL/EN | adresy JSON + tekst, składnie, kodeki; co nie weszło (np. `predicate` jako napis bez ewaluacji, jeśli tak zostało) |
| **K3-2** selektor wyodrębniony, bajty identyczne | kernel | §1d, `CE:1164-1650`, badanie A.2 etapy 5a–5j | `loom/include/loom/context_selector.h`, `loom/src/context_select/select.cpp`, `services.h` (porty), `build_messages` jako klient; `loom/tests/test_context_select.cpp` (dzisiejsze zachowanie przez `select`) | złote testy `test_chat_engine.cpp:134-190,461-501`, `test_context_receipt.cpp:143`, `chat_preset_parity.cpp` zielone bez zmiany fixture; paragon bajt w bajt (porównanie JSON) ; T11 w wersji minimalnej (puste pole widzenia = dzisiejsze liczby) | lista miejsc, gdzie `build_messages` nadal robi coś poza `select` (ogon, zadanie aktywne) |
| **K3-3** osie jako dane, szerokość na drucie | kernel + data | §1c; `AR:832-900`, `CE:1232-1246`, `CEH:81-155`, `loom.h:195-203` | `loom/data/runtime/context_axes.pack`; `ChatOptions.context = {axes:{<axis>:{absolute|steps}}}`; mapowanie `step_keys` z danych; paragon `+field_of_view`; `loom.h` komentarz; `test_abi_compat.py` bez zmian eksportów | T6, T7; `context_depth` nadal działa (alias osi `graph.hops`); nieznany klucz w `context` → błąd; złote bajty bez zmian | które osie `drives` jądro realizuje, a które są `unapplied` |
| **K3-4** pozycja i szkic odpowiedzi | kernel + data | §1d (węzeł `0`), §1b; `CE:1859`, `CE:2266`, `GM:33-68` (ziarna) | `Position` w `ContextQuery`; adresy `0/-1/-n` w `trace.addresses` i w paragonie; ziarna grafu z adresów `-1..-k` (preset k w `context_selector.pack`); opcja `persist_draft` | T3/T4 na prawdziwym grafie testowym; `persist_draft:false` → zero nowych wierszy bazy (test liczy wiersze); paragon pokazuje adres każdej wiadomości historii | co jeszcze adresuje po staremu (np. `stored_receipt` `CF:665-676`) |
| **K3-5a** pamięć podręczna: pomiar | kernel + web | §1f; `CE:1581-1650`, `CE:1901`, `CE:2245-2258`, `context-receipt-state.ts:179-185` | skróty per klasa w śladzie i w `context_trace.class_hashes`; `trace.cache` ze wspólnym prefiksem; `usage` w metadanych asystenta (`chat_store_usage`); UI „Co wysłaliśmy": zmierzony prefiks zamiast heurystyki | T8; złote bajty bez zmian (ślad to metadane, nie ciało); e2e: paragon pokazuje „wspólny prefiks N bajtów" | gdzie prefiks łamie się w praktyce (lista z E.1 potwierdzona pomiarem na fixture) |
| **K3-5b** pamięć podręczna: dostawcy | kernel + data | §1f; `adapters.cpp:154-158, 296-318`; dokumentacja dostawców (web: oficjalna najpierw) | `loom/data/runtime/provider_cache.pack` (TTL jako datowane obserwacje ze źródłem i wiarygodnością); `provider.cache_control` per trasa; adapter dopisuje znaczniki tylko gdy `on` | T9 (off → bajty identyczne); test `on` dla jednego adaptera porównuje z fixture; każda liczba TTL ma `source`, `observed_on`, `valid_until` | które trasy czatu w ogóle trafiają na adapter z znacznikami |
| **K3-6** wnioskowanie zakresu | kernel + data | §1e; `context_engine.h:115-122`, `context_goal_cues.pack`, `perspectives.h`, `OPS:2432-2506` | `loom/data/runtime/context_inference.pack`; `inference.mode` (wpisy startowe: `cues`, `model`, `off`; lista otwarta); `trace.inferred`; korekta → krawędź mapy (wymaga K3-7 albo zapis tymczasowy w `context_trace` do czasu K3-7 — wybór zapisany w karcie); bramka ×10 dla poszerzeń wnioskowanych | T13, T14; brak profilu → `inference:off` (bajty); tryb `model` ma test z atrapą modelu i szacunkiem kosztu przed wywołaniem | reguły, które weszły; co klasyfikator celu nie rozróżnia |
| **K3-7** mapa preferencji | kernel + data | §1h; `CC:33-338`, `scope_resolver.h:34-60`, `scopes.pack`, `projection.cpp:1515-1524` | `loom/include/loom/preference_map.h`, `loom/src/graph/preference_map.cpp` (`read`, `add_point`, `materialize`); profile jako wirtualne krawędzie `preset`; `Binding.preference_node`; `freedom.*`, `ask_level` jako węzły; `advisory.<code>.ask_every` | T10; `materialize:on_interaction` → zero wierszy przed pierwszą interakcją (test); `Resolution.chain` niesie pochodzenie krawędzi; `tests/compat` zielone | które klucze ustawień już czytają mapę, a które jeszcze `config.json` |
| **K3-8** porty: agent, eksperymentator, ponowienie, zawężenie | kernel + server + web | §1d (wołający), §1g; `agent.cpp:156-168`, `app.cpp:707`, `db.cpp:627-634`, `representation.cpp:419-448` | `loom_context_select` w `loom.h` + `capi_context_select.cpp` + `test_abi_compat.py`; `POST /api/context/select`; `agent.context_selector`; `retry.as` (kształty startowe `version`, `turn`; lista otwarta); `topic_change.pack`, `topic_return` predyktor; „ponów szerzej" i „wróć do szerokiego" w web | `agent.context_selector:off` → bajty agenta jak dziś (fixture); ponowienie `version` tworzy wiersz w tej samej grupie wersji (test); zawężenie zawsze z podpowiedzią w paragonie; e2e web | co eksperymentator (`loom/tools/eval`) już woła, a co jeszcze nie |
| **K3-9** kontrolki jako dane, Warsztat bez atrap (R67a) | data + web | §1h (kontrolka), §1i; `SF:1272-1301`, `ChatView.tsx:1399-1412`, `AUDYT_R67_ATRAPY_2026-10-23.md:36-40` | `loom/data/runtime/controls.pack`; klucze `control`/`preference_node` w `SF`; web renderuje kontrolkę z danych (knob/slider/toggle/field jako pierwsze wpisy, lista otwarta); blok „Context for next message" zastąpiony polem widzenia + wnioskowaniem z paragonu (po polsku, z danych) | e2e: kontrolka związana z węzłem zmienia pole widzenia i jest widoczna w paragonie z pochodzeniem `user`; żaden checkbox-atrapa w `chat`; teksty PL mówią, co działa dziś | kontrolki nieobsłużone w web (np. gest, głos) jako luka |

Kolejność to zależności, nie świętość: K3-5a/b i K3-6 mogą iść równolegle po K3-3 (dwa worktree, jedna bramka naraz, CLAUDE.md). K3-7 przed K3-6 jest dopuszczalne (wtedy korekta od razu trafia do mapy).

---

## 5. Decyzje z odwrotem (koordynator, 2026-10-23; żadna nie jest słowem właściciela, chyba że z cytatem)

| id | decyzja | powód | odwrót |
|---|---|---|---|
| D1 | selektor to nowy obszar jądra (`context_select/`), a `build_messages`, `ContextEngine`, `GraphMemorySelector` są jego klientem/dostawcami | trzy dzisiejsze selektory mają różne wejścia (badanie C/R69); opakowanie tańsze od scalania; `ContextEngine` jest związany z przebiegiem wiedzy | usunąć obszar, `build_messages` wraca do kodu wbudowanego (K3-2 trzyma bajty, więc odwrót nie zmienia żądań) |
| D2 | pierwszy pakiet = K3-1 (rejestr + adres), nie wyodrębnienie | §4, trzy powody | zacząć od K3-2; K3-1 i tak potrzebny później |
| D3 | węzeł `0` = wirtualny szkic odpowiedzi; zapis jako wiersz `nodes` tylko opcją (`persist_draft`, preset `false`) | odpowiedź powstaje po wysyłce (`CE:2266`); brak wierszy = brak ryzyka dla wartownika compat | zmienić preset na `true` (jedna linia w `context_selector.pack`) |
| D4 | adres to JSON; tekst to reprezentacja wg składni z paczki; `-1."00"` to alias w presecie `owner_2026-10-23`, forma kanoniczna `-1/version:max.last` (przykład) | R74: zapis to „tylko przykład syntaksu"; dwie składnie naraz pokazują, że znaki są danymi | zmienić preset `default` w paczce; parser/drukarka bez zmian |
| D5 | osie pola widzenia i mapowanie kroków → wielkości to dane (`context_axes.pack`), nie kod (`CE:1232-1246`) | badanie F.10; R83 „żadne dwa pokrętła" | zostawić oba: kod czyta paczkę, a przy braku wpisu używa dzisiejszego mapowania (zapisane jako wpis `legacy`) |
| D6 | wnioskowanie domyślnie `cues` tylko z profilem selektora; bez profilu `off` (dzisiejsze bajty); reguła `when:{}` = dzisiejsze liczby | złoty plik (badanie E.2); właściciel R67: domyślnie wnioskowane — spełnione dla każdego profilu, w tym `loom` | zmienić preset ścieżki bez profilu na `cues` razem ze świadomą zmianą złotego pliku |
| D7 | wartość preferencji żyje na krawędzi `valued_in`, nie w `content` węzła | R86: „wymiary" mapy to krawędzie; historia zostaje bez nadpisywania | dodać `content` jako kopię najnowszej wartości (denormalizacja), jeśli odczyt okaże się za drogi |
| D8 | profile z `chat_context.pack` są rzutowane jako krawędzie `preset` wirtualnie; zapis do bazy przy pierwszej interakcji (`materialize:on_interaction`) | zero wierszy przed interakcją chroni fixture i compat; R85: rozdzielczość rośnie z interakcji | `materialize:always` (zapis przy starcie) albo `never` (mapa tylko w pamięci) |
| D9 | rejestr **kodeków** współrzędnych zamknięty tylko w warstwie porównania natywnego; w warstwie danych otwarty (nieznany → `name` + `codec_unknown`, §1a). Wszystkie pozostałe listy dokumentu — wymiary, osie, rodzaje współrzędnych, tryby `between`, słowa adresu, tryby wnioskowania, kształty ponowienia, pochodzenia, reguły konfliktu, sędziowie, sygnały, wiązania, kontrolki, wołający — otwarte | R84: lista tylko gdzie wymusza rzecz sama; obserwacja z 2026-10-23: kodek = funkcja porównania w C++ | nowy kodek = nowy wpis w rejestrze w kodzie + test; porównanie jako predykat z danych (§6.3) otwiera także tę warstwę |
| D10 | `ask_level` i `ask_every` to liczby (próg na skali z danych, przykład 0..1; co n-ty raz), nie wyliczenia | R82 daje trzy przykłady położeń, R83/R84 zabraniają zamknąć listę | dodać nazwane presety (`standard`, `everything`, `nothing`) jako aliasy liczb w danych |
| D11 | paragon zostaje `loom.context_receipt/1` z kluczami dodatkowymi | UI czyta /1; koszt zmiany w web na tym etapie (koordynator) | podnieść do /2 przy pierwszej niezgodnej zmianie |
| D12 | `usage` dostawcy zapisywane w metadanych asystenta (preset `chat_store_usage:true`) | bez pomiaru nie ma „świadomości cache" (badanie E.1); to metadane, nie ciało żądania | ustawienie na `false`; klucz pozostaje opcjonalny w czytnikach |
| D13 | TTL cache dostawców tylko jako datowane obserwacje w paczce, zbierane z dokumentacji oficjalnej (K3-5b), nigdy stała | R60; dostawcy zmieniają zasady | preset TTL w danych z `valid_until`; stała w kodzie tylko jako wartość startowa wpisu w paczce; poszczególne wpisy mają `valid_until` |
| D14 | zawężenie przy zmianie tematu nie pyta ×10 (zmniejsza koszt), ale zostawia podpowiedź i jedno dotknięcie powrotu | zasada ×10 dotyczy wzrostu; zasada koordynatora „nic nie dzieje się po cichu" wymaga podpowiedzi | dodać potwierdzenie także dla zawężeń (preset `narrow.ask`) |
| D15 | K3 nie dodaje kolumn ani tabel: nowy stan to wiersze `nodes`/`links` i klucze w `messages.metadata` (§2) | wartownik compat od 2026-09-29 (CLAUDE.md) jest wartownikiem regresji, nie ograniczeniem nowej pracy; brak migracji jest wyborem najmniejszego ruchu, nie zakazem | świadoma migracja schematu: podniesienie `_SCHEMA_VERSION`/`schema_version` i aktualizacja wartownika compat w tym samym commicie |

Fakty o stanie dzisiejszym cytowane w tym dokumencie pochodzą z badania (odczyt kodu 2026-10-23, `origin/main` 94872898) i z własnych odczytów `scope_resolver.h:34-60`, `context_engine.h:50-95`, `view_engine.cpp:156-158`, `CC:33-62`, `loom.h:194-206` z tej samej daty i tego samego commita.

---

## 6. Luki i pytania (techniczne; dla właściciela: żadnych — nic tu nie dotyczy jego pieniędzy ponad budżet, jego danych prywatnych u nowego dostawcy ani sprzeczności z wymaganiem)

1. **`loom/tests/compat/test_chat_compat.py` nieczytane** — zanim K3-4/K3-7 zapiszą cokolwiek do `nodes`/`links` w drodze czatu, pakiet sprawdza, co ten test porównuje (ryzyko: liczy wiersze po `send`). Presety `persist_draft:false` i `materialize:on_interaction` zostały dobrane tak, by to ryzyko znieść bez czytania; czytanie i tak jest pierwszym krokiem K3-4.
2. **Trasa adaptera dla czatu** (badanie E.1): nie wiadomo, czy czat kiedykolwiek kieruje na `anthropic_messages` (gałąź z gotowym ciałem `adapters.cpp:296-303`). K3-5b zaczyna od tego ustalenia; bez niego `cache_control` może nie mieć gdzie zadziałać dla dzisiejszych tras (fakt do zmierzenia, nie argument przeciw opcji — CLAUDE.md „absence today").
3. **Ewaluacja predykatów w adresie** (`[pred]`): język wyrażeń reguł istnieje w `action_rules`/`scopes` (nieczytany pod tym kątem). K3-1 może przyjąć predykat jako napis z kodekiem `predicate` i odłożyć ewaluację do K3-4 (karta to zapisze).
4. **`between: interpolate/derive`** — stan `method_registry.cpp` nieustalony (nieczytany, badanie F.13); K3-1 zaczyna od jego odczytu i zapisuje stan jako datowaną obserwację w karcie; do podpięcia rejestru metod K3-1 implementuje `nearest`, a pozostałe tryby są wpisami z podpowiedzią `between_unavailable` — wpisane w §3 jako luka, nie „poza zakresem".
5. **Zbieżność perspektyw** (R70 „widzi we wszystkich perspektywach") — koszt BFS po wszystkich perspektywach rośnie z ich liczbą; K3-6 mierzy na fixture i wystawia budżet jako oś pola widzenia (`perspectives.max`), nie stałą.
6. **Agent i duplikacja**: selektor grafu wyklucza bieżącą rozmowę (`GM:146-148`), historia bierze tylko bieżącą (badanie E.5); gdy agent przejdzie przez `select`, trzeba sprawdzić, czy jego własna historia narzędzi nie dubluje się z klasą `history`.
7. **Współbieżność** (badanie E.5): `select` wołany równolegle z `send`/`preview_usage`/`context_guard`; nowe odczyty (węzły mapy, perspektywy) wchodzą do weryfikacji `native_request_inputs` (`CE:433`) albo są z niej jawnie wyłączone — decyzja w K3-2 z zapisem w karcie.
8. **Rozmiar paragonu**: opis wielowymiarowego wyboru może przekroczyć `context_receipt_max_bytes` (65536, `CE:1642`); zwijanie (`CF:641-656`) musi objąć nowe sekcje — K3-3 dodaje test z paragonem ponad limit.
9. **Profile `like_*`** (badanie F.12): `search`, `kb`, `search_limit` nadal `unapplied` do czasu, aż istnieje dostawca części `search`; selektor zgłasza to jak dziś, z podpowiedzią `axis_unapplied`.
10. **Rejestr po badaniu**: istnieją R88 (jedna specyfikacja w ustalonym formacie) i R89 (dedykowany interfejs współpracy). W pakiecie projektowym sprawdzono tylko tytuły; treść odczytano w kontroli przed użyciem (`tools/spec.py show R88 R89`, 2026-10-23, obserwacja): oba dotyczą interfejsu i ekonomii pracy koordynator–właściciel (jedna specyfikacja, tani interfejs konwersacyjny, żonglerka modelami, research o oszczędzaniu tokenów); jedyny styk z K3 to §1f (pamięć podręczna dostawcy jako jedno ze źródeł oszczędności) — bez zmiany projektu. Ważne do następnego wpisu w rejestrze.
11. **Nieobjęte testami** (§3, ostatni akapit): trafienia cache u dostawcy (opt-in z szacunkiem kosztu), jakość wnioskowania modelem, Android — pomiar w księdze zużycia i eksperymenty, nie testy jednostkowe; pełny T11 i tryby `between` poza `nearest` — luki do K3-3 / podpięcia rejestru metod.

---

## Kontrola przed użyciem (chatadhd, 2026-10-23)

Kontrola read-only (Haiku, 4 antywzorce + pytanie R60) dała 33 ustalenia: 10 blokujących, 23 słowne. Poprawki naniesione jako addendum (nie restart), bez zmiany treści decyzji i bez dodawania zakresu. Cytaty właściciela sprawdzono w `tools/spec.py show R67 R70 R72 R74 R79 R82 R83 R84 R60 R88 R89`.

**Zastosowane: 31.** Blokujące (10): §1a `codec` i `between` — rejestr kodeków otwarty w warstwie danych, tryby `between` jako wpisy rejestru metod (przykłady startowe); §1b `Coord` — klucz = nazwa kodeka, rodzaje to przykłady; §1d `inference.mode` i pierwszeństwo osi — preset w danych (`precedence`), tryby jako wpisy paczki; §1g `retry.as` — kształt z danych, `version`/`turn` to przykłady; §1h `provenance` — tekst z otwartego rejestru; §2 schemat bazy — wartownik compat to od 2026-09-29 wartownik regresji, brak migracji to preset (nowy wiersz D15); §3 T6 — monotoniczność tylko dla osi `monotone: up`, inne kierunki testowane własną regułą; T10 — własność zawężona do `most_specific_wins`, inne reguły osobno. Słowne (21): D9 bez „jedyna lista zamknięta”, lista otwartych rozszerzona; waga zbieżności perspektyw jako dana; skala `ask_level` jako przykład (0..1), `min_density` i reguła wyprowadź/zapytaj jako presety (§1h, D10); rodzaje wiązań, interakcji, reguł konfliktu, położeń `materialize` oznaczone jako listy otwarte; generator T1 bez górnej granicy w kodzie; §1f trasa adaptera jako nieustalona (zgodnie z §6.2); §6.10 — treść R88/R89 odczytana, wpływ zapisany jako datowana obserwacja; „ręczny wybór … nie jedyny mechanizm” przypisane koordynatorowi (R67 ma `rozumienie: proposed`); cytaty R79, R74, R70 przywrócone dosłownie (w tym pisownia „najuniversalniej” z zapisu właściciela); „co ważne” i „nic ukryte po cichu” bez cudzysłowu właściciela (nie występują w rejestrze); zasada ×10 dla poszerzeń wnioskowanych jako decyzja koordynatora z odwrotem; D13 z odwrotem zamiast powołania na R60; D11 bez oceny „bez wartości dla właściciela”; test cache z prawdziwymi wywołaniami jako opt-in z szacunkiem kosztu (§3, §6.11); §6.4 — `method_registry.cpp` jako nieustalony stan do odczytu w K3-1, a tryby poza `nearest` i pełny T11 wpisane w §3 jako luki.

**Odrzucone: 2.** (a) D1 „opakowanie tańsze od scalania” — to decyzja architektoniczna koordynatora o wnętrzu jądra, nie opcja użytkownika; koszt jest dopuszczalnym powodem decyzji technicznej (CLAUDE.md „Decisions”), a odwrót jest zapisany; dopisanie „scalenia jako osobnego etapu” byłoby nowym zakresem. (b) §4 „większe ryzyko i ponad 50 wywołań” — to kolejność pakietów wg zależności i ryzyka, nie degradacja opcji: K3-2 zachowuje pełny zakres, a D2 ma odwrót („zacząć od K3-2”).

Odpowiedź R60 z kontroli (trzy miejsca oparte na nieustalonych faktach: §6.4, §2, D13) — wszystkie trzy poprawione powyżej.
