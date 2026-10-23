# R89 — ścieżka „interf”: interfejsy współpracy człowiek–AI nad specyfikacją i decyzjami

- **Ścieżka:** interf (jak produkty i badania projektują stronę CZŁOWIEKA we wspólnej pracy z AI nad jedną żywą
  specyfikacją zamiast czatu: narzędzia spec-driven, interfejsy akceptacji i przeglądu, wspólne dokumenty z agentami,
  wzorce „plan, potem wykonanie”, badania nad interakcją o mieszanej inicjatywie)
- **Data:** 2026-10-23
- **Autor:** agent badawczy (Fable 5.1) w worktree `/home/user/chatadhd`
- **Zasady:** R58/R83/R84 (przykłady są przykładami, liczba na liście nie jest listą), R59 (każde ustalenie ma źródło,
  datę, typ i ocenę wiarygodności z powodem), R60 (fakt o stanie obecnym = datowana obserwacja, nie ograniczenie).
  Wszystkie liczby pochodzą z przeczytanych stron lub z abstraktów przeczytanych w całości; czego nie przeczytałem,
  oznaczam („przez podsumowanie wyszukiwarki”).

## Jak szukałem

- 13 wyszukiwań WebSearch (tryb standard), 3 zapytania Consensus (abstrakty prac recenzowanych), 3 zapytania do
  lustra arXiv na Hugging Face, ok. 25 prób odczytu stron, z czego **10 udanych**. Preset z briefu to 8–12 wyszukiwań
  i 6–10 odczytów; przekroczyłem próby odczytu, bo proxy sesji odpowiada 403 na CONNECT dla większości hostów
  (`/__agentproxy/status`: `connect_rejected … policy denial`). Zablokowane w tej ścieżce: `kiro.dev`,
  `docs.github.com`, `github.blog`, `linear.app`, `arxiv.org`, `forum.cursor.com`, `cursor.com`, `docs.devin.ai`,
  `openai.com`, `survey.stackoverflow.co`, `stackoverflow.blog`, `metr.org`, `newsletter.getdx.com`, `notion.com`,
  `lovable.dev`, `replit.discourse.group`, `atlassian.com`, `theregister.com`, `adr.github.io`, `verdent.ai`.
  Działały: `code.claude.com`, `raw.githubusercontent.com`, Hugging Face (MCP), Consensus (MCP).
- Dla stron zablokowanych korzystam z **podsumowań wyszukiwarki** (cytaty ze stron oficjalnych w wynikach), zawsze
  oznaczając to w ocenie wiarygodności („medium — oficjalne źródło, ale przez podsumowanie wyszukiwarki, strony nie
  przeczytałem”). Dokumentacja GitHub w repozytorium `github/docs` pod zgadywanymi ścieżkami dała 404 (dwie próby).
- Kolejność źródeł zgodnie z R59: dokumentacja oficjalna (Claude Code, repozytorium `github/spec-kit`, `agents.md`)
  → prace recenzowane (abstrakty przez Consensus / HF, pełnych tekstów nie czytałem) → oficjalne strony producentów
  przez podsumowania wyszukiwarki → fora i serwisy branżowe (oznaczone „low”).
- Zatrzymałem się, gdy trzy ostatnie źródła (README `agents.md`, dwa 404 na lustrze dokumentacji GitHub, strona
  `agent-view`) dodały tylko jeden nowy wzorzec (kolejka decyzji między sesjami) i poza tym potwierdzały to, co już
  miałem.
- Skala: **kaliber** = small / medium / large / scientific / experimental (rozmiar rozwiązania u źródła);
  **wysiłek** = small / medium / large (dla nas). „U nas” = jądro Loom z propozycjami (R33: wznowienie po propozycji,
  widok propozycji w web), wywiad profilowy (K2b), `spec.json` z cytatami i statusami (SPEC1), advisories (R58).

---

## interf-01 — Claude Code, tryb planowania: trzy odpowiedzi na plan, edycja planu w edytorze, akceptacja nigdy nie wygasa

- **Źródło:** https://code.claude.com/docs/en/permission-modes — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high**: strona producenta, przeczytana.
- **Twierdzenie:** „Plan mode tells Claude to research and propose changes without making them. Claude reads files,
  runs shell commands to explore, and writes a plan, but does not edit your source.” Gdy plan jest gotowy, „Claude
  presents it and asks how to proceed” z opcjami: „Yes, and use auto mode” (lub „Yes, auto-accept edits” / „Yes, and
  switch to BYPASS PERMISSIONS …” zależnie od sesji), „Yes, manually approve edits: approve and review each edit
  individually”, „No, keep planning: stay in plan mode and tell Claude what to change”. „Press `Ctrl+G` to open the
  proposed plan in your default text editor and edit it directly before Claude proceeds.” Opcja
  `showClearContextOnPlanAccept` dodaje „approves the plan and clears the planning context”. Z strony narzędzi:
  „permission prompts, including plan approval, never auto-resolve on idle”. Wejście: `Shift+Tab`, `/plan`,
  `--permission-mode plan`; `defaultMode: plan` w ustawieniach projektu.
- **Mechanizm:** odpowiedź człowieka na plan to **wybór z krótkiej listy + opcjonalna edycja tekstu planu**, nie nowa
  wiadomość czatu; jedna akceptacja przełącza tryb uprawnień na resztę sesji (koniec pytań o każdą edycję). Zmiana
  planu przed startem jest tańsza niż poprawianie wykonania (brief R89: decyzje jako dane).
- **U nas:** propozycja (R33) z trzema ścieżkami odpowiedzi jako preset: akceptuj-i-wykonuj-bez-pytań / akceptuj-i-pytaj-
  o-każdy-krok / popraw-plan; plan jako edytowalny węzeł tekstowy (edycja = wersja z diffem), nie jako komunikat.
  Opcja „wyczyść kontekst po akceptacji” to osobna gałka (oszczędza tokeny kontekstu planowania — por. prac-16).
- **Kaliber:** large (produkt). **Wysiłek:** medium.
- **Zastrzeżenia:** dokumentacja opisuje mechanizm, nie mierzy liczby tur ani oszczędności.

## interf-02 — Claude Code `AskUserQuestion`: pytania wielokrotnego wyboru z wierszem „Other”, czas na odpowiedź jako ustawienie

- **Źródło:** https://code.claude.com/docs/en/tools-reference — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high**: przeczytana.
- **Twierdzenie:** „Claude uses `AskUserQuestion` to ask you multiple-choice questions when it needs a decision or a
  clarification. Answer by picking an option, or type your own text through the `Other` row or the notes field.”
  „When you answer by typing your own text, Claude Code relays the answer with neutral wording so Claude follows what
  you wrote, including a request to wait or explain first.” „Questions stay open until you answer them” — chyba że
  użytkownik ustawi `askUserQuestionTimeout` na `60s`, `5m` lub `10m`; wtedy „the dialog closes on its own: it
  submits any options you'd already selected and tells Claude you may be away from your keyboard, so Claude proceeds
  on its own judgment and can re-ask later. You see a countdown for the last 20 seconds. Press any key to restart the
  timer. While your terminal reports that its window is focused, the timer doesn't count down.” Timer nie działa w
  sesjach w tle, w trybie czytnika ekranu i pod Remote Control. Narzędzie nie jest nigdy auto-akceptowane w żadnym
  trybie.
- **Mechanizm:** decyzja = wybór opcji (dane), tekst wolny tylko gdy człowiek chce; brak odpowiedzi nie blokuje
  (domyślnie blokuje — preset), a agent dostaje jawną informację „użytkownik mógł odejść”, więc dalej działa z
  własnym osądem i może zapytać ponownie. To wprost oszczędza tury: jedno pytanie z opcjami zamiast wymiany
  „co masz na myśli?”.
- **U nas:** wywiad profilowy (K2b) i advisories (R58) już mają pytania; brakuje presetu „czas na odpowiedź”, listy
  wartości przykładowych (60 s, 5 min, 10 min — przykłady, pole otwarte), zasady „wyślij to, co już zaznaczone”, i
  jawnego komunikatu do modelu „właściciel być może odszedł”. Zgodne z R62 (gdy właściciela nie ma, działamy).
- **Kaliber:** large. **Wysiłek:** small–medium.
- **Zastrzeżenia:** brak pomiaru, ile tur to oszczędza; „Other” pokazuje, że lista opcji nigdy nie jest zamknięta (R84).

## interf-03 — Anthropic „Let Claude interview you”: wywiad → `SPEC.md` → świeża sesja; „precyzja promptu = mniej poprawek”

- **Źródło:** https://code.claude.com/docs/en/best-practices — dokumentacja oficjalna, dostęp 2026-10-23.
  Wiarygodność **high** jako zapis zaleceń producenta; **brak pomiarów** (to porady, nie badanie).
- **Twierdzenie:** „For larger features, have Claude interview you first. Start with a minimal prompt and ask Claude
  to interview you using the `AskUserQuestion` tool.” Przykładowy prompt: „Interview me in detail … Don't ask obvious
  questions, dig into the hard parts I might not have considered. Keep interviewing until we've covered everything,
  then write a complete spec to SPEC.md.” „Once the spec is complete, start a fresh session to execute it. The new
  session has clean context focused entirely on implementation.” „The most useful specs are self-contained: they name
  the files and interfaces involved, state what is out of scope, and end with an end-to-end verification step … Time
  spent making the spec precise pays off more than time spent watching the implementation.” Oraz: „The more precise
  your instructions, the fewer corrections you'll need.”
