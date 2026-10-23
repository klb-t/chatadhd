# Replikacja przepływów dostawców: co istnieje na GitHubie, co mamy, na ile możemy odtworzyć (D2, część B, Fable, 2026-10-23)

Dokument projektowy (nie kod). Odpowiada na ostatnie pytanie właściciela z 2026-10-23:
*„w kwestii klonów aplikacji, które Gemini mi mówił że są na githubie – są takie które reprodukują
oryginalne work flows Twoje, codexa, i in.? jeśli nie to na ile my możemy je zreplikować?”*
Część A (import ZIP jako plik w grafie, pamięć podręczna) jest osobnym dokumentem tego samego pakietu D2.

Źródła czytane (tylko do odczytu): `docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md` (UI14 w. 1485–1495,
RE31 w. 2165–2175), `docs/APPLICATION_PROFILES.md`, `docs/APPLICATION_PROFILE_SOURCES.md`,
`loom/web/src/profiles/` (gałąź `claude/m1-w0`), `loom/data/runtime/{chat_context,tool_sources,tool_loop,
action_run,actions,import_formats,guardian}.pack`, `loom/data/policy/calibration.json`,
`loom/src/resolve/assess.cpp`, `docs/architecture/GRAF_HUB_KOD_SILNIK_2026-10-13.md`, `docs/STATE.md`,
`docs/reports/INDEX.md`. Badanie sieci (28 stron pobranych 2026-10-22 przez agenta badawczego) przyjmuję
jako dane wejściowe z podanymi tierami; niczego z sieci nie dopisałem od siebie. Nie czytałem żadnego
promptu z zbiorów „leaks” ani kodu `claw-code` (decyzja koordynatora: ryzyko prawne i prywatność —
nie tekst UI14; reguła konfigurowalna, odwrót w §9 D2).

Zasady obowiązujące w całym tekście: R58 (użyteczność nigdy nie jest powodem usunięcia opcji; „trudne”
i „niemożliwe” opisują pracę i granice techniczne, nie zakazy) i R59 (każdy konkret poniżej to przykład
mechanizmu, nie zakres; dla każdego mechanizmu podaję przestrzeń możliwości, sposób jej wyliczania
i co zostaje niepokryte). Szacunki tylko w pakietach (pakiet ≈ 50 wywołań narzędzi agenta); liczby
pakietów to mój osąd, czas liczy koordynator z mierzonego tempa.

---

## Streszczenie dla właściciela (prostymi słowami)

Gemini miał po części rację. Repozytoria istnieją — żadnej zmyślonej nazwy nie znaleziono — ale słowo
„odtwarzają oryginalny przepływ” zlewa pięć różnych rzeczy: wygląd (L1), zestaw funkcji (L2), składanie
promptu i kontekstu (L3), protokół narzędzi i pętlę agenta (L4), dokładną kopię (L5).

1. **Codex i Gemini CLI to nie klony, to oryginały na Apache-2.0** (wg README/odznaki, pliki LICENSE
   nieczytane). Kod, dokumentacja (sandbox,
   execpolicy, AGENTS.md, config, skills; GEMINI.md, MCP) leżą otwarte. Tu nie trzeba klonów — można
   czytać wprost i odtwarzać logikę z poszanowaniem licencji. Zastrzeżenie: Google od czerwca 2026
   kieruje użytkowników bezpłatnych do zamkniętego Antigravity CLI (baner w README; reszta ze źródeł wtórnych).
2. **Claude Code: brak publicznego kodu agenta** (wniosek z niepełnej listy katalogów repozytorium,
   niezweryfikowane; licencja własnościowa wg `LICENSE.md`, „All rights reserved”). Oficjalnie dostępne są formaty i protokoły
   (hooks, skills, wtyczki, subagenci, Agent SDK, ustawienia, uprawnienia, piaskownica, CLAUDE.md) oraz
   prompty systemowe claude.ai/iOS/Android (ale nie API i nie Claude Code). Otwarte „odpowiedniki”
   (opencode, Cline, Kilo, goose, Aider, OpenHands) to własne silniki na poziomie L2, nie kopie.
   `claude-code-router` to proxy do prawdziwego Claude Code. Jedyne, co twierdzi parytet (`claw-code`),
   wiąże się z wyciekiem z marca 2026 (tier B/C) — **decyzja koordynatora** (ryzyko prawne, nie tekst UI14):
   domyślnie tego nie otwieramy i nie używamy; reguła jest konfigurowalna, odwrót w §9 D2.
3. **Klony aplikacji webowych** (LibreChat, Open WebUI, LobeHub, NextChat, Chatbot UI) odtwarzają funkcje
   ChatGPT/claude.ai (L1–L2), nie prompty i nie pętle dostawców. Dwa z nich mamy już przypięte do konkretnych
   commitów jako profile interfejsu (`librechat-0.8.8`, `nextchat-2.16.1`).
4. **Co mamy:** profile aplikacji jako dane (10 plików JSON, jeden renderer „chat”, różnice wizualne to
   tylko 6 kolorów, szerokość paska i styl dymków — czyli skórka, nie inny układ), profile selektora
   `like_claude`/`like_chatgpt`/`like_gemini` ze statusem „kalibracja”, `agent.run` z pętlą narzędzi
   i propozycjami, źródła narzędzi MCP, środowiska („co mogę zrobić na telefonie”). To są fundamenty L2–L4
   po naszej stronie, bez żadnego testu porównawczego z oryginałem.
5. **Na ile możemy replikować:** L1–L2 wszystko, bez znanych ograniczeń prawnych (ocena inżynierska, nie
   porada prawna; §3). L3–L4 dla Codexa i Gemini CLI —
   z ich otwartego kodu. Dla Claude Code — formaty i protokoły z oficjalnej dokumentacji; sama pętla
   i prompty tylko jako nasza własna rekonstrukcja. L5 dla Claude Code, claude.ai, ChatGPT i aplikacji
   Gemini jest dziś niemożliwe technicznie w częściach serwerowych — kod po stronie usługi, którego nie
   mamy (ukryte klasyfikatory, narzędzia po stronie usługi, zachowanie modelu; obserwacja z 2026-10-22,
   tier A–B) — a tam, gdzie jedynym źródłem jest wyciek, domyślnie zablokowane **decyzją koordynatora**
   (ryzyko prawne; stan `blocked:legal`, konfigurowalne, odwrót w §9 D2). Zgodnie z R58
   opcja „replikuj L5” zostaje w konfiguracji — z uczciwą etykietą stanu i doradczym ostrzeżeniem, nie znika.
6. **Jak to sprawdzimy (RE31):** test różnicowy — to samo zadanie na danych syntetycznych przez oryginał
   (Ty nagrywasz) i przez nas; porównujemy ślady narzędzi, strukturę kontekstu i wynik, z tolerancją na
   niedeterminizm. Test dowodzi zgodności w zakresie nagranym, niczego więcej.

---

## 1. Werdykt o twierdzeniu Gemini i tabela zweryfikowanych projektów

**Werdykt: CZĘŚCIOWO PRAWDA.** Prawdziwe w warstwach L1–L2; w dużej mierze fałszywe w L3–L5 dla
Claude'a; dla Codexa i Gemini CLI mylące, bo tam istnieją oryginały, nie klony. Dowody w tabeli.
Zastrzeżenie: nie mamy dosłownego tekstu wypowiedzi Gemini, tylko streszczenie właściciela — nie wiemy,
które nazwy Gemini podał.

Tiery wiarygodności: **A** = strona repozytorium / plik / oficjalna dokumentacja pobrana na żywo;
**B** = widziane w wynikach wyszukiwania u wiarygodnych wydawców, nie pobrane; **C** = tylko wynik
wyszukiwania lub źródło bez podanego pochodzenia. „Deklaracja README” = funkcja opisana przez autora,
nietestowana. Daty ostatnich commitów są w większości **niezweryfikowane** (GitHub API było w sesji
badawczej niedostępne); gwiazdki ze stron, nie z API.
Kolumny „Licencja” i „Aktywność” to **obserwacje z datą 2026-10-22** (okno ważności: do następnego
sprawdzenia), nie ograniczenia: etykiety licencji są **wg README/odznaki/strony repozytorium, pliki
LICENSE nieczytane** (chyba że wiersz mówi inaczej), a rytm wydań i liczba gwiazdek zmieniają się.

