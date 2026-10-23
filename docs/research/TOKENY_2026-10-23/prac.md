# R89 — ścieżka „prac”: praktycy, ekonomia tokenów w agentach kodujących

- **Ścieżka:** prac (praktycy: jak ludzie oszczędzają tokeny i pieniądze w agentach kodujących i długich sesjach)
- **Data:** 2026-10-23
- **Autor:** agent badawczy (Fable 5.1) w worktree `/home/user/chatadhd`
- **Zasady:** R58/R83/R84 (przykłady są przykładami, liczba na liście nie jest listą), R59 (każde ustalenie
  ma źródło, datę, typ i ocenę wiarygodności), R60 (fakt o stanie obecnym = datowana obserwacja, nie
  ograniczenie). Wszystkie liczby poniżej pochodzą z przeczytanych stron; czego nie przeczytałem, oznaczam.

## Jak szukałem

- 13 wyszukiwań (WebSearch, tryb standard) i 14 udanych odczytów stron (WebFetch). Preset z briefu to 8–12
  wyszukiwań i 6–10 odczytów; przekroczyłem go, bo ok. 10 adresów okazało się **zablokowanych przez proxy
  sesji** (DNS `ENOTFOUND`, także przez `curl`): `aider.chat`, `docs.cline.bot`, `cline.bot`, `manus.im`,
  `cursor.com`, `docs.github.com`, `developers.openai.com`, `docs.devin.ai`, `claude.dev` (cel przekierowania
  z `claude.com/blog`), blogi osobiste (`codex.danielvaughan.com`, `ghuntley.com`, `tech.autoscout24.com`,
  Medium). Obchodziłem to przez `raw.githubusercontent.com` (zadziałało dla dokumentacji aidera, nie dla Cline
  i Codex) oraz `github.com` (issue, gist). `gh api` jest w tej sesji związane z repozytoriami sesji (403).
- Kolejność źródeł zgodnie z R59: dokumentacja oficjalna (platform.claude.com, code.claude.com,
  anthropic.com/engineering, aider via GitHub) → zapisy praktyków z pomiarami (issue GitHub, gist z analizą
  kodu) → podsumowania wyszukiwarki dla stron zablokowanych (oznaczone jako słabsze, wiarygodność „low”
  lub „medium”).
- Zatrzymałem się, gdy trzy ostatnie źródła (podsumowanie wpisu Manus, fragment wpisu autoscout24,
  podsumowanie wpisu „Prompt caching is everything”) tylko potwierdzały to, co już miałem.
- Skala: **kaliber** = small / medium / large / scientific / experimental (rozmiar rozwiązania u źródła);
  **wysiłek** = small / medium / large (dla nas).

---

## prac-01 — Cennik cache w API Anthropic: zapis 1,25× / 2×, odczyt 0,1× (z wyjątkami 0,05× i 0,025×)

- **Źródło:** https://platform.claude.com/docs/en/build-with-claude/prompt-caching — dokumentacja oficjalna,
  dostęp 2026-10-23. Wiarygodność **high**: strona producenta, tabela cen.
- **Twierdzenie:** zapis do cache 5 min kosztuje 1,25× ceny wejścia, zapis 1 h 2×, odczyt 0,1× — z wyjątkami:
  Fable 5.1 / Mythos 5.1 odczyt 0,025×, Opus 5.5 / Sonnet 5.5 odczyt 0,05×. Przykłady z tabeli (USD / mln
  tokenów, baza / zapis 5m / zapis 1h / odczyt / wyjście): Fable 5.1: 10 / 12,50 / 20 / 0,25 / 50;
  Opus 5.5: 4 / 5 / 8 / 0,20 / 20; Sonnet 5.5: 2 / 2,50 / 4 / 0,10 / 10; Sonnet 4.6/4.5/4: 3 / 3,75 / 6 /
  0,30 / 15; Haiku 5.5 (≤100K promptu): 0,10 / 0,125 / 0,20 / 0,01 / 0,50; Haiku 4.5: 1 / 1,25 / 2 / 0,10 / 5.
  „Adding more `cache_control` breakpoints doesn't increase your costs.”
- **Mechanizm:** każda powtórzona część przedrostka płacona jest po stawce odczytu (10–40× taniej niż
  wejście), a zapis jednorazowo drożej; opłaca się, gdy przedrostek wraca ≥ 2 razy w oknie TTL.
- **Dla nas:** profil ekonomii tokenów (P38 slice 4) musi trzymać **cztery stawki na model** (wejście, zapis
  5m, zapis 1h, odczyt) jako datowane dane, nie dwie; wyjątki per model pokazują, że mnożnik odczytu jest
  polem danych, nie stałą. Dotyczy: kernel aplikacji (estymator kosztu), sesja koordynatora.
- **Kaliber:** small (tabela). **Wysiłek:** small.
- **Uwagi:** ceny zmieniają się z wersjami modeli — to datowana obserwacja (R60).

## prac-02 — Co unieważnia przedrostek cache: hierarchia tools → system → messages

- **Źródło:** jak prac-01 (platform.claude.com), dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** „Changes at each level invalidate that level and all subsequent levels.” Zmiana definicji
  narzędzi unieważnia wszystko; przełączenie web search / citations / speed zachowuje tools, unieważnia
  system i messages; `tool_choice` i obrazy unieważniają tylko messages; parametry myślenia i `effort`
  zawsze unieważniają messages (na części modeli także wcześniejsze warstwy); porzucone bloki myślenia
  (Fable 5.1, Opus 5.5, Sonnet 5.5, Haiku 5.5) unieważniają od swojej pozycji. „Cache hits require 100 percent
  identical prompt segments.” Ostrzeżenie: języki losujące porządek kluczy JSON (Swift, Go) psują cache.
- **Mechanizm:** cache jest hashem przedrostka; każda zmiana bajtu wcześniej niż punkt przerwania kosztuje
  pełne przetworzenie reszty.
- **Dla nas:** kolejność budowania zapytania w kernelu (narzędzia → system → pamięć/graf → historia) jest
  decyzją kosztową: to, co zmienia się najrzadziej, idzie najwcześniej; serializacja JSON musi być
  deterministyczna (porządek kluczy), timestamp nie może stać na początku systemu. Dotyczy: kernel,
  agenci workflow, narzędzia spec.
- **Kaliber:** medium. **Wysiłek:** small–medium (audyt kolejności i serializacji).
- **Uwagi:** zachowanie „model-specific” — strona nie podaje pełnej listy per model.

## prac-03 — TTL 5 min / 1 h, odświeżanie bez dopłaty, czas liczony od początku żądania, rozgrzewanie

- **Źródło:** jak prac-01, dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** domyślny TTL 5 minut, opcja `"ttl": "1h"`; „The cache is refreshed for no additional cost
  each time the cached content is used.” „The lifetime is measured from the start of the request that writes
  or reads the cache entry, not from the end of its response” (odpowiedź strumieniowana 4 min zostawia ok.
  1 min). Wpisy 1 h muszą poprzedzać wpisy 5 min. Rozgrzewanie: `max_tokens: 0` zapisuje cache bez wyjścia
  (odrzucane przy `stream: true`, extended thinking, structured outputs, `tool_choice` tool/any, w Batches);
  „send a new pre-warm request at least every 5 minutes”. Równoległość: „a cache entry only becomes
  available after the first response starts” — żądania równoległe trzeba opóźnić do startu pierwszej
  odpowiedzi. Cache izolowany per organizacja (i per workspace).
- **Mechanizm:** długie wywołania narzędzi (build, testy) zjadają TTL; przerwa > TTL = pełny zapis od nowa.
- **Dla nas:** potwierdza zasadę „żadne wywołanie dłużej niż ~4 minuty” z CLAUDE.md; rozgrzewanie `max_tokens:
  0` i pingi (por. aider, prac-14) to tani sposób utrzymania cache koordynatora, gdy czeka on na agenta;
  w fan-out startować drugiego agenta po pierwszym bajcie odpowiedzi pierwszego (Claude Code robi to
  automatycznie, prac-11). Dotyczy: koordynator, agenci workflow, kernel.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** strona nie podaje, jak cena ponownego zapisu po wygaśnięciu zależy od długości przerwy (jest
  binarna: trafiony / nie).