- **Mechanizm:** rozmowa o wymaganiach i wykonanie to **dwie sesje** — kontekst wywiadu (drogi, pełen pytań) nie
  wchodzi do kontekstu wykonania; spec jest jedynym nośnikiem. To ten sam wzorzec, który właściciel opisał w
  CLAUDE.md (parafraza → `understanding` węzła spec → start pracy).
- **U nas:** wywiad jako przepływ, który kończy się węzłem spec (z `understanding`), i start wykonania w nowym
  kontekście z samym węzłem. Pytania „nieoczywiste, o trudne części” jako preset stylu wywiadu.
- **Kaliber:** medium (wzorzec pracy). **Wysiłek:** small (mamy części).
- **Zastrzeżenia:** brak liczb; wartość „opłaca się bardziej” to opinia producenta.

## interf-04 — Kiedy planować, a kiedy nie: „jeśli diff mieści się w jednym zdaniu, pomiń plan”; po dwu nieudanych poprawkach — nowa sesja

- **Źródło:** https://code.claude.com/docs/en/best-practices — j.w., **high** (zalecenia, bez pomiaru).
- **Twierdzenie:** „Plan mode is useful, but also adds overhead. For tasks where the scope is clear and the fix is
  small (like fixing a typo, adding a log line, or renaming a variable) ask Claude to do it directly. Planning is most
  useful when you're uncertain about the approach, when the change modifies multiple files, or when you're unfamiliar
  with the code being modified. If you could describe the diff in one sentence, skip the plan.” „If you've corrected
  Claude more than twice on the same issue in one session, the context is cluttered with failed approaches. Run
  `/clear` and start fresh with a more specific prompt … A clean session with a better prompt almost always
  outperforms a long session with accumulated corrections.” Sterowanie: `Esc` (stop, kontekst zostaje), `Esc Esc` /
  `/rewind` (przywróć rozmowę, kod lub oba; „Summarize from here / up to here”), „Undo that”.
- **Mechanizm:** koszt planowania jest jawny i porównywany z kosztem zadania; liczba poprawek w jednej sprawie jest
  sygnałem do zmiany strategii (nowy kontekst + lepszy prompt zamiast kolejnej tury).
- **U nas:** próg „ile poprawek tej samej sprawy, zanim zaproponować nową sesję z podsumowaniem” jako preset
  (przykład: 2) z advisory, nie reguła; przycisk „cofnij do decyzji” na osi propozycji. Oba zgodne z „waste control 6”.
- **Kaliber:** medium. **Wysiłek:** small.
- **Zastrzeżenia:** „almost always” bez danych.

## interf-05 — Weryfikacja zamyka pętlę: „bez sprawdzianu TY jesteś pętlą weryfikacji”; dowód zamiast deklaracji; recenzent zawsze coś znajdzie

- **Źródło:** https://code.claude.com/docs/en/best-practices — j.w., **high** (zalecenia).
- **Twierdzenie:** „Claude stops when the work looks done. Without a check it can run, 'looks done' is the only signal
  available, and you become the verification loop: every mistake waits for you to notice it.” Cztery stopnie:
  w jednym promptcie; `/goal` (ewaluator po każdej turze); Stop hook (deterministyczna bramka); „verification
  subagent … a fresh model try to refute the result”. „Have Claude show evidence rather than asserting success …
  Reviewing evidence is faster than re-running the verification yourself, and it works for sessions you weren't
  watching.” O recenzencie: „A reviewer prompted to find gaps will usually report some, even when the work is sound …
  Tell the reviewer to flag only gaps that affect correctness or the stated requirements, and treat the rest as
  optional.”
- **Mechanizm:** mniej tur człowieka, bo błędy łapie sprawdzian, a człowiek czyta dowód (wynik testu, zrzut) zamiast
  odtwarzać pracę; recenzja jest skalibrowana („tylko poprawność”) — inaczej generuje szum i dodatkowe tury.
- **U nas:** propozycja niesie **dowody** (wyjście testów, zrzut, polecenie i jego wynik) jako załączniki węzła; filtr
  recenzenta „tylko poprawność / wymagania” jako preset promptu recenzji. Por. interf-28 (szum recenzji automatów).
- **Kaliber:** medium. **Wysiłek:** medium.
- **Zastrzeżenia:** brak liczb.

## interf-06 — Zmęczenie zgodami: „po dziesiątej zgodzie klikasz, zamiast przeglądać” — klasyfikator, allowlisty, sandbox

- **Źródło:** https://code.claude.com/docs/en/best-practices i …/permission-modes — oficjalne, **high**.
- **Twierdzenie:** „In Manual mode, Claude Code asks before actions that might modify your system … That's safe but
  tedious. After the tenth approval you're clicking through rather than reviewing.” Remedia: „Permission allowlists”,
  „Sandboxing”, oraz tryb auto: „A separate classifier model reviews actions before they run, blocking anything that
  escalates beyond your request, targets unrecognized infrastructure, or appears driven by hostile content Claude
  read. Explicit ask rules still force a prompt.” Tabela trybów: auto — „Long tasks, reducing prompt fatigue”;
  `acceptEdits` — „Use `acceptEdits` when you want to review changes in your editor or via `git diff` after the fact
  rather than approving each edit inline.” Ostrzeżenie: „Auto mode reduces permission prompts but does not guarantee
  safety.”
- **Mechanizm:** liczba pytań do człowieka jest traktowana jako koszt o malejącej wartości (po N zgodach jakość
  decyzji spada); część decyzji przenosi się na tani model-klasyfikator i na reguły (dane), a przegląd przenosi się
  z „każdej edycji” na „diff po fakcie”.
- **U nas:** gałki: kto decyduje (człowiek / tani model / reguła) per klasa akcji, z presetami; przegląd „po fakcie”
  jako diff węzłów propozycji; licznik zgód w sesji jako sygnał advisory („dziesiąta zgoda — przełączyć na przegląd po
  fakcie?”). Wszystko konfigurowalne, nic ukrytego.
- **Kaliber:** large. **Wysiłek:** medium.
- **Zastrzeżenia:** „dziesiąta” to przykład producenta, nie pomiar.

## interf-07 — Kolejka decyzji między sesjami (agent view): „Needs input” na górze, odpowiedź cyfrą, podpowiedź pod Tab

- **Źródło:** https://code.claude.com/docs/en/agent-view — dokumentacja oficjalna (research preview), dostęp
  2026-10-23. Wiarygodność **high**: przeczytana (pierwsze 100 k znaków).
- **Twierdzenie:** „Agent view groups sessions so the ones that need input are at the top, with `Ready for review`
  and `Needs input` above `Working` and `Completed`.” Stan „Needs input”: „Claude is waiting on something only you can
  provide: an answer to a question, a permission decision, or another prompt only you can answer”. Odpowiadanie z
  jednego ekranu: „press a choice's number to fill it in, then `Enter` to send it, or type your own answer instead”;
  „When the empty input shows a suggested reply, press `Tab` to fill it in and edit it before sending”; dla okien
  uprawnień „replying doesn't answer it. Your message waits in the queue. To answer the dialog, attach with `→`.”
  Powiadomienia: „when a local background session starts needing your input, finishes, or fails”; sesje cykliczne
  „notify only when they need your input”; tytuł karty „2 awaiting input · claude agents”; stopka „← 2 agents”.
  Etykieta PR: żółta „Waiting on checks or review, or checks failed”, zielona „Checks passed and no review is
  blocking”, fioletowa — scalony. Liczby ze strony: odświeżanie stopki co ok. 10 s, wiersz najwyżej co 15 s,
  nieprzypięte skończone/oczekujące sesje zatrzymywane po ok. 1 h, limit licznika `99+`.
- **Mechanizm:** decyzje z wielu równoległych prac są **jedną kolejką** posortowaną „najpierw to, co blokuje”;
  każda pozycja ma najkrótszą możliwą odpowiedź (cyfra, Tab = przyjmij podpowiedź) — czas właściciela idzie tylko na
  decyzje, nie na czytanie postępu.
- **U nas:** właściciel czyta w zrywach (CLAUDE.md): widok „Czeka na Ciebie” jako perspektywa grafu (propozycje,
  pytania, zgody) z sortowaniem „blokujące najpierw”, odpowiedź jednym gestem, podpowiedź domyślna do przyjęcia;
  powiadomienia tylko dla stanów „potrzebuje Ciebie” (preset). Por. interf-16 (stany sesji Linear).
- **Kaliber:** large. **Wysiłek:** medium–large.
- **Zastrzeżenia:** research preview; brak pomiaru skrócenia czasu.

## interf-08 — GitHub Spec Kit `/speckit.clarify`: ≤5 pytań, po jednym, tabela 2–5 opcji z rekomendacją, odpowiedź „yes” przyjmuje, zapis `Q → A` do specyfikacji z datą sesji

- **Źródło:** https://raw.githubusercontent.com/github/spec-kit/main/templates/commands/clarify.md — plik szablonu
  w oficjalnym repozytorium GitHub, dostęp 2026-10-23. Wiarygodność **high**: przeczytany w całości.
