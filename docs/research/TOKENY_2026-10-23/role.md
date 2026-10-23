# R89 — ścieżka „role”: role modeli, żonglerka modelami i telemetria kosztów w frameworkach agentowych

- **Ścieżka:** role (jak frameworki i produkty agentowe przydzielają ROLE modelom o różnej cenie: router/triage,
  sekretarz/skryba, sędzia/weryfikator, planista vs wykonawca; tani-najpierw z eskalacją; LLM-jako-sędzia;
  jak w praktyce robi się telemetrię kosztów i co się mierzy)
- **Data:** 2026-10-23
- **Autor:** agent badawczy (Fable 5.1) w worktree `/home/user/chatadhd`
- **Zasady:** R58/R83/R84 (przykłady są przykładami, liczba na liście nie jest listą), R59 (każde ustalenie ma
  źródło, datę, typ i ocenę wiarygodności), R60 (fakt o stanie obecnym = datowana obserwacja, nie ograniczenie).
  Wszystkie liczby poniżej pochodzą ze stron, które przeczytałem; gdzie opieram się tylko na streszczeniu
  wyszukiwarki (strona zablokowana), piszę to wprost i obniżam wiarygodność.

## Jak szukałem

- 12 wyszukiwań (WebSearch, 11 w trybie standard, 1 extended) i 17 udanych odczytów źródeł (WebFetch, GitHub
  code search przez MCP, mirror arXiv na Hugging Face `hf://papers/<id>/paper.md`). Preset z briefu to 8–12
  wyszukiwań i 6–10 odczytów; odczytów jest więcej, bo **22 próby WebFetch skończyły się blokadą DNS proxy sesji**
  (`ENOTFOUND`): `openrouter.ai`, `www.openrouter.ai`, `openrouter.helicone.ai`, `api.ifq.ai`, `mintlify.com`,
  `langfuse.com`, `lmsys.org`, `openai.github.io`, `docs.crewai.com`, `docs.ag2.ai`, `arxiv.org`,
  `docs.helicone.ai`, `docs.langchain.com`, `getunblocked.com`, `orbi.build`, `cloudzero.com`, `warp.dev`,
  `simonwillison.net`, `feeds.simonwillison.net`, `towardsdatascience.com`, `campusiq.com`, `hiddedesmet.com`.
- Obejścia: (a) `raw.githubusercontent.com` dla dokumentacji trzymanej w repozytoriach (OpenAI Agents SDK,
  Swarm, RouteLLM, AutoGen 0.2, Langfuse, LangSmith, CrewAI, Helicone); (b) GitHub code search (MCP) do
  znalezienia właściwych ścieżek plików i do odczytu **schematu `usage` OpenRouter z oficjalnego SDK**, bo
  dokumentacja była niedostępna; (c) mirror arXiv na Hugging Face dla czterech prac (MetaGPT, LLM-as-a-judge,
  FrugalGPT, RouteLLM). Działały bez obejść: `anthropic.com`, `code.claude.com`, `platform.claude.com`.
- Kolejność wg R59: dokumentacja oficjalna i kod oficjalnych SDK → prace naukowe (arXiv przez mirror) → wpisy
  inżynieryjne producentów → streszczenia wyszukiwarki dla stron zablokowanych (oznaczone „low”, nieprzeczytane).
- Zatrzymałem się, gdy trzy ostatnie źródła (Helicone custom properties, README Swarm, LangSmith cost tracking)
  tylko potwierdzały wzorce, które już miałem (atrybucja kosztu przez etykiety, przekazanie = narzędzie,
  cennik jako mapa model→stawki za typ tokena).
- Skala: **kaliber** = small / medium / large / scientific / experimental (rozmiar rozwiązania u źródła);
  **wysiłek** = small / medium / large (dla nas). „Dotyczy” = dla kogo u nas (sesja koordynatora, agenci
  workflow, narzędzia spec, jądro aplikacji, interfejs współpracy, właściciel na telefonie).

---

## role-01 — Anthropic „Building effective agents”: katalog wzorców — routing tanie/drogie, orkiestrator–robotnicy, ewaluator–optymalizator

- **Źródło:** https://www.anthropic.com/engineering/building-effective-agents — wpis inżynieryjny producenta,
  dostęp 2026-10-23 (strona aktualizowana: przykłady nazywają Claude Haiku 4.5 i Sonnet 4.5). Wiarygodność
  **high** jako opis wzorców (to definicje, nie pomiary); typ: vendor_blog.
- **Twierdzenie:** pięć wzorców „workflow” (ścieżki ustalone w kodzie) i osobno „agenci” (model sam steruje
  procesem). *Routing*: „classifies an input and directs it to a specialized followup task” — przykład: łatwe,
  częste pytania do „smaller, cost-efficient models like Claude Haiku 4.5”, trudne do Sonnet 4.5. *Prompt
  chaining* z programowymi bramkami między krokami. *Parallelization*: sectioning (niezależne podzadania) i voting
  (to samo zadanie wiele razy). *Orchestrator-workers*: „a central LLM dynamically breaks down tasks, delegates them
  to worker LLMs, and synthesizes their results” — podzadania wynikają z wejścia, nie są z góry znane.
  *Evaluator-optimizer*: „one LLM call generates a response while another provides evaluation and feedback in a
  loop” — opłaca się, gdy kryteria oceny są jasne. Rada ogólna: „finding the simplest solution possible, and only
  increasing complexity when needed”; „Agentic systems often trade latency and cost for better task performance”;
  wiele zastosowań potrzebuje jednego wywołania z retrievalem i przykładami.
- **Mechanizm:** rola = osobne wywołanie z własnym promptem i własnym modelem; router (tani model lub klasyfikator)
  decyduje, które wejścia w ogóle trafiają do drogiego modelu; ewaluator jest osobnym wywołaniem, więc może być
  tańszy lub droższy niż generator — to ustawienie, nie reguła.
- **Dotyczy:** jądro aplikacji (przepływy jako dane), agenci workflow, sesja koordynatora.
- **Kaliber:** medium. **Wysiłek:** small — router klasyfikujący już mamy (P31/P34); brak u nas jawnej pętli
  ewaluator–optymalizator jako przepływu z osobnym modelem sędziego.
- **Uwagi:** wpis nie podaje żadnych pomiarów oszczędności; podział na pięć wzorców to przykłady producenta, nie
  zamknięta lista (R58). Nazwy modeli w przykładzie to stan strony z 2026-10-23 (R60).

## role-02 — Anthropic „multi-agent research system”: lider Opus 4 + podagenci Sonnet 4; ×4 i ×15 tokenów; tokeny tłumaczą 80 % wariancji

- **Źródło:** https://www.anthropic.com/engineering/multi-agent-research-system — wpis inżynieryjny producenta
  o własnym systemie (Research w Claude), dostęp 2026-10-23 (daty publikacji nie odczytałem ze strony; wpis z
  2025 r.). Wiarygodność **high** dla tego, co mówią o własnym systemie; pomiary na wewnętrznej ewaluacji, bez
  niezależnej replikacji; typ: vendor_blog.
- **Twierdzenie:** role: lider = Claude Opus 4 (planuje, dzieli, syntetyzuje), podagenci = Claude Sonnet 4
  (równolegle, każdy z własnym oknem kontekstu), osobny „CitationAgent” dopasowuje cytowania (model nienazwany).
  Układ wieloagentowy z Opus 4 jako liderem i Sonnet 4 jako podagentami pobił pojedynczego Opus 4 o **90,2 %**
  na wewnętrznej ewaluacji badawczej. Koszt: agenci zużywają ok. **4×** tokenów czatu, systemy wieloagentowe ok.
  **15×**. Na BrowseComp **samo zużycie tokenów tłumaczy 80 % wariancji** wyniku; z liczbą wywołań narzędzi
  i wyborem modelu — **95 %**. Reguły skalowania wysiłku wpisane w prompt lidera (przykłady): proste ustalenie
  faktu — 1 agent, 3–10 wywołań narzędzi; porównania — 2–4 podagentów po 10–15 wywołań; złożone badanie —
  >10 podagentów z jasno podzielonymi zakresami. Lider uruchamia 3–5 podagentów równolegle, podagenci 3+ narzędzi
  równolegle — skrócenie czasu „up to 90 %” dla złożonych zapytań.
- **Mechanizm:** droższy model tylko tam, gdzie decyzje są strukturalne (plan, podział, synteza); masa pracy
  (czytanie, szukanie) w tańszym modelu i w osobnych kontekstach, do lidera wraca skondensowany wynik. Budżet
  wysiłku (liczba agentów i wywołań) jest **jawnie zależny od złożoności zapytania** i zapisany jako dane w
  promptcie — to dokładnie „presety, nie reguły”.
- **Dotyczy:** sesja koordynatora, agenci workflow (nasze workflow już mają podział badanie→projekt→pakiety),
  jądro (przepływ „research” jako dane).
- **Kaliber:** large. **Wysiłek:** medium — mamy podagentów i workflow; brakuje nam jawnej, konfigurowalnej tabeli
  „złożoność → liczba agentów → budżet wywołań” jako danych z presetami.