## prac-04 — Punkty przerwania: do 4, automatyczny `cache_control` na szczycie, okno 20 pozycji, minimum 512–4096

- **Źródło:** jak prac-01, dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** „You can define up to 4 cache breakpoints”; automatyczne cache'owanie (top-level
  `cache_control`) zużywa jeden z czterech slotów i „moves it forward as conversations grow”. „The system
  checks at most 20 positions for each breakpoint”; kolejne `tool_use` / `tool_result` liczą się jako jedna
  pozycja. „Place `cache_control` on the last block whose prefix is identical across the requests you want to
  share a cache”; przy zmiennym sufiksie (timestamp, bieżąca wiadomość) punkt na końcu statycznego
  przedrostka. Minimalna długość cache'owalna: 512 tokenów (Fable 5.1, Opus 5.5, Sonnet 5.5, Haiku 5.5…),
  1024 (Sonnet 5, 4.6, 4.5, Opus 4.1…), 2048 (Opus 4.7, Haiku 3.5), 4096 (Opus 4.6, 4.5, Haiku 4.5); krótszy
  prompt nie jest cache'owany i **nie zgłasza błędu** — sprawdzać `cache_creation_input_tokens` i
  `cache_read_input_tokens` (oba 0 = brak cache).
- **Mechanizm:** ogon rozmowy rosnący > 20 bloków między zapisami wypada z okna; lekarstwem drugi punkt.
- **Dla nas:** w kernelu: pomiar po każdym żądaniu obu pól i alarm (advisory), gdy oba są 0 przy
  przedrostku > minimum; drugi punkt przerwania przed ostatnią wiadomością w długich turach narzędziowych.
  Dotyczy: kernel, profil ekonomii tokenów.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** czytałem pierwsze 100 000 znaków strony (z 162 274); sekcja o pętlach agentowych może zawierać
  więcej.

## prac-05 — Punkt odniesienia kosztu Claude Code: ≈ 13 USD/dev/dzień, 150–250 USD/mies., 90 % < 30 USD/dzień

- **Źródło:** https://code.claude.com/docs/en/costs — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high** (dane producenta z wdrożeń enterprise), ale to średnie, nie rozkład.
- **Twierdzenie:** „the average cost is around $13 per developer per active day and $150-250 per developer per
  month, with costs remaining below $30 per active day for 90% of users”. Tło: procesy w tle „typically under
  $0.04 per session”. „Agent teams use approximately 7x more tokens than standard sessions when teammates run
  in plan mode”. Zalecane limity TPM/RPM na użytkownika maleją ze wzrostem zespołu (1–5 osób: 200–300k TPM;
  500+: 10–15k TPM).
- **Mechanizm:** benchmark do porównania własnego wydatku; mnożnik ×7 dla zespołów agentów to miara kosztu
  równoległości z osobnymi kontekstami.
- **Dla nas:** kalibracja progu „×10” z zasady właściciela: zespół agentów (×7) jest blisko granicy
  potwierdzenia. Dotyczy: koordynator, profil ekonomii.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** liczby dotyczą użytkowników enterprise; subskrypcje liczą użycie w oknach 5 h / tydzień, nie w USD.

## prac-06 — Warstwy zapytania Claude Code i lista akcji, które psują / zachowują cache

- **Źródło:** https://code.claude.com/docs/en/prompt-caching — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high**.
- **Twierdzenie:** trzy warstwy: system prompt (instrukcje, definicje narzędzi) → kontekst projektu (CLAUDE.md,
  auto memory, reguły) → rozmowa. **Psują cache:** zmiana modelu (każdy model ma własny cache; `opusplan`
  przełącza model przy każdym wejściu w plan mode), zmiana effort (na większości modeli; na Opus 5.5 /
  Sonnet 5.5 / Haiku 5.5 / Fable 5.1 z kluczem API lub subskrypcją — nie), fast mode (nagłówek w kluczu cache,
  raz na rozmowę), podłączenie/odłączenie serwera MCP (gdy narzędzia nie są odroczone), plugin z MCP, deny
  całego narzędzia (bez tool search), kompaktowanie (warstwa rozmowy), partia usuniętych obrazów, upgrade
  Claude Code. **Zachowują cache:** edycja plików repo (dopisek `<system-reminder>`), edycja CLAUDE.md w
  trakcie sesji (ale zmiana **nie działa** do `/clear`, `/compact` lub restartu), zmiana trybu uprawnień,
  output style, skille i komendy (dopisywane jako wiadomości), `/recap`, `/rewind`, start subagenta
  (własny cache; fork dziedziczy cache rodzica). Zasięg: cache „effectively scoped to one machine and
  directory” (ścieżki auto memory w systemie, katalog/branch w pierwszej wiadomości); sesje równoległe w tym
  samym katalogu czytają wspólny cache.
- **Mechanizm:** wszystko, co trafia do przedrostka systemowego, staje się kosztem każdej zmiany; wszystko
  dopisywane na końcu jest tanie.
- **Dla nas:** w naszych workflow: ustalić model i effort na początku agenta, nie zmieniać w trakcie; plan
  i skille jako wiadomości, nie jako zmiana systemu; stan katalogu roboczego (branch, ostatnie commity)
  trzymać poza przedrostkiem systemowym, jeśli ma być współdzielony między agentami. Dotyczy: agenci
  workflow, koordynator, kernel (ta sama architektura przy naszych własnych wywołaniach).
- **Kaliber:** medium. **Wysiłek:** small (reguły), medium (przebudowa kolejności w kernelu).
- **Uwagi:** wiele reguł ma warunek wersji (v2.1.2xx) — datowane.

## prac-07 — Ekonomia `/compact` i `/rewind`: kompaktowanie jest tanie, gdy cache ciepły, drogie po przerwie

- **Źródło:** jak prac-06, dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** żądanie podsumowania „repeats your conversation's system prompt, tools, and the history
  being summarized … While the cache is warm, that request reads your prefix from the cache. A mid-session
  `/compact` then costs a fraction of what the context size suggests”; „After a break longer than the cache
  lifetime … the summarization request reprocesses that history as uncached input. This is why `/compact`
  costs the most when you resume an old session.” `/rewind` „truncates back to a prefix that is already
  cached, rather than building a new one as compaction does” — każda późniejsza tura utrzymywała ten wpis
  ciepły. Przy TTL 1 h Claude Code może skompaktować bezczynną rozmowę tuż przed wygaśnięciem
  (`idleCompaction`, v2.1.290+). `/usage` pokazuje linię „Prompt cache (main)”: liczba żądań, % tokenów
  wejścia z cache, liczba chybień z prawdopodobną przyczyną („likely cause: tool definitions changed”),
  ciepły/zimny z TTL.
- **Mechanizm:** kompaktowanie czyta cały kontekst raz; cena zależy od tego, czy ten odczyt jest cache'owany.
- **Dla nas:** koordynator powinien kompaktować / zapisywać kartę wznowienia **przed** przerwą (zanim
  wygaśnie TTL), nie po; a gdy porzuca ścieżkę — ucinać do checkpointu zamiast streszczać. Nasz profil
  ekonomii powinien liczyć osobno „oczekiwane przebudowy” (po kompaktowaniu) i „chybienia” (błąd
  konfiguracji), jak robi to `/usage`. Dotyczy: koordynator, agenci workflow, kernel (własne kompaktowanie).
- **Kaliber:** medium. **Wysiłek:** small.
- **Uwagi:** brak liczb bezwzględnych „ułamka” kosztu — zależy od długości i stawek.

## prac-08 — Higiena kontekstu sesji: `/clear` między zadaniami, `/compact <instrukcje>`, `/btw`, dwa błędy → nowa sesja