- **Twierdzenie:** „Maximum of 5 total questions across the whole session” / „Never exceed 5 total asked questions
  (clarification retries for a single question do not count as new questions)”. Pytania w kolejce, pokazywane po
  jednym: „Never reveal future queued questions in advance”; najpierw kategorie o największym wpływie; przy >5
  nierozstrzygniętych kategoriach wybór wg heurystyki (Impact × Uncertainty). Format odpowiedzi: wybór z 2–5
  wzajemnie wykluczających się opcji w tabeli Markdown (Option | Description), rekomendacja jako pierwsza
  („**Recommended:** Option [X] - <reasoning>”), opcjonalny wiersz „Short” na odpowiedź wolną; pytanie krótkie —
  odpowiedź „<=5 words” z „**Suggested:**”. Użytkownik przyjmuje propozycję słowami „yes”, „recommended”,
  „suggested”. Zapis: sekcja `## Clarifications`, podsekcja `### Session YYYY-MM-DD`, wpis „- Q: <question> → A:
  <final answer>”, a potem zmiana właściwej sekcji specyfikacji („Contradictory earlier text is replaced rather than
  duplicated”), zapis po każdej integracji. Taksonomia pokrycia (10 kategorii, każda Clear / Partial / Missing):
  Functional Scope & Behavior; Domain & Data Model; Interaction & UX Flow; Non-Functional Quality Attributes;
  Integration & External Dependencies; Edge Cases & Failure Handling; Constraints & Tradeoffs; Terminology &
  Consistency; Completion Signals; Misc / Placeholders. Stop: wszystko krytyczne rozstrzygnięte, słowa „done”,
  „good”, „no more”, „stop”, „proceed”, lub limit 5.
- **Mechanizm:** to najpełniejszy znaleziony przykład **decyzji jako danych**: każde pytanie ma opcje i rekomendację
  (odpowiedź = jedno słowo), każda odpowiedź ląduje w specyfikacji z datą i zastępuje sprzeczny tekst; liczba pytań
  jest ograniczona i priorytetyzowana, więc człowiek odpowiada na 5 ważnych zamiast na 30 drobnych.
- **U nas:** węzeł spec z sekcją „Doprecyzowania” (sesja z datą, Q → A, link do zmienionego fragmentu); pytania z
  rekomendacją i jednosłowną akceptacją; limit pytań na sesję, porządek i taksonomia pokrycia jako **presety**
  (5 i 10 kategorii to przykłady Spec Kit, nie nasze stałe — R84). Taksonomia pokrycia jako perspektywa „co w
  specyfikacji jest Clear / Partial / Missing”.
- **Kaliber:** medium (szablon promptu, bez silnika). **Wysiłek:** medium.
- **Zastrzeżenia:** brak pomiaru skuteczności; to projekt promptu, nie badanie.

## interf-09 — Spec Kit, metodologia: znaczniki `[NEEDS CLARIFICATION: …]` zamiast zgadywania, checklisty do samooceny modelu, „converge”; liczby o czasie bez pomiaru

- **Źródła:** https://raw.githubusercontent.com/github/spec-kit/main/spec-driven.md i
  https://raw.githubusercontent.com/github/spec-kit/main/README.md — oficjalne repozytorium, dostęp 2026-10-23.
  Wiarygodność **high** dla mechanizmu, **low** dla liczb o czasie (dokument sam ich nie mierzy).
- **Twierdzenie:** szablon nakazuje modelowi: „Mark all ambiguities: Use [NEEDS CLARIFICATION: specific question]”
  (przykład: „[NEEDS CLARIFICATION: auth method not specified - email/password, SSO, OAuth?]”); checklisty „force the
  LLM to self-review its output systematically”; konstytucja w `memory/constitution.md` z dziewięcioma artykułami
  (IV–VI definiuje projekt); fazy specify → plan → tasks (zadania niezależne oznaczone `[P]`) → implement; README:
  „Run these skills in your agent's chat, not your terminal, one at a time, reviewing each result before continuing”;
  „Repeat implement and converge until convergence reports `Converged`”; „Create a constitution once per project and
  repeat the remaining steps for each feature”. Porównania czasu w dokumencie („~12 hours” tradycyjnej dokumentacji
  vs ok. 15 minut poleceń SDD; „in minutes, not days”) są **bez danych**.
- **Mechanizm:** niepewność jest **widoczna w artefakcie** (znacznik z konkretnym pytaniem), a nie ukryta w
  założeniu; człowiek przegląda fazę po fazie, a konwergencja (spec ↔ kod) jest jawnym stanem, nie wrażeniem.
- **U nas:** znacznik „niepewne — pytanie” jako typ węzła/adnotacji w spec, zliczany i pokazywany w perspektywie
  „co jeszcze nierozstrzygnięte”; stan „zbieżne / rozbieżne” między spec a implementacją jako wynik sprawdzianu
  (dowód, nie deklaracja). Konstytucja = nasz CLAUDE.md / `spec.json` (już mamy).
- **Kaliber:** medium. **Wysiłek:** small–medium.
- **Zastrzeżenia:** liczby o czasie traktować jako marketing dokumentu (R59: niska wiarygodność liczb).

## interf-10 — Kiro: trzy pliki specyfikacji (requirements / design / tasks), EARS, „naturalne punkty pauzy” między fazami, warianty requirements-first / design-first / bugfix

- **Źródła:** https://kiro.dev/blog/from-chat-to-specs-deep-dive/ (blog producenta) oraz przewodniki
  https://www.verdent.ai/guides/agents/kiro-spec-driven-development (deklaruje sprawdzenie z dokumentacją Kiro
  2026-08-25), https://dzone.com/articles/kiro-feature-to-requirements-design-tasks, https://sdtimes.com/?p=57508 —
  wszystko **przez podsumowanie wyszukiwarki** (hosty zablokowane), dostęp 2026-10-23. Wiarygodność **medium**:
  spójne między kilkoma źródłami, w tym blogiem producenta, ale stron nie przeczytałem.
- **Twierdzenie:** specyfikacja to trzy pliki Markdown w `.kiro/specs/`: `requirements.md` (historyjki i kryteria
  akceptacji w notacji EARS — „WHEN [condition] THE SYSTEM SHALL [behavior]”), `design.md` (architektura, diagramy
  przepływu danych, interfejsy, schemat, endpointy), `tasks.md` (zadania i podzadania uszeregowane wg zależności,
  powiązane z wymaganiami). Blog producenta: faza spec tworzy „natural pause points where humans can review, modify,
  and approve the direction before resources are invested in implementation”. Przewodnik: „What you gave up is the
  forced pause between phases, which is what catches a wrong assumption before it reaches the task list.” Warianty:
  Requirements-First („starts from the behavior you want, then derives design and tasks”), Design-First (istniejąca
  architektura / ostre wymagania niefunkcjonalne), Bugfix Spec (`bugfix.md` zamiast requirements: „current behavior,
  expected behavior, and unchanged behavior”). Pliki żyją w repozytorium — „można je przeglądać w pull requestach jak
  kod”. Steering (`.kiro/steering/product.md, tech.md, structure.md`) = stałe konwencje; hooks = akcje na zapis pliku.
- **Mechanizm:** przegląd człowieka jest **bramką między fazami** z małym artefaktem do przeczytania (plik, nie
  transkrypt); błędne założenie jest tańsze do złapania w wymaganiach niż w kodzie; specyfikacja ma kilka kształtów
  wejścia (od zachowania, od projektu, od błędu), więc nie zmusza do jednego porządku.
- **U nas:** spec jako trzy (lub więcej — lista otwarta) powiązane węzły z osobnymi bramkami akceptacji; „od czego
  zaczynamy” (wymagania / projekt / błąd / przykład / zrzut) jako preset wejścia; kryteria akceptacji w formie
  zdaniowej (EARS jako jeden z formatów, nie jedyny).
- **Kaliber:** large. **Wysiłek:** medium.
- **Zastrzeżenia:** brak liczb o turach; EARS to przykład notacji, nie wymóg.

## interf-11 — GitHub Copilot coding agent: iteracja przez `@copilot` w komentarzach PR, sesja pamięta poprzednie, workflowy wymagają zgody człowieka

- **Źródło:** https://docs.github.com/en/copilot/how-tos/use-copilot-agents/cloud-agent/review-copilot-prs —
  dokumentacja oficjalna **przez podsumowanie wyszukiwarki** (host zablokowany; dwie ścieżki w `github/docs` dały
  404), dostęp 2026-10-23. Wiarygodność **medium**.
- **Twierdzenie:** po zakończeniu zadania Copilot prosi o przegląd; zaleca się „a thorough review before merging”;
  iteracja: wzmianka `@copilot` w komentarzu PR, „Each new session picks up context from earlier sessions”; jeśli PR
  utworzył agent niestandardowy, wzmianka używa tego samego agenta; Copilot reaguje emoji „eyes” i wpisem na osi czasu
  „Copilot has started work”; workflowy Actions nie uruchamiają się same na pushach Copilota — człowiek je zatwierdza,
  a zmiany w plikach workflow trzeba sprawdzić najpierw. Rozliczenie: por. prac-27 („jedna premium request na sesję”).
- **Mechanizm:** jedyny kanał korekty to **komentarz w miejscu diffu** (ten sam co dla ludzi), a jednostką kosztu jest
  sesja — więc łączenie uwag w jedną recenzję zamiast serii pojedynczych komentarzy zmniejsza liczbę sesji (wniosek
  z modelu rozliczenia, nie cytat).
- **U nas:** korekta propozycji = adnotacja na fragmencie (węźle/diffie), nie wiadomość; grupowanie adnotacji w jedną
  „rundę” z jawnym kosztem rundy (nasz profil ekonomii tokenów, P38 slice 4).
- **Kaliber:** large. **Wysiłek:** medium.
- **Zastrzeżenia:** nie przeczytałem strony; brak liczb o turach.