- **Uwagi:** 90,2 % mierzy jakość, nie koszt; wpis mówi wprost, że to podejście ma sens dla zadań „where the
  outcome is worth the expense” i że kodowanie (silnie zależne podzadania) jest dla niego gorsze. Liczby 4×/15×
  to obserwacja z 2025 r. na ich systemie (R60).

## role-03 — Ten sam wpis: pamięć planu lidera, kompresja u podagentów, tryby awarii orkiestracji

- **Źródło:** jak role-02 (https://www.anthropic.com/engineering/multi-agent-research-system), dostęp 2026-10-23,
  wiarygodność **high** (opis własnego systemu), vendor_blog.
- **Twierdzenie:** lider **zapisuje plan do pamięci** (Memory), bo gdy kontekst przekroczy 200 000 tokenów,
  „it will be truncated” — plan musi przeżyć ucięcie. Przy limicie kontekstu agent może uruchomić świeżego
  podagenta z czystym kontekstem i przekazać mu pracę (handoff), a plan odtworzyć z pamięci. Podagenci pracują we
  własnych oknach i **kondensują** najważniejsze informacje dla lidera; część wyników może iść do systemu plików,
  a do koordynatora wracają tylko „lightweight references”. Tryby awarii z wczesnych wersji: „spawning 50
  subagents for simple queries”, szukanie nieistniejących źródeł, rozpraszanie się nadmiarem aktualizacji;
  praca zdublowana (jeden podagent badał kryzys chipów 2021, dwa inne dublowały łańcuchy dostaw 2025); nadmiar
  narzędzi (agenci kontynuowali po uzyskaniu wystarczających wyników, zbyt rozwlekłe zapytania, złe narzędzia);
  „LLM agents are not yet great at coordinating and delegating” w czasie rzeczywistym.
- **Mechanizm:** „sekretarz” tego systemu to nie osobny model, lecz **trwały zapis planu poza kontekstem + reguła,
  że do lidera wraca streszczenie, a nie surowiec**; awarie orkiestracji to w większości **nadmiar** (za dużo
  agentów, za dużo wywołań, dublowanie), więc kontrola kosztu = jawne budżety i podział zakresów, nie zakaz
  delegowania.
- **Dotyczy:** sesja koordynatora (plan w `QUEUE_S1.md` to już ten wzorzec), agenci workflow, jądro (karty wyników
  jako referencje zamiast treści w kontekście).
- **Kaliber:** large. **Wysiłek:** small–medium — „referencja zamiast treści” jako domyślny format wyniku
  podagenta; wykrywanie dublowania zakresów przed startem.
- **Uwagi:** 200 000 to stan ich modelu w 2025 r. (R60); wymienione awarie są przykładami z jednego systemu.

## role-04 — Ten sam wpis: sędzia = jedno wywołanie LLM, rubryka, wynik 0,0–1,0 plus zaliczone/niezaliczone

- **Źródło:** jak role-02, dostęp 2026-10-23, wiarygodność **high** dla opisu własnej metody, vendor_blog.
- **Twierdzenie:** do oceny wyników badawczych użyli LLM-jako-sędziego z rubryką: trafność faktów, trafność
  cytowań, kompletność, jakość źródeł, **efektywność użycia narzędzi** (tool efficiency). Próbowali wielu sędziów;
  najbardziej spójne i najlepiej zgodne z ludźmi było „a single LLM call with a single prompt outputting scores
  from 0.0–1.0 and a pass-fail grade”.
- **Mechanizm:** rola sędziego to osobne, tanie w stosunku do zadania wywołanie; efektywność kosztowa (ile
  narzędzi zużyto) jest **kryterium oceny**, nie tylko licznikiem — sędzia karze marnotrawstwo.
- **Dotyczy:** agenci workflow (recenzje pakietów), narzędzia spec (ocena „czy to przykład czy wymaganie”),
  jądro (przepływy oceny trafności kontekstu — K3).
- **Kaliber:** medium. **Wysiłek:** small — mamy recenzentów Haiku/Sonnet; dodać rubrykę z pozycją „koszt /
  efektywność” i format 0–1 + pass/fail jako preset.
- **Uwagi:** rubryka i skala to ich przykład; model sędziego nie jest nazwany; brak liczb o zgodności z ludźmi
  (te są w role-18).

## role-05 — Claude Code: podagenci we własnym oknie kontekstu, do głównej rozmowy wraca tylko streszczenie; kolejność wyboru modelu podagenta

- **Źródło:** https://code.claude.com/docs/en/sub-agents — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high**; typ: official_docs.
- **Twierdzenie:** „Each subagent runs in its own context window with a custom system prompt, specific tool
  access, and independent permissions”; „only the relevant summary returns to your main conversation”. Pole
  `model` w frontmatter: „`sonnet`, `opus`, `haiku`, `fable`, a full model ID such as `claude-opus-5-5`, or
  `inherit`”. Kolejność wyboru, gdy nie podano: (1) parametr `model` w wywołaniu, (2) `model` z definicji
  podagenta (`inherit` = model głównej rozmowy), (3) zmienna `CLAUDE_CODE_SUBAGENT_MODEL`, (4) model głównej
  rozmowy. `CLAUDE_CODE_SUBAGENT_MODEL` **nie** zmienia modelu wbudowanych Explore i Plan — do tego
  `CLAUDE_CODE_SUBAGENT_MODEL_FORCE=1` (od v2.1.257). Wbudowane podagenty i ich modele (stan strony): Explore —
  model głównej rozmowy, tylko odczyt; Plan — dziedziczy; general-purpose — `CLAUDE_CODE_SUBAGENT_MODEL` jeśli
  ustawiony, inaczej model główny; statusline-setup — Sonnet; claude-code-guide — Haiku. Inne pola: `tools` /
  `disallowedTools`, `permissionMode`, `maxTurns` (po limicie wynik oznaczony jako częściowy, do wznowienia),
  `memory` (`user` / `project` / `local`, „Enables cross-session learning”), `background`, `isolation: worktree`.
  Koszt: „Control costs by routing tasks to faster, lower-cost models like Haiku”; ostrzeżenie: „Running many
  subagents that each return detailed results can consume significant context, and each subagent spends tokens
  of its own while it runs”; podagent, który nie jest forkiem, „starts fresh and may need time to gather
  context”. Domyślny limit 20 podagentów naraz.
- **Mechanizm:** izolacja kontekstu (surowiec zostaje u podagenta) + czteropoziomowa kolejność wyboru modelu
  (wywołanie > definicja > środowisko > dziedziczenie) — czyli model roli jest **ustawieniem z presetem na każdym
  poziomie**, dokładnie w duchu naszej filozofii. Osobne pole `memory` to zalążek „sekretarza międzysesyjnego”.
- **Dotyczy:** sesja koordynatora i agenci workflow (bezpośrednio), jądro (wzór dla `agent.run`: model roli jako
  pole z kolejnością rozstrzygania).
- **Kaliber:** medium. **Wysiłek:** small — już używamy; do przeniesienia do jądra: kolejność rozstrzygania
  modelu jako dane, pole `memory` dla ról.
- **Uwagi:** strona była ucięta po ok. 100 000 znaków (sekcja o forkach i dalsze nie zostały odczytane). Lista
  wbudowanych podagentów i ich modeli to **datowana obserwacja** (zob. role-06).

## role-06 — Datowana obserwacja (R60): ta sama rola (Explore) w 2025 r. na Haiku 4.5, w 2026 r. na modelu głównej rozmowy

- **Źródła:** (a) https://code.claude.com/docs/en/sub-agents (dostęp 2026-10-23, official_docs, **high**):
  Explore używa „the main conversation's model”, a użytkownik może to zmienić własnym podagentem `Explore` z
  `model: haiku`; (b) streszczenie wyszukiwarki wpisu https://simonwillison.net/2025/Oct/15/catherine-wu/
  (2025-10-15; strona **zablokowana, nieprzeczytana**; wiarygodność **low**): wg streszczenia product manager
  Claude Code napisała w październiku 2025, że Haiku 4.5 zasila podagenta Explore.
- **Twierdzenie:** przypisanie modelu do roli u producenta zmieniło się w ciągu roku w obu kierunkach
  (tańszy → dziedziczony); dokumentacja mówi dziś wprost, że przypisanie jest nadpisywalne.
- **Mechanizm:** mapowanie rola→model jest **danymi z datą i źródłem**, nigdy stałą; każdy nasz preset „Explore na
  Haiku” to obserwacja o jakimś dniu, do odświeżenia.
- **Dotyczy:** narzędzia spec (ledger R59/R60), jądro (profil ekonomii tokenów z P38 — ceny i przypisania ról
  jako datowane rekordy), sesja koordynatora.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** połowa (b) nie została przeczytana — to streszczenie wyszukiwarki, nie cytat; jeśli ktoś chce na nim
  budować, trzeba odczytać wpis. Lekcja nie zależy od tej połowy: dokumentacja sama mówi, że przypisanie jest
  zmienne i nadpisywalne.

## role-07 — Claude Agent SDK: `AgentDefinition` — model, `effort`, `maxTurns`, `autoCompactWindow`, `memory`; co podagent dostaje, a czego nie; skan wyniku

- **Źródło:** https://code.claude.com/docs/en/agent-sdk/subagents (przekierowanie z platform.claude.com) —
  dokumentacja oficjalna, dostęp 2026-10-23. Wiarygodność **high**; official_docs.
- **Twierdzenie:** pola definicji: `description` (kiedy używać — na tym opiera się automatyczna delegacja),
  `prompt`, `tools` / `disallowedTools` (także `mcp__server__*`), `model` („an alias such as `'fable'`, `'opus'`,
  `'sonnet'`, `'haiku'`, `'inherit'`, or a full model ID”), `skills` (preload, „At most the first 32 distinct
  names”), `memory` (`user` | `project` | `local`), `mcpServers`, `maxTurns` (wynik częściowy, do wznowienia),
  `autoCompactWindow` (obniżenie okna auto-kompaktowania podagenta „to a token count from `100000` to
  `1000000`”, tylko TS ≥ 0.3.296), `background`, `omitClaudeMd`, **`effort`** (`'low' | 'medium' | 'high' |
  'xhigh' | 'max' | number`) i `permissionMode`. Kontekst podagenta (nie-forka) „starts fresh, with no parent
  conversation, but isn't empty”: dostaje własny prompt systemowy + prompt z narzędzia Agent, CLAUDE.md
  projektu, definicje narzędzi; **nie dostaje** historii rodzica, jego wyników narzędzi ani promptu systemowego
  rodzica. „intermediate tool calls and results stay inside the subagent; only its final message returns to the
  parent”. Przykład dynamicznej konfiguracji: `model="opus" if is_strict else "sonnet"` dla recenzji
  bezpieczeństwa. Od v2.1.210 harness **skanuje końcową wiadomość podagenta** pod kątem wzorców wyglądających jak
  instrukcje (imitacja tagów harnessu, znaczniki tur `Human:`/`Assistant:`) i neutralizuje je backslashem,
  „it never removes or rewords the subagent's text”.
- **Mechanizm:** rola = (opis, prompt, narzędzia, model, wysiłek, pamięć, okno kompaktowania) — wszystko jako pola
  danych per rola; przekazanie do roli to **jawny prompt**, nie dziedziczenie kontekstu („include any file paths,
  error messages, or decisions the subagent needs directly in that prompt”) — czyli sekretarz musi spisać
  decyzje, bo nic innego nie przechodzi; wynik roli jest traktowany jako dane (skan), nie jako polecenia.
- **Dotyczy:** jądro (`agent.run` — pola roli), agenci workflow, narzędzia spec (karta wznowienia = ten prompt).
- **Kaliber:** medium. **Wysiłek:** medium — mamy część (model, narzędzia); brak u nas `effort` i okna
  kompaktowania per rola jako ustawień, oraz skanu „wynik roli to dane”.
- **Uwagi:** numery wersji (2.1.210, 0.3.296) to stan strony z 2026-10-23 (R60); lista pól jest zamknięta tylko
  u producenta — u nas lista pól roli pozostaje otwarta (R84).

## role-08 — Claude Agent SDK: limity głębokości (3), współbieżności (20) i budżetu (`maxBudgetUsd`, bez limitu); koszt podagentów wchodzi do `total_cost_usd`; Opus 5 deleguje chętniej

- **Źródło:** jak role-07 (sekcja „Cap subagent depth, concurrency, and spend”), dostęp 2026-10-23, **high**,
  official_docs.
- **Twierdzenie:** „Claude decides on its own when to spawn a subagent and how many to spawn. Each subagent
  makes its own API requests, which count toward the query's `total_cost_usd`, and a subagent can spawn
  subagents of its own, so one prompt can grow into a tree of agents.” Trzy ograniczenia: głębokość
  `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH` (domyślnie 3; 1 = podagenci nie rodzą własnych), współbieżność
  `CLAUDE_CODE_MAX_CONCURRENT_SUBAGENTS` (domyślnie 20; po przekroczeniu `Concurrent subagent limit reached`;
  sesje z „ultracode” nigdy nie są odrzucane), wydatek `maxBudgetUsd` / `max_budget_usd` (**„No limit”**
  domyślnie; po osiągnięciu: odmowa nowych podagentów `Budget limit reached`, zatrzymanie podagentów w tle,
  zakończenie z `error_max_budget_usd`). „Claude Opus 5 delegates to subagents more readily than earlier
  models” — przy presecie `claude_code` harness dodaje do promptu systemowego linię, by nie wywoływać Agent bez
  prośby; przy własnym promptcie trzeba ją dodać samemu; „Either instruction only steers Claude, so set the
  limits as well.” Dla dziesiątek–setek agentów: narzędzie `Workflow` przenosi orkiestrację do skryptu poza
  kontekstem rozmowy.
- **Mechanizm:** koszt drzewa agentów jest **zliczany u korzenia** (jedna liczba `total_cost_usd` dla całego
  zapytania) i ograniczany trzema osobnymi pokrętłami, wszystkimi z domyślnymi presetami i jednym z nich
  („budżet”) domyślnie bez limitu — to zgodne z naszą zasadą (limity to ustawienia, nie reguły). Silniejszy model
  w roli orkiestratora ma **tryb awarii „nadmiar delegowania”**, leczony instrukcją + limitem, nie zakazem.
- **Dotyczy:** sesja koordynatora (nasze `pace.py`, ledger limitów), jądro (budżet przebiegu jako pole
  `agent.run`, licznik kosztu z podagentami), agenci workflow.
- **Kaliber:** medium. **Wysiłek:** small–medium — mamy estymatę/strażnika (M1-K1c1); brakuje zliczania drzewa
  podagentów do jednej liczby i trzech osobnych limitów jako presetów.
- **Uwagi:** wartości domyślne (3, 20, brak) to stan z 2026-10-23 i wymagają konkretnych wersji SDK (R60).

## role-09 — OpenAI Agents SDK: przekazanie (handoff) = narzędzie `transfer_to_<agent>`; odbiorca widzi całą historię, chyba że `input_filter` ją przycina

- **Źródło:** https://raw.githubusercontent.com/openai/openai-agents-python/main/docs/handoffs.md — dokumentacja
  w oficjalnym repozytorium (strona `openai.github.io` zablokowana), dostęp 2026-10-23. Wiarygodność **high**;
  official_docs.
- **Twierdzenie:** „Handoffs are represented as tools to the LLM”; przekazanie do `Refund Agent` to narzędzie
  `transfer_to_refund_agent`. Domyślnie „it's as though the new agent takes over the conversation, and gets to see
  the entire previous conversation history”. `input_filter` to funkcja, która dostaje dotychczasowe wejście i
  zwraca nowe `HandoffInputData`; gotowy filtr `handoff_filters.remove_all_tools` „will automatically remove all
  tool-related items from the history” (usuwa strukturalne wpisy narzędzi; nie redaguje argumentów skopiowanych
  już do wiadomości). `RECOMMENDED_PROMPT_PREFIX` / `prompt_with_handoff_instructions` dodają do promptu
  instrukcje o przekazywaniu. `on_handoff` — callback przy przekazaniu; `input_type` — schemat argumentów
  przekazania („for model-decided metadata, not for application state or dependencies you already have
  locally”); `is_enabled` — bool lub funkcja, oceniana przy przygotowaniu listy przekazań.
- **Mechanizm:** triage/router to zwykły agent, którego jedyne „narzędzia” to przekazania; oszczędność tokenów
  w tym modelu **nie jest domyślna** (odbiorca dostaje całą historię) — pojawia się dopiero przez `input_filter`,
  czyli przez jawny krok „co sekretarz przepisuje, a co wycina” przy zmianie roli. Każdy agent ma własny `model`
  (potwierdzone w Swarm, role-10; dla SDK streszczenie wyszukiwarki, nieprzeczytane).
- **Dotyczy:** jądro (przepływy: przekazanie między rolami jako akcja z filtrem wejścia jako danymi), interfejs
  współpracy (pokazać, co poszło dalej, a co wycięto).
- **Kaliber:** medium. **Wysiłek:** small — filtr wejścia przy przekazaniu jako pole przepływu z presetami
  („wszystko”, „bez narzędzi”, „tylko decyzje”).
- **Uwagi:** brak w dokumencie liczb o oszczędności; model per agent dla SDK nie został przez mnie odczytany z
  dokumentacji SDK (tylko z README Swarm).

## role-10 — OpenAI Swarm (eksperymentalny, zastąpiony przez Agents SDK): agenci + przekazania, bezstanowy, `model` per agent, przykład `triage_agent`

- **Źródło:** https://raw.githubusercontent.com/openai/swarm/main/README.md — oficjalne repozytorium, dostęp
  2026-10-23. Wiarygodność **high** dla opisu; **R60: projekt oznaczony jako zastąpiony** („Swarm is now replaced
  by the OpenAI Agents SDK”, „a production-ready evolution of Swarm”). Typ: official_docs.
- **Twierdzenie:** „Swarm (experimental, educational)”; „two primitive abstractions: `Agent`s and **handoffs**”;
  „An `Agent` can hand off to another `Agent` by returning it in a `function`”; `context_variables` wypełniane z
  `client.run()` i aktualizowane przez `Result`; pole `model` agenta „The model to be used by the agent”, domyślnie
  `"gpt-4o"`; „Swarm is entirely powered by the Chat Completions API and is hence stateless between calls”;
  przykłady: `triage_agent` „Simple example of setting up a basic triage step to hand off to the right agent”.
- **Mechanizm:** najprostszy możliwy router: funkcja zwraca następnego agenta; stan (to, co „ustalono”) żyje w
  `context_variables`, które przenosi programista — sekretarz jest więc **po stronie kodu, nie modelu**.
- **Dotyczy:** jądro (minimalny model przekazania jako odniesienie), narzędzia spec.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** jako obserwacja historyczna: producent sam przeszedł od bezstanowych przekazań do SDK z filtrami
  wejścia i sesjami — kierunek rozwoju, nie ograniczenie.

## role-11 — AutoGen 0.2 (dziś AG2): menedżer czatu grupowego z własnym `llm_config` wybiera mówcę; `summary_method="reflection_with_llm"` i `carryover` jako wbudowany „sekretarz”

- **Źródło:** https://raw.githubusercontent.com/microsoft/autogen/0.2/website/docs/tutorial/conversation-patterns.ipynb
  — oficjalny notatnik dokumentacji (gałąź 0.2; `docs.ag2.ai` zablokowane), dostęp 2026-10-23. Wiarygodność
  **high** dla wersji 0.2; **R60: to wersja historyczna** (projekt rozwidlił się na AG2 i AutoGen 0.4+). Typ:
  official_docs.
- **Twierdzenie:** `initiate_chat` ma `summary_method`: domyślnie `last_msg` („the last message of the chat”);
  `reflection_with_llm` „takes a list of messages from the conversation and summarize them using a call to an
  LLM”; domyślny `summary_prompt`: **„Summarize the takeaway from the conversation. Do not add any introductory
  phrases.”**, nadpisywalny. „The summary method first tries to use the recipient's LLM, if it is not available
  then it uses the sender's LLM.” W czatach sekwencyjnych streszczenie „brings the summary of the previous chat to
  the context of the next chat”; „Carryover accumulates as the conversation moves forward” (w następnym czacie
  pojawia się sekcja „Context:”). Menedżer czatu grupowego przyjmuje własny `llm_config` „so it can use the LLM to
  select the next agent”, po czym rozsyła wiadomość; strategie wyboru mówcy: `auto` (LLM decyduje, domyślna),
  `round_robin`, `random`, `manual` (pyta człowieka).
- **Mechanizm:** to najbliższy gotowy „sekretarz, który spisuje, co ustalono”: osobne wywołanie LLM z własnym
  promptem, którego wynik (nie cała rozmowa) jedzie dalej; model streszczenia wynika z konfiguracji odbiorcy,
  więc może być tańszy niż modele rozmawiające — ustawienie, nie reguła. Router (menedżer) również ma własny
  model, czyli trzy role (rozmówcy, router, skryba) mogą mieć trzy różne modele.
- **Dotyczy:** jądro (krok „streszcz ustalenia” jako akcja przepływu z własnym modelem i promptem jako danymi),
  sesja koordynatora (kolejka QUEUE jako carryover), interfejs współpracy (pokazać streszczenie, które poszło
  dalej).
- **Kaliber:** medium. **Wysiłek:** small — mamy kompaktowanie i karty; brakuje jawnej akcji „skryba” z osobnym
  modelem i promptem, której wynik jest rekordem „co ustalono”.
- **Uwagi:** notatnik nie podaje pomiarów oszczędności; w przykładach wszystkie role mają ten sam model gpt-4 —
  że mogą się różnić, wynika z tego, że każdy obiekt ma własny `llm_config`, nie z pomiaru.

## role-12 — AutoGen 0.2: telemetria kosztu „actual vs total” (bez / z odtworzeniami z cache), `gather_usage_summary`, ceny własne, pułapka Azure

- **Źródło:** https://raw.githubusercontent.com/microsoft/autogen/0.2/notebook/agentchat_cost_token_tracking.ipynb
  — oficjalny notatnik (gałąź 0.2), dostęp 2026-10-23. Wiarygodność **high** dla 0.2; R60 jak wyżej. Typ:
  official_docs.
- **Twierdzenie:** `OpenAIWrapper.print_usage_summary(mode=...)`: domyślnie `["actual", "total"]` — „actual”
  tylko wywołania niecache'owane, „total” wszystkie łącznie z odtworzeniami z lokalnego cache odpowiedzi;
  `clear_usage_summary()` zeruje. `gather_usage_summary(agents)` zwraca `usage_including_cached_inference` i
  `usage_excluding_cached_inference`, każde z `total_cost` i wpisem per model: `cost`, `prompt_tokens`,
  `completion_tokens`, `total_tokens`. Cache odpowiedzi: „By default, cache_seed is set to 41 and enabled. If
  you don't want to use cache, set cache_seed to None.” — powtórzone identyczne wywołanie nie zwiększa „actual”,
  zwiększa „total”. Ceny własne: pole `price` w `config_list` jako lista dwóch liczb (prompt, completion) na 1k
  tokenów, gdy cena modelu jest nieznana lub nieaktualna. Azure: model w odpowiedzi bez wersji, więc koszt może
  być zły, ale „the token count summary is accurate” — koszt można policzyć samemu.
- **Mechanizm:** dwie księgi — „co faktycznie poszło do dostawcy” i „co by poszło bez cache odpowiedzi” — dają
  wprost **wartość cache jako różnicę**; tokeny są prawdą pierwotną, cena to nakładka (może być zła, podmienialna).
- **Dotyczy:** jądro (profil ekonomii tokenów z P38: dodać osobną księgę dla odtworzeń z cache odpowiedzi;
  cena jako datowana nakładka z własnym nadpisaniem — już mamy jako dane).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** to cache **odpowiedzi** po stronie klienta (identyczne wywołanie → ta sama odpowiedź), nie prompt
  caching dostawcy; obie rzeczy warto księgować osobno.

## role-13 — CrewAI: proces hierarchiczny — menedżer (`manager_llm` lub własny `manager_agent`) przydziela, deleguje i waliduje; `usage_metrics`; w changelogu „brakujące tokeny menedżera”

- **Źródła:** https://raw.githubusercontent.com/crewAIInc/crewAI/main/docs/edge/en/learn/hierarchical-process.mdx
  (dokumentacja w oficjalnym repozytorium, gałąź „edge”, dostęp 2026-10-23, **high**, official_docs); fragmenty z
  GitHub code search w tym samym repozytorium: `docs/edge/en/concepts/crews.mdx`, `flows.mdx`, `agents.mdx`,
  `changelog.mdx` (dostęp 2026-10-23, **high**); krytyka praktyka: streszczenie wyszukiwarki artykułu
  https://towardsdatascience.com/why-crewais-manager-worker-architecture-fails-and-how-to-fix-it/ (strona
  zablokowana, nieprzeczytana, **low**, practitioner_writeup).
