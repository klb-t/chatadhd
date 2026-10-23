# K3 — badanie: jak dziś powstaje kontekst wysyłany do modelu

Etap: badanie (tylko odczyt kodu, bez budowania i bez wywołań modeli). Pakiet K3-research-context-selector.
Stan kodu: `origin/main` 94872898, czyli `/home/user/wt/ro-main` (odczyt 2026-10-23). Każde zdanie o kodzie to **obserwacja z tej daty** (R60: fakt o teraźniejszości, ważny do pierwszego commita, który zmienia wskazany plik; źródło: odczyt kodu bez uruchomienia, wiarygodność wysoka dla „co jest w pliku", brak pomiaru dla „co robi w runtime"). Luki w sekcji C to fakty o stanie dzisiejszym, nie ograniczenia projektu.

Ścieżki są względne do korzenia repo (`loom/...`). Skróty w tekście:
`CE` = `loom/src/chat/chat_engine.cpp`, `CEH` = `loom/include/loom/chat_engine.h`, `CC` = `loom/data/runtime/chat_context.pack`, `AR` = `loom/data/runtime/action_rules.pack`, `GM` = `loom/src/graph/graph_memory.cpp`, `SF` = `loom/data/runtime/settings_fields.pack`, `OPS` = `loom/src/actions/ops.cpp`, `CF` = `loom/src/chat/context_format.cpp`.

## 0. Rama: słowa właściciela, które to badanie ma obsłużyć

- R85 (`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md:865`, cytat dosłowny): „właśnie to co ty mówisz jedno pokrętło to ja widzę wielowymiarową daną mapowaną coraz większą rozdzielczością na podstawie interakcji z użytkownikiem". Dziś głębokość i szerokość to pojedyncze liczby (patrz A.2 etap 4b i B.2), więc to jest najważniejsza luka (C, wiersz R85).
- R86 i R87 padły **po** briefie (rejestr `tools/spec.py list --theme owner_requirements`, ostatnie pozycje, 2026-10-23): R86 (`OWNER_REQUIREMENTS_2026-09-26.md:870`) „po prostu węzeł w grafie powiązany z innymi bogatymi krawędziami"; R87 (`:875`) pokrętło jako kontrolka definiowana i renderowana jako dane, przypisana do węzła grafu, a węzeł do pola konfiguracji. Dotykają ustawień i kontekstu, więc mają własne wiersze w C. Nie widziałem R88 i nowszych (sprawdzone na końcu pracy, patrz F.0).
- Wszystko konkretne w briefie (R67..R85: liczby, składnia `-1."00"`, nazwy wymiarów) czytam jako przykłady, które silnik ma obsłużyć w każdej konfiguracji (CLAUDE.md, „Lists are examples", „Examples are examples").

---

## A. Potok dziś

### A.1 Schemat (tekst)

```
wiadomość użytkownika (web/CLI/C ABI)
  |  POST /api/chat  -> loom_chat_ex -> ChatEngine::send
  |  (zdanie "szerzej"/"głębiej"/"profil selektora X" jako CAŁA wiadomość NIE idzie do modelu:
  |   router -> akcja context.modify -> TurnCarrier; dotyczy NASTĘPNEJ wiadomości)
  v
[1] ChatOptions::from_json            (nieznany klucz = błąd)
[2] TurnClaim: odbiór wartości "na tę turę" z TurnCarrier
[3] select_representation             (profil selektora -> reprezentacja/przepływ; guardian::select: poziomy L0/L1/L2, filtr wątków, reguły)
[4] send(): rozwiązanie rozmowy, polityka paragonu, [zadanie aktywne], ZAPIS wiadomości użytkownika
[5] build_messages (CE:1164)  <- tu powstaje kontekst
      4a profil selektora + kolejność części (order)
      4b liczby: hops / nodes / window  (żądanie > profil > ustawienie) + modyfikatory (szerzej/głębiej)
      4c części: system, rules, profile  (stałe)
      4d przepływ (flow) i ewentualny pakiet wyboru (packet)
      4e memory   (get_active_context, drzewo pamięci, domyślnie do 16000 znaków)
      4f graph    (GraphMemorySelector: ziarna z TEKSTU pytania, BFS po linkach do głębokości, max węzłów)
      4g project  (opt-in: ContextEngine/knowledge albo "unified")
      4h history  (cała rozmowa: aktywne wiadomości; okno; budżet)
      4i składanie według order + ogon: [zadanie] [instrukcja odpowiedzi-graf] PYTANIE
      4j ślad + paragon (loom.context_receipt/1)
[6] retain_request_metadata           (ślad/paragon do metadata wiadomości użytkownika)
[7] compose_chat_payload              (model, messages, temperature, max_tokens, stream, plugins, reasoning, include_reasoning)
[8] (opcjonalnie) przepis graph-reply łata payload
[9] request_guard w paragonie (porównanie z bazą rodzaju, reguła x10)
[10] llm::prepare_call -> adapter openai_chat: ciało wysyłane bajt w bajt -> execute_call (bramka zużycia)
[11] odpowiedź: usage zwrócone w ChatResult, wiadomość asystenta zapisana PO odpowiedzi
```

### A.2 Tabela etapów (nazwa, miejsce, co wchodzi i wychodzi)