## interf-12 — Cursor Plan Mode: pytania o niejasności przed planem, plan jako edytowalny Markdown z ścieżkami plików, sugestia trybu dla złożonych zadań

- **Źródła:** https://cursor.com/es/blog/plan-mode (blog producenta, wersje językowe) i
  https://www.testingcatalog.com/icycursor-releases-plan-mode-for-structured-code-planning.md,
  https://mer.vin/2025/12/planning-mode-in-ai-coding-assistants/ — **przez podsumowanie wyszukiwarki**, dostęp
  2026-10-23. Wiarygodność **medium** (blog producenta w streszczeniu, strony nie przeczytałem).
- **Twierdzenie:** agent „scans the codebase, reviews documentation, and asks questions to resolve ambiguity before it
  writes a plan”; plan to plik Markdown „with file paths and code references”, który można zmieniać bezpośrednio,
  „including adding or removing to-dos”; budowa „directly from it”; zapis do repozytorium opcjonalny; `Shift+Tab`
  włącza; „Cursor may suggest the mode for complex tasks”. Przewodnik: plany jako pliki wirtualne w `.cursor/plans/`
  z przyciskiem „Save to workspace”.
- **Mechanizm:** jak interf-01, plus **sugestia planowania wyzwalana złożonością zadania** (system proponuje droższy
  tryb, człowiek decyduje) — odpowiednik naszej zasady ×10 i advisory.
- **U nas:** heurystyka „to wygląda na duże — zaplanować najpierw?” jako advisory z presetem progu (liczba plików,
  szacowany koszt), nigdy automatyczna blokada.
- **Kaliber:** large. **Wysiłek:** small (mamy advisories).
- **Zastrzeżenia:** brak liczb.

## interf-13 — Na co narzekają użytkownicy Cursora: ręczne edycje planu ignorowane, plik planu nie do zapisu, Plan Mode nie edytuje własnego `.plan.md`

- **Źródła:** https://forum.cursor.com/t/models-ignore-manual-plan-edits/139129,
  https://forum.cursor.com/t/plan-markdown-editing-broken/142956,
  https://forum.cursor.com/t/plan-mode-cannot-edit-plan-files-plan-md-even-though-it-s-supposed-to/154315 — forum
  producenta **przez podsumowanie wyszukiwarki** (host zablokowany), dostęp 2026-10-23. Wiarygodność **low–medium**:
  zgłoszenia użytkowników, bez liczby potwierdzeń i bez odpowiedzi producenta w moim odczycie.
- **Twierdzenie:** zgłoszenia opisują (a) agentów ignorujących ręczne zmiany w planie, (b) niemożność zapisu planu,
  dopóki nie zostanie przeniesiony do workspace, (c) tryb planowania odmawiający edycji własnego pliku `.plan.md`,
  podczas gdy tryb agenta mógł go edytować.
- **Mechanizm (lekcja):** edycja planu przez człowieka musi być **źródłem prawdy** dla wykonawcy — jeśli agent
  pracuje z kopią w kontekście, poprawka człowieka ginie i rodzi dodatkowe tury („dlaczego zrobiłeś po staremu?”).
- **U nas:** plan = węzeł z wersjami; wykonawca czyta zawsze aktualną wersję (odczyt tuż przed krokiem), a diff
  „człowiek zmienił plan po akceptacji” trafia jawnie do kontekstu agenta jako addendum (waste control 6c).
- **Kaliber:** small. **Wysiłek:** small.
- **Zastrzeżenia:** anegdoty; wersji Cursora i dat nie odczytałem.

## interf-14 — Devin Interactive Planning: plan z cytowaniami kodu (deep-link do IDE), domyślnie 30 s na reakcję, potem wykonanie; czas jest ustawieniem

- **Źródło:** https://docs.devin.ai/work-with-devin/interactive-planning — dokumentacja oficjalna **przez
  podsumowanie wyszukiwarki** (host zablokowany), dostęp 2026-10-23; relacje z premiery 2.0 (analyticsvidhya,
  fast.io, 2025-04) — wtórne. Wiarygodność **medium**.
- **Twierdzenie:** Devin skanuje repozytorium i „proposes a detailed plan you can adjust”; przegląd planu jest
  „always helpful”, zwłaszcza dla złożonych zadań i obcego kodu; „By default, Devin will wait thirty seconds for
  feedback from you before automatically proceeding with its plan” — do zmiany w ustawieniach; plan „often includes
  code citations and code snippets you can inspect before approving”, a kliknięcie cytowania „deep-links into the
  Devin IDE”. Komentarz wtórny: narzędzie działa najlepiej na dobrze określonych, zamkniętych zadaniach; otwarte
  wymagają „heavy human guidance”.
- **Mechanizm:** odwrócony domyślny tryb względem Claude Code (interf-01/02): plan **przechodzi sam**, chyba że
  człowiek zareaguje w oknie czasu; cytowania kodu skracają weryfikację planu (klik zamiast szukania).
- **U nas:** „okno reakcji przed wykonaniem” jako preset per klasa zadania (0 = czekaj bez końca, 30 s, 5 min…,
  wartości przykładowe); plan z odsyłaczami do węzłów/plików, które dotyka (klik = podgląd). Zgodne z R62 (działanie
  pod nieobecność właściciela) — ale wybór należy do użytkownika.
- **Kaliber:** large. **Wysiłek:** small–medium.
- **Zastrzeżenia:** strony nie przeczytałem; brak liczb o skuteczności.

## interf-15 — OpenAI Codex (cloud): tryby „Ask” / „Code”, każdy task we własnym sandboksie, wynik jako diff; „brak edytora — drobna poprawka = kolejne zlecenie”; `AGENTS.md` jako „README dla agentów”

- **Źródła:** https://raw.githubusercontent.com/agentsmd/agents.md/main/README.md (oficjalne repozytorium formatu,
  przeczytane, **high**); https://verdent.ai/guides/codex-app-first-impressions-2026 (recenzja 2026-03, **low–medium**,
  przez podsumowanie), https://dev.to/_37bbf0c253c0b3edec531e/openais-codex-what-it-ishow-to-work-and-how-to-use-2gem
  (**low**, przez podsumowanie); oficjalne strony OpenAI zablokowane. Dostęp 2026-10-23.
- **Twierdzenie:** AGENTS.md: „a simple, open format for guiding coding agents”, „a README for agents: a dedicated,
  predictable place to provide context and instructions to help AI coding agents work on your project”; przykładowe
  sekcje: Dev environment tips, Testing instructions, PR instructions. Codex (wtórne): tryb „Code” (zmiany w plikach)
  i „Ask” (wyjaśnienia bez zmian); „each coding task is dispatched to its own isolated sandbox environment preloaded
  with the user's repository”; recenzja 2026-03: agent kończy i pokazuje diff w aplikacji, „it has no built-in
  editor, so small fixes mean asking the agent again or editing elsewhere”.
- **Mechanizm:** rozdzielenie „pytam” od „zmieniaj” jako jawny tryb (tani odczyt vs drogi zapis); stałe instrukcje
  w pliku, nie w każdej wiadomości (oszczędność tur i tokenów — por. prac-10 o CLAUDE.md). Skarga o braku edytora
  pokazuje koszt braku **edycji w miejscu**: każda drobna poprawka kosztuje całą turę agenta.
- **U nas:** tryby „pytaj / proponuj / wykonuj” jako presety przepływu; poprawka ręczna wyniku propozycji w miejscu
  (edycja węzła) zamiast nowej tury; stałe instrukcje = nasz spec/konstytucja.
- **Kaliber:** large (produkt), small (plik AGENTS.md). **Wysiłek:** small.
- **Zastrzeżenia:** szczegóły Codex wtórne; liczba repozytoriów używających AGENTS.md nie była na stronie.

## interf-16 — Linear: delegat zamiast przypisanego („agent nie może być rozliczany”), zasady AIG (szybkie potwierdzenie, wycofanie na żądanie), stany sesji z `awaitingInput`

- **Źródła:** https://linear.app/developers/aig.md, https://linear.app/developers/agent-interaction,
  https://linear.app/docs/agents-in-linear, https://linear.app/blog/our-approach-to-building-the-agent-interaction-sdk
  — oficjalne **przez podsumowanie wyszukiwarki** (host zablokowany), dostęp 2026-10-23; typy aktywności z
  https://skills.sh/5dlabs/cto/linear-agent-api (strona trzecia). Wiarygodność **medium** (oficjalne, nieprzeczytane);
  typy aktywności **low**.
- **Twierdzenie:** przypisanie issue aplikacji czyni ją **delegatem**, nie assignee — człowiek zachowuje własność;
  uzasadnienie z bloga: issues można przypisać tylko ludziom i delegować tylko agentom, „because an agent cannot be
  held accountable”. AIG: agenci używają tych samych akcji UI co człowiek, dają szybkie potwierdzenie po wywołaniu,
  wycofują się niezwłocznie na prośbę, utrzymują jasno odpowiedzialnego człowieka. Sesje agenta powstają przy
  @wzmiance lub delegacji; stany: pending, active, error, **awaitingInput**, complete; typy aktywności (strona
  trzecia): thought, action, elicitation, response. API w Developer Preview.
- **Mechanizm:** relacja człowiek–agent jest **danymi w modelu** (delegat ≠ właściciel; stan `awaitingInput` jest
  pierwszoklasowy, więc kolejki „czeka na Ciebie” da się zbudować bez czytania transkryptu); agent komunikuje się
  tymi samymi prymitywami co człowiek (komentarz, status), nie osobnym czatem.