- **Twierdzenie:** „'manager' agent coordinates the workflow, delegates tasks, and validates outcomes”; „A
  manager agent allocates tasks among crew members based on their roles and capabilities”; „The manager
  evaluates outcomes to ensure they meet the required standards”. „Configuring the `manager_llm` parameter is
  crucial for the hierarchical process” (przykład `manager_llm="gpt-4o"`); alternatywnie `manager_agent` z
  własnym `role`, `goal`, `backstory`, `allow_delegation=True`. Zdanie o tokenach: „The hierarchical process is
  designed to leverage advanced models like GPT-4, optimizing token usage while handling complex tasks with greater
  efficiency” — **deklaracja projektowa bez pomiaru**. Telemetria: po `kickoff()` `crew.usage_metrics` „to view
  the language model (LLM) usage metrics for all tasks executed by the crew”; `flow.usage_metrics` zbiera „every
  LLM call” w przebiegu, łącznie z wywołaniami w narzędziach agentów i gołymi `LLM.call(...)`; wynik agenta też ma
  `usage_metrics`. Changelog: „Fixed missing `manager_agent` tokens in `usage_metrics` from kickoff”. Wg
  streszczenia krytyki (nieprzeczytane): menedżer często nie koordynuje, zadania i tak idą sekwencyjnie, dodatkowe
  wywołania narzędzi, duże opóźnienie.