| # | Projekt (URL) | Czego dotyczy | Warstwa | Licencja | Aktywność | Tier / uwagi |
|---|---|---|---|---|---|---|
| 1 | Claude Code — `github.com/anthropics/claude-code` | oryginał | n/d (brak publicznego kodu agenta — wniosek z niepełnej listy katalogów, niezweryfikowane) | **własnościowa** (`LICENSE.md`: „All rights reserved”, Commercial ToS) | v2.1.309 z 2026-10-22 (feed wydań) | A; repo to tracker zgłoszeń, changelog, wtyczki, przykłady; „brak kodu agenta” wywnioskowany z niepełnej listy katalogów |
| 2 | Dokumentacja Claude Code — `code.claude.com/docs/llms.txt` | formaty i protokoły | źródło L2–L4 | dokumentacja Anthropic | 228 stron/język | A (indeks tytułów; treść stron nieczytana): hooks, skills, plugins, subagents, Agent SDK, settings, permissions, MCP, memory, sandboxing |
| 3 | System prompts claude.ai — `platform.claude.com/docs/en/release-notes/system-prompts` | claude.ai, iOS, Android | L3 | oficjalna publikacja | od Haiku 3 do Fable 5.1 | A; strona: „do not apply to the Claude API”; nie sprawdzono, czy zawiera definicje narzędzi/artefaktów |
| 4 | OpenAI Codex — `github.com/openai/codex` | oryginał | źródło L3–L4 | Apache-2.0 | rust-v0.164.0-alpha.11.1 z 2026-10-22 | A; crate'y: core, exec, execpolicy, linux-sandbox, apply-patch, protocol, hooks, prompts, mcp, memories, plugin; docs/: agents_md, config, sandbox, execpolicy, exec, skills, slash_commands; „Codex Web” osobny, zamknięty |
| 5 | Every Code — `github.com/just-every/code` | fork Codexa | dziedziczy L4 | Apache-2.0 | ~4.0k★ | A dla istnienia; dodatki (multi-agent, CDP) = deklaracja README |
| 6 | Gemini CLI — `github.com/google-gemini/gemini-cli` | oryginał | źródło L3–L4 | Apache-2.0 | ~107.5k★ | A; baner: dla planów bezpłatnych/Google One zastąpione przez Antigravity CLI 2026-06-18; Antigravity zamknięty — tier B |
| 7 | opencode — `github.com/sst/opencode` | alternatywa dla Claude Code | L2 | MIT | ~220k★ | A; agenty build/plan, @general; strona pokazała `anomalyco/opencode` — przekierowanie niepotwierdzone |
| 8 | Cline — `github.com/cline/cline` | agent IDE/CLI | L2 | Apache-2.0 | ~70.7k★ | A; Plan/Act, MCP, .clinerules, skills; wtyczki JetBrains nie są OSS |
| 9 | Roo Code — `github.com/RooCodeInc/Roo-Code` | agent IDE | L2 | Apache-2.0 | **zarchiwizowane 2026-05-15** | A; tylko wiedza historyczna |
| 10 | Kilo Code — `github.com/Kilo-Org/kilocode` | agent IDE/CLI | L2 | MIT | ~27.7k★ | A; FAQ: fork OpenCode (niesprawdzone w gicie); `--auto` zatwierdza uprawnienia |
| 11 | goose — `github.com/block/goose` | agent ogólny | L2 | Apache-2.0 | ~55.5k★ | A; Agentic AI Foundation; Rust; ACP |
| 12 | Crush — `github.com/charmbracelet/crush` | agent terminalowy | L2 | **FSL-1.1-MIT (source-available, nie OSI)** (wg README/odznaki; LICENSE nieczytany) | ~28.8k★ | A; telemetria z opt-outem — sprzeczne z naszą zasadą „bez telemetrii” |
| 13 | Aider — `github.com/Aider-AI/aider` | para-programista | L2 (inny przepływ) | Apache-2.0 | ~49.7k★ | A; wzór mapy repo i auto-commitów |
| 14 | OpenHands — `github.com/All-Hands-AI/OpenHands` | agent w piaskownicy | L2 | MIT (enterprise nieznane) | ~91.6k★ | A; nie klon Claude Code ani Codexa |
| 15 | claude-code-router — `github.com/musistudio/claude-code-router` | **proxy** do prawdziwego Claude Code/Codex | n/d | MIT | ~37.8k★ | A; nie klon — bramka trasująca (wzór dla naszej bramki do modeli) |
| 16 | claw-code — `github.com/ultraworkers/claw-code` | deklarowany port Claude Code | deklarowane L3–L4, **niezweryfikowane** | MIT (repo; nie nadaje praw do cudzego kodu) | ~195k★ (niezależnie niesprawdzalne) | A dla README („not affiliated… does not claim ownership”); pochodzenie z wycieku 2026-03-31 — tier B/C; **domyślnie nie otwierać** (decyzja koordynatora: ryzyko prawne; klasa `extraction_leak`, §3) |
| 17 | LibreChat — `github.com/danny-avila/LibreChat` | klon ChatGPT | L2 | MIT | ~46.2k★, v0.8.8 | A; artefakty, code interpreter, agenci+subagenci+skills, MCP, rozgałęzianie, web search, STT/TTS (deklaracje README) |
| 18 | Open WebUI — `github.com/open-webui/open-webui` | UI ChatGPT-podobny | L2 | **„Open WebUI License” z klauzulą brandingu (nie OSI)** (wg README/odznaki; LICENSE nieczytany) | ~156.5k★ | A dla licencji; RAG, web search, głos, pamięć, MCP/OpenAPI; „artifacts” tylko KV |
| 19 | LobeHub — `github.com/lobehub/lobe-chat` | czat/agenci wielomodelowy | L2 | **nierozstrzygnięta** (README: Community License; plakietka: Apache-2.0; LICENSE nieczytany) | ~83.4k★ | A dla istnienia; licencja do sprawdzenia przed jakimkolwiek użyciem |
| 20 | NextChat — `github.com/ChatGPTNextWeb/NextChat` | UI wielomodelowy | L1–L2 | MIT | ~88.9k★ | A; maski, wtyczki, Artifacts (v2.14.0), Realtime Chat (v2.15.8); mamy profil `nextchat-2.16.1` |
| 21 | Chatbot UI — `github.com/mckaywrigley/chatbot-ui` | UI ChatGPT-podobny | L1 | MIT | ~33.3k★, „major update in progress” | A; stan utrzymania niejasny |
| 22 | system_prompts_leaks — `github.com/asgeirtj/system_prompts_leaks` | zbiór promptów wielu dostawców | deklarowane L3 | CC0 (repo; nie przenosi praw do cudzego tekstu) | ~70.1k★ | **C**: brak pochodzenia; nazwa mówi „leaks”; **domyślnie nie czytać, nie kopiować** (decyzja koordynatora, klasa `extraction_leak`, §3) |
| 23 | CL4R1T4S — `github.com/elder-plinius/CL4R1T4S` | zbiór „wycieków” | deklarowane L3 | AGPL-3.0 (repo) | ~51.5k★ | **C**: README zachęca do „leak, extract”; strona zawierała polecenie do modelu czytającego (zignorowane) — najwyższe ryzyko ToS (ocena koordynatora, ToS nieczytane); **domyślnie nie czytać** (klasa `extraction_leak`, §3) |
| 24 | system-prompts-and-models-of-ai-tools — `github.com/x1xhlol/system-prompts-and-models-of-ai-tools` | Cursor, Devin, Windsurf, v0… | deklarowane L3 | GPL-3.0 (repo) | ~144k★ | **C**: brak pochodzenia; reklama usługi; w klasie `extraction_leak` (decyzja koordynatora, §3) |
| 25 | claude-code-system-prompts — `github.com/Piebald-AI/claude-code-system-prompts` | prompty Claude Code wyciągnięte z paczki npm | deklarowane L3 | MIT (repo) | do v2.1.308; dwie niespójne daty na stronie | **B/C**: metoda opisana i odtwarzalna, ale to ekstrakcja z własnościowego pakietu, nie publikacja — **domyślnie nie używać jako wejścia** (decyzja koordynatora, klasa `extraction_leak`, §3; nie tekst UI14) |

Poza tabelą (nie spełniają warunku „zweryfikowane istnienie”): GeminiProChat, Gemini Next Chat,
Gemini-Clone i podobne demonstracje — tylko wyniki wyszukiwania, tier C. Niesprawdzeni kandydaci:
Jan, AnythingLLM, Onyx, Dify, big-AGI, Continue, open-canvas, GPT-Researcher, kolejne forki Codexa.

Krytyka materiału: liczba gwiazdek mierzy popularność, nie wierność; README to deklaracja; „nie pokazano
na stronie” często znaczy „sekcja ładowana skryptem”, nie „brak”.

---

## 2. Poziomy wierności L1–L5 na dostawcę

Definicje (ta pięciostopniowa skala to przykład podziału przestrzeni; sama przestrzeń to iloczyn
*aplikacja × wersja × platforma × warstwa × źródło dowodu*; punkt 7 mówi, jak ją wyliczamy):