- **U nas:** węzeł zadania z polem „delegat” i „odpowiedzialny”, stany sesji agenta jako dane grafu (w tym
  „czeka na wejście”), aktywności agenta jako zdarzenia o typach (lista otwarta: myśl, akcja, pytanie, odpowiedź…).
  Spójne z P37 (jeden silnik, dziennik zdarzeń).
- **Kaliber:** large. **Wysiłek:** medium.
- **Zastrzeżenia:** nie przeczytałem stron; API w podglądzie, może się zmienić (R60: obserwacja datowana).

## interf-17 — Atlassian Rovo Dev w Jira: plan z listą plików i repozytoriów do dotknięcia, postęp w zgłoszeniu, PR do przeglądu

- **Źródła:** https://www.atlassian.com/blog/announcements/rovo-dev-in-jira,
  https://www.atlassian.com/blog/bitbucket/ai-powered-workflows-rovodev,
  https://thenewstack.io/atlassians-new-ai-product-gives-developers-access-to-agents/ — blogi producenta i serwis
  branżowy **przez podsumowanie wyszukiwarki** (hosty zablokowane), dostęp 2026-10-23. Wiarygodność **medium–low**
  (marketing, bez dokumentacji referencyjnej).
- **Twierdzenie:** Rovo Dev czyta zgłoszenie Jira, dociąga powiązany kod i „creates a detailed code plan based on
  your issue details”; w przykładzie sprzątania flagi proponuje „a clear cleanup plan for the flag, including which
  files and repos it will touch”; użytkownik widzi plan i postęp w Jira, przegląda kod i testy przed scaleniem;
  możliwość „regenerating output or editing it by hand until it gets you all the way to a PR” (The New Stack, pod
  starszą nazwą Auto Dev); PR w Bitbucket bez kopiowania; AI-recenzja przed ludzką.
- **Mechanizm:** plan deklaruje **zasięg** (pliki, repozytoria) zanim cokolwiek się zmieni — człowiek akceptuje
  zasięg, nie tylko cel; postęp jest w tym samym obiekcie co wymaganie (zgłoszenie), nie w osobnym czacie.
- **U nas:** propozycja z jawną listą węzłów/plików, które dotknie (zasięg jako dane, porównywany po fakcie z tym, co
  faktycznie dotknięto — advisory przy rozjeździe); postęp jako zdarzenia na węźle zadania.
- **Kaliber:** large. **Wysiłek:** small–medium.
- **Zastrzeżenia:** brak liczb; źródła marketingowe.

## interf-18 — Notion (wydanie 2026-08-28): agenci proponują zmiany zamiast je wykonywać; akceptacja „od góry do dołu”, dziennik aktywności; Google Docs / Gemini — nic nie znalazłem

- **Źródła:** https://www.notion.com/releases/2026-08-28 (noty wydania producenta, **przez podsumowanie
  wyszukiwarki**, host zablokowany), https://spaces.collab365.com/posts/notion-agents-can-now-work-inside-your-pages-and-w-8yi_cn,
  https://www.eesel.ai/blog/notion-ai-review — wtórne. Dostęp 2026-10-23. Wiarygodność **medium** (oficjalny cytat
  w streszczeniu), wtórne **low**.
- **Twierdzenie:** „Your agents can now propose changes instead of making them directly. Ask your agent to suggest
  edits, then move top to bottom to approve each one.” Wtórnie: „Agent proposes line-level changes and the human
  accepts them one by one”; agenci „respect existing permissions, can run on a schedule, and leave activity logs
  admins can review”. Cennik (eesel, 2026-09): Custom Agents na kredytach 10 USD / 1 000 kredytów miesięcznie.
  O funkcjach Google Docs z Gemini w trybie sugestii **nic nie znalazłem** w wynikach (jedno wyszukiwanie).
- **Mechanizm:** zmiana agenta jest **sugestią w dokumencie** (jak tryb „suggesting” edytorów), a przegląd to seria
  jednogestowych akceptacji w kolejności tekstu — bez czatu i bez czytania diffu całości.
- **U nas:** propozycje edycji tekstu/węzła jako sugestie liniowe z akceptacją pojedynczą i zbiorczą („wszystkie
  poniżej”), dziennik aktywności agenta jako zdarzenia grafu (już w P37/R33 kierunku).
- **Kaliber:** large. **Wysiłek:** medium.
- **Zastrzeżenia:** noty nie przeczytałem; brak liczb o turach.

## interf-19 — Na co narzekają użytkownicy builderów (Replit Agent, Lovable): pętla naprawiania własnych błędów zjada kredyty; Lovable: „Try to fix” bez kredytu, tryb planu 1 kredyt / wiadomość

- **Źródła:** https://lovable.dev/faq/ai-agent/errors/wasted-credits-bugs (FAQ producenta, **przez podsumowanie
  wyszukiwarki**, **medium**); https://replit.discourse.group/t/would-replit-consider-an-appeal-system-to-get-refunds-on-rollbacks/1198
  (forum, **low**); https://www.rapidevelopers.com/md/lovable-issues/reducing-token-usage-while-debugging-in-lovable
  (blog firmy doradczej, **low**); recenzje Trustpilot (**low**). Dostęp 2026-10-23; daty wpisów nieodczytane.
- **Twierdzenie:** Lovable FAQ: przycisk „Try to fix” nie zużywa kredytów, prompty w trybie Agent zużywają, „Plan mode
  costs one credit per message”. Replit (forum): „recursive loop” naprawiania błędu wprowadzonego poprzednią naprawką
  to „one of the biggest complaints”; jeden użytkownik: pętla przy wygasłym certyfikacie SSL kosztowała ok. 40 %
  miesięcznych checkpointów, a agent nie wrócił do działającego stanu mimo prośby o rollback. Blog o Lovable: pętla
  „fix fails → different approach breaks something else → repeat until credits run out” jako najczęściej zgłaszany
  problem. Trustpilot: pętla na jednym błędzie „until the credits are gone”.
- **Mechanizm (lekcja):** bez widocznego licznika „ile razy ta sama sprawa” i bez punktu kontrolnego, do którego da
  się wrócić, człowiek płaci za tury, których nie wybrał; tania ścieżka naprawy (bez kredytu) i jawny koszt tury
  planowania to odpowiedź producenta.
- **U nas:** licznik powtórzeń tej samej sprawy (interf-04) + advisory z szacunkiem kosztu dalszej pętli; checkpoint
  przed każdą propozycją (mamy w gicie dla agentów; potrzebny w grafie użytkownika); koszt tury pokazany przed
  wysłaniem (K1c1 już ma „oszacowanie przed wysłaniem”).
- **Kaliber:** small (skargi) / medium (model cenowy). **Wysiłek:** small.
- **Zastrzeżenia:** anegdoty bez dat; FAQ nie przeczytane.

## interf-20 — Stack Overflow Developer Survey 2025: 66 % — frustracja „prawie dobrymi” rozwiązaniami AI; 45,2 % — debugowanie kodu AI bardziej czasochłonne

- **Źródła:** https://survey.stackoverflow.co/2025/ai (oficjalna strona wyników),
  https://stackoverflow.blog/2025/07/29/developers-remain-willing-but-reluctant-to-use-ai-the-2025-developer-survey-results-are-here/
  — **przez podsumowanie wyszukiwarki** (hosty zablokowane); omówienia: venturebeat, admin-magazine, infoworld,
  theregister (2025-07-29). Wiarygodność **medium**: liczby zgodne w kilku omówieniach, ale strony oficjalnej nie
  przeczytałem; liczby o zaufaniu **rozbieżne** między omówieniami (29 % / 33 % ufa; 46 % nie ufa; InfoWorld podaje
  64 % zamiast 66 %).
- **Twierdzenie:** najczęstsza frustracja: „AI solutions that are almost right” — 66 %; „debugging AI-generated code
  is more time-consuming” — 45,2 %; blog: 66 % „spending more time fixing 'almost-right' AI-generated code”.
- **Mechanizm (dla interfejsu):** „prawie dobrze” to koszt przeglądu i poprawek po stronie człowieka — argument za
  bramką przed wykonaniem (plan / spec) i za dowodami weryfikacji (interf-05), nie za szybszym generowaniem.
- **U nas:** metryka „ile propozycji zaakceptowano bez zmian / ze zmianami / odrzucono” w profilu ekonomii (por.
  interf-27) — mierzmy własne „prawie dobrze”.
- **Kaliber:** scientific-light (ankieta, duża próba; liczby respondentów nie odczytałem). **Wysiłek:** small (metryka).
- **Zastrzeżenia:** ankieta deklaratywna; rozbieżności cytatów o zaufaniu — używać tylko 66 % i 45,2 % z ostrożnością.

## interf-21 — METR, RCT (2025-07): doświadczeni programiści z AI byli o 19 % wolniejsi, choć prognozowali −24 % i po fakcie czuli −20 %

- **Źródło:** Becker, Rush, Barnes, Rein, „Measuring the Impact of Early-2025 AI on Experienced Open-Source Developer
  Productivity”, arXiv 2507.09089, opublikowane 2025-07-12 — abstrakt przeczytany na lustrze
  https://huggingface.co/papers/2507.09089 (arxiv.org zablokowane). Wiarygodność **high** (praca pierwotna, abstrakt
  w całości; pełnego tekstu nie czytałem).