- **Mechanizm:** rola menedżera (router + walidator) z osobnym modelem jest tu domyślna i wymagana; tryb awarii
  telemetrii: **tokeny roli nadzorczej wypadały z sumy** — koszt routera/sędziego trzeba liczyć do zadania
  (u nas P31: „routing cost in estimate”).
- **Dotyczy:** jądro (księgowanie kosztu ról nadzorczych w tej samej sumie co wykonawcze), narzędzia spec
  (rozróżnić deklaracje producenta od pomiarów — R59).
- **Kaliber:** medium. **Wysiłek:** small.
- **Uwagi:** nie znalazłem w przeczytanej stronie, czy robotnicy mogą mieć inny `llm` niż menedżer (w przykładach
  mają tylko role/goal/backstory); pól `usage_metrics` nie odczytałem (semantyka pod „UsageMetrics field
  semantics” w `flows.mdx`, nieprzeczytane).

## role-14 — MetaGPT (ICLR 2024): pięć ról z profilem, komunikacja dokumentami zamiast dialogu, wspólna pula wiadomości z subskrypcją wg roli

- **Źródło:** arXiv 2308.00352 (v7, data mirroru 2024-11-04) przez mirror `hf://papers/2308.00352/paper.md`
  (arxiv.org zablokowane), dostęp 2026-10-23; publikacja ICLR 2024. Wiarygodność **high** (praca recenzowana);
  typ: academic. Uzupełniająco README https://raw.githubusercontent.com/geekan/MetaGPT/main/README.md
  („Code = SOP(Team)”, wejście „one line requirement”, wyjście „user stories / competitive analysis /
  requirements / data structures / APIs / documents, etc.”).