- **Źródło:** https://code.claude.com/docs/en/best-practices (przekierowanie z anthropic.com/engineering/
  claude-code-best-practices) oraz https://code.claude.com/docs/en/costs — dokumentacja oficjalna, dostęp
  2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** „the context window is the most important resource to manage”; „If you've corrected Claude
  more than twice on the same issue in one session … Run `/clear` and start fresh with a more specific
  prompt”; `/compact Focus on the API changes`; instrukcje kompaktowania w CLAUDE.md („When compacting, always
  preserve the full list of modified files and any test commands”); „Summarize from here / up to here” w
  `/rewind`; `/btw` — pytanie poboczne, którego odpowiedź „never enters conversation history”; subagenci do
  badań („Subagents run in separate context windows and report back summaries”); „A clean session with a
  better prompt almost always outperforms a long session with accumulated corrections.” Z `costs`: „Stale
  context wastes tokens on every subsequent message”; `/clear` „costs nothing”.
- **Mechanizm:** każdy token w historii jest płacony (po stawce odczytu) w każdej kolejnej turze; usunięcie
  nieaktualnej historii obniża koszt liniowo i poprawia jakość.
- **Dla nas:** wzorzec „po dwóch nieudanych korektach — nowy agent z lepszym briefem” jest tym samym, co nasza
  zasada „świeży agent z precyzyjnym briefem zamiast wznawiania bardzo dużego kontekstu”. Dla aplikacji:
  pytania poboczne użytkownika (`/btw`) jako gałąź nie wchodząca do historii rozmowy — mamy już gałęzie
  wiadomości, brakuje flagi „nie wysyłaj dalej”. Dotyczy: koordynator, interfejs współpracy, kernel.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** brak pomiarów przed/po w dokumentacji; to zalecenia.

## prac-09 — Co przeżywa kompaktowanie i gdzie jest próg: 5 plików, 5 000 / 25 000 tokenów skilli, ~967K na 1M

- **Źródło:** https://code.claude.com/docs/en/context-window i https://code.claude.com/docs/en/model-config
  — dokumentacja oficjalna, dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** po kompaktowaniu Claude Code „re-reads up to five of the files Claude has read or edited in
  the session, choosing the ones modified most recently”; plik > 5 000 tokenów wraca jako sama ścieżka;
  treści skilli „capped at 5,000 tokens per skill and 25,000 tokens total; oldest dropped first”, ucięcie
  zachowuje początek pliku; reguły z `paths:` i zagnieżdżone CLAUDE.md są streszczane razem z rozmową
  (trwałe tylko w CLAUDE.md korzenia); hooki `SessionStart` ze źródłem `compact` dopisują kontekst po
  kompaktowaniu; procesy w tle działają dalej. Próg: modele z natywnym oknem 1M kompaktują „at about 967K
  tokens by default”; Sonnet 4.6 / Opus 4.6 bez extended context przy 200K; `/autocompact 500k` ustawia
  okno 100K–1M (`autoCompactWindow`, `CLAUDE_CODE_AUTO_COMPACT_WINDOW`; uwaga: zmienna bez sufiksu czyta
  wartość jako tokeny, więc `200` = 200 tokenów). Podsumowanie dziedziczy ustawienie extended thinking sesji.
- **Mechanizm:** kompaktowanie to streszczenie + odtworzenie z dysku tego, co jest plikiem; wszystko, co było
  tylko w historii, przechodzi przez streszczenie.
- **Dla nas:** nasza „karta wznowienia” powinna wymieniać pliki do ponownego odczytu (lista, nie treść) i
  stawiać najważniejsze instrukcje na początku (ucięcie od końca); progi kompaktowania jako ustawienie
  (preset), także w kernelu dla rozmów użytkownika. Dotyczy: koordynator, agenci workflow, kernel.
- **Kaliber:** medium. **Wysiłek:** small–medium.
- **Uwagi:** liczby na stronie `context-window` są „representative”, nie zmierzone na konkretnej sesji.

## prac-10 — CLAUDE.md: krótki, < 200 linii, pytanie „czy usunięcie spowoduje błąd?”, reszta do skilli

- **Źródło:** https://code.claude.com/docs/en/best-practices i https://code.claude.com/docs/en/costs,
  dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** „CLAUDE.md is loaded every session, so only include things that apply broadly”; „For each
  line, ask: ‘Would removing this cause Claude to make mistakes?’ If not, cut it. Bloated CLAUDE.md files
  cause Claude to ignore your actual instructions!”; „If you emphasize many lines, none of them stands out”;
  `/doctor` proponuje cięcia; `/context` pokazuje, co załadowano; „Aim to keep CLAUDE.md under 200 lines by
  including only essentials”; skille ładują się na żądanie; `/usage` pokazuje rekomendacje w stylu „25%
  Trim ~/code/api/CLAUDE.md · 49k tokens per request” (klawisz `f` stosuje część z nich).
- **Mechanizm:** CLAUDE.md jest w przedrostku każdego żądania i każdego subagenta — jego rozmiar mnoży się
  przez liczbę żądań (por. prac-18: 1 310 odczytów z cache na 1 token pracy).
- **Dla nas:** nasz CLAUDE.md ma ok. 300+ linii z historią decyzji; część (pomiary, zasady narzędziowe)
  nadaje się do skilli / plików czytanych na żądanie. To obserwacja, nie decyzja — właściciel ustala, co jest
  zasadą stałą. Dotyczy: koordynator, agenci workflow (każdy dostaje CLAUDE.md na starcie).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** „200 linii” to preset dokumentacji, nie prawo; u nas część długości to zasady właściciela, których
  każdy agent ma dostać (CLAUDE.md, waste control 6b).

## prac-11 — Gałki modelu i myślenia: effort, thinking liczony jak wyjście, `MAX_THINKING_TOKENS`, 1M bez dopłaty

- **Źródło:** https://code.claude.com/docs/en/costs i https://code.claude.com/docs/en/model-config,
  dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** „Thinking tokens are billed as output tokens, and the default budget can be tens of
  thousands of tokens per request”; poziomy effort `low … max` (domyślnie `high`; Opus 5.5 / Sonnet 5.5 /
  Haiku 5.5 `medium`; Opus 4.7 `xhigh`); `max` „may show diminishing returns and is prone to overthinking”;
  „You can't turn thinking off on Opus 5.5, Sonnet 5.5, Haiku 5.5, or the Fable models”; `MAX_THINKING_TOKENS`
  tylko dla modeli ze stałym budżetem. Model: „Sonnet handles most coding tasks well … Reserve Opus for
  complex architectural decisions”; `model: haiku` dla prostych subagentów; przełączenie na Opus dziedziczą
  subagenci. Okno 1M: „standard model pricing with no premium for tokens beyond 200K, except on Haiku 5.5,
  which costs more on prompts longer than 100K tokens”.
- **Mechanizm:** myślenie to wyjście (najdroższa klasa tokenów); effort i wybór modelu per etap są
  największą pojedynczą gałką kosztu po rozmiarze kontekstu.
- **Dla nas:** potwierdza zasadę z CLAUDE.md (Haiku/Sonnet do mechaniki, Fable do projektowania); w kernelu
  effort / budżet myślenia to ustawienie per przepływ i per klasa zadania (otwarta wartość, presety jako
  przykłady). Dotyczy: koordynator, agenci workflow, kernel, właściciel na telefonie (koszt widoczny).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** brak liczb „ile oszczędza `medium` vs `high`” — dokumentacja nie podaje.

## prac-12 — Filtrowanie wyjścia narzędzi przed modelem: hook PreToolUse, log 10 000 linii → setki tokenów