- **Twierdzenie:** „16 developers with moderate AI experience complete 246 tasks in mature projects on which they have
  an average of 5 years of prior experience. Each task is randomly assigned to allow or disallow usage of early 2025
  AI tools … developers primarily use Cursor Pro … and Claude 3.5/3.7 Sonnet. Before starting tasks, developers
  forecast that allowing AI will reduce completion time by 24%. After completing the study, developers estimate that
  allowing AI reduced completion time by 20%. Surprisingly, we find that allowing AI actually increases completion
  time by 19% … This slowdown also contradicts predictions from experts in economics (39% shorter) and ML (38%
  shorter).” Zbadano „20 properties of our setting” jako możliwe przyczyny. Wtórnie (getdx, przez podsumowanie):
  autor o sugestiach „directionally correct, but not exactly what's needed”.
- **Mechanizm (dla interfejsu):** subiektywne poczucie przyspieszenia nie jest pomiarem — interfejs powinien pokazywać
  zmierzony czas i koszt rund (nasz profil ekonomii), a nie polegać na wrażeniu; w dojrzałych repozytoriach z wysokim
  standardem koszt przeglądu i poprawek „prawie dobrego” kodu przewyższał zysk.
- **U nas:** pomiar czasu człowieka na przegląd propozycji (od otwarcia do decyzji) obok tokenów; eksperyment A/B
  „z planem / bez planu” na własnych zadaniach jako przepływ badawczy (R59 ledger).
- **Kaliber:** scientific. **Wysiłek:** medium (telemetria czasu przeglądu).
- **Zastrzeżenia:** 16 osób, narzędzia z II–VI 2025; autorzy zastrzegają możliwe artefakty; podział czasu na
  przegląd/czekanie nie jest w abstrakcie (nie czytałem pełnego tekstu).

## interf-22 — „Pytać czy działać” jako decyzja o oczekiwanej użyteczności: Horvitz 1999, Fleming 2001, Dong 2026 (Value of Information, do +1,36 pkt użyteczności przy wysokim koszcie pytania)

- **Źródła (abstrakty przez Consensus, dostęp 2026-10-23):** E. Horvitz, „Principles of mixed-initiative user
  interfaces”, 1999, 1 830 cytowań — https://consensus.app/papers/details/46927d48d0cc5857b4138cb2a884487c/ ;
  M. W. Fleming i in., „A utility-based theory of initiative in mixed-initiative systems”, 2001 —
  https://consensus.app/papers/details/b23768e7dbaf503bb1f0173f6b77bff9/ ; Y. R. Dong i in., „Value of Information: A
  Framework for Human-Agent Communication”, 2026, 16 cytowań — https://consensus.app/papers/details/2f6805a169f5556b92976eab64cf26bc/ ;
  R. Cohen i in., 2004 — https://consensus.app/papers/details/07b42d2e3f5d5215b328686c65bba8b5/ . Wiarygodność **high**
  dla istnienia i treści abstraktów (recenzowane / klasyczne); pełnych tekstów nie czytałem.
- **Twierdzenie:** Horvitz: „an elegant coupling of automated services with direct manipulation” (zasady decydowania,
  kiedy automat ma działać, pytać lub milczeć, na podstawie niepewności i kosztów). Fleming: „a utility-based decision
  making process … in determining when to take the initiative … based on a calculation of the expected utility of
  various courses of action and the likelihood that the user will be an effective contributor of information”. Cohen:
  decyzja „based on whether the perceived benefits of communication exceed the expected costs”, z modelem użytkownika,
  zadania i dialogu. Dong 2026: „user requests are underspecified, yet agents must decide whether to act on incomplete
  information or interrupt users for clarification. Existing approaches either rely on brittle confidence thresholds …
  or fail to account for the varying stakes”; VoI „dynamically weigh the expected utility gain from asking questions
  against the cognitive cost imposed on users”; „requires no hyperparameter tuning”; w czterech domenach (20 Questions,
  diagnoza medyczna, rezerwacja lotów, e-commerce) „consistently matches or exceeds the best manually-tuned baselines,
  achieving up to 1.36 utility points higher in high-cost settings”.
- **Mechanizm:** pytanie do człowieka ma **koszt** (czas, uwaga) i **wartość** (ile zmieni decyzję); agent pyta, gdy
  wartość informacji przewyższa koszt, a stawka decyzji podnosi gotowość do pytania. To formalna podstawa dla gałki
  „jak często pytać” (CLAUDE.md: „how often to ask, up to never, is a configurable preset”).
- **U nas:** próg pytania jako funkcja (stawka decyzji × niepewność) ÷ koszt uwagi właściciela, z presetami i z
  jawnym uzasadnieniem przy każdym pytaniu („pytam, bo odwrócenie kosztowałoby X”); nic z tego nie jest zamkniętą
  listą.
- **Kaliber:** scientific. **Wysiłek:** medium (model), small (uzasadnienie przy pytaniu).
- **Zastrzeżenia:** abstrakty; domeny Dong nie obejmują kodu ani specyfikacji.

## interf-23 — Kiedy pytać o doprecyzowanie: „clarifications lepsze na początku, suggestions lepsze po pokazaniu wyników”; brak dominującej strategii (CIKM 2021, 49 tematów TREC)

- **Źródło:** M. Aliannejadi i in., „Analysing Mixed Initiatives and Search Strategies during Conversational Search”,
  CIKM 2021, 50 cytowań — https://consensus.app/papers/details/2e88f183a934549fac08cee45a1c6f16/ (abstrakt przez
  Consensus; PDF https://arxiv.org/pdf/2109.05955 zablokowany). Wiarygodność **high** (recenzowane; abstrakt).
- **Twierdzenie:** strategie Feedback-First vs Feedback-After × Query Suggestions vs Query Clarifications; „there is no
  superior or dominant combination, instead it shows that query clarifications are better when asked first, while
  query suggestions are better when asked after presenting results”; najlepsza strategia „depends on the trade-offs
  between the relative costs between querying and giving feedback, the performance of the initial query, the number of
  assessments per query, and the total amount of gain required”.
- **Mechanizm:** **moment** pytania zależy od jego rodzaju: pytanie o znaczenie — przed pracą (tanio zapobiega złej
  gałęzi); propozycja alternatyw — po pokazaniu wyniku (człowiek wybiera widząc konkret). Dla specyfikacji: niejasność
  wymagania → pytaj przed planem (jak Spec Kit clarify); warianty rozwiązania → pokaż plan/wynik i daj wybór.
- **U nas:** dwa typy pytań w przepływie z presetem czasu: „doprecyzowanie” (przed propozycją) i „wybór wariantu”
  (po propozycji, z porównaniem); koszty obu jako dane profilu ekonomii.
- **Kaliber:** scientific. **Wysiłek:** small–medium.
- **Zastrzeżenia:** domena wyszukiwania, symulacja offline (nie badanie z ludźmi); przenoszę przez analogię.

## interf-24 — TiCoder (IEEE TSE 2024): doprecyzowanie intencji przez testy zamiast rozmowy — 15 programistów trafniej ocenia kod, mniejsze obciążenie; +45,97 pkt pass@1 w ≤5 interakcjach (symulacja)

- **Źródło:** S. Fakhoury i in., „LLM-Based Test-Driven Interactive Code Generation: User Study and Empirical
  Evaluation”, IEEE Transactions on Software Engineering 2024, 166 cytowań —
  https://consensus.app/papers/details/991b1050bb035e3eab62b3bb4413177d/ (abstrakt przez Consensus). Wiarygodność
  **high** (recenzowane; abstrakt).
- **Twierdzenie:** „interactive workflow TiCoder for guided intent clarification (i.e., partial formalization) through
  tests”; „mixed methods user study with 15 programmers … participants using the proposed workflow are significantly
  more likely to correctly evaluate AI generated code, and report significantly less task-induced cognitive load”;
  „an average absolute improvement of 45.97% in the pass@1 code generation accuracy for both datasets and across all
  LLMs within 5 user interactions” (z „an idealized proxy for a user feedback”), plus automatycznie generowane testy.
- **Mechanizm:** pytanie do człowieka ma formę **„czy ten przykład wejście→wyjście jest tym, czego chcesz?”** (tak/nie
  na konkretnym teście), nie „co masz na myśli?”; każda odpowiedź jest od razu artefaktem (testem), więc decyzja jest
  danymi i sprawdzianem zarazem (łączy interf-05 i interf-08).
- **U nas:** pytania doprecyzowujące jako **przykłady do zatwierdzenia** (dla kodu — testy; dla spec — przykłady
  zachowania „gdy X, to Y”), z zatwierdzonymi przykładami dołączanymi do węzła jako kryteria akceptacji. Zgodne z
  „Necessary, insufficient” (przykłady + pokrycie przestrzeni).
- **Kaliber:** scientific. **Wysiłek:** medium.
- **Zastrzeżenia:** 15 osób; +45,97 z symulowanym użytkownikiem; zadania małe (zbiory Pythona).

## interf-25 — Modele kodu generują zamiast pytać: >60 % odpowiedzi przy niejasnym opisie (HumanEvalComm, TOSEM 2024; pass@1 spada o 35–52 %); agent Okanagan +58 pkt wskaźnika komunikacji; ClarifyCoder 2025; ClarifyCodeBench 2026 mierzy „Turn-discounted Key Question Rate”