- **Twierdzenie:** role: Product Manager, Architect, Project Manager, Engineer, QA Engineer; każda ma profil
  (name, profile, goal, constraints), własny kontekst i umiejętności (PM — wyszukiwarka, Engineer — wykonanie
  kodu). SOP zakodowane w sekwencje promptów; „agents in MetaGPT communicate through documents and diagrams
  (structured outputs) rather than dialogue” — argument „telephone game”: po kilku rundach swobodnej rozmowy
  informacja się zniekształca; strukturalne artefakty „contain all necessary information, preventing irrelevant
  or missing content”. **Wspólna pula wiadomości** (publish-subscribe): agenci publikują strukturalne wiadomości i
  czytają cudze bez pytania i czekania; ale „Sharing all information with every agent can lead to information
  overload”, więc **subskrypcja wg profilu roli**: „agents utilize role-specific interests to extract relevant
  information”; „an agent activates its action only after receiving all its prerequisite dependencies”
  (Architekt czyta PRD od PM, dokumenty QA mało go dotyczą). Pętla wykonawcza: Engineer uruchamia testy, poprawia,
  „until the test is passed or a maximum of 3 retries is reached”.
- **Mechanizm:** „sekretarz” to tu **sam format**: każda rola oddaje dokument o ustalonym schemacie (PRD →
  projekt → lista zadań), a następna rola czyta tylko to, co subskrybuje — selekcja kontekstu wg roli jest
  oszczędnością tokenów wbudowaną w architekturę, nie w prompt.
- **Dotyczy:** jądro (K3 selektor kontekstu: subskrypcja wg roli jako jedna z polityk; artefakty ról jako węzły
  grafu), narzędzia spec (schematy wyjść ról), interfejs współpracy.
- **Kaliber:** scientific. **Wysiłek:** medium.
- **Uwagi:** praca z 2023 r. na GPT-4; pięć ról to przykład ich „firmy”, nie lista; nie podaje pomiaru, ile
  tokenów oszczędza subskrypcja w porównaniu z pełnym rozsyłaniem (jest tylko argument jakościowy; tabela 9
  „Information Overload” w dodatku E — nieprzeczytana).

## role-15 — MetaGPT: liczby — więcej tokenów na zadanie (31 255 vs 19 292), ale 2× mniej tokenów na linię kodu (124,3 vs 248,9); każda dodana rola podnosi koszt i obniża poprawki

- **Źródło:** jak role-14 (sekcja 4, tabele 1 i 3), dostęp 2026-10-23, **high**, academic.
- **Twierdzenie:** HumanEval **85,9 %**, MBPP **87,7 %** Pass@1 (z GPT-4). SoftwareDev (7 z 70 zadań),
  ChatDev / MetaGPT bez feedbacku / MetaGPT: wykonywalność 2,25 / 3,67 / 3,75 (skala 1–4); czas 762 / 503 /
  541 s; **tokeny 19 292 / 24 613 / 31 255**; linie kodu 77,5 / 194,6 / 251,4; **„Productivity” = tokeny na linię
  kodu 248,9 / 126,5 / 124,3**; poprawki ludzkie 2,5 / 2,25 / 0,83. Definicja kosztu w pracy: (B) „running time,
  token usage, expenses”; (D) „number of token usage divided by the number of lines of code”; (E) „Human
  Revision Cost”. Ablacja ról (tabela 3): sam Engineer — 83 linie, **0,915 USD**, 10 poprawek, wykonywalność
  1,0; + Product Manager — 1,059 USD, 6,5, 2,0; + Architect — 1,204 USD, 4,0, 2,5; + Project Manager (bez
  Architekta) — 1,251 USD, 3,5, 2,0; wszystkie cztery — 191 linii, **1,385 USD**, 2,5 poprawki, 4,0. „While more
  roles slightly increase the expenses, the overall performance improves noticeably.” Feedback wykonawczy:
  +4,2 % HumanEval, +5,4 % MBPP; poprawki 2,25 → 0,83.
- **Mechanizm:** miarą oszczędności nie jest „mniej tokenów”, lecz **tokeny na jednostkę użytecznego wyniku**
  (tu: linia kodu) oraz **koszt poprawek człowieka** — dodatkowe role zwiększają koszt o ~50 % (0,915 → 1,385
  USD), a zmniejszają poprawki 4× (10 → 2,5).
- **Dotyczy:** jądro (profil ekonomii tokenów: metryki „na jednostkę wyniku” i „poprawki po stronie człowieka”
  jako pola), sesja koordynatora (ocena pakietów), narzędzia spec.
- **Kaliber:** scientific. **Wysiłek:** small — to definicje metryk do dodania do danych; sam pomiar poprawek
  wymaga rejestrowania, co człowiek zmienił po agencie.
- **Uwagi:** próbka 7 zadań, ceny GPT-4 z 2023 r., ocena wykonywalności przez ludzi; liczby służą jako wzór
  metryk, nie jako wartości odniesienia.

## role-16 — FrugalGPT (2023): trzy strategie — adaptacja promptu, aproksymacja LLM, kaskada LLM; do 98 % niższy koszt przy jakości GPT-4

- **Źródło:** arXiv 2305.05176 (maj 2023; Chen, Zaharia, Zou, Stanford) przez mirror
  `hf://papers/2305.05176/paper.md`, dostęp 2026-10-23. Wiarygodność **high** jako praca naukowa (preprint z
  wersją warsztatową ICML 2023); wyniki na benchmarkach i modelach z 2023 r. Typ: academic.
- **Twierdzenie:** ceny API „can differ by two orders of magnitude” (prompt 10M tokenów: 30 USD GPT-4 vs 0,2 USD
  GPT-J u Textsynth); trzy strategie: „1) prompt adaptation, 2) LLM approximation, and 3) LLM cascade”;
  FrugalGPT (kaskada) „learns which combinations of LLMs to use for different queries” i „can match the
  performance of the best individual LLM (e.g. GPT-4) with up to 98% cost reduction or improve the accuracy over
  GPT-4 by 4% with the same cost” (98 % na zbiorze HEADLINES). Koszt zapytania = koszt promptu + koszt generacji
  (+ czasem stała opłata).
- **Mechanizm:** tani model pierwszy; **osobny oceniacz** decyduje, czy odpowiedź jest wystarczająca, inaczej
  eskalacja do droższego; kaskada płaci dwa razy za zapytania eskalowane, więc opłaca się, gdy większość
  zapytań jest łatwa. Komentarz w streszczeniu wyszukiwarki (nieprzeczytany): wymaga danych etykietowanych do
  wytrenowania oceniacza.
- **Dotyczy:** jądro (przepływ „tani-najpierw z eskalacją” jako dane: próg akceptacji, model oceniacza, lista
  modeli w kaskadzie — wszystko konfigurowalne), agenci workflow (recenzje: Haiku pierwszy, eskalacja).
- **Kaliber:** scientific. **Wysiłek:** medium — mechanizm prosty, ale oceniacz trafności wymaga danych lub
  sędziego LLM (role-18).
- **Uwagi:** 98 % to maksimum na jednym zbiorze; RouteLLM (role-17) zauważa, że kaskady płacą opóźnieniem i
  wielokrotnymi wywołaniami w porównaniu z routerem, który wybiera jeden model przed wywołaniem.

## role-17 — RouteLLM (2024): router uczony na preferencjach z Chatbot Arena, próg kalibrowany do udziału drogiego modelu; >2× (artykuł) / do 85 % (README) oszczędności przy 95 % jakości GPT-4

- **Źródła:** arXiv 2406.18665 (czerwiec 2024; Ong, Almahairi i in., UC Berkeley/Anyscale/Canva) przez mirror
  `hf://papers/2406.18665/paper.md` (dostęp 2026-10-23, **high**, academic); README
  https://raw.githubusercontent.com/lm-sys/RouteLLM/main/README.md (oficjalne repozytorium, dostęp 2026-10-23,
  **high**, official_docs); reprodukcja Anyscale wg streszczenia wyszukiwarki (nieprzeczytana, **medium**).