- **Źródło:** https://code.claude.com/docs/en/costs (sekcja „Offload processing to hooks and skills”),
  dostęp 2026-10-23 — oficjalna, **high**. Wzmianki pomocnicze (nieprzeczytane bezpośrednio, tylko snippety
  wyszukiwarki): Firecrawl „Filter tool output before Claude reads it: 80-99% compression on build and test
  logs” (vendor_blog, **low**); autoscout24, S. Shynkarenko, 2026-06-22, technika 1: filtrowanie rutynowego
  szumu (np. `git status` z 80 liniami niewersjonowanych plików) (practitioner_writeup, **low** — strona
  zablokowana).
- **Twierdzenie:** „Instead of Claude reading a 10,000-line log file to find errors, a hook can grep for
  `ERROR` and return only matching lines, reducing context from tens of thousands of tokens to hundreds.”
  Przykład w dokumentacji: hook PreToolUse na `Bash` przepisuje `npm test|pytest|go test` na
  `… 2>&1 | grep -A 5 -E '(FAIL|ERROR|error:)' | head -100`. Dodatkowo: pluginy code intelligence
  („A single ‘go to definition’ call replaces what might otherwise be a grep followed by reading multiple
  candidate files”), CLI (`gh`, `aws`) zamiast MCP („don't add any per-tool listing”), definicje MCP
  odroczone domyślnie (tool search).
- **Mechanizm:** tokeny wyjścia narzędzi wchodzą do historii na stałe i są płacone w każdej turze; cięcie u
  źródła działa mnożnikowo.
- **Dla nas:** w `tools/workflows/*` i w kernelu: wrapper na komendy testowe/budowy, który zwraca tylko
  porażki + N linii kontekstu; filtr jako dane (regex per komenda), z przełącznikiem „pokaż całość”.
  Dotyczy: agenci workflow, koordynator, kernel (narzędzia aplikacji).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** liczba „80–99 %” pochodzi z blogu dostawcy, bez metody — traktować jako hipotezę do własnego
  pomiaru.

## prac-13 — Tryb bezgłowy i fan-out: `claude -p` w pętli, `--allowedTools`, `/batch` 5–30 subagentów

- **Źródło:** https://code.claude.com/docs/en/best-practices, dostęp 2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** `claude -p "…" --output-format json|stream-json`; pętla po liście plików z
  `--allowedTools "Edit,Bash(git commit *)" --permission-mode dontAsk`, test na 2–3 plikach przed całością;
  `/batch <instruction>` dzieli zmianę na „5 to 30 subagents. Each subagent works in its own worktree”;
  wzorzec Writer/Reviewer w dwóch sesjach („A fresh context improves code review since Claude won't be biased
  toward code it just wrote”); „Let Claude interview you … write a complete spec to SPEC.md … start a fresh
  session to execute it”. Przegląd adwersarialny w subagencie: „sees only the diff and the criteria you give
  it, not the reasoning that produced the change”; ostrzeżenie: recenzent proszony o luki zawsze je znajdzie
  — ograniczyć do poprawności i wymagań.
- **Mechanizm:** każdy element listy dostaje świeży, mały kontekst (koszt liniowy w liczbie elementów, nie
  kwadratowy w długości); specyfikacja w pliku odcina koszt „rozmowy o wymaganiach” od kosztu implementacji.
- **Dla nas:** nasz rozdział badanie → projekt → pakiety (waste control 6a) jest tym wzorcem; brakuje nam
  jawnego kroku „spec w pliku, potem świeży agent”. Dotyczy: koordynator, agenci workflow.
- **Kaliber:** medium. **Wysiłek:** small.
- **Uwagi:** brak liczb kosztu per plik.

## prac-14 — Dynamiczne workflow: wyniki pośrednie w zmiennych skryptu, nie w kontekście; stagger 5 s dla cache

- **Źródło:** https://code.claude.com/docs/en/workflows — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high**.
- **Twierdzenie:** „A workflow script holds the loop, the branching, and the intermediate results itself, so
  Claude's context holds only the final answer.” Fan-out: „Claude Code holds all but the first until the first
  agent's response begins, then releases the held agents together so their first requests read the shared
  prefix” — limit `CLAUDE_CODE_WORKFLOW_PREFIX_STAGGER_MS` = 5000 ms domyślnie; warunek wspólnego
  przedrostka: ten sam model, effort, typ agenta, narzędzia, schemat wyjścia i katalog roboczy. TTL agentów
  workflow: 5 min także na subskrypcji (`subagentPromptCacheTtl: 1h` wydłuża, droższy zapis). Ostrzeżenie
  „Large workflow” przy > 25 agentach lub prognozie > 1,5 mln tokenów (doradcze, nie blokuje); 16 agentów
  równolegle domyślnie (1–256); do 1 000 agentów na przebieg, 4 096 elementów w `pipeline()`/`parallel()`;
  size guideline `small` (< 5) / `medium` (< 10) / `large` (< 50). Wznowienie: ukończeni agenci zwracają
  zapisane wyniki; „The first agent whose prompt differs from the previous run … runs again, and so does every
  agent after it”; `Date.now()` i `Math.random()` rzucają w skrypcie, żeby replay był deterministyczny.
  Ultracode (`/effort ultracode`) planuje workflow dla każdego zadania — „each request uses more tokens”.
- **Mechanizm:** orkiestracja w kodzie zamiast w kontekście modelu usuwa koszt „koordynator czyta wszystkie
  wyniki w każdej turze”; współdzielony przedrostek zamienia N zapisów cache na 1 zapis + N−1 odczytów.
- **Dla nas:** to dokładnie nasze `tools/workflows/*.js`: (a) reguła „nigdy nie edytuj pierwszego `agent()`,
  jeśli późniejsze wyniki mają zostać” ma potwierdzenie w dokumentacji; (b) nasze agenty w jednym workflow
  mogłyby dzielić przedrostek, jeśli brief po CLAUDE.md jest identyczny do pewnego punktu — dziś briefy
  różnią się od pierwszej linii, więc stagger nic nie daje; warto układać brief „część wspólna → część
  własna”. Dotyczy: koordynator, agenci workflow, narzędzia spec (generator briefów).
- **Kaliber:** large. **Wysiłek:** small (układ briefu), medium (TTL agentów jako ustawienie).
- **Uwagi:** limity liczbowe to presety produktu, u nas konfigurowalne (R58).

## prac-15 — Dlaczego użycie rośnie w długiej sesji: pełny kontekst przy każdym wyzwoleniu w tle

- **Źródło:** https://code.claude.com/docs/en/costs (sekcja „Why usage climbs in a long session”), dostęp
  2026-10-23. Wiarygodność **high**.
- **Twierdzenie:** „Claude Code sends your full conversation with every request, and each time Claude uses
  tools it sends another request carrying that batch of tool results … a one-line question in a session that
  has been open all day still draws usage for the whole conversation”; pierwszy komunikat po przerwie > TTL
  „misses the cache and reprocesses your full context”; zadania zaplanowane „fires on its interval even while
  the session is idle, sending your full context each time”; wiadomości między sesjami „as a new turn … sending
  your full context each time” (`crossSessionInbound: hold`); check-iny celu `/goal` — „at most three idle
  check-ins per goal” (przed v2.1.246 bez limitu; `CLAUDE_CODE_GOAL_CHECKIN_MINUTES=0` wyłącza). `/usage`
  rekomenduje zmianę dla zachowań ≥ 10 % użycia.
- **Mechanizm:** koszt tury = rozmiar całego kontekstu × stawka (odczyt lub pełna); każde automatyczne
  „obudzenie” sesji z dużym kontekstem to pełna tura.
- **Dla nas:** koordynator budzony przez `send_later` / pętle / wiadomości od agentów płaci cały swój kontekst
  za każde obudzenie — stąd nasz pomiar 17 % wydatku na przepisywanie kontekstu po pauzach (prac-28). Reguła:
  budzić koordynatora rzadko i hurtem; w aplikacji — automatyzacje (K3/graf) nie powinny wysyłać pełnej
  historii rozmowy, lecz własny mały kontekst. Dotyczy: koordynator, kernel (automatyzacje), właściciel na
  telefonie (koszt przerw).
- **Kaliber:** medium. **Wysiłek:** small.
- **Uwagi:** limity (3 check-iny) to presety produktu, datowane wersją.

## prac-16 — Anthropic „Effective context engineering”: kompaktowanie, notatki poza kontekstem, subagenci 1–2k tokenów

- **Źródło:** https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents — blog
  inżynieryjny producenta, 2025-09-29. Wiarygodność **high** (źródło pierwotne; brak pomiarów liczbowych
  poza przykładami).
- **Twierdzenie:** kompaktowanie: „Start by maximizing recall to ensure your compaction prompt captures every
  relevant piece”; „One of the safest lightest touch forms of compaction is tool result clearing”; Claude Code
  po kompaktowaniu kontynuuje z „the five most recently accessed files”. Notatki strukturalne: „the agent
  regularly writes notes persisted to memory outside of the context window” (NOTES.md; Pokémon: „for the last
  1,234 steps I've been training my Pokémon in Route 1”). Subagenci: każdy „might explore extensively, using
  tens of thousands of tokens or more”, zwraca „only a condensed, distilled summary of its work (often
  1,000-2,000 tokens)”. Just-in-time: lekkie odwołania („file paths, stored queries, web links”) ładowane
  narzędziami w razie potrzeby; CLAUDE.md z góry + glob/grep na żądanie. System prompt na „right altitude”;
  narzędzia bez nakładania się; mało, ale kanonicznych przykładów few-shot.
- **Mechanizm:** trzy kanały długiego horyzontu (streszczenie, pamięć plikowa, delegacja) wymieniają koszt
  kontekstu na koszt zapisu/odczytu plików i na krótki wynik podrzędnego agenta.
- **Dla nas:** nasz model „karta wznowienia + `git log`” to „structured note-taking”; wynik agenta ma być
  rzędu 1–2k tokenów (dziś StructuredOutput bywa dłuższy); czyszczenie wyników narzędzi jako pierwszy, tani
  krok przed streszczaniem — do zrobienia w kernelu dla rozmów użytkownika. Dotyczy: koordynator, agenci
  workflow, kernel, interfejs współpracy (pokazywać, co zostało wycięte).
- **Kaliber:** large (architektura). **Wysiłek:** medium.
- **Uwagi:** brak liczb „przed/po”.

## prac-17 — Anthropic „Multi-agent research system”: agenci ×4, wieloagentowe ×15, tokeny tłumaczą 80 % wariancji

- **Źródło:** https://www.anthropic.com/engineering/multi-agent-research-system — blog inżynieryjny,
  2025-06-13. Wiarygodność **high** (dane własne producenta; metoda szczegółowo nieopisana).
- **Twierdzenie:** „agents typically use about 4× more tokens than chat interactions, and multi-agent systems
  use about 15× more tokens than chats”; w BrowseComp „token usage by itself explains 80% of the variance”;
  „multi-agent systems require tasks where the value of the task is high enough to pay for the increased
  performance”. Reguły skalowania wysiłku w promptach lidera: „Simple fact-finding requires just 1 agent with
  3-10 tool calls … direct comparisons might need 2-4 subagents with 10-15 calls each … complex research might
  use more than 10 subagents”. Każdy subagent dostaje „an objective, an output format, guidance on the tools
  and sources to use, and clear task boundaries”. Kontekst: „agents summarize completed work phases and store
  essential information in external memory”, przy końcu okna „spawn fresh subagents with clean contexts”.
  Równoległe wywołania narzędzi „cut research time by up to 90% for complex queries”.
- **Mechanizm:** liczba tokenów jest głównym predyktorem jakości — oszczędzanie ma sens tylko tam, gdzie
  tokeny nie przekładają się na wynik (powtórzenia, szum); wysiłek skalowany jawnie z trudnością zadania.
- **Dla nas:** reguły skalowania (ile agentów, ile wywołań) jako dane w briefie koordynatora — presety, nie
  limity (R58); mnożniki ×4/×15 do kalibracji progu ×10 właściciela. Dotyczy: koordynator, agenci workflow,
  kernel (przepływy wieloagentowe w grafie).
- **Kaliber:** large. **Wysiłek:** small (reguły), large (architektura w kernelu).
- **Uwagi:** liczby z systemu badawczego, nie kodującego; przeniesienie na kodowanie jest założeniem.

## prac-18 — Pomiar użytkownika Claude Code: 5,09 mld odczytów z cache na 3,89 mln tokenów pracy (1 310 : 1, 99,93 %)

- **Źródło:** https://github.com/anthropics/claude-code/issues/24147 — issue GitHub „Cache read tokens
  consume 99.93% of usage quota…”, autor RedhatEnt, otwarte 2026-02-08, otwarte nadal. Typ: forum /
  practitioner. Wiarygodność **medium**: liczby z własnego skryptu na transkryptach JSONL (metoda opisana),
  próba jednej osoby, bez kontroli; komentarze innych osób — pojedyncze obserwacje.
- **Twierdzenie:** 30 dni (9 I – 8 II 2026): I/O 3 887 759; odczyty z cache 5 092 500 074; zapisy cache
  176 498 498; stosunek 1 310 : 1; odczyty = 99,93 % wszystkich tokenów. Tydzień 1–8 II (762 sesje): 21 047
  wiadomości API, wejście 515 830, wyjście 401 908, zapis cache 133 643 608, odczyt 1 914 841 217 (93,43 %;
  2 086 : 1). Komentarz (SDpower, ccusage_go): odczyty z cache = 97,7 % „kosztu sesji”. Komentarz (Bedrock):
  wydatek dzienny 10–15 → 60–100 USD, pojedyncze wiadomości z 1–2 wywołaniami 4–10 USD. Wniosek autora:
  „quota consumption scales with instruction-file size and message count, not with workload”; prośba o
  wyłączenie odczytów z limitu, niższą wagę albo delty/ładowanie na żądanie. Kontrargument w wątku: odczyty
  z cache są tanie w USD, ale liczą się do okien subskrypcji.
- **Mechanizm:** architektura „wyślij wszystko w każdej turze” daje liniowy koszt na turę i kwadratowy na
  sesję; cache obniża cenę jednostkową, nie liczbę tokenów.
- **Dla nas:** potwierdza naszą obserwację „98 % odczytów w pięciu długich agentach” (prac-28) z zewnątrz;
  profil ekonomii musi rozróżniać dwie waluty: USD (odczyt 0,1×) i **okno limitu subskrypcji** (waga odczytu
  nieznana, zmienna w czasie — komentarz z IX 2026 o zmianie 5–7×). Dotyczy: koordynator, profil ekonomii,
  właściciel (jego okno 5 h / tydzień).
- **Kaliber:** medium (pomiar), small (metoda). **Wysiłek:** small (powtórzyć skrypt na naszych logach).
- **Uwagi:** liczba „1,47 USD vs 64,98 USD (44×)” w komentarzu jest niejasna co do definicji — nie używać bez
  weryfikacji.

## prac-19 — Aider: cache obejmuje system, mapę repo, pliki read-only i pliki czatu; pingi utrzymujące co 5 min

- **Źródło:** https://raw.githubusercontent.com/Aider-AI/aider/main/aider/website/docs/usage/caching.md
  (dokumentacja oficjalna aidera, kopia w repozytorium; `aider.chat` zablokowany), dostęp 2026-10-23.
  Wiarygodność **high** (dokument producenta), ale strona jest stara (przykład z claude-3-5-sonnet, „Anthropic
  provides caching for Sonnet and Haiku”).
- **Twierdzenie:** `--cache-prompts` włącza cache dla: system promptu, plików `--read` / `/read-only`, mapy
  repozytorium, plików edytowalnych w czacie. „By default, Anthropic keeps your cache for 5 minutes.”
  `--cache-keepalive-pings N`: „Aider will ping up to `N` times over a period of `N*5` minutes after each message
  you send.” „Due to limitations in the provider APIs, caching statistics and costs are not available when
  streaming responses” (`--no-stream`).
- **Mechanizm:** stały przedrostek (system + mapa + pliki) jest cache'owany; pingi płacą małe odczyty, by nie
  zapłacić pełnego zapisu po przerwie użytkownika.
- **Dla nas:** wzorzec „ping utrzymujący” jako ustawienie przepływu (liczba pingów, odstęp = TTL) — w API
  Anthropic dostępny dziś także jako `max_tokens: 0` (prac-03); kalkulacja opłacalności: ping kosztuje odczyt
  całego przedrostka (0,1×), zapis po wygaśnięciu 1,25× — ping opłaca się, gdy prawdopodobieństwo powrotu
  w oknie × koszt zapisu > koszt pingu. Dotyczy: koordynator, kernel.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** brak liczb oszczędności na stronie; informacja o statystykach przy streamingu może być
  nieaktualna (API dziś zwraca pola cache w strumieniu).

## prac-20 — Aider: mapa repozytorium — sygnatury zamiast plików, budżet 1k tokenów, ranking grafowy, rozmiar dynamiczny

- **Źródło:** https://raw.githubusercontent.com/Aider-AI/aider/main/aider/website/docs/repomap.md
  (oficjalna dokumentacja aidera, kopia w repo), dostęp 2026-10-23 — **high**. Uzupełnienie (tylko snippet
  wyszukiwarki, nieprzeczytane): issue https://github.com/Aider-AI/aider/issues/752 (2024, v0.40.6): przy
  `--map-tokens 1024` `/tokens` pokazywał > 16 000 tokenów mapy — forum, **low** (stara wersja).
- **Twierdzenie:** mapa to „concise map of your whole git repository” z kluczowymi klasami/funkcjami i „their
  types and call signatures”, wysyłana z każdym żądaniem; dla dużych repo ranking „using a graph ranking
  algorithm” (pliki = węzły, zależności = krawędzie), wybór „the most important parts of the codebase which
  will fit into the active token budget”; „The token budget is influenced by the `--map-tokens` switch, which
  defaults to 1k tokens”; „Aider adjusts the size of the repo map dynamically based on the state of the chat …
  it does expand the repo map significantly at times, especially when no files have been added to the chat.”
  Model może poprosić o konkretne pliki, aider proponuje ich dodanie.
- **Mechanizm:** stały, mały, rankingowany indeks zamiast odczytów całych plików — model sam „zamawia” resztę;
  koszt stały na turę (mapa) zamiast kosztu zmiennego (pełne pliki).
- **Dla nas:** odpowiednik dla naszego grafu wiedzy / selektora kontekstu (K3): „mapa” węzłów (etykiety,
  rodzaje, sygnatury) z budżetem tokenów i rankingiem (PageRank po krawędziach) jako warstwa przed
  treścią; rozmiar budżetu jako otwarte ustawienie z adaptacją (więcej mapy, gdy mało treści w kontekście).
  Dotyczy: kernel (selektor K3), interfejs współpracy („co poszło do modelu”).
- **Kaliber:** medium. **Wysiłek:** medium.
- **Uwagi:** strona nie porównuje liczbowo mapy z wysyłaniem całych plików; przekroczenia budżetu zgłaszane
  w issue — budżet jest celem, nie twardym limitem.

## prac-21 — Codex CLI: kompaktowanie jako „CONTEXT CHECKPOINT COMPACTION”, kontekst początkowy + ostatnie 20k + streszczenie

- **Źródło:** https://gist.github.com/badlogic/cd2ef65b0697c4dbe2d13fbecb0a0a5f — gist Mario Zechnera
  (badlogic), 2025-12-02, porównanie kompaktowania w Claude Code, Codex CLI, OpenCode i Amp na podstawie kodu
  źródłowego (`codex-rs/core/src/compact.rs`). Typ: practitioner_writeup. Wiarygodność **medium-high**: analiza
  kodu z cytatami, ale snapshot z grudnia 2025. Oficjalny opis ustawień (`model_context_window`,
  `model_auto_compact_token_limit`) widziałem tylko w snippecie wyszukiwarki z
  https://developers.openai.com/codex/config-reference (strona zablokowana) — **medium**; blogi o „90 %
  clamp” i domyślnych progach (258 400 / 244 800) — **low**, nieoficjalne.
- **Twierdzenie:** `/compact` ręcznie, auto gdy użycie przekroczy `model_auto_compact_token_limit` („180k for
  some and 244k for others”); nowa historia = „initial context + recent user messages (up to 20k tokens) +
  summary”; prompt: „You are performing a CONTEXT CHECKPOINT COMPACTION. Create a handoff summary for another
  LLM that will resume the task” — postęp i decyzje, ograniczenia i preferencje użytkownika, pozostałe kroki,
  krytyczne dane/odwołania; prefiks w nowym kontekście: „Another language model started to solve this problem
  and produced a summary of its thinking process.” Margines `effective_context_window_percent` 95 %; retry z
  backoffem; ostrzeżenie z kodu: „Long conversations and multiple compactions can cause the model to be less
  accurate.” Z wyszukiwania: dwie ścieżki — lokalna (streszczenie tekstowe, dowolny dostawca) i zdalna
  (`POST /responses/compact`, zaszyfrowany blob) — tylko blog, **low**.
- **Mechanizm:** streszczenie pisane jako przekazanie zadania innemu modelowi (handoff), z zachowaniem
  ostatnich wiadomości użytkownika dosłownie — intencja nie przechodzi przez streszczenie.
- **Dla nas:** szablon naszej karty wznowienia może przyjąć te cztery sekcje + „ostatnie słowa właściciela
  dosłownie” (mamy to już w `spec.json`/OWNER_REQUIREMENTS — spójne). Dotyczy: koordynator, agenci
  workflow, kernel (kompaktowanie rozmów użytkownika).
- **Kaliber:** medium. **Wysiłek:** small.
- **Uwagi:** progi liczbowe zależą od modelu i wersji; nie przeczytałem oficjalnej strony konfiguracji.

## prac-22 — Cline: kompaktowanie przy zbliżaniu do limitu, `.clineignore`, `new_task` jako przekazanie, checkpoint po każdym narzędziu

- **Źródło:** podsumowania wyszukiwarki dla https://docs.cline.bot/getting-started/understanding-context-management,
  https://docs.cline.bot/features/checkpoints, https://docs.cline.bot/core-workflows/task-management,
  https://cline.bot/blog/clines-context-window-explained-maximize-performance-minimize-cost i
  https://cline.bot/blog/unlocking-persistent-memory-how-clines-new_task-tool-eliminates-context-window-limitations
  (wszystkie zablokowane dla odczytu; dostęp do snippetów 2026-10-23). Typ: official_docs / vendor_blog.
  Wiarygodność **medium** dla faktów o funkcjach (dokumentacja producenta, ale przez snippet), **low** dla
  progów procentowych (dwa różne w dwóch wpisach tego samego dostawcy).
- **Twierdzenie:** gdy okno zbliża się do limitu, Cline „automatically compresses older conversation”;
  zalecenia: wąski zakres zadania, `.clineignore` „to cut baseline token use”, ustawienie Auto-Compact
  (Settings → Features); checkpointy „after each tool use … saved as snapshots in a shadow Git repository”
  z porównaniem i przywracaniem; narzędzie `new_task` przenosi wybrany kontekst do nowego zadania. Progi
  degradacji wg blogów Cline: „roughly 70-80% full” w jednym wpisie, „past about 50% usage” w drugim.
- **Mechanizm:** ignorowanie plików zmniejsza stały przedrostek; przekazanie do nowego zadania zastępuje
  streszczanie całej historii wyborem, co zabrać; checkpointy w cieniu gita umożliwiają cofnięcie bez
  kosztu kontekstu.
- **Dla nas:** `.clineignore` ≈ nasz filtr „co nigdy nie idzie do modelu” (mamy wymaganie `secrets_in_chat`);
  „shadow git” to ten sam pomysł, co nasz checkpoint w worktree po każdym pakiecie; progi 50/70–80 % jako
  presety alarmu, nie reguły. Dotyczy: kernel, koordynator, interfejs współpracy (pasek zajętości okna).
- **Kaliber:** medium. **Wysiłek:** small–medium.
- **Uwagi:** żadnej strony nie przeczytałem w całości; liczby z blogów dostawcy bez metody.

## prac-23 — Manus: KV-cache hit rate jako główna metryka, wejście : wyjście ≈ 100 : 1, 0,30 vs 3 USD (10×)

- **Źródło:** wpis „Context Engineering for AI Agents: Lessons from Building Manus” (manus.im/blog, 2025;
  strona zablokowana) — cytaty z podsumowania wyszukiwarki i mirrorów (https://agentic-ai.readthedocs.io/en/latest/ContextEngineering/manus/,
  https://dev.to/contextspace_/context-engineering-for-ai-agents-key-lessons-from-manus-1lgb), dostęp
  2026-10-23. Typ: vendor_blog / practitioner_writeup. Wiarygodność **medium** (cytaty dosłowne z pierwotnego
  tekstu, ale nieprzeczytane u źródła; część lekcji tylko w streszczeniu wtórnym).
- **Twierdzenie:** „If I had to choose just one metric, I'd argue that the KV-cache hit rate is the single most
  important metric for a production-stage AI agent.” „In Manus … the average input-to-output token ratio is
  around 100:1.” „cached input tokens cost 0.30 USD/MTok, while uncached ones cost 3 USD/MTok—a 10x
  difference.” „even a single-token difference can invalidate the cache from that token onward”; typowy błąd:
  „including a timestamp—especially one precise to the second—at the beginning of the system prompt”.
  Z mirrorów: deterministyczna serializacja JSON; maskowanie narzędzi (logit mask) zamiast ich usuwania;
  system plików jako kontekst; recytacja zadania przez `todo.md`; zachowanie błędów w kontekście (te trzy
  ostatnie — tylko streszczenie, **low**).
- **Mechanizm:** w agencie prawie całe tokeny to wejście; wtedy mnożnik cache (10×) decyduje o rachunku
  bardziej niż jakakolwiek terseness wyjścia.
- **Dla nas:** metryka „udział tokenów z cache” jako pierwszy wskaźnik w profilu ekonomii i w `/usage`-podobnym
  widoku aplikacji; maskowanie zamiast usuwania narzędzi — odpowiednik w kernelu: stała lista narzędzi w
  przedrostku, dostępność sterowana poza promptem (tool_choice / reguły). Dotyczy: kernel, koordynator,
  interfejs współpracy.
- **Kaliber:** large (architektura produkcyjna). **Wysiłek:** medium.
- **Uwagi:** ceny z 2025 (Sonnet 3/0,30) — datowane; stosunek 100 : 1 dotyczy Manus, nie kodowania.

## prac-24 — Pętla „Ralph Wiggum”: świeży kontekst w każdej iteracji, stan w plikach i w gicie

- **Źródło:** podsumowania wyszukiwarki (strona autora https://ghuntley.com/ralph/ zablokowana):
  https://codecentric.de/en/knowledge-hub/blog/the-ralph-wiggum-loop-autonomous-code-generation-with-a-fresh-context,
  https://www.dreamhost.com/blog/ralph-wiggum/, https://stevekinney.com/writing/the-ralph-loop, dostęp
  2026-10-23. Typ: practitioner_writeup / forum. Wiarygodność **low**: nie przeczytałem żadnej strony;
  jedyna liczba (kontrakt 50 000 USD za 297 USD API, wg wpisu Huntleya na X z 2025-07-11) jest z drugiej ręki
  i niezweryfikowana.
- **Twierdzenie:** pętla bash uruchamia agenta od zera z tym samym promptem; „the agent's scratch space is
  reset between iterations, while the durable state persists on disk” (plan.md / progress.txt lub TASKS.md /
  PROGRESS.md); motywacja: „details get lost when the LLM has to summarize the context (compacting)”;
  działa najlepiej dla „mechanical, well-defined tasks with automatic verification”; późniejsze narzędzia
  (ralph-orchestrator) dodały limity wydatków, bezpieczniki i checkpointy w gicie.
- **Mechanizm:** zamiast kwadratowego kosztu jednej długiej sesji — wiele krótkich o stałym przedrostku;
  pamięć to pliki, których model czyta tyle, ile potrzebuje.
- **Dla nas:** to nasz model „restart co N ruchów z kartą wznowienia” (prac-28, ok. 50 % mniej odczytów przy
  N ≈ 50) doprowadzony do skrajności (N = 1 zadanie); warunek — automatyczna weryfikacja (gate), którą mamy.
  Dotyczy: koordynator, agenci workflow.
- **Kaliber:** small (skrypt) / experimental. **Wysiłek:** small.
- **Uwagi:** brak kontrolowanych pomiarów; przykłady to projekty od zera ze specyfikacją.

## prac-25 — Cursor Max Mode: 200k domyślnie, większe okno za dopłatą; do 200 wywołań narzędzi i 750 linii na odczyt

- **Źródło:** podsumowanie wyszukiwarki dla https://docs.cursor.com/context/max-mode i
  https://cursor.com/help/ai-features/max-mode.md (zablokowane) oraz wątków forum
  https://forum.cursor.com/t/max-mode-vs-non-max-mode-context-max-not-thinking-max/154215, dostęp 2026-10-23.
  Typ: official_docs (snippet) / forum. Wiarygodność **medium** dla limitów, **low** dla cen (trzy sprzeczne
  wersje: 1,2× stawki API; stawka API; +20 % na starych planach).
- **Twierdzenie:** „Normally, Cursor uses a context window of 200k tokens, and Max Mode extends it to the
  maximum available” dla części modeli; Max Mode: „up to 200 tool calls, and a read-file limit of 750 lines”;
  „a bit slower and more expensive”.
- **Mechanizm:** produkt sprzedaje większe okno jako osobny tryb z dopłatą — jawny sygnał kosztu dla
  użytkownika zamiast cichego rośnięcia kontekstu.
- **Dla nas:** wzorzec interfejsu: tryb „szerzej/głębiej” z jawną etykietą kosztu (mamy modyfikatory
  szerzej/głębiej w selektorze — brakuje etykiety kosztu obok). Dotyczy: interfejs współpracy, właściciel na
  telefonie.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** nieprzeczytane u źródła; ceny nierozstrzygnięte.

## prac-26 — Devin: sesja 42 ACU zamiast ~12 — koszt eksploracji repo, naprawiany lepszym promptem / wiedzą

- **Źródło:** podsumowanie wyszukiwarki dla https://docs.devin.ai/pt-BR/use-cases/gallery/analyze-session-acu-efficiency
  (wersje tłumaczone; wersja angielska i cała domena zablokowane), dostęp 2026-10-23. Typ: official_docs
  (snippet). Wiarygodność **medium** (dokument producenta, przykład ilustracyjny); „ACU ≈ 15 minut aktywnej
  pracy” — źródło trzecie (https://www.fast.io/resources/devin-acu-guide.md), **low**.
- **Twierdzenie:** przykład sesji za 42 ACU, oczekiwanej na ~12; Session Insights (darmowe dla każdej
  ukończonej sesji) rozbija koszt na fazy; naprawa: prompt z kontekstem repozytorium, który pozwala pominąć
  eksplorację 23 plików i od razu wybrać właściwy typ paginacji. Jednostka rozliczeniowa to ACU (czas i liczba
  akcji), nie tokeny.
- **Mechanizm:** brakujący kontekst wejściowy = koszt eksploracji (odczyty, próby); jednostka „ACU” ukrywa
  tokeny, ale raport per faza pokazuje, gdzie poszedł budżet.
- **Dla nas:** „Session Insights per faza” = nasz postulat „profil ekonomii per przebieg / model / narzędzie”;
  dodatkowo: szacunek oczekiwany vs rzeczywisty jako advisory (×3,5 w przykładzie). Jednostka inna niż token
  — profil musi być agnostyczny co do jednostki (token, ACU, premium request, minuta). Dotyczy: profil
  ekonomii, interfejs współpracy.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** nieprzeczytane u źródła; brak publicznej ceny ACU.

## prac-27 — GitHub Copilot: agent kodujący rozliczany „jedna premium request na sesję”; osobne SKU od 2025-11-01

- **Źródło:** podsumowanie wyszukiwarki dla https://docs.github.com/en/billing/concepts/product-billing/github-copilot-premium-requests
  (zablokowane; kopia w repo github/docs pod zgadywaną ścieżką — 404) i dyskusji
  https://github.com/orgs/community/discussions/165798, dostęp 2026-10-23. Typ: official_docs (snippet) /
  forum. Wiarygodność **low-medium**: fakt o SKU z dokumentacji (snippet), „1 request na sesję” z dyskusji
  społeczności, cena 0,04 USD i zmiana na „AI credits” (VI 2026) tylko ze źródeł trzecich, niepotwierdzone.
- **Twierdzenie:** „Premium requests for Spark and Copilot coding agent are tracked in dedicated SKUs from
  November 1, 2025”; wg społeczności „a full session only consumes one premium request, while Copilot Chat
  still consumes one premium request per message”; nadwyżki wg źródeł trzecich 0,04 USD/request; jedno źródło
  twierdzi, że od 2026-06-01 plany miesięczne liczą tokeny jako „AI Credits”.
- **Mechanizm:** jednostka „sesja” przenosi ryzyko długości kontekstu na dostawcę; użytkownik nie ma bodźca
  do oszczędzania tokenów w sesji agenta — odwrotnie niż przy API.
- **Dla nas:** kolejna jednostka rozliczeniowa (sesja) do profilu agnostycznego (por. prac-26); dla
  właściciela: wybór dostawcy/planu zmienia, czego w ogóle warto oszczędzać. Dotyczy: profil ekonomii.
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** cennik zmienny w czasie — ściśle datowana obserwacja; nieprzeczytane u źródła.

## prac-28 — Nasz własny pomiar (S1, 2026-10-21): przedrostek ≈ 60k, 98 % odczytów w pięciu długich agentach, 24 % wydatku to nie praca

- **Źródło:** `docs/reports/INDEX.md` (sekcja „S1 — kontrola marnotrawstwa”, 2026-10-21, linie ~1700–1725),
  `CLAUDE.md` (Working with subagents, waste control 5), `docs/reports/LIMITS_LEDGER.md`, `tools/usage_report.py`.
  Typ: our_own_measurement. Wiarygodność **medium-high** dla pomiarów (nasze logi agentów workflow K0 i
  zdarzenia limitów), **low** dla części „model” (symulacja z założeniami, nie pomiar).
- **Twierdzenie (pomiar):** „Stały przedrostek świeżego agenta ≈ 60 tys. tokenów (≈ 40,5 tys. z cache +
  ≈ 19,5 tys. nowych)”; agent `impl` 501 ruchów, średnio 365 tys. kontekstu, 183 mln odczytów z cache;
  `fix:r1` 549 ruchów i 214 mln; recenzenci 28–38 mln każdy; 30 agentów „refute” (3–18 ruchów) razem ≈ 9 mln;
  „98 % odczytów całego workflow siedziało w pięciu długich agentach, nie w liczbie agentów”; kontekst rósł
  1,2–1,5 tys./ruch (agent W0: ≈ 4 tys./ruch, 276 tys. po 54 ruchach i zero commitów). Z CLAUDE.md
  (2026-10-21, `tools/usage_report.py`): „24 % of agent spend was not work — 7 % the start of each agent
  (prompt prefix), 17 % whole contexts rewritten after a pause over 5 minutes”; 2026-10-10: pięć równoległych
  agentów zużyło okno w 8 minut (57,5 mln odczytów z cache, 1,34 mln zapisów). **Model (nie pomiar):**
  restart agenta co N ruchów z kartą wznowienia (25 tys.) obniża „równoważniki odczytów” o 35 % (N = 150),
  45 % (100), ok. 50 % (25–75); założenie zapis 1 h = 20 × odczyt.
- **Mechanizm:** koszt agenta ∝ (długość)²; przerwa > TTL przepisuje cały kontekst; stały przedrostek
  mnoży się przez liczbę agentów.
- **Dla nas:** to jest nasz punkt odniesienia do testowania wszystkiego powyżej: (a) skrypt z prac-18 na
  naszych JSONL, (b) powtórzenie pomiaru po wprowadzeniu małych pakietów i kart, (c) weryfikacja „modelu N”
  na pierwszym pełnym przebiegu pakietowym (zapowiedziana w INDEX.md, nieodnotowana jako wykonana).
  Dotyczy: koordynator, agenci workflow, profil ekonomii.
- **Kaliber:** medium. **Wysiłek:** small (pomiar), medium (zmiana procesu).
- **Uwagi:** wagi odczytu z cache w oknie limitu subskrypcji nieznane (por. prac-18); liczby z jednego
  workflow.

---

## Czego nie znalazłem

- **Kontrolowanego pomiaru „przed/po” dla jakiejkolwiek techniki** (cache, `/compact`, subagenci, filtrowanie
  wyjścia) w agentach kodujących — ani w dokumentacji producentów, ani w zapisach praktyków. Są: tabele cen
  (prac-01), modelowe przykłady arytmetyczne (blogi dostawców, pominięte), pojedyncze pomiary własnych
  logów (prac-18, prac-28) i deklaracje bez metody (80–99 % kompresji logów). Wniosek: pomiar trzeba zrobić
  samemu, na naszych logach (`tools/usage_report.py`, skrypt z prac-18).
- **Oficjalnej dokumentacji Cline, Codex CLI (config-reference), Cursor, Copilot i Devin w pełnym tekście** —
  domeny zablokowane przez proxy sesji; mam tylko snippety wyszukiwarki (oznaczone low/medium). Kopie na
  GitHubie znalazłem tylko dla aidera.
- **Wpisu Anthropic „Lessons from building Claude Code: Prompt caching is everything”** (claude.com → claude.dev,
  zablokowane) — dokumentacja Claude Code cytuje go jako uzasadnienie plan mode, odroczonego ładowania
  narzędzi i kompaktowania (prac-06, prac-07); z podsumowań wtórnych: spadek hit rate traktowany jak incydent
  produkcyjny (próg „< 90 %” — jedno źródło wtórne, niezweryfikowane).
- **Wagi odczytu z cache w oknach limitu subskrypcji** (5 h / tydzień) — nikt nie publikuje; issue #24147
  sugeruje, że zmieniała się w czasie. Dla właściciela (subskrypcja) to ważniejsze niż cena w USD.
- **Danych o Devin Knowledge / Playbooks** i o kompaktowaniu w Gemini CLI — poza zakresem tego, co dało się
  przeczytać.
- Drugiej połowy strony API prompt caching (znaki 100 000–162 274) — mogła zawierać sekcję o pętlach
  agentowych.

## Gdzie się zatrzymałem

Po 13 wyszukiwaniach i 14 odczytach: trzy ostatnie źródła (podsumowanie wpisu Manus, snippet wpisu
autoscout24, podsumowanie wpisu „Prompt caching is everything”) potwierdzały już zebrane mechanizmy (stały
przedrostek, brak timestampu na początku, filtrowanie wyjścia, kompaktowanie z zachowaniem przedrostka) i
nie wnosiły nowych liczb. Następny krok, gdyby ścieżka miała ciąg dalszy: (1) nasz własny pomiar na JSONL
sesji (skrypt jak w prac-18) i porównanie z liczbami z 2026-10-21; (2) odczyt zablokowanych stron z maszyny
bez proxy (lista domen w „Jak szukałem”).