- **Źródła (abstrakty):** J. Wu i in., „HumanEvalComm: Benchmarking the Communication Competence of Code Generation
  for LLMs and LLM Agents”, ACM TOSEM 2024, 40 cytowań — https://consensus.app/papers/details/b20bdec349e354e5a992214ab58a8022/
  (arXiv 2406.00215, lustro https://huggingface.co/papers/2406.00215); J. J. W. Wu i in., „ClarifyCoder”, arXiv
  2504.16331, 2025-04-23 — abstrakt przeczytany na https://huggingface.co/papers/2504.16331 ; Z. Fang i in.,
  „ClarifyCodeBench”, 2026 — https://consensus.app/papers/details/e3039172f33d5f7ab4b327b9a6e9b52a/ ; H. R. Darji i
  in., „Curiosity by Design”, 2025 — https://consensus.app/papers/details/a112ac498e855805a0512d74d1829199/ .
  Wiarygodność **high** (TOSEM recenzowane), **medium** (preprinty 2025–2026).
- **Twierdzenie:** HumanEvalComm: trzy klasy problemów w opisie — „Inconsistency, Ambiguity, and Incompleteness”;
  „more than 60% of responses from Code LLMs still generate code rather than ask questions when the problem
  descriptions are manually modified”; „Pass@1 and Test Pass Rate of most Code LLMs drop by 35% — 52% and by 17% —
  35%”; agent Okanagan „increases the Communication Rate and Good Question Rate by an absolute 58% and 38%”, a Pass@1
  i Test Pass Rate o 8 i 7 pkt. ClarifyCoder: „human engineers actively seek clarification when faced with ambiguous
  requirements, while LLMs typically generate code regardless”; fine-tuning uczący „prioritize seeking clarification
  over immediate code generation” (snippet wyszukiwarki podaje „over 63% of ambiguous scenarios” — liczby z pełnego
  tekstu nie czytałem). ClarifyCodeBench: metryki „Turn-discounted Key Question Rate, which penalizes inefficient
  questioning, and Optimal Round Adherence”; wyniki: „Capability Decoupling” (dobry kod ≠ dobre pytania), „Reasoning
  Paradox” (więcej myślenia mało pomaga w wykrywaniu niejasności), „Multi-ambiguity Ceiling” (jakość pytań spada
  ostro z gęstością niejasności). Curiosity by Design: klasyfikator niejasnych zapytań + model generujący pytania;
  „users find the clarification questions generated by our model to outperform the baseline”.
- **Mechanizm:** domyślne zachowanie modelu to **zgadywanie**, więc interfejs musi wymusić wykrywanie niejasności
  (klasyfikator / znaczniki / limit pytań) i mierzyć jakość pytań **z karą za liczbę tur** — dokładnie ta miara, o
  którą pyta brief („fewer turns”).
- **U nas:** tani etap „czy prośba jest niejednoznaczna?” przed drogim przepływem (jak router reguł P34), metryka
  „trafne pytania na turę” w profilu ekonomii; świadomość, że przy wielu niejasnościach jednocześnie lepiej dzielić
  spec na mniejsze węzły (ceiling).
- **Kaliber:** scientific. **Wysiłek:** medium.
- **Zastrzeżenia:** benchmarki na małych zadaniach (HumanEval); liczby z abstraktów.

## interf-26 — Jak wyglądają prawdziwe rozmowy z asystentem kodu: liczba tur korygujących jako miara (SWE-Together, 109 zadań z 11 260 sesji); jakość nie rośnie po ≥5 turach (CodeChat, 587 568 rozmów); wzorce liniowy / gwiazda / drzewo

- **Źródła (abstrakty przez Consensus, dostęp 2026-10-23):** Y.-F. Wu i in., „SWE-Together: Evaluating Coding Agents
  in Interactive User Sessions”, 2026, 17 cytowań — https://consensus.app/papers/details/78b60adb95b858939027b736883ae04c/ ;
  S.-Z. Zhong i in., „Developer-LLM Conversations: An Empirical Study of Interactions and Generated Code Quality”,
  2025 — https://consensus.app/papers/details/cc17cd60233a593e9c97453c4cc093bb/ ; B. Zhang i in., „Decoding Human-LLM
  Collaboration in Coding”, 2025 — https://consensus.app/papers/details/dd524fb7e9725aab9dcfa29c3dac6391/ .
  Wiarygodność **medium** (preprinty; duże zbiory).
- **Twierdzenie:** SWE-Together: „we curate 109 repository-level tasks from 11,260 recorded sessions”; symulator
  użytkownika „provides feedback when the coding agent's progress requires it”; „we measure both final repository
  correctness and the number of corrective feedback turns required”; „stronger agents generally achieve higher final
  success rates while requiring fewer interventions”. CodeChat: „587,568 real-world developer-LLM conversations and
  1.7 million code snippets”; „conversations often grow long because developers repeatedly shift use cases”; problemy
  jakości „does not consistently decrease in interactions lasting at least five turns”; „prompts that explicitly
  identify the error and request a fix are the most frequently observed” wśród udanych napraw. Zhang: wzorce
  interakcji „linear, star and tree” zależnie od zadania; „Bug fixing and code refactoring pose greater challenges to
  LLMs' instruction following”; „requirements-driven development tasks show lower user satisfaction”.
- **Mechanizm:** liczba **tur korygujących** jest mierzalną miarą jakości współpracy; długie rozmowy wynikają z
  mieszania spraw (argument za „jedna sprawa — jeden węzeł / sesja”, por. interf-04); skuteczna korekta nazywa błąd
  wprost (argument za adnotacją na fragmencie zamiast ogólnej uwagi).
- **U nas:** licznik tur korygujących na propozycję w profilu ekonomii; advisory „zmieniasz temat — nowy węzeł?”;
  szablon korekty „co jest źle + czego oczekuję” jako preset formularza adnotacji.
- **Kaliber:** scientific (duże zbiory, bez eksperymentu z ludźmi). **Wysiłek:** small–medium.
- **Zastrzeżenia:** dane z czatów ogólnych (WildChat/LMSYS), nie z narzędzi agentowych.

## interf-27 — Interwencje człowieka w PR-ach agentów (MSR 2026): rzadziej (52,17 % vs 83,59 %), ale droższe; 58,02 % to „prowadzenie” (ograniczanie działań, konwencje), 21,16 % decyzje, 17,05 % kod; 83,8 % PR-ów Claude Code scalonych, 54,9 % bez zmian

- **Źródła (abstrakty przez Consensus):** S. Khelifi i in., „Behind Agentic Pull Requests: An Empirical Study on
  Developer Interventions in AI Agent-Authored Pull Requests”, MSR 2026 —
  https://consensus.app/papers/details/e0afac35fc5a578d963a8a5e4e50dab6/ ; M. Watanabe i in., „On the Use of Agentic
  Coding: An Empirical Study of Pull Requests on GitHub”, ACM TOSEM 2025, 100 cytowań —
  https://consensus.app/papers/details/4e69ad3f723e50b98ad34aa0131a2a41/ ; K. Duma i in., „These Aren't the Reviews
  You're Looking For”, 2026 — https://consensus.app/papers/details/49dceaa4b73b5cbd85c0c8f4478cfb23/ ; C. Nachuma i in.,
  MSR 2026 — https://consensus.app/papers/details/00e7fbfb4a6452e083d487713c91383a/ . Wiarygodność **high** (TOSEM,
  MSR recenzowane; abstrakty).
- **Twierdzenie:** Khelifi (zbiór AIDev): „human interventions occur less frequently in APRs than in HPRs (52.17% vs.
  83.59%), but when it occurs in APRs, it requires higher review effort, including larger code churn and longer
  durations”; taksonomia „4 high-level categories and 42 intervention actions”; „guidance-level interventions with
  58.02%, focusing on restricting the agent's actions and enforcing project-conventions, followed by decision-level
  interventions at 21.16%, direct code changes at 17.05% and operational-level intervention at 3.69%”; „collaboration
  with coding agents, is shifting developer work from implementation to supervision, guidance and quality control”.
  Watanabe: „567 GitHub pull requests (PRs) generated using Claude Code … across 157 diverse open-source projects …
  83.8% of these agent-assisted PRs are eventually accepted and merged … with 54.9% of the merged PRs being integrated
  without further modification. The remaining 45.1% require additional changes … especially for bug fixes,
  documentation, and adherence to project-specific standards.” Duma: „most AI-generated PRs receive no review and,
  when reviewed, are largely dominated by AI agents rather than humans … human involvement frequently expressed
  through agent steering rather than standalone evaluation”. Nachuma: „reviewer engagement has the strongest
  correlation with successful integration, whereas larger change sizes and coordination-disrupting actions, such as
  force pushes, are associated with a lower likelihood of merging”; „iteration intensity alone provides limited
  explanatory power”.
- **Mechanizm:** większość pracy człowieka to **prowadzenie** (zakres, konwencje) i **decyzje**, nie pisanie kodu —
  interfejs powinien więc czynić te dwa typy wejścia najtańszymi (reguły jako dane, decyzje jako wybór), a koszt
  korekty rośnie z rozmiarem zmiany (argument za małymi propozycjami).
- **U nas:** kategorie adnotacji na propozycji (zakres / konwencja / decyzja / kod / operacyjne — lista otwarta) z
  automatycznym przeniesieniem „konwencji” do stałych reguł (konstytucja), aby ta sama uwaga nie wracała; metryka
  „zaakceptowane bez zmian / ze zmianami” jak Watanabe (por. interf-20).
- **Kaliber:** scientific. **Wysiłek:** medium.
- **Zastrzeżenia:** dane z publicznych repozytoriów OSS; abstrakty.