- **Twierdzenie:** zadanie binarne: model silny (np. GPT-4) vs słaby (np. Mixtral-8x7B); cel „minimize costs
  while achieving a specific performance target, e.g., 90% of the strong model”; router = model przewidujący
  P(win_strong | zapytanie) + próg α; metryki: **CPT** (call-performance threshold — jaki udział wywołań drogiego
  modelu potrzebny do zadanej jakości) i **APGR** (average performance gain recovered). Artykuł: „reduce costs by
  over 2 times without sacrificing response quality”, generalizacja na pary modeli nieobecne w treningu; przykład
  w wstępie: Claude 3 Haiku vs Opus różnią się ponad **50×** ceną wyjścia (1,25 vs 75 USD/M). README: „reduce
  costs by up to 85%” przy „95% GPT-4 performance on widely-used benchmarks like MT Bench”; „>40% cheaper” niż
  oferty komercyjne; routery `mf` (faktoryzacja macierzy, zalecany), `sw_ranking` (ważone Elo wg podobieństwa do
  zapytania), `bert`, `causal_llm`, `random`; kalibracja: `calibrate_threshold --strong-model-pct 0.5` →
  „For 50.0% strong model calls for mf, threshold = 0.11593”; „recommend calibrating on data that resembles your
  own queries”. Wg streszczenia Anyscale: w ich reprodukcji do 70 % na MT Bench.
- **Mechanizm:** w odróżnieniu od kaskady router wybiera **jeden** model przed wywołaniem (brak podwójnej opłaty i
  opóźnienia); pokrętłem nie jest „który model”, lecz **docelowy udział drogiego modelu** albo docelowa jakość —
  próg wynika z kalibracji na własnych danych, czyli jest datowanym parametrem z presetem.
- **Dotyczy:** jądro (nasz router klasyfikujący z P31: dodać kalibrowalny próg i cel „udział drogiego modelu”
  jako ustawienie), sesja koordynatora (dobór modelu recenzenta do trudności pakietu).
- **Kaliber:** scientific. **Wysiłek:** medium — mechanizm progu jest prosty; dane preferencji do treningu
  routera w naszej domenie trzeba by zbierać (ocena użytkownika „czy odpowiedź wystarczyła”).
- **Uwagi:** 85 % vs 70 % vs „>2×” — trzy liczby z trzech źródeł o różnych założeniach; wartości zależą od
  benchmarku i pary modeli (R60); README jest niespójny co do progu (0,11593 vs 0,1881 w innym miejscu).

## role-18 — LLM-jako-sędzia (Zheng i in., 2023): GPT-4 zgadza się z ludźmi w >80 % — tyle, co ludzie między sobą; znane uprzedzenia: pozycja, rozwlekłość, samoocena, słabe rozumowanie

- **Źródło:** arXiv 2306.05685 „Judging LLM-as-a-Judge with MT-Bench and Chatbot Arena” (czerwiec 2023; UC
  Berkeley i in.) przez mirror `hf://papers/2306.05685/paper.md`, dostęp 2026-10-23. Wiarygodność **high**
  (praca szeroko cytowana, dane publiczne: 3K głosów ekspertów, 30K rozmów); typ: academic.
- **Twierdzenie:** „strong LLM judges like GPT-4 can match both controlled and crowdsourced human preferences
  well, achieving over 80% agreement, the same level of agreement between humans”; „LLM-as-a-judge is a scalable
  and explainable way to approximate human preferences, which are otherwise very expensive to obtain”; badane
  ograniczenia: „position, verbosity, and self-enhancement biases, as well as limited reasoning ability”, z
  propozycjami łagodzenia części z nich.
- **Mechanizm:** sędzia LLM zastępuje kosztowną ocenę ludzką przy porównywaniu odpowiedzi (także: „czy ten
  fragment kontekstu był trafny”); uprzedzenia wymagają zabezpieczeń projektowych — zamiana pozycji w parach,
  kontrola długości, sędzia inny niż oceniany model.
- **Dotyczy:** jądro (K3: ocena trafności kontekstu przez tani model jako jedna z polityk; przepływy sędziego z
  presetami łagodzenia uprzedzeń), agenci workflow (recenzje), narzędzia spec (sprawdzenie „przykład czy
  wymaganie”).
- **Kaliber:** scientific. **Wysiłek:** small — rubryka + zamiana pozycji + inny model sędziego to dane
  przepływu.
- **Uwagi:** praca z 2023 r. o GPT-4 jako sędzim; nie odczytałem liczb z części dalszych niż abstrakt
  (szczegóły łagodzenia uprzedzeń w treści); „80 %” dotyczy preferencji między odpowiedziami czatu, nie oceny
  trafności fragmentów kontekstu — przeniesienie na nasz przypadek to hipoteza do zmierzenia.

## role-19 — OpenRouter: pola `usage` w odpowiedzi — `cost`, `cost_details`, `cached_tokens`, `cache_write_tokens`, `reasoning_tokens`, `is_byok` (ze schematu oficjalnego SDK)

- **Źródła:** oficjalny SDK TypeScript OpenRouter, pliki `src/models/chatusage.ts`, `src/models/costdetails.ts`,
  `src/models/activityitem.ts` w https://github.com/OpenRouterTeam/typescript-sdk (odczyt przez
  raw.githubusercontent.com i GitHub code search), dostęp 2026-10-23 — wiarygodność **high** (kod producenta;
  typ: official_docs jako schemat); dokumentacja https://openrouter.ai/docs/guides/administration/usage-accounting
  **zablokowana** — tylko streszczenie wyszukiwarki (**medium**, nieprzeczytane).
- **Twierdzenie (schemat):** `ChatUsage`: `prompt_tokens` („Number of tokens in the prompt”), `completion_tokens`,
  `total_tokens`, `cost` („Cost of the completion”), `cost_details` („Breakdown of upstream inference costs”:
  `upstream_inference_cost`, `upstream_inference_prompt_cost`, `upstream_inference_completions_cost`), `is_byok`
  („Whether a request was made using a Bring Your Own Key configuration”), `server_tool_use_details` („Usage for
  server-side tool execution (e.g., web search)”); `prompt_tokens_details`: `cached_tokens` („Cached prompt
  tokens”), **`cache_write_tokens`** („Tokens written to cache. Only returned for models with explicit caching and
  cache write pricing.”), `audio_tokens`, `video_tokens`; `completion_tokens_details`: `reasoning_tokens`
  („Tokens used for reasoning”), `audio_tokens`, `accepted_prediction_tokens`, `rejected_prediction_tokens`.
  Komentarz w `activityitem.ts`: `cached_tokens` to „Total prompt tokens read from the provider prompt cache
  (cache hits). Generally a subset of `prompt_tokens`; for replayed response-cache hits the provider may report
  the two counters over disjoint token sets, so `cached_tokens` can exceed `prompt_tokens`.” Wg streszczenia
  dokumentacji (nieprzeczytane): usage jest dziś zawsze dołączane (`usage: {include: true}` i
  `stream_options.include_usage` „deprecated and have no effect”), w strumieniu przychodzi w ostatniej wiadomości
  SSE, `cost` to kwota faktycznie obciążona, `upstream_inference_cost` dotyczy tylko BYOK, a strona o prompt
  caching wspomina pole `cache_discount`.
- **Mechanizm:** odpowiedź niesie zarówno tokeny (prawdę pierwotną, z podziałem na odczyt / zapis cache /
  rozumowanie), jak i **cenę policzoną przez dostawcę** — co pozwala kalibrować naszą estymatę i wykrywać
  rozjazdy (M1-K1c1); edge case „cached > prompt” to pułapka dla każdego, kto liczy „hit rate = cached/prompt”.
- **Dotyczy:** jądro (profil ekonomii tokenów: pola zapisu do cache, tokenów rozumowania, kosztu dostawcy jako
  osobne kolumny; obsługa przypadku `cached_tokens > prompt_tokens`), interfejs współpracy („co kosztowało”).
- **Kaliber:** small. **Wysiłek:** small — rozszerzenie parsera `usage`.
- **Uwagi:** schemat SDK to stan repozytorium z 2026-10-23; dokumentacji nie przeczytałem — zdania o „zawsze
  dołączane” i `cache_discount` są do potwierdzenia; `cost_details` ma więcej pól niż wymieniłem (plik odczytany
  fragmentarycznie).

## role-20 — Anthropic Usage & Cost Admin API: tokeny niecache'owane / cache'owane / zapis cache / wyjście w kubełkach 1m/1h/1d, grupowanie po kluczu, workspace, modelu, service tier, oknie kontekstu; koszt w centach USD, tylko dziennie

- **Źródło:** https://platform.claude.com/docs/en/manage-claude/usage-cost-api — dokumentacja oficjalna, dostęp
  2026-10-23. Wiarygodność **high**; official_docs.