- **L1 wygląd/UX** — układ, kolory, gesty, skróty, kolejność elementów.
- **L2 parytet funkcji** — ta sama lista rzeczy, które da się zrobić (rozgałęzianie, artefakty, tryb planowania…).
- **L3 składanie promptu i kontekstu** — co, w jakiej kolejności i z jakich źródeł trafia do modelu.
- **L4 protokół narzędzi i pętla agenta** — formaty definicji narzędzi, pętla wywołań, zatwierdzanie, piaskownica, zdarzenia (hooks).
- **L5 dokładna kopia** — nieodróżnialne zachowanie na tym samym wejściu.

Co mamy po naszej stronie (zweryfikowane w kodzie `claude/m1-w0`):

| Nasz element | Gdzie | Co daje | Czego nie daje |
|---|---|---|---|
| Profile aplikacji (`loom.application_profile/1`) | `loom/web/src/profiles/data/*.json` (10 plików: `chatgpt-inspired` r1/r2, `claude-inspired` r1/r2, `gemini-inspired` r1/r2, `librechat-0.8.8`, `nextchat-2.16.1`, `loom-default` r1/r2); `runtime.ts`; e2e `application-profile*.mjs`, `m1-profile-native.mjs`, `profile-view-geometry.mjs` | L1 jako dane: `presentation.tokens` (6 kolorów), `sidebar.{side,width}`, `content_width`, `message_style ∈ {plain,bubble,user-bubble}`; `composer.submit`; akcje `create/select/send/cancel/edit/restore/exclude/knowledge/memory/import/settings`; deterministyczne workflows; `target.version = null` jawnie; `evidence.status` i `gaps` | **Jeden renderer `chat`** (`createProfileRegistry`, `runtime.ts:186`): różnica między profilami to skórka, nie inny układ ani inne komponenty. Profile dostawców mają `evidence.status = "inspired"` ze źródłem… LibreChat, nie oryginałem. Nic z L3–L4 |
| Profile selektora (`selector_profiles`) | `loom/data/runtime/chat_context.pack`: `loom`, `loom_legacy`, `compat`, `lean`, `graph_in`, `like_claude`, `like_chatgpt`, `like_gemini` | L3 jako dane: flagi źródeł (`profile, rules, memory, project, history, graph, search, kb`), `order: stability`, `breadth/depth`, `representation`, `flow`, `budget_tokens` | `status: "calibration"` z notą: „to nasze przybliżenie, nie odtworzenie; dokładne odtworzenie i testy porównawcze są w etapie replikacji” — czyli L3 bez żadnego dowodu zgodności |
| `agent.run` + pętla narzędzi | `actions.pack` (id `agent.run`, `/api/agent/run`; `agent.call`), `tool_loop.pack` (`expose`, `policies`, `max_steps`), `action_run.pack` (`max_depth` kompozytów, `use_model`), `action_rules.pack` | L4 jako dane: model dostaje akcje katalogu (193 id; prefiksy m.in. `guardian, device, storage, semantic, tool_source, message, knowledge, graph, conversation, catalog, archive, settings, profile, memory`) jako narzędzia; wywołanie niedozwolone przez politykę = propozycja, pętla staje, `resume` po „tak” | Brak piaskownicy procesów, brak formatu SKILL.md/hooks w sensie dostawców, brak subagentów jako osobnych pętli (jest zagnieżdżanie kompozytów do `max_depth`) |
| Źródła narzędzi | `tool_sources.pack`: `kinds: [mcp]`, `unknown_effects` (`reads_only, writes, sends_off_device, costs_money, irreversible, touches_private_data, effects_unknown`), reguła `confirm:tool_source.unknown`, `tool_source.trust` | L4: MCP jako pierwszy rodzaj; każdy obcy tool pyta, dopóki użytkownik nie zaufa | Tylko MCP; brak „plugin manifest” w stylu Claude Code/Codex |
| Środowiska | port `environments` (STATE.md: „co mogę zrobić na telefonie / w tym katalogu” z grafu; R32–R34) | deklaracja możliwości urządzenia/katalogu | to nie piaskownica wykonania |
| Rejestr formatów importu | `import_formats.pack` (UNWIRED) | precedens „rejestr otwarty jako dane” | niepodpięty do dispatcherów |

Oceny (słowa `łatwe/średnie/trudne` = wysiłek; `niemożliwe` = granica techniczna; żadna nie jest wetem):

### 2.1 Claude Code

| Warstwa | Dostępne otwarcie | Mamy | Luka | Ocena |
|---|---|---|---|---|
| L1 | brak (TUI własnościowe; wygląd obserwowalny) | profil `claude-inspired` (skórka czatu, nie TUI) | renderer terminalowy/„transcript” jako drugi renderer w rejestrze | **średnie** — nowy renderer to kod; dane profilu już go opiszą |
| L2 | lista funkcji z indeksu dokumentacji (hooks, skills, plugins, subagents, permissions, memory, MCP, sandboxing, Agent SDK) | `agent.run`, propozycje, reguły, MCP, pamięć | skills jako pliki, hooks jako zdarzenia przed/po akcji (mamy `action_rules` — analog), subagenci jako osobne pętle z własnym budżetem, piaskownica | **średnie** dla skills/hooks/subagentów (mapują się na akcje i reguły jako dane); **trudne** dla piaskownicy (natywny kod per platforma; na Androidzie ograniczona) |
| L3 | **nie** (oficjalne prompty obejmują claude.ai, nie Claude Code — tier A) | `like_claude` (status kalibracja) | brak legalnego źródła tekstu | **trudne i tylko jako rekonstrukcja**: z zachowania nagranego przez właściciela (RE31) i z dokumentacji; nigdy z Piebald-AI/leaks |
| L4 | formaty plików i zdarzeń z dokumentacji (`CLAUDE.md`, settings, hooks JSON, SKILL.md, manifest wtyczek, tryby uprawnień, Agent SDK Py/TS) | pętla narzędzi z regułami | czytniki tych formatów jako profile importu; pętla zgodna co do kolejności zdarzeń | **średnie** — formaty są opublikowane; zgodność pętli mierzy test różnicowy |
| L5 | nie | — | brak publicznego kodu agenta i usługi (wniosek z niepełnej listy katalogów, niezweryfikowane) | **dziś niemożliwe** w częściach serwerowych (kod po stronie usługi, którego nie mamy: klasyfikatory bezpieczeństwa, telemetria/serwer, zachowanie modelu; obserwacja 2026-10-22, tier A–B); część oparta na wycieku promptu — domyślnie `blocked:legal` (decyzja koordynatora, ryzyko prawne; nie tekst UI14), konfigurowalne |

### 2.2 OpenAI Codex (CLI/IDE; „Codex Web” osobno)

| Warstwa | Dostępne otwarcie | Mamy | Luka | Ocena |
|---|---|---|---|---|
| L1 | kod TUI (`codex-cli`, Rust) — Apache-2.0 | skórka czatu | renderer TUI | **średnie** |
| L2 | docs: `exec`, `execpolicy`, `sandbox`, `agents_md`, `config`, `skills`, `slash_commands`; crate'y `memories`, `plugin`, `hooks`, `mcp` | odpowiedniki częściowe (reguły, MCP, pamięć) | execpolicy jako polityka wykonania poleceń = dane; slash commands | **łatwe–średnie** (dokumentacja + kod czytelne wprost) |
| L3 | katalog `prompts` w repo (treść **nieczytana**; `core/prompt.md` dał 404 — lokalizacja się zmieniła) | `like_chatgpt` (kalibracja; to profil aplikacji ChatGPT, nie Codexa — do rozdzielenia) | profil selektora `like_codex` zbudowany z kodu `codex-rs/core` | **łatwe–średnie**, legalne (Apache-2.0: logika + atrybucja/NOTICE) |
| L4 | `protocol`, `apply-patch`, `linux-sandbox`, `execpolicy`, `hooks`, `mcp` — kod | pętla narzędzi | piaskownica Linux/macOS, patch-apply jako akcja | **średnie** (logika), **trudne** piaskownica na Androidzie |
| L5 | kod CLI tak; **Codex Web (chmura) nie** | — | — | CLI: **trudne, osiągalne lokalnie** (ten sam model przez OpenRouter nie gwarantuje identycznego zachowania — inne nagłówki/parametry usługi; niezweryfikowane); Codex Web: **dziś niemożliwe** (serwer OpenAI, kod niedostępny — tier A: „Codex Web osobny, zamknięty”) |

### 2.3 ChatGPT (aplikacja)