## interf-28 — Projekt komentarza recenzji, który prowadzi do zmiany: zwięzły, z fragmentem kodu, ręcznie wyzwolony, na poziomie hunka (TSE 2025, 22 000 komentarzy); recenzje tylko-automat scalane w 45,20 % vs 68,37 %; programiści zachowują 52 % sugestii

- **Źródła (abstrakty przez Consensus):** K. Sun i in., „Does AI Code Review Lead to Code Changes? A Case Study of
  GitHub Actions”, IEEE TSE 2025 — https://consensus.app/papers/details/3f3a523ce65e51ee9239f124a740e40d/ ;
  K. Chowdhury i in., „From Industry Claims to Empirical Reality: An Empirical Study of Code Review Agents in Pull
  Requests”, MSR 2026 — https://consensus.app/papers/details/266cd832fc69555683dfb09fc87e42d3/ ; S. Sabouri i in.,
  „Trust Dynamics in AI-Assisted Development”, ICSE 2025 — https://consensus.app/papers/details/ded8d4328fcc52ac8660f129e5fbf6e6/ ;
  B. B. Magnússon i in., „Rethinking Code Review Workflows with LLM Assistance”, ESEM 2025 —
  https://consensus.app/papers/details/1e233fb005d05c1da73af12524c91bc0/ . Wiarygodność **high** (recenzowane; abstrakty).
- **Twierdzenie:** Sun: „16 popular AI-based code-review actions … more than 22,000 review comments in 178
  repositories”; „Comments that are concise, contain code snippets, and are manually triggered, particularly those
  from hunk-level review tools, are more likely to result in code changes.” Chowdhury: z „AIDev's 19,450 PRs”
  analiza 3 109 PR-ów; „CRA-only PRs achieve a 45.20% merge rate, 23.17 percentage points lower than human-only PRs
  (68.37%)”; „60.2% of closed CRA-only PRs fall into the 0–30% signal range, and 12 of 13 CRAs exhibit average signal
  ratios below 60%”; rekomendacja: agenci recenzji „should augment rather than replace human reviewers”. Sabouri
  (n=29 ankieta, n=10 obserwacja): „comprehensibility and perceived correctness were the most frequently used
  factors”; „developers often alter their trust decisions, keeping only 52% of original suggestions”. Magnússon
  (eksperyment polowy, dwie wersje: recenzja z góry vs na żądanie): „developers generally preferred the AI-led
  approach, especially for large or unfamiliar pull requests, though preferences varied with codebase familiarity and
  review risk”; „hybrid proactive/on-demand designs best balancing efficiency, trust, and reviewer control”.
- **Mechanizm:** komentarz jest skuteczny, gdy jest **krótki, z propozycją kodu, w miejscu zmiany i na żądanie**;
  szum automatycznych recenzji (niski stosunek sygnału) koreluje z porzucaniem PR-ów — każda nieskuteczna uwaga to
  stracona tura człowieka. To uzasadnia kalibrację recenzenta z interf-05.
- **U nas:** adnotacje (człowieka i modelu) na poziomie fragmentu z propozycją zmiany „jednym kliknięciem przyjmij”;
  recenzja automatyczna „z góry / na żądanie / obie” jako preset per rozmiar zmiany; wskaźnik „ile uwag recenzenta
  doprowadziło do zmiany” jako kalibracja wiarygodności recenzenta (R59: precedens `calibration.json`).
- **Kaliber:** scientific. **Wysiłek:** medium.
- **Zastrzeżenia:** abstrakty; dane z GitHub OSS.

## interf-29 — Inicjatywa systemu w pracy wspólnej: poziom dialogu vs poziom zadania (27 par, Wizard of Oz, 2022); adaptacyjna mieszana inicjatywa zmniejsza obciążenie (DIS 2026); model procesu z 6 aktami użytkownika i 7 agenta (37 268 wypowiedzi, 2025)

- **Źródła (abstrakty przez Consensus):** S. Avula i in., „The Effects of System Initiative during Conversational
  Collaborative Search”, PACM HCI 2022, 29 cytowań — https://consensus.app/papers/details/cffc92baadc15444b5dc44e62cbf5369/ ;
  L. Chen i in., „When Systems Take Initiative: A Design Framework for Adaptive, Mixed-initiative Database Querying”,
  DIS 2026 — https://consensus.app/papers/details/7d8e998d3a755babb256467487e219cc/ ; S. Fu i in., „Towards a
  conversational information seeking process model”, JASIST 2025 — https://consensus.app/papers/details/31e6888e323950deb4443d1337fc9749/ .
  Wiarygodność **high** (recenzowane; abstrakty).
- **Twierdzenie:** Avula: 27 par współpracowało w Slacku z „searchbotem” granym przez bibliotekarza; trzy warunki:
  „(1) no initiative, (2) only dialog-level initiative, and (3) both dialog- and task-level initiative”; rozróżnienie:
  „dialog-level initiative involves leading a conversation for the sole purpose of establishing mutual belief …
  task-level initiative involves leading a conversation with the intent to influence the goals of the other agent(s)”;
  badane skutki: użyteczność, obciążenie, współpraca, wzorce komunikacji, korzyści i trudności (wyników liczbowych w
  abstrakcie brak). Chen: system „interprets user behavioral cues (e.g., tentativeness, focus shifts) to infer intent
  and dynamically adapts its support strategies … switching between responsive and proactive modes, and integrating
  textual responses with graphical previews”; „A controlled user study demonstrates that this adaptive approach not
  only improves task efficiency and usability but, more importantly, reduces cognitive load”. Fu: z 37 268 wypowiedzi
  „six user acts, including querying, assessing, elaborating, reformulating, instructing, and deciding, and seven
  agent acts, including answering, inquiring, checking, eliciting, soliciting, informing, and offering”; model z
  jednym podprocesem podstawowym i czterema opcjonalnymi: „need negotiation, process collaboration, result evaluation,
  and query elicitation”.
- **Mechanizm:** inicjatywa systemu ma **dwa poziomy** (upewnianie się, że rozumiemy to samo, vs wpływanie na cel) —
  pierwszy jest tani i bezpieczny, drugi wymaga zgody; adaptacja do sygnałów niepewności użytkownika (wahanie, zmiana
  fokusu) i podgląd graficzny obok tekstu zmniejszają obciążenie; akty mowy to gotowa, otwarta lista typów zdarzeń
  dla dziennika współpracy.
- **U nas:** typy inicjatywy agenta jako dane (upewnienie / propozycja celu), z osobnymi presetami zgody; „podgląd
  obok tekstu” = nasz graf jako podgląd propozycji; akty użytkownika i agenta jako typy zdarzeń w dzienniku (P37) —
  lista otwarta, seed z Fu.
- **Kaliber:** scientific. **Wysiłek:** medium.
- **Zastrzeżenia:** domeny wyszukiwania i baz danych; przenoszę przez analogię; liczb Avuli w abstrakcie brak.

---

## Czego nie znalazłem

- **Pomiaru liczby tur** w żadnym produkcie: ani Kiro, ani Spec Kit, ani Claude Code, Cursor, Devin, Copilot nie
  publikują (w przeczytanych/streszczonych źródłach) danych „o ile mniej wymian dzięki planowi / wywiadowi”. Jedyne
  liczby o turach są akademickie (interf-24: ≤5 interakcji; interf-26: tury korygujące jako miara; interf-25: kara za
  liczbę pytań w metryce).
- **Google Docs z Gemini** w roli agenta proponującego zmiany do akceptacji — jedno wyszukiwanie nic nie dało; nie
  twierdzę, że nie istnieje (R60).
- **Jira / Atlassian — dokumentacji referencyjnej** kroków akceptacji planu (tylko blogi).
- **Pełnych tekstów prac** — arXiv zablokowany; wszystkie liczby akademickie są z abstraktów (pełne wyniki, np.
  podział czasu w METR na przegląd/czekanie, nie zostały odczytane).
- **Oficjalnych stron** Kiro, GitHub Docs, Linear, Devin, Cursor, Notion, Lovable, OpenAI — zablokowane; opieram się
  na cytatach w podsumowaniach wyszukiwarki (oznaczone „medium”), których nie zweryfikowałem na stronie.
- **ADR / MADR** (decyzje architektoniczne jako rekordy ze statusem proposed/accepted/superseded) — `adr.github.io`
  zablokowane; wzorzec znany, ale bez odczytu nie wpisuję go jako ustalenia.
- **Badań o „zmęczeniu zgodami” z liczbami** — tylko zdanie producenta („po dziesiątej zgodzie”, interf-06).

## Gdzie się zatrzymałem

- Po 13 wyszukiwaniach, 3 zapytaniach Consensus, 3 zapytaniach HF i ok. 25 próbach odczytu (10 udanych). Trzy ostatnie
  źródła (README `agents.md`, dwa 404 w `github/docs`, strona `agent-view`) dodały tylko wzorzec kolejki decyzji
  (interf-07) i poza tym potwierdzały wcześniejsze ustalenia; dalsze szukanie w zablokowanym otoczeniu dawałoby
  kolejne streszczenia zamiast źródeł. Następny krok, gdyby ktoś wracał: z maszyny bez proxy przeczytać strony
  oznaczone „przez podsumowanie wyszukiwarki” i podnieść (lub obniżyć) ich wiarygodność; pełne teksty METR
  (2507.09089), HumanEvalComm (2406.00215) i Khelifi (MSR 2026) dla podziału czasu i taksonomii 42 interwencji.