- **Twierdzenie:** endpoint `/v1/organizations/usage_report/messages`: „Measure uncached input, cached input,
  cache creation, and output tokens”; filtrowanie i grupowanie po `api_key_id`, `workspace_id`, `model`,
  `service_tier` (np. `batch`, `priority`), `context_window` (np. `0-200k`), `inference_geo`, `speed`
  (`standard`/`fast`); także użycie narzędzi serwerowych (web search). Kubełki: `1m` (domyślnie 60, maks. 1 440),
  `1h` (24 / 168), `1d` (7 / 31). Endpoint `/v1/organizations/cost_report`: „All costs are in USD, reported as
  decimal strings in lowest units (cents)”, tylko `1d`, grupowanie po `workspace_id` i `description` (z
  rozbiciem na `model`, `inference_geo`); koszty Priority Tier nie wchodzą do cost_report; wykonanie kodu widoczne
  tylko w cost_report. Świeżość: „typically appears within 5 minutes”; polling „once per minute for sustained
  use”. Wymaga klucza Admin API / OAuth `org:admin`; „The Admin API is unavailable for individual accounts”. Do
  kosztu per użytkownik Claude Code — osobny „Claude Code Analytics API”. Wymienione cele: „Cache efficiency:
  Measure and optimize prompt caching”, „Cost reconciliation”, „Budget monitoring”.
- **Mechanizm:** druga, niezależna księga **po stronie dostawcy** (nie z odpowiedzi), z której można policzyć
  współczynnik cache (cached / (uncached + cached)) i uzgodnić własne liczniki z fakturą; jednominutowe kubełki
  umożliwiają pomiar „ile zjadł ten pakiet” po czasie.
- **Dotyczy:** sesja koordynatora (ledger limitów — dziś z `rate_limit_event`), jądro (uzgadnianie estymaty z
  dostawcą), właściciel (koszt per klucz/workspace).
- **Kaliber:** medium. **Wysiłek:** small–medium — klient REST + mapowanie na nasz profil; dostępność zależy od
  rodzaju konta (fakt o koncie, do odnotowania jako obserwacja, nie ograniczenie — R60).
- **Uwagi:** „niedostępne dla kont indywidualnych” to stan z 2026-10-23; wymiary `inference_geo`/`speed` dotyczą
  modeli od lutego 2026.

## role-21 — Langfuse: `usage_details` jako rozłączne kubełki („each token must be counted in exactly one key”), cena per typ tokena z regexem modelu, progi cenowe (np. `input > 200000`), koszt liczony przy przyjęciu

- **Źródło:** https://raw.githubusercontent.com/langfuse/langfuse-docs/main/content/docs/observability/features/token-and-cost-tracking.mdx
  — dokumentacja w oficjalnym repozytorium (`langfuse.com` zablokowane), dostęp 2026-10-23. Wiarygodność
  **high**; official_docs.
- **Twierdzenie:** każda generacja ma `usage_details` (jednostki per typ) i `cost_details` (USD per typ); typy
  są rozłączne, `total` wyliczany. Przykład konwersji z OpenAI: odpowiedź z 17 903 tokenami promptu (w tym 17 817
  z cache) i 188 wyjścia zapisuje się jako `input: 86`, `input_cached_tokens: 17817`, `output: 188`,
  `total: 18091`. Koszt wnioskowany z definicji modelu: `match_pattern` (regex, np. `(?i)^(gpt-4-0125-preview)$`),
  cena per typ użycia; definicje użytkownika mają pierwszeństwo przed wbudowanymi; **„Cost is calculated at
  ingestion”** — zmiana cennika nie działa wstecz. Progi cenowe (tiers) oceniane wg priorytetu, warunki na sumie
  kluczy użycia z operatorami `gt/gte/lt/lte/eq/neq` (przykład: Claude Sonnet 4.5 „Large Context”: `input >
  200000`), warunki na parametrach modelu i metadanych. Nieznany model → brak kosztu wnioskowanego; modele
  rozumujące wymagają przesłania liczników (Langfuse „nie widzi” tokenów rozumowania). Przykład Anthropic
  przesyła `input`, `output`, `cache_read_input_tokens`, z cenami zastępczymi („we assume the input and output
  cost are 1 USD each and half the price for cached tokens”). Agregacje: dashboardy „across models, tags, or
  users”; Metrics API filtruje po typie aplikacji, użytkowniku, tagach.
- **Mechanizm:** schemat księgowy, w którym **cena jest funkcją (model, typ tokena, próg, data przyjęcia)**, a
  tokeny są rozłączne — to dokładnie to, czego potrzebuje nasz profil ekonomii tokenów, by porównywać dostawców
  (OpenRouter podaje cached jako podzbiór prompt; Langfuse chce rozłącznie — konwersja musi być jawna).
- **Dotyczy:** jądro (P38 slice 4: rozłączne kubełki + progi cenowe + „cena z dnia przyjęcia” jako pola),
  interfejs współpracy (dashboard kosztów po roli/tagu/użytkowniku).
- **Kaliber:** medium. **Wysiłek:** medium.
- **Uwagi:** strona nie opisuje agregacji per trace/sesja wprost; ceny w przykładach są zastępcze, nie rzeczywiste.

## role-22 — LangSmith: kategorie Input / Output / Other, `input_token_details.cache_read`, `output_token_details.reasoning`, mapa cen z regexem, dostawcą i datą aktywacji; cennik nieliniowy liczyć po stronie klienta

- **Źródło:** https://raw.githubusercontent.com/langchain-ai/docs/main/src/langsmith/cost-tracking.mdx —
  dokumentacja w oficjalnym repozytorium (`docs.langchain.com` zablokowane), dostęp 2026-10-23. Wiarygodność
  **high**; official_docs.
- **Twierdzenie:** trzy kategorie: Input („Tokens in the prompt sent to the model”; podtypy: cache reads, text,
  image), Output (podtypy: reasoning, text, image), Other („Costs from tool calls, retrieval steps, or any custom
  runs”). `usage_metadata`: `input_tokens` („Sum of all input token types”), `output_tokens`, `total_tokens`
  (opcjonalny), `input_token_details` (np. `{"cache_read": 5}`), `output_token_details` (np. `{"reasoning": 5}`).
  Mapa cen: regex dopasowany do `ls_model_name`, opcjonalnie `ls_provider`, cena za 1M tokenów wejścia/wyjścia,
  „price breakdown” per typ (`cache_read`, `video`, `audio`), **„Model Activation Date (Optional): The date from
  which the pricing is applicable”**. Dla modeli o cenniku nieliniowym: „calculate costs client-side and send
  them as `usage_metadata`” (wyjątek: Gemini 2.5 Pro ze stopniową funkcją kosztu obsługiwaną domyślnie). Koszt
  ręczny dla nie-LLM (narzędzia, retrieval) przez `total_cost`. Widoki: drzewo trace „displays the total usage
  for the entire trace” z agregacją na rodzicach; panel projektu; dashboardy w czasie.
- **Mechanizm:** to samo co Langfuse z dwiema różnicami wartymi przejęcia: **koszt kroków niemodelowych** (narzędzia,
  retrieval) w tej samej księdze i **data aktywacji cennika** jako pole; agregacja po drzewie przebiegu daje
  „koszt tego zadania z podagentami” bez dodatkowej pracy.
- **Dotyczy:** jądro (profil ekonomii: koszt narzędzi i OCR/ASR obok tokenów; agregacja po drzewie
  `action_run`), interfejs współpracy.
- **Kaliber:** medium. **Wysiłek:** small.
- **Uwagi:** wbudowane cenniki (OpenAI/Anthropic/Gemini) to stan z 2026-10-23 i rzecz do nadpisania (R60).

## role-23 — Helicone: atrybucja kosztu przez nagłówki `Helicone-Property-*` i `Helicone-User-Id` („per-user cost analytics”); etykiety można dopisać po fakcie

- **Źródło:** https://raw.githubusercontent.com/Helicone/helicone/main/docs/features/advanced-usage/custom-properties.mdx
  — dokumentacja w oficjalnym repozytorium (`docs.helicone.ai` zablokowane), dostęp 2026-10-23. Wiarygodność
  **high** dla tej strony; official_docs. Rozbieżność kosztów cache (trzecia strona, wg streszczenia
  wyszukiwarki, nieprzeczytana): **low**.
- **Twierdzenie:** „Name your header in the format `Helicone-Property-[Name]`”; `Helicone-User-Id` „Enables
  per-user cost analytics and usage metrics”; cele: „Calculate cost per user, conversation, or feature to
  understand your application's profitability”, „Track performance and costs across different environments and
  deployments”, „Group related requests in multi-step AI processes” (`Helicone-Session-Id`); właściwości można
  zmienić po fakcie: `PUT /v1/request/{id}/property`; „Calculate costs and metrics grouped by properties”.
  Strona nie wspomina tokenów cache. Wg streszczenia wyszukiwarki pewien blog zgłosił, że szacunek Helicone dla
  żądania Anthropic z tokenami cache różnił się istotnie od liczby „Calculated by Anthropic”.
- **Mechanizm:** koszt staje się porównywalny dopiero po **oznaczeniu każdego żądania etykietami** (rola, przepływ,
  użytkownik, środowisko, sesja) — i etykieta musi być edytowalna wstecz (np. gdy dopiero po recenzji wiadomo,
  że pakiet „nie wyszedł”).
- **Dotyczy:** jądro (każde `action_run` z etykietami: rola, przepływ, pakiet, użytkownik; etykiety jako dane
  edytowalne), sesja koordynatora (koszt per pakiet w INDEX/ledgerze).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** nie znalazłem w przeczytanych źródłach Helicone, jak wycenia tokeny cache; sygnał o rozbieżności z
  liczbą Anthropic jest nieprzeczytany i z jednego bloga — nadaje się jedynie jako powód, by **ufać liczbie
  dostawcy, a cenę pośrednika traktować jako estymatę**.