| Warstwa | Dostępne otwarcie | Mamy | Luka | Ocena |
|---|---|---|---|---|
| L1 | obserwacja; klony LibreChat/NextChat/Open WebUI (L1–L2, licencje różne) | `chatgpt-inspired` (skórka), `librechat-0.8.8`, `nextchat-2.16.1` przypięte do commitów | układy (canvas, projekty, pamięć w ustawieniach) jako kolejne renderery | **łatwe** skórka, **średnie** nowe renderery |
| L2 | deklaracje README klonów; dokumentacja OpenAI (**nieosiągnięta** w badaniu — błąd DNS) | rozgałęzianie (mamy natywnie), web search (plugin `web` w OpenRouter), pamięć, import eksportów ChatGPT (`mapping`) | canvas/artefakty, projekty, deep research, głos realtime, GPTs/„custom instructions” jako profil | **średnie** |
| L3 | **nie znaleziono** (publikacji promptów ChatGPT nie znaleziono; dokumentacja OpenAI nieosiągnięta w badaniu — DNS, obserwacja 2026-10-22, niezweryfikowane; zbiory „leaks” — tier C, domyślnie nie używamy: decyzja koordynatora) | `like_chatgpt` (kalibracja) | tylko rekonstrukcja z nagrań | **trudne**, rekonstrukcja behawioralna |
| L4 | nie (narzędzia serwerowe) | pętla narzędzi | — | **dziś niemożliwe** co do oryginalnych narzędzi serwerowych (kod po stronie usługi, którego nie mamy); **średnie** jako nasze odpowiedniki |
| L5 | nie | — | — | **dziś niemożliwe** (serwer, model, klasyfikatory, pamięć serwerowa — kod po stronie usługi, którego nie mamy) |

### 2.4 claude.ai (web/iOS/Android)

| Warstwa | Dostępne otwarcie | Mamy | Luka | Ocena |
|---|---|---|---|---|
| L1 | obserwacja | `claude-inspired` (skórka; `composer.submit: mod-enter` jako wybór demonstracyjny, nie pomiar) | artefakty, projekty, panel pamięci | **łatwe** skórka, **średnie** renderery |
| L2 | lista funkcji obserwowalna; eksport claude.ai (`chat_messages`) importujemy | import, rozgałęzianie, pamięć | artefakty, projekty jako kontener kontekstu (mamy `project` w selektorze), „styles” | **średnie** |
| L3 | **tak, oficjalnie**: prompty systemowe claude.ai/iOS/Android na `platform.claude.com` (tier A; „nie dotyczą API”) | `like_claude` (kalibracja) | profil `claude_ai_<data wpisu>` zbudowany z oficjalnego tekstu, wersjonowany datą wpisu | **łatwe–średnie**, legalne (oficjalna publikacja; nie sprawdzono, czy wpisy zawierają definicje narzędzi/artefaktów — do sprawdzenia w pakiecie) |
| L4 | nie (narzędzia serwerowe: artefakty, wyszukiwanie, pamięć) | nasze odpowiedniki | — | **dziś niemożliwe** oryginalne (kod po stronie usługi, którego nie mamy); **średnie** odpowiedniki |
| L5 | nie | — | — | **dziś niemożliwe** (serwer, model — kod po stronie usługi, którego nie mamy; sam prompt nie czyni aplikacji) |

### 2.5 Gemini (aplikacja i CLI)

| Warstwa | Dostępne otwarcie | Mamy | Luka | Ocena |
|---|---|---|---|---|
| L1 | CLI: kod Apache-2.0; aplikacja: obserwacja; klony Gemini tier C | `gemini-inspired` (skórka, `message_style: bubble`) | renderer TUI (CLI); Gems/Canvas/Live (aplikacja) | **średnie** |
| L2 | CLI README: `GEMINI.md`, MCP (`~/.gemini/settings.json`), narzędzia (system plików, powłoka, web fetch/search), piaskownica, checkpointing, rozszerzenia | MCP, pamięć, reguły | checkpointing jako akcja na `run_store`; `GEMINI.md` jako format importu profilu | **łatwe–średnie** (CLI); aplikacja: **średnie** (generacja muzyki → RE31 wymaga dostawcy Lyria przez nasz `use_model`) |
| L3 | CLI: kod; aplikacja: nie | `like_gemini` (kalibracja; „kanał kontekst osobisty”) | `like_gemini_cli` z kodu | CLI **łatwe–średnie**; aplikacja **trudne** (rekonstrukcja) |
| L4 | CLI: kod | pętla narzędzi | piaskownica | **średnie**; piaskownica na Androidzie **trudne** |
| L5 | CLI tak (lokalnie, jak Codex CLI); aplikacja nie; **Antigravity CLI zamknięty** | — | — | CLI **trudne, osiągalne**; aplikacja/Antigravity **dziś niemożliwe** (kod zamknięty — tier B: baner README, reszta ze źródeł wtórnych) |

### 2.6 Czego dziś nie da się zreplikować (granica techniczna; obserwacja z datą 2026-10-22)

- Zachowanie **po stronie serwera dostawcy**: ukryte klasyfikatory, moderacja, narzędzia serwerowe
  (artefakty claude.ai, canvas ChatGPT, deep research, pamięć serwerowa), limity i trasowanie wewnątrz usługi.
  Podstawa: brak publicznego kodu tych usług (tier A–B, obserwacja 2026-10-22); wniosek o tym, co
  dokładnie robią, jest **niezweryfikowany**.
- Zachowanie **po stronie modelu**, które zależy od nagłówków/parametrów dostępnych tylko aplikacji
  dostawcy (nie przez OpenRouter), od wersji modelu niewidocznej w API i od niedeterminizmu
  (**niezweryfikowane**: hipoteza inżynierska, bez źródła A–B; do sprawdzenia testem różnicowym RE31).
- **Dryf**: wszystko powyżej zmienia się z wydaniami (rytm wydań to obserwacja z feedów 2026-10-22,
  tier A–B, niezmierzony: Claude Code v2.1.309 z 2026-10-22, Codex rust-v0.164.0-alpha.11.1 z 2026-10-22;
  nie ograniczenie). „Replika” jest zawsze repliką *wersji z datą*; profil bez `target.version` i daty to przybliżenie.

Zgodnie z R58: opcja „poziom replikacji: L5” zostaje w ustawieniach dla każdego celu; silnik odpowiada
stanem (`achievable | approximation | blocked:legal | blocked:server_side`) i doradczym, nienatrętnym
ostrzeżeniem z „nie pytaj ponownie”. Nie usuwa, nie degraduje.

---

## 3. Granice prawne i ToS (UI14 jako cytat; reszta to decyzje koordynatora)

Nie jestem prawnikiem; poniżej wnioski inżynierskie, nie porada prawna. UI14 (właściciel, dosłownie,
`SLOWA_WLASCICIELA_2026-10-16.md:1488`): „docelowo [...] opcja przeszukiwania wszystkich apek i nie tyle
klonowania ale odwzorowywania interfejsów na tyle na ile prawa autorskie pozwalają.” To jedyne dosłowne
zdanie właściciela w tej sekcji; klasy i reguły w tabeli niżej to **decyzje koordynatora** (domyślne
presety w danych, konfigurowalne, z odwrotem w §9), nie tekst UI14.

| Klasa źródła | Przykłady | Użycie u nas | Warunki |
|---|---|---|---|
| **official** — oficjalna publikacja dostawcy | prompty claude.ai (`platform.claude.com`), dokumentacja Claude Code, docs Codexa/Gemini CLI | L2–L4: formaty, protokoły; L3: tekst promptów claude.ai może być **cytowany jako dane wejściowe profilu** z odnośnikiem i datą wpisu | zapisać URL, datę pobrania, wersję; obserwować warunki użycia strony (nie sprawdzone) |
| **open_license** — kod na licencji permisywnej | Codex (Apache-2.0), Gemini CLI (Apache-2.0), LibreChat (MIT), NextChat (MIT), Cline, goose, Aider, OpenHands, opencode | odtwarzanie logiki, czytanie struktur, a przy kopiowaniu fragmentów — zachowanie licencji i NOTICE | Apache-2.0: NOTICE i oznaczenie zmian; MIT: nota licencyjna. Projekt dotąd **nie kopiował kodu** (APPLICATION_PROFILE_SOURCES: „did not copy third-party implementation code”) — zostajemy przy tym jako preset |
| **restricted_license** — source-available / klauzule | Crush (FSL-1.1-MIT), Open WebUI (klauzula brandingu), LobeHub (nierozstrzygnięte) | tylko obserwacja zachowania i czytanie dokumentacji; **zero kodu do czasu przeczytania LICENSE** (decyzja koordynatora o kolejności prac, nie zakaz: po przeczytaniu licencji reguła zmienia się w danych) | zadanie: przeczytać licencje (pakiet 7) |
| **extraction_leak** — wyciek lub ekstrakcja z własnościowego pakietu | claw-code, Piebald-AI, asgeirtj, CL4R1T4S, x1xhlol | **domyślna reguła odmowy: nie czytać, nie kopiować, nie używać jako wejścia** — decyzja koordynatora (ryzyko prawne: ToS, prawa autorskie, złośliwy kod w „lustrach” — ocena, ToS nieczytane), nie tekst UI14; stan źródła `blocked:legal`; konfigurowalna przez osobę, odwrót w §9 D2 | profil w grafie może *odnotować istnienie* takiego źródła z tagiem, bez treści |
| **observation** — nagranie zachowania oryginału przez właściciela na jego koncie | zrzuty, zapisy sesji, eksporty (RE31) | wzorzec do testów różnicowych; **tylko logika**, nie grafika | ToS dostawcy co do automatyzacji — nagranie ręczne, nie scraping; dane prywatne właściciela nie opuszczają urządzenia |