| # | Etap | Gdzie (file:line) | Wejście -> wyjście |
|---|---|---|---|
| 0 | Wejścia | `loom/server/src/app.cpp:707` (POST /api/chat), `loom/src/capi/capi_chat.cpp:126` (`rt->chat().send`), `loom/cli/main.cpp:411`, `loom/include/loom/loom.h:204` (`loom_chat_ex`) | JSON żądania -> `ChatEngine::send` |
| 0b | Zdania-modyfikatory przed wysyłką | `loom/src/actions/route.cpp:745-850` (cała wiadomość „szerzej/węziej/głębiej/płycej/profil selektora X" -> `context.modify`, bez zapisu reguły; „na stałe" lub zakres -> `rules.set`), `loom/src/actions/ops.cpp:2357-2510` (`context_modify`), tabela fraz `loom/data/runtime/intent_routing.pack:4804` | zdanie -> wartość w TurnCarrier |
| 1 | Parsowanie opcji | `CE:614-696` (`ChatOptions::from_json`; klucze przyjmowane: conv_id, model, attachments, web_search, deep_research, reasoning_effort, temperature, max_tokens, system_prompt, context_order, context_depth, stream, include_memory, include_graph_memory, include_history, knowledge_context, trace_context, active_task_*, flow, selector_profile, usage_confirmation, new_conversation...; nieznany klucz -> `CE:690` błąd) | JSON -> `ChatOptions` (`CEH:81-155`) |
| 2 | Odbiór wartości „na tę turę" | `CE:1679-1689` (`TurnClaim`, oddaje wartości przy błędzie), `CE:79` (`with_carried_turn`), `loom/src/chat/turn_carrier.cpp:4-60` (`add/peek/take/give_back`) | TurnCarrier -> `opts.turn_values` |
| 3 | Reprezentacja i przepływ | `CE:915-960` (`select_representation`: nazwa reprezentacji i przepływu z profilu selektora; `guardian::select` `loom/include/loom/guardian.h:62`), filtr wątków `loom/src/guardian/representation.cpp:419-448` | opts -> `history_text`, `history_skip`, `guardian_rules`, `active_task_spec` |
| 4 | Rozwiązanie rozmowy, paragon, zapis pytania | `CE:1700-1737` (rozmowa), `CE:1749-1765` (`trace_full`, `record_context`, `stored_trace`), `CE:1773-1832` (ścieżka zadania aktywnego: kompilacja, ABA-check `native_request_inputs` `CE:433`), `CE:1845-1859` (zapis wiadomości użytkownika PRZED zbudowaniem kontekstu), `CE:1917` (zdarzenie MSG_CREATED) | tekst -> wiersz `messages` (rola user) |
| 5 | **Budowa wiadomości** | `CE:1164` (`build_messages`) | rozmowa + tekst + opts -> tablica `messages` + ślad |
| 5a | Profil selektora i kolejność | `CE:1191-1203`, `CE:801-863` (`SelectorChoice`, `choose_selector_profile`) | pack `chat_context` -> wybrany wiersz profilu, nazwa kolejności |
| 5b | Liczby (głębokość, szerokość, okno) i modyfikatory | `CE:1227-1250`, `CE:865-902` (`Delta`, `read_delta`, `apply_steps`) | request/profil/ustawienie + kroki -> `hops_eff`, `nodes_eff`, `window_eff` |
| 5c | Części stałe: system, reguły, profil | `CE:1249-1280`, `loom/src/chat/profile_prefix.cpp` (render deterministyczny, `loom/src/chat/profile_prefix.h:1-47`) | config/opts/profil użytkownika -> `part_system/rules/profile` |
| 5d | Przepływ (flow) i pakiet wyboru | `CE:1291-1307`, `CE:761-799` (`selection_entries`, `selection_packet`), `loom/src/chat/context_format.cpp` | profil guardian `context_io` -> packet (graf wiadomości z linkami) |
| 5e | Pamięć (drzewo) | `CE:1308-1338`, `loom/include/loom/memory_engine.h:18,103-105` | `memory_->get_active_context()` -> `part_memory` („User's memory/context:\n...") |
| 5f | Pamięć grafowa | `CE:1339-1379`, `GM:89-198` (`select_impl`) | tekst pytania -> ziarna -> BFS -> wiadomości z INNYCH rozmów -> `part_graph` |
| 5g | Projekt / wiedza | `CE:1380-1440`, `loom/src/runtime.cpp:527` (builder), `loom/include/loom/context_engine.h:53-93`, `loom/src/context/unified_context.cpp:199` | `knowledge_context` lub `context_execution.request` -> `part_project` |
| 5h | Historia, okno, budżet | `CE:1442-1530`, `loom/src/db/db.cpp:773-788` (`get_msgs`) | wszystkie `status='active'` wiersze rozmowy -> `hist` (okno `CE:1501-1506`, budżet `CE:1507-1530`) |
| 5i | Składanie | `CE:1543-1566` (pakiet, pętla po klasach kolejności), `CE:1567-1580` (ogon: zadanie aktywne, instrukcja odpowiedzi-graf, PYTANIE) | części -> `messages` |
| 5j | Ślad i paragon | `CE:1581-1650`, `CF:628-656` (`receipt_finish`, `receipt_bound`) | -> `context_trace` i `loom.context_receipt/1` |
| 6 | Zachowanie śladu | `CE:1874-1915` (`retain_request_metadata`; `CE:1901` zapis `metadata.context_trace`) | -> `messages.metadata` wiersza użytkownika |
| 7 | Treść żądania | `CE:137-155` (`compose_chat_payload`, wywołanie `CE:1975`) | `messages` -> `{model, messages, temperature, max_tokens, stream, [plugins], [web_search_options], reasoning, include_reasoning}` |
| 8 | Przepis graph-reply | `CE:1976-2011`, `CE:1956` | opcjonalna łata payloadu |
| 9 | Strażnik wzrostu | `CE:91-129` (`request_guard`), `CE:2015-2018` | rozmiar ciała vs baza rodzaju -> `receipt.guard` |
| 10 | Transport | `CE:2021-2071`, `loom/src/llm/use_model.cpp:313` (`prepare_call`), `loom/src/llm/adapters.cpp:154-158` (ciało `json::dump(spec.body)` bez zmian) | `mreq.body = payload` -> HTTP |
| 11 | Odpowiedź | `CE:2245-2275` (zapis wiadomości asystenta, `db_.create_msg(anm)` `CE:2266`), `CE:2298` (`result.usage = usage`) | odpowiedź -> wiersz `messages` (rola assistant) |

Boczne wejścia do tego samego `build_messages` bez wysyłania: `preview_usage` (`CE:982`), `context_guard` (`CE:1010`), `context_selection` (`CE:962`, tylko pakiet wyboru). Podgląd i strażnik **podglądają** TurnCarrier (`peek`), a `send` go **zużywa** (`take`) (`CE:992` vs `CE:1688`).

### A.3 Co jest w ciele żądania i w jakiej kolejności

Ciało (`CE:137-155`): `model`, `messages`, `temperature`, `max_tokens`, `stream`, opcjonalnie `plugins:[{id:web,...}]` i `web_search_options`, pola rozumowania (`chat::apply_reasoning_recipe`), zawsze `include_reasoning:true`. **Nie ma klucza `tools`** w żądaniu czatu; narzędzia dodaje osobna pętla `loom/src/llm/tool_loop.cpp:506` (`req.parameters["tools"]`, tylko gdy `native && !tools_wire.empty()`), używana przez agenta, nie przez `ChatEngine::send`.

Kolejność części w `messages` wynika z nazwy kolejności (`CC:8-31`), potem ogon (`CE:1557-1580`):

| kolejność (preset) | klasy po kolei |
|---|---|
| `legacy` | system, memory, graph, project, history |
| `stable_first` | system, memory, project, history, graph |
| `stability` (domyślna, `CC:7`) | system, profile, rules, memory, project, history, graph |

Ogon zawsze na końcu: [zadanie aktywne, rola user] [instrukcja „odpowiedz grafem", rola system] **pytanie** (rola user, `build_content` `CE:1579`). Pod kolejnością bez klasy `rules` reguły o osobie zostają w części system (`CE:1254-1262`).
Zamrożone bajty: `loom/tests/fixtures/golden/chat_request_legacy.json` ma trzy warianty (`legacy`: SYS, RULES, MEM, „Relevant context from past conversations", HIST, pytanie; `stable_first` i `stability`: SYS, RULES, MEM, HIST, graf, pytanie), przypięte w `loom/tests/test_chat_engine.cpp:134-190` (ciało nie-strumieniowe i strumieniowe) oraz `:461-501`, i w `loom/tests/test_context_receipt.cpp:143`.

### A.4 Skąd bierze się każda klasa (reguła wyboru)

| klasa | reguła dziś | file:line |
|---|---|---|
| system | `opts.system_prompt` albo ustawienie `system_prompt`; nic, gdy puste | `CE:1186,1249-1252` |
| rules | `opts.guardian_rules` (z `guardian::select`), jeśli źródło `rules` włączone w profilu | `CE:1254-1262` |
| profile | deterministyczny blok „co użytkownik o sobie powiedział": tylko wartości z warstwy `user`; poziom `profile_detail_level` (minimal/normal/detailed) | `CE:1264-1280`, `loom/src/chat/profile_prefix.h:1-47`, `CC:339` |
| memory | `memory_->get_active_context()` (cały aktywny podzbiór drzewa; nagłówek `memory_engine.h:18` mówi o 16000 znakach) | `CE:1308-1338` |
| graph | ziarna z TEKSTU pytania (`extract_seed_labels_checked`, `GM:33-68`, szczegółów ekstrakcji NIE czytałem), `find_node(label)` i `find_node(lower(label),"topic")` (`GM:123-126`), BFS po `get_links` do `max_d` (`GM:137-162`), tylko wiadomości `m_...` spoza bieżącej rozmowy (`GM:146-148`), do `max_n` wiadomości, kolejność = kolejność BFS; bez wag i bez punktacji; wiersz z szablonu (tytuł 30 znaków, fragment 500) | `GM:89-198`, `loom/data/runtime/graph_memory.pack` (defaults) |
| project | opt-in: `opts.knowledge_context` albo `context_execution.request`; `request.text` domyślnie = tekst pytania | `CE:1380-1440`, `CE:1391`, `loom/src/runtime.cpp:527` |
| history | WSZYSTKIE wiersze rozmowy o `status='active'` w kolejności `created, rowid`; `parent_id` nie jest śledzony; pomijane: bieżące pytanie, ruch strażnika, ruch komend, `history_skip`, zastąpione przez zadanie; podmieniane: poziomy L1/L2; prefiks `[IMPORTANT]` dla `weight>1.5`, `[low priority]` dla `<0.5`; okno „ostatnie N" (`window_eff`, domyślnie null = cała); budżet (`budget_tokens`, domyślnie null): najpierw wypada klasa graph, potem najstarsze wiadomości, ostatnia zostaje | `CE:1442-1530`, `loom/src/db/db.cpp:773-788` |
| tail | zadanie aktywne, instrukcja odpowiedzi-graf, pytanie | `CE:1567-1580` |

### A.5 Paragon „co poszło do modelu"

`loom.context_receipt/1` powstaje z tych samych części co żądanie (`CE:1581-1650`): sekcje według kolejności, pozycje (element listy pamięci, wiadomość z grafu, wiadomość historii z poziomem), `dropped` z powodem (`source_off`, `budget`, `history_window`, `excluded`, `command_traffic`, `thread_filter`, `representation_level`, `replaced_by_task`), `profile` (id, źródło, status, `applied`: order/nodes/history_window/hops/representation/flow/budget, `unapplied`: `search`, `kb`), `modifiers` (kroki szerzej/głębiej i zakres tury/łańcucha), `guard`, `sha256` całej tablicy. Polityka zapisu: ustawienie `context_receipt_store` (`SF:4393`; `auto|always|never`, `CE:1206`), limit `context_receipt_max_bytes` domyślnie 65536 (`CE:1642`, `SF:4453`); po przekroczeniu lista elementów zwija się do pozycji zbiorczych (`CE:1642-1651`, `CF:641-656`). Do `messages.metadata` trafia pełny ślad albo odchudzony (`stored_trace`, klucze: kind, version, context_order, messages_sha256, pre_graph_messages_sha256, selection, flow, receipt). Paragon opisuje sam siebie jako „compiled request, not proof that the provider got it" (`loom/web/src/components/ContextReceipt.tsx:3`).

---

## B. Dane (paczki, pola, presety i ich domyślne wartości)

### B.1 Paczki biorące udział w budowie kontekstu

| paczka / plik | co trzyma | domyślne (na dziś) | file:line |
|---|---|---|---|
| `loom/data/runtime/chat_context.pack` | kolejności, **profile selektora**, przepis `profile_prefix` | `context_order: "stability"`; 3 kolejności; 8 profili | `CC:7`, `CC:8-31`, `CC:33-338`, `CC:339` |
| `loom/data/runtime/action_rules.pack` (`scoped_values.keys`) | klucze „na turę": `context.profile`, `context.breadth_delta`, `context.depth_delta`; kroki jako dane | wszystkie `default_level: "turn"`; kroki: `wider` = mnożenie x2, `narrower` = dzielenie /2 (min. 1), `deeper` = +1, `shallower` = -1 (nie poniżej 0) | `AR:749-750`, `AR:811`, `AR:832`, `AR:866`; wykonanie `CE:890-902` |
| `loom/data/runtime/intent_routing.pack` | frazy zdań „szerzej/węziej/głębiej/płycej/profil selektora ...", „na stałe", „co wysłaliśmy" | frazy PL i EN | `intent_routing.pack:4804` (drugi wpis `:6310` nieczytany) |
| `loom/data/runtime/graph_memory.pack` | stałe selektora grafowego i `ContextSelector` | `default_max_nodes 20`, `default_depth 2`, `zero_as_default true`, tytuł 30, fragment 500, nagłówek „Relevant context from past conversations:"; sekcja `context`: `max_tokens 4000`, `memory_score 1.0`, `graph_score 0.8`, `search_limit 10`, `codepoints_per_token 4`, `source_order [memory,graph,search]` | `loom/data/runtime/graph_memory.pack` (defaults), `GM:306-372` |
| `loom/data/runtime/settings_fields.pack` | pola ustawień (z `levels`, `flows`, tekstami PL/EN) | `graph_memory_depth` (levels `person`,`turn`; flows `chat.send`), `graph_memory_max_nodes`, `context_execution`, `chat_context_order`, `profile_detail_level` (status `partial`), `context_receipt_store`, `context_receipt_max_bytes` | `SF:1273`, `SF:1304`, `SF:3034`, `SF:3616`, `SF:4325`, `SF:4393`, `SF:4453` |
| `loom/data/runtime/guardian.pack` | poziomy reprezentacji L0/L1/L2, presety `chat_representation`, filtr wątków, `context_io` (przepływy) | reprezentacja `raw`; filtr wątków wyłączony („Every thread of the conversation is sent (preset)") | `guardian.pack:728-766`, `:1121`, `:1174-1200` |
| `loom/data/graph/scopes.pack` | łańcuch zakresów (od ogółu do szczegółu): engine, app_preset, installation, organization, group_of_groups, team, person, role_in_context, project, conversation, turn; reguły konfliktów (most_specific_wins, locked_by, merge, intersection, min, max, weighted_vote, ask...) | „przykład kalibracyjny, nie lista zamknięta" (opis pliku) | `loom/data/graph/scopes.pack` (chain, conflict_rules), `loom/data/runtime/graph_views.pack:1845` (`unit_kinds`) |
| `loom/data/goals/goal_types.json` | typy celu dla `ContextEngine` (role, poziomy, rozdzielczość) | używane tylko ścieżką wiedzy | `loom/include/loom/context_engine.h:19-24` |
| konfiguracja `config.json` (bez paczki) | klucze czytane wprost w `build_messages`/`send`: `system_prompt`, `context_receipt_store` (`auto`), `context_receipt_max_bytes` (65536), `graph_memory_max_nodes` (20), `graph_memory_depth` (2), `context_execution` (`{}`: unified wyłączone, graph_reply `off`), `chat_history_command_traffic` (`skip_exchange`), `chat_context_order`, `selector_profile`, `context_breadth_delta`, `context_depth_delta` | jak w nawiasach | `CE:1186,1206,1232-1246,1285-1291,1642,1690` |

### B.2 Profile selektora w `CC:33-338` (ośmiu; presety i „kalibracja", nie reguły)

| id | order | źródła włączone | nodes | search_limit | hops | representation | flow | status |
|---|---|---|---|---|---|---|---|---|
| `loom` | stability | profile, rules, memory, project, history, graph | null (= ustawienie, 20) | 0 | null (= ustawienie, 2) | null | null | preset |
| `loom_legacy` | legacy | rules, memory, project, history, graph (bez profile) | null | 0 | null | null | null | preset |
| `compat` | legacy | tylko history | 0 | 0 | 0 | raw | plain | preset |
| `lean` | stability | wszystkie sześć | 10 | 0 | 1 | lean | null | preset |
| `graph_in` | stability | wszystkie sześć | null | 0 | 3 | null | graph_in | calibration |
| `like_claude` | stability | profile, memory, project, history | 0 | 0 | 0 | raw | plain | calibration |
| `like_chatgpt` | stability | profile, rules, memory, history, search | 0 | 10 | 0 | raw | plain | calibration |
| `like_gemini` | stability | history, search | 0 | 10 | 0 | raw | plain | calibration |

We wszystkich ośmiu `history_window` i `budget_tokens` mają wartość null (czyli: cała rozmowa, bez własnego limitu). Źródła `search` i `kb` oraz liczba `search_limit` **nie są stosowane przez jądro**: paragon wpisuje je do `unapplied` (`CE:1616-1619`); `search_limit` nie jest czytany w `build_messages` (grep po `CE`: jedyne trafienie `search` to `CE:1618`). Profile `like_*` są więc dziś częściowo deklaracją.

### B.3 Kolejność pierwszeństwa wartości (dziś)

- Profil: `opts.selector_profile` > wartość tury `context.profile` > reguły łańcucha zakresów na `context.profile` > ustawienie `selector_profile` > brak profilu (wtedy „ustawienia takie, jakie są", bajty jak dawniej) (`CE:817-858`).
- Kolejność części: `opts.context_order` > `profile.order` > ustawienie `chat_context_order` > domyślna paczki; nieznana nazwa = błąd (`CE:1196-1202`).
- Głębokość (`hops`): `opts.context_depth` (jedyny argument bezwzględny na drucie) > `profile.depth.hops` > ustawienie `graph_memory_depth` > 2; potem krok `context.depth_delta` (`CE:1228,1231,1245-1246`).
- Szerokość (`nodes`): `profile.breadth.nodes` > ustawienie `graph_memory_max_nodes` > 20; potem krok `context.breadth_delta`; ten sam krok skaluje `history_window`, ale tylko gdy profil ma liczbę (`CE:1229-1242`). **Brak argumentu bezwzględnego szerokości na drucie** (lista kluczy z A.2 etap 1).
- Zero (hops albo nodes) wyłącza klasę graph (`CE:1339-1340`).

---

## C. Czego żądają R67, R69, R70, R72, R74... a czego nie ma dziś

Każdy wiersz: „Dziś" to obserwacja z 2026-10-23 (ro-main 94872898); „Luka" to fakt o teraźniejszości (R60), nie ograniczenie i nie propozycja rozwiązania.

| Id | Co mówi właściciel (skrót; pełne słowa w `tools/spec.py show`) | Dziś (file:line) | Luka (fakt o teraźniejszości, R60) |
|---|---|---|---|
| R67 | (a) bez atrap: tylko pełna funkcjonalność; (b) kontekst wywołania wnioskowany z szerokiego, wielowymiarowego kontekstu (historia, teleologia, perspektywa); (c) głębokość i szerokość to argumenty opcjonalne, domyślnie wnioskowane, przekazywalne jako bezwzględne lub modyfikujące | (a) blok „Context for next message" z czterema checkboxami to Warsztat (`loom/web/src/components/ChatView.tsx:1399-1412`, tylko `!chat`); audyt: `docs/reports/AUDYT_R67_ATRAPY_2026-10-23.md:7-40`, ustawienia „planned": `SF:804,3881,4092,4222,4287` (numery za audytem). (b) kontekst powstaje z ustawień + profilu + kroków; jedyne „wnioskowanie" to ziarna grafu z tekstu pytania (`GM:114`) i opcjonalne typowanie celu tylko w warstwie wiedzy (`loom/include/loom/context_engine.h:115-122`). (c) bezwzględna jest tylko głębokość: `context_depth` (`CEH:94`, `CE:685-688`); szerokość (`nodes`) tylko przez profil, ustawienie albo krok; modyfikatory istnieją (`AR:832,866`), ale poza drutem `/api/chat` (idą TurnCarrierem, `OPS:2357`) | brak wnioskowania wartości domyślnych głębokości/szerokości z kontekstu (domyślne to stałe 2 i 20: `graph_memory.pack`); brak argumentu bezwzględnego szerokości na drucie (`CE:690` odrzuca nieznany klucz); ręczny wybór (flagi `include_*`, `CEH:102-104`) jest dziś jedynym mechanizmem „co dołączyć", nie jedną z konfiguracji |
| R69 | głębokość i szerokość jako argumenty FUNKCJI; funkcja „kontekst selektor" bierze węzeł grafu odpowiadający (wypowiedź urwana, ciąg w R70) | `ChatEngine::build_messages(conv_id, current_text, attachments, opts, exclude_message_id, trace)` (`CE:1164-1166`) jest składową silnika czatu; selektor grafowy bierze **tekst** (`GraphSelectOptions{analysis,max_nodes,depth,current_conv_id}`, `loom/include/loom/graph_memory.h:37-42`; `GM:76-100`); `ContextEngine::build(ContextRequest)` bierze tekst + `targets` (id encji) (`context_engine.h:53-93,139`) | nie ma funkcji, której argumentem jest węzeł grafu; „bieżący węzeł" nie istnieje jako obiekt (pytanie jest tekstem, wiadomość użytkownika zapisana `CE:1859`); trzy selektory (czat, `ContextSelector` `graph_memory.h:131`, `ContextEngine`) mają różne sygnatury |
| R70 | selektor bierze węzeł generowanej odpowiedzi (pozycja w wymiarze wiadomości); widzi, co się schodzi we WSZYSTKICH perspektywach; zna stan pamięci podręcznej; modyfikacje dopisuje na końcu; buduje od ogółu do szczegółu (projekt > gałąź > moduł > funkcja); woła go użytkownik, agent, eksperymentator; po niewystarczającej odpowiedzi ponawia szerzej/głębiej; przy zmianie tematu zawęża, gdy nie będzie powrotu w czasie życia cache | węzła odpowiedzi nie ma przed wysyłką (wiadomość asystenta powstaje po odpowiedzi, `CE:2266`); perspektywy (`loom/include/loom/perspectives.h:1-12`: relacje, kierunek, `hops`, filtr węzłów) służą widokowi grafu, `chat_engine.cpp` ich nie czyta (grep `perspective` w `CE`: brak); cache: tylko kolejność klas (B.1, A.3) i deterministyczny blok profilu (`profile_prefix.h:1-7`), brak `cache_control`, brak pomiaru (patrz E.1); „modyfikacje na końcu": ogon `CE:1567-1580` (zadanie, instrukcja, pytanie) i klasa graph na końcu w `stability` (`CC:23-31`); od ogółu do szczegółu: pasma stable -> project -> goal tylko w `ContextEngine` (`context_engine.h:22-25`, `loom/include/loom/model.h:108`); wołający: użytkownik = `send`; agent = `loom/src/agent/agent.cpp:156-168` składa płaską historię rozmowy (aktywne user/assistant/system) bez selektora; eksperymentator: nie znalazłem wejścia (jest tylko obszar grafu `experiments` związany z `calibration.items`: `loom/data/runtime/graph_entry.pack:978-1000`); ponowienie szerzej/głębiej: brak (grep `insufficient|retry_wider|topic_change` bez trafień poza niepowiązanymi: `guardian.cpp:343,447`, `onboarding/profile.cpp:436`); zawężenie przy zmianie tematu: najbliższy jest filtr wątków guardiana, wyłączony w presecie (`representation.cpp:419-448`, `guardian.pack:1174-1200`), nie zna cache | brak: węzła odpowiedzi, wejścia „węzeł -> kontekst", czytania perspektyw przez czat, stanu cache w decyzji, hierarchii projekt>gałąź>moduł>funkcja w czacie, wejścia dla agenta (selektor) i eksperymentatora, ponawiania szerzej/głębiej, zawężania przy zmianie tematu świadomego cache |
| R72 | (korekta) 0 = generowana odpowiedź, -1 = prompt użytkownika to nie etykiety/preset, lecz współrzędne względne; zakres wnioskowany z kilku ostatnich wiadomości i celu | zakres historii to całość albo `window_eff` z profilu (we wszystkich ośmiu profilach null) i `budget_tokens` (null): `CE:1501-1530`; typowanie celu istnieje tylko w warstwie wiedzy (`context_engine.h:115-122`, `loom/data/goals/goal_types.json`) | brak wnioskowania zakresu z ostatnich wiadomości i celu w czacie; brak jakiejkolwiek notacji pozycji względnej (patrz R74). Część o teleporcie nie dotyczy K3 |
| R74 | współrzędne względne w perspektywie kolejnych wiadomości: 0 = bieżąca, -1 = poprzednia; ten sam mechanizm w każdym wymiarze (wersje, gałęzie, perspektywy) i w złożeniu; składnia to tylko przykład | adresy grafu to nieprzezroczyste napisy z prefiksami (`area:`, `party:self`, `subgraph:self/<id>`, `memory:`, `preference:`, `policy:`...: `loom/src/graph/projection.cpp:715-716,857,1352,1379,1515-1524`), mapowane z id bazy przez `db_forms` (`projection.cpp:2470-2480`); wiadomości mają `parent_id`, `version_group_id`, `version_num` (`loom/include/loom/db.h:73,78-79`), `get_versions` (`db.h:374`), nowa wersja przestawia starą na `status='version'` (`loom/src/db/db.cpp:627-634`); „poprzednia wiadomość użytkownika" liczona jednorazowo ad hoc w `stored_receipt` (`CF:665-676`) | nie znalazłem adresowania względnego w jądrze (grep po `projection.cpp`, `port_graph.h`, `perspectives.h` za previous/last/next/relative: jedyne trafienie to typ relacji `relative_to`, `projection.cpp:2344`, niepowiązany); historia jest listą płaską `created,rowid` (`db.cpp:778`), gałęzie (`parent_id`) nie są śledzone przy budowie kontekstu (`CE:1450-1462`) |
| R76 | adres względny po hierarchii (z poprzedniej dużej wersji ostatnia mała/poprawka); współrzędne nie tylko liczby całkowite | wersje wiadomości: płaskie (grupa, numer) (`db.h:78-79`, `db.cpp:632`); osie wpisu do grafu `about/kind/time` to etykiety (`graph_entry.pack:988-992`, tylko ten fragment czytałem) | brak wymiaru hierarchicznego (duża.mała.poprawka) i brak współrzędnych innych niż liczby/identyfikatory |
| R77 | współrzędne zmiennoprzecinkowe tam, gdzie można interpolować/ekstrapolować | jedyny ciągły wymiar używany przy doborze to `weight` wiadomości (typ rzeczywisty), tylko jako prefiks tekstu `[IMPORTANT]`/`[low priority]` (`CE:1442-1500`); czas to tekst ISO (`created`) | brak interpolacji i adresu „między węzłami"; waga nie wpływa na wybór, tylko na tekst |
| R79 | zoom = pole widzenia (zakres), ruch = osobno; krok ruchu zależy od pola widzenia | pole widzenia ~ `hops`, `nodes`, `window`, `budget_tokens`; kroki to stałe dane (x2, /2, +1, -1: `AR:832-900`, `CE:890-902`); krok mnożący skaluje się z wartością, krok `deeper/shallower` nie; skok >= x10 potwierdza użytkownik (`OPS:2432-2506`, `CE:91-129`) | pozycja (0 = bieżąca) i pole widzenia nie są rozdzielone w kodzie; krok ruchu po osi nie zależy od pola widzenia; „zoom" i „ruch" nie mają osobnych pojęć |
| R81 | granice ustawiane przez użytkownika (inwencja przy niepewnych danych, moc obliczeniowa; lista otwarta) | granice liczbowe: `budget_tokens` profilu (null, `CE:1507-1530`), polityka zużycia i limity wydatków (`CE:91-129`, `loom/src/policy/spend_limits.cpp:17`, `loom/data/runtime/usage_policy.pack`), `unified` opcje „caller policy, not product ceilings" (`loom/src/context/unified_context.h:15-35`) | brak granicy „ile inwencji/ekstrapolacji wolno"; granice są skalarami ustawień, nie mapą per zadanie/węzeł |
| R82 | proste silnik robi sam, o trudniejsze pyta; użytkownik może zażądać „pytaj o wszystko" albo „o nic" | jedyne pytanie w obszarze kontekstu: skok x10 (`asked`, `requires_confirmation`: `CE:91-129`, `OPS:2432-2490`); odmowy zapamiętywane gdzie indziej: pola profilu `declined` (`loom/src/onboarding/profile.cpp:436`), ważenie reguł guardiana (`guardian.cpp:343,447`, `reask_factor`) | brak ustawienia „poziom pytania" dla decyzji kontekstu; brak kryteriów prosty/trudny jako dane dla wyboru kontekstu; brak ogólnej pamięci „nie pytaj następnym razem" dla ostrzeżeń (patrz D.9) |
| R84 | listy opcji tylko gdy konieczne; domyślnie wartość otwarta z presetami; zamknięta lista z powodem i datą | klasy kontekstu to zamknięta lista w kodzie: system/profile/rules/memory/project/history/graph (`CE:1558-1565`, „a class this kernel does not know sends nothing"); nazwy źródeł profilu (`profile, rules, memory, project, history, graph, search, kb`: `CC:33-338`); tryby tekstowe (`context_receipt_store`, `chat_history_command_traffic`); kolejności są danymi i użytkownik może dodać własną (`CC:5` opis), nieznana nazwa = błąd (`CE:1202`) | zamknięte listy klas i kluczy kroków (`context.breadth_delta` -> nodes+window, `context.depth_delta` -> hops są zaszyte w kodzie: `CE:1232-1246`) bez zapisanego powodu i daty |
| R85 | „pokrętło" to wielowymiarowa dana, mapowana z coraz większą rozdzielczością z interakcji | dziś pokrętło = skalar: `nodes`, `hops`, `window`, `budget_tokens` w profilu/ustawieniu; interakcja zostawia ślad tylko jako (a) krok na jedną turę (`TurnCarrier`), (b) reguła na zakresie (`rules.set`, `route.cpp:779-846`), (c) wpis `modifiers` w paragonie tej tury (`CE:1634-1637`) | nic nie kumuluje punktów z interakcji w mapę o rosnącej rozdzielczości; presety (osiem profili) są płaską tabelą; rozdzielczość odczytu nie istnieje |
| R86 (po briefie) | preferencja/ustawienie = węzeł grafu z bogatymi krawędziami (waga, pochodzenie, data, kontekst, kto ustawił) | preferencje są **projekcją** do grafu (`projection.cpp:1515-1524`, krawędź `same_as` do slotu), wartość żyje w `profiles/user.pack` (warstwy onboardingu) i w `config.json`; łańcuch zakresów zwraca pochodzenie i warstwę (`loom/include/loom/scope_resolver.h:34-60`, `Resolution::to_json`) | ustawienia kontekstu nie są wierszami `nodes`/`links` z krawędziami ważonymi; ich pochodzenie to warstwa łańcucha, nie krawędź z atrybutami |
| R87 (po briefie) | kontrolka (pokrętło) definiowalna i renderowalna jako dane, przypisana do węzła grafu, węzeł do pola konfiguracji; trzy luźno sprzężone części | pole ustawień ma `scope`, `type`, `levels`, `flows`, `group/subgroup`, etykiety PL/EN, `has_default` (`SF:1272-1301`), ale w tym wpisie nie ma klucza typu kontrolki (widżet); web ma kontrolki napisane w TSX (`ComposerMenu.tsx`, `use-context-modify.ts`) | brak definicji kontrolki jako danych i wiązania kontrolka -> węzeł -> pole; (nie czytałem reszty `SF`, więc nie twierdzę, że żadne pole nie ma opisu widżetu) |

Uwaga do R67 (a): w `AUDYT_R67_ATRAPY_2026-10-23.md:36-40` audyt już wskazał blok kontekstu Warsztatu (`ChatView.tsx:1399-1450`) i wprost zostawił wnioskowanie głębokości/szerokości dla K3; tu tylko cytuję.

---

## D. Elementy do rozbudowy zamiast budowy od zera

1. **Profile selektora jako dane + trzy źródła wartości**: `SelectorChoice` (`CE:801-863`), pack `CC:33-338`; pierwszeństwo request > tura > łańcuch zakresów > ustawienie jest już zaimplementowane i opisane w paragonie. Pasuje jako „gruba mapa startowa" dla R85.
2. **Wartości „na turę" i łańcuch zakresów**: `scopes::Resolver` z poziomem `turn` ponad `conversation` (`scope_resolver.h:34-60`), `TurnCarrier` (peek/take/give_back: `turn_carrier.cpp`), `context.modify` (`OPS:2357-2510`) z potwierdzeniem x10. Reguły konfliktów jako dane (`scopes.pack`: m.in. `weighted_vote`, `ask`, `min`, `max`).
3. **Kroki jako dane**: `apply_steps` (`CE:890-902`) i `scoped_values.keys.*.steps` (`AR:832-900`); dodanie klucza kroku nie wymaga kodu poza mapowaniem klucz -> liczba (`CE:1232-1246`, zaszyte).
4. **Paragon**: `loom.context_receipt/1` z sekcjami, powodami odrzuceń, `modifiers`, `guard`, hashem, zwijaniem rozmiaru (`CE:1581-1656`, `CF:628-656`); UI „Co wysłaliśmy" (`ContextReceipt.tsx`, `context-receipt-state.ts`). To gotowy nośnik dla „co wywnioskowano i dlaczego".
5. **Rozdzielenie zasięgu i szczegółu**: `context::ContextRequest` ma osobno `relation_hops` (zasięg), `budget_tokens`, `detail_resolution` (szczegół), `candidate_channels` (`context_engine.h:53-93`); `compile_unified_context` mówi wprost „Reach and source detail remain independent" i opcje „caller policy, not product ceilings" (`loom/src/context/unified_context.h:23,33`). To najbliższe istniejące „wielowymiarowe pole widzenia".
6. **Typowanie celu**: `ContextEngine::type_goal` (cue classifier, opcjonalnie jeden model z osobnym budżetem: `context_engine.h:115-122`), `loom/data/goals/goal_types.json`, `loom/data/runtime/context_goal_cues.pack` (nieczytany).
7. **Pasma stable -> project -> goal** (`context_engine.h:22-25`, `model.h:108`) — wprost „od ogółu do szczegółu, prefiks stały".
8. **Funkcja sąsiedztwa po id ziaren**: `GraphMemorySelector::expand(seed_ids, depth, max_nodes)` (`GM:200-250`) — BFS po węzłach (nie po tekście), zwraca węzły z głębokością; może być rdzeniem „bierze węzeł".
9. **Ostrzeżenia nieblokujące (R58)**: kształt `reason(code, level, params)` z tekstem PL/EN (`loom/src/views/view_engine.cpp:156-158`) i oceną `promoted/allowed` z listą powodów `channel_unranked`, `channel_low_rank`, `data_type_unranked` (`view_engine.cpp:724-824`); mechanizm „zapytaj/odmów/zapamiętaj" jest rozproszony: potwierdzenie x10 (`OPS:2432-2490`), `declined` w profilu (`onboarding/profile.cpp:436`), ważenie reguł (`guardian.cpp:343,447`). **Nie znalazłem** ogólnej usługi „nie pytaj następnym razem" z konfigurowalną częstością (R58 w CLAUDE.md); nie twierdzę, że jej nie ma poza czytanymi plikami.
10. **Poziomy reprezentacji i filtr wątków**: `guardian::select` (`guardian.h:62`), L0/L1/L2 (`guardian.pack:728-766`), filtr wątków z adnotacji analizatora (`representation.cpp:419-448`) — zawężanie tematyczne już istnieje jako opcja (wyłączona).
11. **Deterministyczny blok profilu**: `profile_prefix.cpp` (czysta funkcja przepisu, podgrafów i poziomu: bez znaczników czasu, stały porządek) — wzorzec dla „stabilnego prefiksu".
12. **Adresy i perspektywy grafu**: `Catalogue::canonical/db_id` z `db_forms` (`projection.cpp:2470-2492`), perspektywy z `hops`/kierunkiem (`perspectives.h`), nowe rodzaje węzłów i krawędzi „przez użycie" (CLAUDE.md; tabele `nodes`, `links` z dowolnym `kind`/`link_type`).
13. **Waga wiadomości** (`messages.weight`) jako istniejąca współrzędna ciągła (`db.h`, `CE:1442-1500`).
14. **Pomiar zużycia po stronie narzędzia Python**: `loom/tools/extract/economics.py:48-49,199-205` liczy `cache_creation`, `cache_read`, `cache_hit` z archiwów (nie z żywego czatu); dane `loom/tools/extract/data/economics/report.json`.

---

## E. Ryzyka dla projektu

### E.1 Cache dostawcy i unieważnianie prefiksu (obserwacje)

- **Jądro nie używa żadnego mechanizmu cache dostawcy.** grep po `loom/src`, `loom/include`, `loom/data`, `loom/web/src` za `cache_control|prompt_cache|cached_tokens|cache_creation|cache_read|cacheable|cache_ttl` nie daje trafień w kodzie jądra; trafienia to teksty zasad i etykiety („stable prefix") w `loom/data/philosophy/principles.json:1404`, `loom/data/goals/goal_types.json:3`, `context_engine.h:22-25` i UI `context-receipt-state.ts:179`. Jedyną „strategią" jest kolejność klas i determinizm bloku profilu.
- **„Stable prefix: tak/nie" w UI to heurystyka statyczna**, nie pomiar: `prefixStable` sprawdza tylko, czy klasa `profile` stoi przed history/graph/memory/project i czy blok profilu ma poziom (`loom/web/src/components/context-receipt-state.ts:179-185`); nie porównuje z poprzednim żądaniem i nie czyta trafień cache.
- **Odczyt zużycia**: surowe `usage` dostawcy wraca w `ChatResult.usage` (`CE:2298`), ale **nie jest zapisywane** w metadanych wiadomości asystenta (klucze metadanych: `reply_format`, `reasoning_profile`, `graph_reply`, `reasoning`, `topics`, `cancelled`, `usage_notices`: `CE:2245-2258`), a pola cache (`cached_tokens`, `cache_read_input_tokens`, `cache_creation...`) nie występują w kodzie jądra (grep po `loom/src` i `loom/include`: zero trafień); `prompt_tokens`/`input_tokens` są czytane w innych miejscach (np. `loom/src/actions/route.cpp:991` dla kosztu routera, `loom/src/extract/semantic.cpp:974`, `loom/src/context/provider_vector.cpp:402`), nie w drodze czatu. Szacunek tokenów to punkty kodowe/4 (`CF:578-595`, `GM:301-304`), strażnik porównuje **bajty** ciała z bazą rodzaju (`CE:68-129`).
- **Gdzie dziś łamie się prefiks** (rozumowanie z kodu, bez pomiaru): (1) `project` stoi przed `history` w `stability`, a przy włączonej wiedzy jest budowany z tekstu bieżącego pytania (`CE:1391`) — zmienia się co turę i unieważnia wszystko po nim; w trybie `unified` także pamięć i graf trafiają do `project` (`CE:1423-1437`, `CE:1310,1342`); (2) okno „ostatnie N" i budżet usuwają **najstarsze** wiadomości (`CE:1501-1506`, `CE:1523-1527`), więc każda eksmisja przesuwa początek historii; (3) prefiksy `[IMPORTANT]`/`[low priority]` zmieniają tekst wiadomości, gdy zmieni się waga; (4) podstawienia L1/L2 zmieniają tekst, gdy analizator zmieni adnotacje; (5) w kolejności `legacy` graf jest w środku (przed `project`/`history`), więc zmiana szerokości/głębokości zmienia prefiks (`CC:9-15`); w `stability` graf jest ostatni, więc „szerzej" nie rusza prefiksu historii (ale rusza ogon przed pytaniem).
- Adapter `openai_chat` wysyła ciało bajt w bajt (`adapters.cpp:154-158`); żaden adapter nie dopisuje znaczników cache. Adapter `anthropic_messages` ma gałąź, która scala role `system` w pole `system` (`adapters.cpp:309-318`), ale gałąź z gotowym ciałem (`:296-303`), której używa czat, jej nie stosuje (nie sprawdzałem, czy czat kiedykolwiek kieruje na ten adapter).

### E.2 Złoty plik i bajty
- Bez profilu selektora żądanie ma być dokładnie takie jak dawniej (`CE:817-819` komentarz „no profile: the settings as they are (today's request bytes)"); trzy warianty zamrożone w `loom/tests/fixtures/golden/chat_request_legacy.json`, testy `test_chat_engine.cpp:134-190,461-501`, `test_context_receipt.cpp:143`, parytet presetów `loom/src/chat/tests/chat_preset_parity.cpp` (+ `run_chat_preset_parity.py`, nieczytane). Każdy domyślny tryb „wnioskuj" musi być wyłączony dla ścieżki bez profilu i dla `legacy/loom_legacy/compat`, albo trzeba zmienić złoty plik świadomie.

### E.3 Niezmiennik zgodności z Pythonem
- CLAUDE.md: zgodność Pythona nie jest już wymagana, ale testy `loom/tests/compat/` (schemat DDL `schema_version "4"`, formatowanie JSON/ID/czasu) działają jako wartownik. Nowe kolumny, zmiana kształtu `messages.metadata` ponad dzisiejsze (`context_trace`, `reasoning_profile` zapisywane przez `CE:1901`) albo nowe tabele wymagają sprawdzenia w tych testach; nie czytałem ich.

### E.4 ABI i drut
- `ChatOptions::from_json` odrzuca nieznane klucze (`CE:690`), więc nowy argument (np. szerokość bezwzględna, adres względny) to zmiana kontraktu `loom_chat_ex` opisanego w `loom.h:196-204`; `libloom.so` nie może eksportować więcej ani mniej niż deklaruje `loom.h` (`loom/tests/compat/test_abi_compat.py`, CLAUDE.md), więc nowa funkcja C = zmiana nagłówka i testu; nowy klucz JSON w istniejącej funkcji jest tańszy.
- Strażnik x10: automatyczne poszerzanie kontekstu przez silnik podlega regule właściciela (potwierdza użytkownik przy spodziewanym wzroście o rząd wielkości): `request_guard` porównuje rozmiar ciała z bazą kohorty (`CE:91-129`); wnioskowane szerzej musi przejść tę samą bramkę (`OPS:2432-2506`).

### E.5 Współbieżność i spójność
- `build_messages` wołają równolegle `send`, `preview_usage`, `context_guard`; w ścieżce zadania aktywnego dane źródłowe są weryfikowane przed/po (`CE:1788-1830`, `native_request_inputs` `CE:433`) i budowa trzyma blokadę bazy (`CE:1169`). Każdy nowy odczyt (węzły, perspektywy) musi wejść do tej weryfikacji albo być z niej wyłączony.
- Selektor grafu wyklucza bieżącą rozmowę (`GM:146-148`), a historia bierze tylko bieżącą; selektor oparty na węźle musi tego nie zdublować.
- Paragon: rozmiar (`context_receipt_max_bytes` 65536, zwijanie `CE:1642-1651`) może nie pomieścić opisu wielowymiarowego wyboru; paragon deklaruje „skompilowane żądanie, nie dowód dostarczenia".

---

## F. Pytania otwarte dla etapu projektu (techniczne, nie dla właściciela)

0. Stan rejestru na koniec pracy: ostatnie pozycje to R86, R87; brak R88+ (sprawdzenie `tools/spec.py list --theme owner_requirements`).
1. **Gdzie mieszka selektor?** Nowa funkcja jądra „węzeł + (opcjonalne) pole widzenia -> zbiór kontekstu + ślad", z której `build_messages` jest klientem, czy rozszerzenie `ContextEngine`/`compile_unified_context`, które już mają `ContextSet`, ślad i rozdzielone zasięg/szczegół? Trzy dzisiejsze selektory (czat, `ContextSelector`, `ContextEngine`) mają różne wejścia.
2. **Reprezentacja „pokrętła jako mapy" (R85/R86/R87)**: węzły i krawędzie w tabelach `nodes/links` (rodzaje „przez użycie") z atrybutami (waga, pochodzenie, data, kontekst, rozdzielczość), czy dane profilu? Jak się to ma do ośmiu wierszy `selector_profiles` (czy profil = gruba mapa startowa z pochodzeniem „preset")? Jak czytać mapę w „potrzebnej rozdzielczości" i co robić, gdzie jest rzadka (wyprowadzić w granicach swobody vs zapytać, R82)?
3. **Gramatyka adresu względnego (R74/R76/R77)**: rejestr wymiarów jako dane bez enuma (R84); typ współrzędnej jako cecha wymiaru (liczba całkowita, double, nazwa, data, zakres, predykat); adresy hierarchiczne (duża.mała.poprawka) i złożenia między wymiarami; znaczenie punktu „między węzłami". Czym jest „0" przed istnieniem odpowiedzi (węzeł-szkic odpowiedzi tworzony razem z wiadomością użytkownika?) — dziś asystent powstaje po odpowiedzi (`CE:2266`), a pytanie jest zapisane przed zbudowaniem kontekstu (`CE:1859`, `exclude_message_id`).
4. **Wnioskowanie zakresu z ostatnich wiadomości i celu (R72)**: deterministycznie (wskazówki jak `ContextEngine::type_goal`) czy z modelem (koszt, R82 poziom pytania)? Jak zapisywać wynik wnioskowania (pochodzenie, dowody), żeby korekta użytkownika była nową krawędzią (R85), a paragon mówił, co wywnioskowano?
5. **Świadomość cache (R70)**: potrzebne są skróty per klasa i wspólny prefiks z poprzednim żądaniem tej rozmowy (dziś tylko `messages_sha256` całości: `CE:1584`, `CF:636`); czy zapisywać je w śladzie? Czas życia cache per dostawca jako **datowana obserwacja** (R60), nie stała. Czy dodać opt-in znaczniki `cache_control` w metadanych trasy (bajty bez zmian, gdy wyłączone), i czy to w ogóle ma sens dla adapterów, którymi kieruje czat?
6. **Pomiar**: zapis surowego `usage` (z polami cache) w metadanych wiadomości asystenta albo w księdze zużycia; połączenie z narzędziem ekonomii (`tools/extract/economics.py`) czy osobny odczyt w jądrze.
7. **Ponawianie szerzej/głębiej i zawężanie przy zmianie tematu**: kto ocenia „niewystarczającą odpowiedź" (użytkownik zdaniem „szerzej" już działa na NASTĘPNĄ wiadomość; agent w pętli; eksperymentator)? Czy ponowienie to nowa wersja odpowiedzi (`version_group_id`) tego samego węzła? Sygnał zmiany tematu: adnotacja wątku analizatora (`representation.cpp:419-448`) czy coś nowego; „brak powrotu w czasie życia cache" wymaga predyktora (dane/uczenie, nie stała).
8. **Wejścia agenta i eksperymentatora**: agent składa płaską historię (`agent.cpp:156-168`) — czy przechodzi przez ten sam selektor? Dla eksperymentatora nie znalazłem żadnego wejścia w jądrze; `loom/tools/eval/` (Python) nie czytałem. Czy to wywołanie `ChatOptions`, czy akcja/port?
9. **Flagi `include_*` i `sources` profilu**: zachować jako jedną z konfiguracji (R67), z jakim pierwszeństwem względem wnioskowania (bezwzględne wygrywa?)
10. **Uogólnienie dwóch pokręteł (R83/R85)**: dziś klucze `context.breadth_delta` i `context.depth_delta` są zaszyte w kodzie jako (nodes+window) i (hops) (`CE:1232-1246`). Czy mapowanie klucz -> wielkość ma być danymi, tak by dowolna nazwana oś pola widzenia (hops, nodes, window, budget, detail_resolution, relation_hops...) dostawała kroki i wartości bez zmian kodu?
11. **Ostrzeżenia o mało użytecznych konfiguracjach (R58)**: czy wystarczy kształt `reason(code, level, params)` z `view_engine.cpp:156-158`, i gdzie przechowywać „nie pytaj następnym razem" (klucz konfiguracji vs krawędź w grafie wg R86); częstość pytania jako preset do „nigdy".
12. **Złoty plik i profile `like_*`**: ich pola `search`, `search_limit`, `kb` nie są stosowane (`CE:1616-1619`); wnioskujący selektor ma je zrealizować, czy nadal zgłaszać jako `unapplied`?
13. **Dane o stanie dzisiejszym nieczytane w tym badaniu**: `loom/src/chat/selector.verify.cc` i `verify_selector.py`, `loom/src/context/method_registry.cpp`, `provider_vector.cpp`, `context_goal_cues.pack`, `loom/tools/eval/*`, reszta `settings_fields.pack`, `loom/tests/compat/*`, ekstrakcja ziaren `GM:33-68`, drugi wpis `context_modifiers` w `intent_routing.pack:6310`.