## role-24 — „Koszt na scalony PR” jako metryka praktyków: definicje się różnią (tylko tokeny vs „fully loaded”), przykłady: 29 USD/PR (53 000 USD / 1 832 PR), mediana 10,6 M tokenów/PR, cache <90 % → koszt rośnie szybko

- **Źródła (wszystkie zablokowane, tylko streszczenia wyszukiwarki, wiarygodność **low**, typ:
  practitioner_writeup / vendor_blog):** https://getunblocked.com/blog/cost-per-merged-pr/ ;
  https://www.cloudzero.com/blog/ai-agent-cost/ ; https://campusiq.com/blogs/what-we-actually-spend ;
  https://orbi.build/blog/deepseek-coding-agent-cost-per-merged-pr/ ;
  https://www.warp.dev/articles/metrics-that-prove-ai-coding-agents-are-working — dostęp 2026-10-23.
- **Twierdzenie (wg streszczeń):** Unblocked: licznik = pełny koszt (tokeny, narzędzia, czas recenzji, przeróbki),
  mianownik = PR-y scalone, które „przeżyły” okno churnu (nie cofnięte ani przepisane); argument: AI zawyża
  surową liczbę PR-ów, podczas gdy recenzja i przeróbki rosną (cytowane dane Faros: czas recenzji +91 % w
  zespołach o wysokim użyciu AI). CloudZero: licznik = wszystkie wywołania modeli łącznie z powtórkami,
  obliczenia orkiestracji, magazyn wektorów, monitoring, udział platformy; mianownik = ukończone jednostki
  (zadania, tickety, PR-y). CampusIQ: **53 000 USD tokenów / 1 832 scalone PR = 29 USD/PR**, z zastrzeżeniem, że
  to nie koszt pełny. Orbi: **mediana 10,6 M tokenów na scalony PR** (DeepSeek); „gdy współczynnik trafień cache
  spada poniżej 90 %, koszt rośnie szybko”. Warp: zestawiać z odsetkiem autonomii, odsetkiem interwencji
  człowieka, odsetkiem defektów/rollbacków i czasem cyklu, zawsze wobec linii bazowej sprzed agentów.
- **Mechanizm:** metryka „na jednostkę wyniku” (jak „Productivity” w MetaGPT, role-15) z jawnie zdefiniowanym
  licznikiem i mianownikiem; różnice definicji (tylko tokeny vs pełny koszt; PR scalony vs scalony i trwały) są
  **parametrami metryki**, nie sporem — u nas wszystkie warianty jako dane.
- **Dotyczy:** sesja koordynatora (koszt per pakiet/merge w `INDEX.md` i ledgerze — dane już są w gicie), jądro
  (profil ekonomii: definicje metryk jako dane), właściciel (jedna liczba „co kosztował ten pakiet”).
- **Kaliber:** small. **Wysiłek:** medium — potrzebne powiązanie przebiegów agentów z commitami/merge'ami i okno
  „przeżycia” zmiany.
- **Uwagi:** **żadnej z pięciu stron nie przeczytałem** — liczby pochodzą ze streszczenia wyszukiwarki i są do
  potwierdzenia przed użyciem; wszystkie źródła to blogi dostawców narzędzi (konflikt interesów).

## role-25 — Synteza: jak wygląda „sekretarz, który zapisuje, co faktycznie ustalono”, w przeczytanych systemach

- **Źródła:** role-03 (Anthropic: plan w pamięci + kondensacja u podagentów), role-05/07 (Claude Code / Agent
  SDK: wraca tylko końcowa wiadomość; do podagenta trafia wyłącznie jawny prompt; pole `memory`), role-09/10
  (OpenAI: pełna historia domyślnie, `input_filter` do przycinania; Swarm: stan w `context_variables` po stronie
  kodu), role-11 (AutoGen: `reflection_with_llm` z własnym promptem i modelem odbiorcy, `carryover` narasta),
  role-14 (MetaGPT: dokumenty o schemacie + pula wiadomości z subskrypcją), role-21/22 (Langfuse/LangSmith:
  księgują koszt per trace, **nie** księgują decyzji). Wiarygodność **high** jako synteza przeczytanych źródeł
  pierwotnych; typ: other (synteza).
- **Twierdzenie:** w żadnym z przeczytanych frameworków „sekretarz” nie jest nazwaną rolą; występuje w czterech
  postaciach: (1) **osobne wywołanie streszczające** z własnym promptem i modelem, którego wynik — nie rozmowa —
  jedzie dalej (AutoGen `reflection_with_llm`, domyślny prompt „Summarize the takeaway…”; Anthropic: podagenci
  kondensują); (2) **trwały zapis poza kontekstem** (plan lidera w pamięci; `memory` user/project/local w Claude
  Code; pliki z „lightweight references”); (3) **format wymuszający treść** (MetaGPT: PRD/projekt/zadania jako
  schematy; Agent SDK: do podagenta wchodzi tylko to, co ktoś wpisał w prompt — „include any … decisions the
  subagent needs directly in that prompt”); (4) **filtr przy przekazaniu** (OpenAI `input_filter`,
  `remove_all_tools`). Narzędzia telemetrii (Langfuse, LangSmith, Helicone) zapisują **koszt** decyzji, ale nie
  jej treść — rekord „co ustalono” to zawsze warstwa aplikacji.
- **Mechanizm:** u nas wszystkie cztery postaci są już po części: `spec.json` i `OWNER_REQUIREMENTS` (trwały
  zapis + format), karty wznowienia (format), kompaktowanie (streszczenie), briefy workflow (filtr). Brakuje
  **jednej akcji przepływu „skryba”** jako danych: własny model i prompt (preset: tani), wejście = fragment
  rozmowy/przebiegu, wyjście = rekord o schemacie (ustalenia, otwarte pytania, co odrzucono, koszt), zapisany jako
  węzeł grafu z datą i źródłem; każda zmiana roli (przekazanie) przechodzi przez skrybę z filtrem jako danymi.
- **Dotyczy:** jądro (akcja „skryba” + węzeł „ustalenie”), sesja koordynatora (status lines i QUEUE jako
  wyjścia skryby), interfejs współpracy (właściciel na telefonie czyta rekord ustaleń, nie rozmowę), narzędzia
  spec.
- **Kaliber:** medium. **Wysiłek:** medium.
- **Uwagi:** to synteza, nie pomiar — żadne z źródeł nie podaje, ile tokenów oszczędza skryba w porównaniu z
  przekazaniem pełnej historii; jedyny pośredni pomiar to MetaGPT (role-15: 2× mniej tokenów na linię kodu przy
  komunikacji dokumentami) i Anthropic (role-02: tokeny tłumaczą 80 % wariancji jakości — kondensacja nie może
  iść za daleko).

---

## Czego nie znalazłem

- **LangGraph** (architektury supervisor / hierarchical / network, `Command(goto=…)`, wspólny stan vs osobne
  schematy): strona `docs.langchain.com` zablokowana, plik `docs/docs/concepts/multi_agent.md` w repozytorium
  `langchain-ai/langgraph` już nie istnieje, a w `langchain-ai/docs` code search nie zwrócił strony koncepcyjnej
  o multi-agent (tylko snippet integracji Google ADK). **Brak ustalenia o LangGraph w tej ścieżce.**
- **OpenAI Agents SDK — model per agent i Sessions** z dokumentacji SDK (nie Swarm): nieprzeczytane (strona
  zablokowana; w repozytorium czytałem tylko `handoffs.md`).
- **CrewAI**: czy robotnicy mogą mieć inny `llm` niż menedżer; semantyka pól `usage_metrics`
  („UsageMetrics field semantics” w `flows.mdx`) — nieprzeczytane.
- **Helicone**: wycena tokenów cache i metryka „cache hit rate” — nie w przeczytanej stronie.
- **OpenRouter**: oficjalna strona „usage accounting” i strona o prompt caching (`cache_discount`) — zablokowane;
  mam tylko schemat SDK i streszczenie wyszukiwarki.
- **Pomiar oszczędności z ról**: żadne przeczytane źródło nie podaje A/B „ta sama praca z jednym modelem vs z
  podziałem ról” w tokenach; najbliższe są: Anthropic (×15 tokenów za +90,2 % jakości), MetaGPT (więcej tokenów na
  zadanie, 2× mniej na linię, +51 % kosztu za 4× mniej poprawek), RouteLLM/FrugalGPT (routing/kaskada, nie role).
- **„Sekretarz” jako nazwana rola** z pomiarem: nie istnieje w przeczytanych źródłach (zob. role-25).
- **Koszt na scalony PR**: pięć źródeł, zero przeczytanych (blokada DNS) — liczby tylko ze streszczeń.
- **Zatrzymałem się** po odczycie Helicone custom properties, README Swarm i LangSmith: trzy ostatnie źródła
  potwierdzały wzorce już zebrane (etykiety do atrybucji kosztu, przekazanie jako narzędzie, cennik jako mapa
  model→typ tokena→stawka z datą) i nie dodały nowych liczb.