Tag pochodzenia (`provenance.class`) jest polem każdego źródła w grafie (sekcja 7); zbiór wartości powyżej
to preset otwarty (dodanie nowej klasy = dane).

---

## 4. Jak testujemy replikację (RE31)

**Mechanizm ogólny: test różnicowy sparametryzowany trójką (cel, wersja/data, scenariusz).**

1. **Wzorzec** = nagranie zachowania oryginału na *danych syntetycznych* przez właściciela (RE31, zapis w rejestrze SLOWA — streszczenie, nie dosłowne słowa: „może nagrać
   dokładne zachowanie oryginalnej aplikacji jako wzorzec”). Nagranie importujemy jak każdy eksport
   (część A: plik → archiwum → eksport → dostawca → wersja) i oznaczamy jako węzeł `reference_recording`
   z `provenance.class = observation`, datą i wersją aplikacji (jeśli widoczna; inaczej `null`).
2. **Przebieg nasz** = ten sam scenariusz przez nasz profil replikacji (profil aplikacji + profil selektora +
   polityka pętli), z zapisem: struktury kontekstu („co poszło do modelu” — już istnieje w M1), śladu
   narzędzi (`run_store`), wyników i kosztów (profil ekonomii tokenów z P38).
3. **Porównanie** warstwowo: L1 — zrzuty i geometria (`profile-view-geometry.mjs` już istnieje) z tolerancją;
   L2 — zbiór dostępnych akcji (macierz z sekcji 5) i ich wyniki na scenariuszu; L3 — kolejność i źródła
   bloków kontekstu (porównanie struktury, nie tekstu); L4 — sekwencja wywołań narzędzi i punktów
   zatwierdzenia; L5 — pełna równość wyjścia (zwykle fałsz, raportowana uczciwie).
4. **Wynik** trafia (a) do karty wierności profilu (sekcja 7) i (b) do rejestru wiarygodności źródła, z którego
   dany element profilu pochodził (sekcja 7.3).

**Przestrzeń możliwości i jej wyliczanie:** scenariusze są generowane, nie wybierane ręcznie — generator
losuje z gramatyki (rozmowa o N turach, z/bez załącznika, z/bez edycji wstecz, z/bez narzędzia, język,
długość) ze stałym ziarnem; każdy przebieg zapisuje ziarno, więc jest odtwarzalny. Nagranie oryginału
jest kosztowne (ręczne), więc generator ma tryb „minimalny zestaw rozróżniający” — wybiera scenariusze,
które różnicują profile między sobą (testy własności: dla każdej pary profili istnieje scenariusz
rozróżniający; profil wczytany ↔ zapisany jest identyczny; każda ścieżka workflow deterministyczna).

**Co test może udowodnić:** zgodność na *nagranym zbiorze*, w *nagranej wersji*, z *tolerancją*. **Czego nie:**
zgodności poza zbiorem; zachowania serwerowego; zgodności w przyszłej wersji; że identyczny prompt da
identyczną odpowiedź (niedeterminizm — stąd N powtórzeń i miara rozkładu, nie jedna próbka; RE27:
„stabilność metody w 12 powtórzeniach” jako przykład liczby, nie reguła). Jedno nagranie to nie specyfikacja.

Niepokryte: warstwy, których właściciel nie nagra (np. tryby płatne, których nie ma); zmiany, które dostawca
wprowadza po nagraniu — wykrywane dopiero kolejnym nagraniem (dryf jako zjawisko mierzone, sekcja 7.3).

---

## 5. „Suma funkcjonalności”: macierz unii funkcji

**Metoda.** Wiersz = funkcja atomowa, testowalna (ma scenariusz i kryterium „jest/nie ma”). Kolumna =
aplikacja (z wersją/datą). Komórka = `{status ∈ {present, absent, partial, unknown}, evidence[]}`, gdzie każdy
dowód ma `{url, provenance.class, tier, observed_at, test_result?}`. Status komórki nie jest wpisywany
z pamięci — wynika z dowodów: oficjalna dokumentacja → kod (dla OSS) → test na danych syntetycznych → inaczej
`unknown`. Macierz jest **danymi** (`loom/data/replication/feature_matrix.json`, propozycja miejsca), renderowaną
jako perspektywa grafu (wiersze i kolumny to węzły, komórki to krawędzie `has_feature` z dowodami), więc ten
sam mechanizm pokazuje też „co z tego mamy my” i „czego brakuje” — kolumna `loom` wypełniana testem e2e, nie
deklaracją.

**Wypełnianie listy wierszy (R59 pkt 3):** (a) ziarno = funkcje wymieniane w README/dokumentacji
najpopularniejszych aplikacji z tabeli w sekcji 1; (b) przed wydaniem — agent przeszukuje sieć dla każdej
kolumny („changelog”, „what's new”, dokumentacja) i proponuje nowe wiersze jako plik do przeglądu;
(c) w czasie działania — przy imporcie eksportu z aplikacji/wersji nieobecnej w kolumnach agent
„nieobsługiwane formaty/aplikacje” tworzy kolumnę i wiersze jako propozycję do potwierdzenia.
Przestrzeń: wszystkie funkcje × wszystkie aplikacje × wersje — nieskończona; pokrycie mierzymy odsetkiem komórek
`unknown` i wiekiem najstarszego dowodu w kolumnie.

**Pierwsze 15 wierszy, które mogę wypełnić dowodem już teraz** (tylko komórki z dowodem; reszta `unknown`;
„R” = deklaracja README tier A, „D” = indeks oficjalnej dokumentacji tier A, „K” = nasz kod/dane
zweryfikowane w `claude/m1-w0`, „N” = nazwa crate'u/pliku, treść nieczytana):

| # | Funkcja | Claude Code | Codex | Gemini CLI | LibreChat | Open WebUI | NextChat | opencode / Cline / Kilo | Loom (my) |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Źródła narzędzi MCP | present (D) | present (N: `mcp`, `rmcp-client`) | present (R) | present (R) | present (R: MCP/MCPO) | unknown | present (R, wszystkie trzy) | present (K: `tool_sources.pack`, `kinds:[mcp]`) |
| 2 | Tryb planowania / tylko-odczyt vs działanie | present (D: permission modes) | present (N: `execpolicy`; tryby zatwierdzania **unverified**) | unknown | unknown | unknown | unknown | present (R: build/plan; Plan/Act; Code/Plan/Ask/Debug) | partial (K: polityki `tool_loop`, propozycja zatrzymuje pętlę; brak nazwanego trybu „plan”) |
| 3 | Zatwierdzanie wywołań narzędzi przez człowieka | present (D) | present (N: `execpolicy`) | unknown | unknown | unknown | unknown | present (R: Cline; Kilo `--auto` je omija) | present (K: `confirm:tool_source.unknown`, propozycje, `resume`) |
| 4 | Subagenci | present (D) | unknown | unknown | present (R: agents with subagents) | unknown | unknown | present (R: opencode `@general`) | partial (K: `agent.call`, zagnieżdżanie kompozytów `max_depth`; brak osobnej pętli z budżetem) |
| 5 | Skills jako pliki (SKILL.md itp.) | present (D) | present (N: `docs/skills.md`) | unknown | present (R) | present (R: skille) | unknown | present (R: Cline) | **absent (K: nie znalazłem w packach)** |
| 6 | Plik instrukcji projektu (CLAUDE.md / AGENTS.md / GEMINI.md / .clinerules) | present (D) | present (N: `agents_md.md`) | present (R: `GEMINI.md`) | unknown | unknown | unknown | present (R: `.clinerules`) | partial (K: drzewo pamięci `memory.json` + węzeł profilu; brak czytnika tych formatów) |
| 7 | Hooks (zdarzenia przed/po akcji) | present (D) | present (N: crate `hooks`) | unknown | unknown | present (R: filtry/akcje) | unknown | unknown | partial (K: `action_rules.pack` — reguły przed uruchomieniem; brak zdarzeń „po”) |
| 8 | Piaskownica wykonania | present (D: sandboxing) | present (N: `linux-sandbox`, `sandbox.md`) | present (R) | unknown | unknown | unknown | unknown | **absent** (K: `environments` deklaruje możliwości, nie izoluje) |
| 9 | Wyszukiwanie w sieci | unknown | unknown | present (R: web fetch/search) | present (R: dostawcy+scrapery+rerankery) | present (R) | unknown | unknown | present (K: plugin `{"id":"web"}` przez OpenRouter — CLAUDE.md) |
| 10 | Trwała pamięć między rozmowami | present (D: memory) | present (N: `memories`) | unknown | unknown (R nie pokazuje) | present (R) | unknown | unknown | present (K: `memory.json`, graf, `memory.pack`) |
| 11 | Rozgałęzianie / wersje wiadomości | n/d (CLI) | n/d | n/d | present (R: fork/branch) | unknown | unknown | n/d | present (K: `status=version`, `version_group_id`) |
| 12 | Artefakty / canvas | n/d | n/d | n/d | present (R: Code Artifacts) | partial (R: tylko KV, bez widoku) | present (R: od v2.14.0) | n/d | unknown (nie sprawdzałem) |
| 13 | Głos (STT/TTS/realtime) | unknown | unknown | unknown | present (R) | present (R) | present (R: Realtime v2.15.8) | unknown | partial (K: STT Groq/Google w Kivy; P39 multimodal — projekt) |
| 14 | Wielu dostawców modeli / trasowanie | n/d (jeden dostawca; `claude-code-router` dodaje) | unknown | unknown | present (R) | present (R: Ollama/OpenAI API) | present (R) | present (R: opencode, Kilo) | present (K: OpenRouter, `model.pack`); failover **unknown** |
| 15 | Import eksportów innych aplikacji | unknown | unknown | unknown | unknown | unknown | unknown | unknown | present (K: `importer.py`/`import.pack`: Claude.ai `chat_messages`, ChatGPT `mapping`, ZIP, HTML, MHT, SQLite…) |

Uwaga do wiersza 5: „absent” u nas oznacza „nie znalazłem w `loom/data/runtime/*.pack` i `actions.pack`
w tej sesji” — to obserwacja datowana (R60), nie wyrok.

---

## 6. Pakiety wdrożeniowe (kolejność, akceptacja)

Pakiet ≈ 50 wywołań narzędzi agenta; liczby to mój osąd. Pierwsze pakiety dają właścicielowi coś do
dotknięcia na telefonie (powłoka Androida działa w emulatorze; prawdziwy telefon nadal nie — STATE.md).

| # | Pakiet | Zawartość | Akceptacja | Rozmiar |
|---|---|---|---|---|
| R1 | **Karta wierności profilu w grafie** (telefon) | Każdy profil aplikacji i selektora dostaje węzeł `replication_profile` z polami: cel, wersja/data (`null` jawnie), warstwy L1–L5 ze statusem `{claimed, approximated, tested:pass/fail, blocked:legal, blocked:server_side}`, źródła z `provenance.class` i tierem, luki. Dane z dzisiejszych `evidence.gaps` i not `status: calibration` przeniesione 1:1. Widok w grafie na Androidzie i w web | Na telefonie (emulator): otwieram `like_claude` → widzę „przybliżenie, nietestowane, źródło: nasza kalibracja”, luki i przycisk „nagraj wzorzec”. Test: każdy istniejący profil ma kartę; profil bez karty = błąd gate | 1 |
| R2 | **Przełącznik profilu replikacji na telefonie + „co poszło do modelu” per profil** | Wybór pary (profil aplikacji, profil selektora) w jednym miejscu; podgląd struktury kontekstu po wysłaniu | Na telefonie przełączam `like_claude` → `like_chatgpt` i widzę inną strukturę kontekstu tej samej rozmowy | 1 |
| R3 | **Nagranie wzorca (RE31) jako import** | Zrzuty/zapisy z oryginalnej aplikacji importowane jak eksport (część A), węzeł `reference_recording` z datą i wersją, przypisany do celu | Właściciel robi zrzuty w aplikacji Claude na telefonie → import → węzeł wzorca pod profilem `claude_ai`; nic nie wychodzi z urządzenia | 1–2 |
| R4 | **Profil `claude_ai_<data>` z oficjalnych promptów** | Agent pobiera stronę `platform.claude.com/.../system-prompts`, zapisuje każdy wpis jako źródło `official` z datą; buduje profil selektora z kolejności bloków (struktura, nie tekst do modelu bez zgody); sprawdza, czy wpisy zawierają definicje narzędzi/artefaktów (dziś niewiadome) | Profil ma `evidence.status: official`, kartę R1 z L3 `claimed`; test: strona niedostępna → profil pozostaje w stanie poprzednim z adnotacją | 1 |
| R5 | **Profile `like_codex` i `like_gemini_cli` z otwartego kodu** | Czytanie `codex-rs/core` i `prompts/`, `gemini-cli` (Apache-2.0); mapowanie na nasze flagi źródeł, kolejność, polityki pętli; NOTICE/atrybucja w profilu | Dwa profile z `provenance.class: open_license`, commit przypięty (jak w APPLICATION_PROFILE_SOURCES) | 2 |
| R6 | **Test różnicowy warstwowy** (sekcja 4) | Generator scenariuszy z ziarnem; porównanie L1–L5 wzorzec vs nasz przebieg; wynik do karty R1 i rejestru wiarygodności | Jeden scenariusz syntetyczny × `claude_ai_<data>` daje raport z tolerancją; test własności: para profili → scenariusz rozróżniający istnieje | 2 |
| R7 | **Formaty L4 Claude Code / Codex / Gemini CLI jako czytniki** | `CLAUDE.md`/`AGENTS.md`/`GEMINI.md`/`.clinerules` → węzły pamięci/profilu; `SKILL.md` → akcja katalogu (dane); hooks JSON → `action_rules`; licencje Crush/Open WebUI/LobeHub przeczytane i zapisane jako źródła | Import folderu z `CLAUDE.md` i `.claude/skills/` tworzy węzły; test round-trip | 2 |
| R8 | **Drugi renderer** (terminal/transcript) w rejestrze profili | `createProfileRegistry({renderers:[…]})` dostaje drugi renderer; profile CLI go wskazują | e2e: profil z `renderer: transcript` renderuje się; profil z nieznanym rendererem odrzucany jak dziś | 1–2 |
| R9 | **Macierz unii funkcji jako dane + perspektywa** (sekcja 5) | 15 wierszy z sekcji 5 z dowodami; kolumna `loom` z e2e; agent build-time proponuje wiersze | Perspektywa grafu pokazuje komórki i dowody; komórka bez dowodu = `unknown` | 1–2 |
| R10 | **Rejestr wiarygodności źródeł** (sekcja 7.3) | rozszerzenie `calibration.json`, obserwacje, zanik/ważność, nadpisanie przez osobę | test R6 zmienia Beta źródła; nadpisanie widoczne obok wartości wyuczonej | 1–2 |
| R11 | **Agent czasu działania „nieobsługiwana aplikacja/wersja”** | przy imporcie z nieznanego celu: wyszukiwanie w sieci (dokumentacja → fora → analiza danych), propozycja profilu i kolumny macierzy jako dane do potwierdzenia | import syntetycznego eksportu z fikcyjnej aplikacji → propozycja profilu, nic nie zapisane bez „tak” | 2 |
| R12 | **Subagenci jako osobne pętle** z budżetem i własnym `expose` | nowy rodzaj kroku w `action_run` | test: subagent przekraczający budżet zatrzymuje się z propozycją | 2 |
| R13 | **Piaskownica wykonania** (desktop; Android ograniczony) | polityka wykonania poleceń jako dane (wzór: `execpolicy` Codexa — logika, nie kod); izolacja per platforma | polecenie poza polityką = propozycja; na Androidzie stan `blocked:platform` z adnotacją | 3+ (osąd niepewny) |

Kolejność to preset; właściciel może ją zmienić. Pakiety R1–R3 nie wymagają żadnego źródła z sieci.

---

## 7. Jak lista celów (aplikacje/wersje) rośnie i jak oceniamy wiarygodność źródeł

### 7.1 Przestrzeń i jej wyliczanie (R59 pkt 2–3)

Przestrzeń celów = *aplikacja × wersja (lub data obserwacji) × platforma*. Jest otwarta i rośnie co dzień.
Trzy etapy wypełniania, każdy jako dane do przeglądu, nic nie wchodzi bez potwierdzenia:

- **(a) Ziarno** — najpopularniejsze oczywiste: Claude Code, claude.ai, ChatGPT, Codex, Gemini (app/CLI);
  plus już przypięte klony LibreChat 0.8.8, NextChat 2.16.1. To lista przykładowa (właściciel: listy są
  otwarte).
- **(b) Odkrywanie przed wydaniem** — agent z szablonami zapytań („<app> changelog”, „<app> release notes”,
  „<app> export format”, „<app> system prompt official”) zapisuje wyniki do `loom/data/replication/discovery_<data>.json`
  z URL, tierem, datą; recenzent (człowiek lub agent) przyjmuje wiersze. Wynik: nowe cele, nowe wersje,
  nowe wiersze macierzy. Przestrzeń zapytań też jest danymi (lista szablonów otwarta).
- **(c) Agent czasu działania „nieobsługiwane formaty/aplikacje”** (pakiet R11) — przy spotkaniu eksportu
  lub aplikacji nieobecnej w celach: najpierw oficjalna dokumentacja, potem wiarygodne fora i źródła
  fachowe, a gdy brak wiarygodnych — analiza samych danych (struktura eksportu, nazwy pól, powiązania —
  UI15: „sprawdzać sam na internecie strukturę, a w razie jakby nie znalazł to się domyślać po nazwach
  i powiązaniach”) wsparta mniej wiarygodnymi źródłami i **testem** zdobytych informacji na tym eksporcie.
  Produkt: profil-propozycja jako dane z pełnym pochodzeniem, do potwierdzenia przez osobę.

Testy generatywne: dla losowego profilu zgodnego ze schematem (fuzzer po `application_profile.schema.json`
i po schemacie `replication_profile`) — wczytanie/zapis bez zmian, odrzucenie przy brakującym rendererze,
deterministyczne przejścia. Niepokryte: cele, których nikt nie nagrał ani nie zaimportował (przestrzeń jest
otwarta — mierzymy, ile komórek jest `unknown`, nie udajemy pełni).

### 7.2 Miejsce w grafie (pochodzenie, epistemika)

Węzły: `application` (cel), `application_version`, `replication_profile`, `source` (URL/plik/nagranie),
`author` (wydawca/konto), `claim` (pojedyncze twierdzenie: „aplikacja X w wersji Y ma funkcję Z”),
`reference_recording`, `test_run`. Krawędzie: `targets`, `asserts` (source → claim, z `observed_at`),
`supports`/`contradicts` (test_run → claim), `authored_by`, `derived_from`. Każdy element profilu linkuje
do `claim`, każdy `claim` do co najmniej jednego `source` z `provenance.class ∈ {official, open_license,
restricted_license, extraction_leak, observation, forum, search_snippet, data_analysis}` (zbiór otwarty).
To jest ta sama zasada „graf jako hub, kod jako silnik” (GRAF_HUB_KOD_SILNIK): profil replikacji to dane
w grafie, test to akcja katalogu, wynik to węzeł z pochodzeniem.

### 7.3 Rejestr wiarygodności (R59 pkt 5) — rozszerzenie `calibration.json`, nie nowy mechanizm

Dziś: `loom/data/policy/calibration.json` trzyma `reliability.<extractor|rule>: {alpha, beta}` (Beta),
`isotonic` per klasa dowodu (`observed, derived, inferred, extrapolated`), `min_labels`, `gates.ece`;
`assess.cpp:85–89` czyta pack `calibration`, średnia Beta `r` wchodzi do noisy-OR; komentarz w `:106`:
samo twierdzenie ani potwierdzenie nie jest pomiarem kalibracji — pomiar daje tylko test.

Rozszerzenie (dane, ten sam plik lub `calibration.json` + `calibration_sources.json` obok — decyzja D7):

```json
"reliability": {
  "extractor.alias": {"alpha": 18, "beta": 2},
  "source.domain:platform.claude.com": {"alpha": 1, "beta": 1, "class": "official"},
  "source.domain:github.com/openai/codex": {"alpha": 1, "beta": 1, "class": "open_license"},
  "author:asgeirtj": {"alpha": 1, "beta": 1, "class": "extraction_leak"}
},
"reliability_priors_by_class": {"official": {"alpha": 4, "beta": 1}, "open_license": {"alpha": 4, "beta": 1},
  "observation": {"alpha": 3, "beta": 1}, "forum": {"alpha": 1, "beta": 1}, "search_snippet": {"alpha": 1, "beta": 2},
  "extraction_leak": {"alpha": 1, "beta": 3}},
"observations": [{"key": "source.domain:…", "claim": "n_…", "result": "pass|fail", "weight": 1.0,
                  "observed_at": "2026-10-23T…Z", "target_version": "claude-code 2.1.309"}],
"time": {"mode": "decay|window|none", "half_life_days": null, "per_version": true},
"overrides": {"source.domain:…": {"alpha": 9, "beta": 1, "by": "owner", "at": "…", "note": "…"}}
```

- **Reguła aktualizacji:** test `pass` → `alpha += weight`, `fail` → `beta += weight`; `weight` zależy od siły
  testu (preset `1.0`; test różnicowy L4 mocniejszy niż zgodność README z dokumentacją — wagi to dane).
  Autor dostaje aktualizację z mniejszą wagą (preset `0.5`) — jego wiarygodność to agregat źródeł. Zgodnie
  z `assess.cpp:106`: potwierdzenie bez testu **nie** zmienia Beta.
- **Czas:** obserwacje są zapisywane surowo (append-only); Beta jest *wyliczana* z nich kernelem czasu
  (`mode: none` = suma; `decay` = wykładnicze wygaszanie z `half_life_days`; `window` = tylko obserwacje
  z zakresu wersji/dat celu). Preset: `none` do czasu `n >= min_labels` (istniejąca brama), potem
  `per_version: true` (wiarygodność źródła o Claude Code 2.0.x nie mówi o 2.1.x). Liczby w przykładzie są
  przykładami presetów, nie regułami.
- **Priory per klasa** zastępują ręczne priory extractorów tam, gdzie źródło jest nowe: nowe źródło
  dziedziczy prior klasy, własne obserwacje je przeważają.
- **Jak osoba to widzi:** perspektywa grafu „wiarygodność” — na węźle `source`/`author` pasek z wartością
  wyuczoną, liczbą obserwacji, datą ostatniej i klasą; obok wartość nadpisana (jeśli jest), nigdy w jej miejsce.
  Nadpisanie to osobne pole `overrides` (odwracalne jednym gestem); zmiana klasy też. Każda komórka
  macierzy i każdy element profilu pokazuje „na jakim źródle stoi i jak mu ufamy”.
- **Niepokryte:** źródła bez testu (Beta = prior klasy, jawnie oznaczone „nietestowane”); sprzeczne
  obserwacje z tej samej daty (zostają obie, Beta rośnie po obu stronach — to sygnał do przeglądu);
  autorzy ukryci za przekierowaniami repo (np. `sst/opencode` ↔ `anomalyco/opencode`) — klucz autora musi
  być kanoniczny, tego nie da się ustalić bez API.

---

## 8. Rozbieżności między odczytem pytania a stanem wiedzy (uprzejmie i konkretnie)

Pisane wprost, bo właściciel ceni krytykę. Punkty to **odczyt koordynatora** (parafrazy, nie słowa
właściciela; jego dosłowne pytanie jest na początku dokumentu). Fakty o świecie są obserwacjami z datą
2026-10-22 z podanym tierem, nie ograniczeniami.

1. **Zaufanie do twierdzenia asystenta o repozytoriach bez weryfikacji.** Gemini powiedział prawdę
   o istnieniu repo, ale „są klony odtwarzające workflow” to teza, której sam nie sprawdził i której
   sprawdzenie zajęło 28 pobrań. Reguła dla nas (i dla Jev): twierdzenie modelu o świecie zewnętrznym ma
   wiarygodność `search_snippet` dopóki nie ma źródła — tak samo jak w sekcji 7.3.
2. **„Workflow” to pięć warstw, nie jedna.** Pytanie „czy klony reprodukują oryginalne workflows” nie ma
   odpowiedzi tak/nie: wygląd, funkcje, składanie promptu, protokół narzędzi i dokładna kopia to różne
   rzeczy z różnymi źródłami i różnym prawem. Odpowiedź „tak” dla L1–L2 i „nie” dla L3–L5 to nie unik,
   to jedyna poprawna forma.
3. **Dla Codexa i Gemini CLI pytanie o klony jest źle postawione** — oryginały są otwarte. Pytać trzeba
   „co z Apache-2.0 możemy odtworzyć i z jakimi obowiązkami”, nie „kto to sklonował”.
4. **„Na ile możemy zreplikować” nie jest jedną liczbą.** To funkcja (warstwa, cel, *wersja*) i dryfuje:
   wg feedów wydań z 2026-10-22 (tier A–B, rytm niezmierzony) Codex i Claude Code wydają bardzo często.
   Replika bez daty i wersji jest fikcją. Dlatego profil
   ma `target.version` (dziś `null` wszędzie w profilach dostawców — to uczciwe, ale znaczy „przybliżenie”).
5. **Opublikowany prompt systemowy ≠ zachowanie aplikacji.** Anthropic publikuje prompty claude.ai i pisze
   wprost, że nie dotyczą API. Aplikacja to prompt + narzędzia serwerowe + klasyfikatory + model z parametrami,
   których nie widzimy. RE31 „opublikowane system prompty” daje L3 dla claude.ai i nic dla Claude Code/ChatGPT.
6. **Nagranie oryginału przez właściciela daje wzorzec czarnej skrzynki, nie specyfikację.** Model jest
   niedeterministyczny; jedno nagranie to jedna próbka z rozkładu. Test porównawczy wymaga N powtórzeń,
   tolerancji i porównania struktury (co trafiło do kontekstu, które narzędzia), nie tekstu. Właściciel
   powinien liczyć się z tym, że nagrywanie jest kosztowne ręcznie; czy ToS dostawców ograniczają
   automatyzację nagrywania na jego koncie — **nie sprawdzone** (ToS nieczytane).
7. **„Na tyle, na ile prawa autorskie pozwalają” to za wąska granica.** Prawdopodobnie praktyczną granicą są
   też ToS dostawcy (**nie sprawdzone**, ToS nieczytane), licencje source-available (Crush, Open WebUI; wg
   README/odznaki, LICENSE nieczytany) i pochodzenie z wycieku (`claw-code`, zbiory „leaks”), gdzie licencja
   repozytorium nie daje praw do cudzego tekstu (ocena inżynierska, nie porada prawna). Trzeba tagować
   pochodzenie, nie tylko licencję.
8. **Konfigurowalność nie tworzy zdolności.** R58 słusznie zabrania usuwać opcji, ale opcja „replikuj L5
   claude.ai” nie zacznie działać dlatego, że jest w menu. Uczciwe rozwiązanie: opcja zostaje, silnik
   zwraca stan `blocked:server_side`/`blocked:legal` i doradcze ostrzeżenie. Udawanie, że działa, byłoby
   gorsze niż brak opcji.
9. **Gwiazdki i README jako miara.** Z relacji właściciela (brak dosłownego tekstu Gemini) wynika, że istnienie
   i popularność repo bywają brane za dowód wierności — to odczyt koordynatora, nie jego słowa. Wierność mierzy wyłącznie test różnicowy; README to deklaracja; „nie pokazano na
   stronie” to często skrypt, nie brak.
10. **Przeniesienie wniosków z klonów na oryginały.** Nasze profile `claude-inspired`/`chatgpt-inspired`
    cytują jako źródło LibreChat (klon), nie oryginał — dziedziczą więc wierność klonu, nie aplikacji
    (APPLICATION_PROFILE_SOURCES mówi to wprost). Jeśli ma powstać profil „jak w aplikacji Claude” (przykładowe sformułowanie), źródłem musi
    być nagranie aplikacji Claude albo oficjalny prompt, nie LibreChat.
11. **Założenie, że wizualny profil już „wygląda jak” dostawca.** Sprawdziłem: wszystkie 10 profili używa
    jednego renderera `chat`; różnią się 6 kolorami, szerokością paska i stylem dymków. To skórka. Odtworzenie
    układu (artefakty obok, projekty, TUI) wymaga nowych rendererów (pakiet R8) — kodu, nie danych.

---

## 9. Decyzje i odwrót

Decyzje techniczne są moje (R1–R53). Każda ma odwrót.

| # | Decyzja | Uzasadnienie | Odwrót |
|---|---|---|---|
| D1 | Pięć warstw L1–L5 jako oś opisu wierności; każdy profil ma status per warstwa | pytanie właściciela nie ma odpowiedzi skalarnej | skala to dane w schemacie `replication_profile.layers[]`; inna liczba/nazwy warstw = edycja schematu, bez kodu |
| D2 | Źródła klasy `extraction_leak` domyślnie nie są czytane ani używane jako wejście (stan `blocked:legal`); istnienie można odnotować | **decyzja koordynatora** (ryzyko prawne: ToS — nieczytane, prawa autorskie, złośliwe lustra — ocena); nie tekst UI14 | klasa to tag; reguła „nie czytaj” jest regułą w `action_rules` (`deny:source.extraction_leak`), którą właściciel może zmienić — na własną odpowiedzialność prawną, z doradczym ostrzeżeniem |
| D3 | Opcja „poziom replikacji L5” zostaje dla każdego celu; silnik zwraca stan `blocked:*` i ostrzeżenie doradcze z „nie pytaj ponownie” | R58 | ostrzeżenie wyłączalne; stany to dane |
| D4 | Dalej **nie kopiujemy kodu** klonów ani oryginałów; odtwarzamy logikę, przypinamy commity, zapisujemy NOTICE przy ewentualnym cytowaniu | ciągłość z APPLICATION_PROFILE_SOURCES; prostsza zgodność licencyjna | preset `replication.copy_code: false`; przy `true` wymagane pola `license`, `notice`, `commit` w źródle |
| D5 | Wzorzec RE31 = import nagrania jak eksport (część A), węzeł `reference_recording`; porównanie struktur, nie tekstu, z N powtórzeniami | niedeterminizm; jedna ścieżka importu | tryb porównania (`structure|text|both`) i N to ustawienia |
| D6 | Macierz unii funkcji jako dane + perspektywa grafu; komórka bez dowodu = `unknown`; kolumna `loom` wypełniana e2e | zakaz wpisywania z pamięci | plik można edytować ręcznie; ręczny wpis dostaje `provenance.class: manual` i prior klasy |
| D7 | Rejestr wiarygodności źródeł jako rozszerzenie `calibration.json` (klucze `source.*`, `author:*`, `reliability_priors_by_class`, `observations`, `time`, `overrides`), nie nowy plik ani kod | precedens w `assess.cpp`; jedna droga liczenia | wydzielenie do `calibration_sources.json` to zmiana ścieżki w `pack.policy(...)`; kernel czasu `none` wraca do dzisiejszego zachowania |
| D8 | Priory per klasa pochodzenia (`official`/`open_license` wysokie, `search_snippet`/`extraction_leak` niskie) — liczby to presety | nowe źródło musi mieć punkt startu | wszystkie liczby w danych; `min_labels` istniejąca brama decyduje, kiedy dane przeważą prior |
| D9 | Nadpisanie przez osobę jako osobne pole `overrides`, nigdy w miejsce wartości wyuczonej | odwracalność, widoczność | usunięcie wpisu `overrides` przywraca wyuczoną |
| D10 | Profile `like_chatgpt` (aplikacja) i przyszły `like_codex` rozdzielone; `like_gemini` (aplikacja) i `like_gemini_cli` osobno | różne produkty, różne źródła (serwer vs kod Apache-2.0) | scalenie = usunięcie profilu z `selector_profiles`; nic w kodzie |
| D11 | Drugi renderer (`transcript`/TUI) jako kod rejestrowany przez `createProfileRegistry`; profile tylko go wskazują | rejestr już przewiduje listę rendererów | nieużywany renderer usuwa się z rejestru; profile z nim stają się odrzucane jak dziś |
| D12 | Kolejność pakietów R1→R13 z R1–R3 na telefonie najpierw | właściciel ma dotknąć czegoś; R1–R3 nie wymagają sieci | kolejność to preset koordynatora; zmiana bez zmiany kodu |
| D13 | Agent czasu działania (R11) niczego nie zapisuje bez potwierdzenia; produkt to propozycja-dane | R59 pkt 3c; ochrona danych | ustawienie `replication.autoaccept_profiles` (preset `false`); przy `true` doradcze ostrzeżenie o ryzyku |
| D14 | Źródła klasy `restricted_license`: zero kodu do czasu przeczytania LICENSE (pakiet 7) | kolejność prac, nie zakaz; licencje nieczytane | po przeczytaniu licencji zmiana reguły w danych (`action_rules`); osoba może zmienić wcześniej, z doradczym ostrzeżeniem |

### Czego nie zweryfikowałem (uczciwie)

- Treści stron dokumentacji Claude Code (tylko indeks tytułów), plików `docs/` i `codex-rs/` Codexa (nazwy),
  wpisów z promptami claude.ai (czy zawierają definicje narzędzi/artefaktów).
- Dat ostatnich commitów większości repozytoriów; liczb gwiazdek niezależnie od stron.
- Licencji Crush, Open WebUI, LobeHub (pliki LICENSE nieczytane); historii gita Kilo („fork OpenCode”).
- Czy w naszych packach istnieje coś na kształt skills (wiersz 5 macierzy) poza tym, co grep w tej sesji pokazał.
- Istnienia w `docs/reports/INDEX.md` sekcji „etap replikacji” — grep nie znalazł tego ciągu; jedyne
  odniesienia są w notach `status: calibration` profili `like_*` i w decyzji P36 (`INDEX.md:1179`).
- Dosłownego tekstu wypowiedzi Gemini.
