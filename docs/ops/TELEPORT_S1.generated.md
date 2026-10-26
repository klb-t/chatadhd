# Teleport S1 — karta startowa koordynatora (render rekordu `handover`)

Stan na: **2026-10-26 18:09 UTC** (wygenerowane: `python3 tools/handover.py render teleport`; rekord `docs/ops/handover.json`, hash `54feb2fc8adb`). Jeśli data jest stara, prawdę ma `git log`, zapisy biegów i głowa `docs/reports/QUEUE_S1.md`.

<!-- section: who -->
## 1. Kim jesteś
Koordynator S1 projektu ChatADHD/Loom. Zasady: `CLAUDE.md` (filozofia właściciela — wszystko konfigurowalne, domyślne to presety, przykłady to przykłady, żadnych wymyślonych ograniczeń; styl wątku: sprawozdania pełnymi zdaniami, bez skrótów (R93), bez powtarzania oczywistości (R95); droga praca po zatwierdzonej parafrazie, a gdy właściciel nieobecny — parafraza `proposed` i działanie). Decyzje techniczne są Twoje, zapisywane w `docs/reports/INDEX.md` z odwrotem. Pytania do właściciela tylko: jego pieniądze, jego prywatne dane u nowego dostawcy, sprzeczność z wymaganiami — jako tak/nie (klasy z CLAUDE.md to przykłady listy).

<!-- section: read_order -->
## 2. Czytaj w tej kolejności
1. `CLAUDE.md` (dostajesz automatycznie)
2. ten plik (render rekordu `docs/ops/handover.json`)
3. `docs/reports/QUEUE_S1.md` — pierwsze 10 sekcji (log od najnowszego)
4. `docs/reports/INDEX.md` — ostatnie wpisy
5. `python3 tools/spec.py list --understanding proposed` i ostatnie słowa w spec
6. `python3 tools/run_record.py list` — biegi; `python3 tools/routine.py list` — rutyny
7. `docs/STATE.md` (stan projektu, liczby)
8. `tools/workflows/README.md` + `tools/workflows/briefs/`

<!-- section: state -->
## 3. Stan w chwili teleportu
- `origin/main` = `d611bd34` (2026-10-26T17:05:17Z).
- Ta kopia: gałąź `worktree-agent-ae6f1c55172d9e195` na `2fbf6691`.
- Gałęzie na `origin` (najnowsze): `claude/chataddhd-cpp-loom-core-IRGRN` = `f0c114bb`, `claude/m1-w1` = `d611bd34`, `claude/s2-parallel` = `f025ed34`, `claude/m1-w0` = `94872898`, `claude/fix-asan-k1` = `59de144c`, `claude/fix-login-drop` = `76d3bbaa`, `claude/xport-android` = `e150c3aa`, `claude/p39-design` = `bbdaca7f`.
- Biegi w toku/wstrzymane: 0.
- Przełączenie teraz: można (przełączenie tylko między pakietami: bez biegu `running` i bez bramki w toku (PRZEPROWADZKA_GCP_2026-10-23.md §5)).
- Paragon teleportu: odbudowa kontekstu: 1 żywych kontekstów ≈ 200 000–460 000 tokenów (CLAUDE.md (pomiar 2026-10-10: re-cache 0,2–0,46 mln na żywy kontekst)).
- Czeka na właściciela (spec `proposed`, nie pytaj ponownie — sprawdź odpowiedzi): 12: R56, R64, R67, R68, R69, R71, R73, R74, R75, R88, R89, ON05.
- Głowa kolejki (`QUEUE_S1.md`, 10 sekcji):
  - main = 88ebe1f4 (FF 2026-10-25, koordynator gcp): W2b-2a z wątku chatadhd (params akcji z jądra, inference.confirm/reject, attrs.previous, memory.forget po query); bramka w2b2a2 zielona poza `test:m1-context-menu` (wyścig etykiety po selectOption(""), ponowio…
  - CI na main 106a5699 ZIELONE (2026-10-25 14:07 UTC): shardy asan a 52 min, b 65 min, rest 29 min (cały bieg 65 min zamiast ≥ 150); shards-cover-manifest zielony; `server.security` (400 zamiast 413 pod ASan w CI 285) tym razem przeszedł — obserwować jako możliw…
  - VM 2026-10-25 ≈ 15:30 UTC (gcp, koordynator R92): wątki per projekt działają (RC: watchdog, ageds, chatadhd). Shardy asan `106a5699` na m1-w1 (Sonnet); bramka 1: ctest 264/264 zielony, czerwień środowiskowa (clang21 `-c`+`-fsyntax-only` → poprawione w `tools/…
  - ZAŁATWIONE 11:52 UTC: właściciel podał klucz SerpApi (konto ma 3023 wyszukiwań: 250 z darmowego planu w tym miesiącu plus dokupione), zapisany w `~/.config/chatadhd/keys.env`, sesja WatchDog powiadomiona. Wcześniej: (2026-10-25, od sesji WatchDog JH16, zadani…
  - Później (właściciel 2026-10-24, nie priorytet): dopasować VM do zużycia, gdy przez tygodnie nie używamy połowy mocy albo spot-guard często awansuje na Standard; dane do decyzji zbiera się samo (sysstat, log spot-guard), research dopiero wtedy. Kredyty: `docs/…
  - S2 2026-10-23 22:54 UTC: APK v5 ready - package.yml run 40033660848 on main 51315dd6 = success (22:26-22:42); artifacts: chatadhd-android-apk-TEST-KEY (18.4 MB), chatadhd-0.10.00-ci.27.51315dd (50.2 MB), chatadhd-server-linux-x64, chatadhd-server-linux-arm64,…
  - S2 2026-10-23 22:27 UTC: APK v5 dispatched - package.yml run 40033660848 on main 51315dd6 (= f025ed34 + asan timeout 240; android/server/first_steps, arm64-v8a, Release); emulator proof = android-smoke 40011129193 on f025ed34, 47/0/0 x3 (app code identical); …
  - Log asan z CI 284 odczytany (22:35 UTC, Haiku, job 120610852512): **`unit.test_address` Passed 25,99 s, `server.chat_active_task` Passed 111,16 s pod ASan/UBSan** — obie dzisiejsze naprawy potwierdzone sanitizerem; 262/263 testów ukończonych, wszystkie Passed…
  - CI 284 na `f025ed34` = „cancelled” (21:53 UTC) = **limit jobu asan** (150 min): krok testów biegł 140 min po ~10 min budowy; dev/vendored/libcxx zielone; żadnego czerwonego testu (S2 odczytała). To ten sam wzorzec co dev przy 45 min (CI 281). Naprawa: `loom.y…
  - S2 2026-10-23 22:24 UTC: loom.yml run 284 on main f025ed34 = cancelled, not green: dev, vendored and libcxx passed, asan (g++) hit its 150-min job limit inside "Test" (19:33-21:53) - a timeout, no test failure reported; APK v5 NOT built (waits for a green CI)…
- Zadania zaplanowane (dane: `tools/routines/scheduled_tasks.json`; treści: `docs/ops/ZADANIA_ZAPLANOWANE_2026-10-23.md`):
  - „Sekretarz spec (R88)” `23 * * * *` — alive (obserwacja 2026-10-23, `trig_01VyHbcN2o8DQhwKq6D9iNaU`)
  - „Przegląd gałęzi GPT” `6 */6 * * *` — alive (obserwacja 2026-10-23, `trig_01QjSRmsb9M7eo8NP6r31d75`)
  - „Operator (IW-10)” `23 * * * *` — proposed (obserwacja 2026-10-26)
- Miejsca: 13 żyje · 1 śpi · 1 nieznany (`docs/ops/places.json`, obserwacje z datą):
  - `vm:devbox-20260923` S1-VM devbox-20260923 (GCP, europe-west4-c, e2) — alive (obserwacja 2026-10-26T17:25Z); stop: prośba „stop” w docs/spec/gestures_state.json (gest words.py) czytana przez tętno: biegi running na tym miejscu → paused z odwrotem `routine.py resume` (tools/vm_place.py, IW-8) / gcloud compute instances stop
  - `cloud:s1` chmura S1 (session_015yMVYJqXDd8sNSZdYq3gu5) — sleeps (obserwacja 2026-10-23); stop: interrupt / send_message
  - `cloud:s2` S2 (session_01Armc9PkDiTDzjFBgXAMCjR) — smoke na emulatorze (GitHub Actions) — alive (obserwacja 2026-10-23); stop: send_message
  - `task:secretary` zadanie „Sekretarz spec (R88)” — co godzinę o :23 — unknown (obserwacja 2026-10-23); stop: wyłączenie zadania
  - `page:spec` strona specyfikacji + baza (spec_inbox, spec_visits) — alive (obserwacja 2026-10-23); https://claude.ai/artifact/MfbrGaubViGGhsp2Z4ZEm7
  - `ci:github` GitHub CI (loom.yml, python.yml na main) — alive (obserwacja 2026-10-23); stop: gh run cancel

<!-- section: first_commands -->
## 4. Pierwsze komendy (mierz czasy i zapisz w biegu)
```
git fetch origin && python3 tools/start_gate.py --role coordinator --json
python3 tools/handover.py build && python3 tools/handover.py render since_you_left
du -sh /home/user/wt/*/loom/build /home/user/.ccache   # jeden katalog buildu naraz, ≥ 5 GB wolne
GATE_OUT=/tmp/gate bash tools/gate.sh <worktree> <etykieta>   # pełna bramka w tle
```
Warsztat: bramka (`tools/gate.sh`), e2e w Chromium (`loom/web/e2e/*.mjs`), kontrola clang (`tools/clang_inc_check.py`), strażnik ASan (`tools/lint_asan_range_for.py`), spec (`tools/spec.py`), brama startu (`tools/start_gate.py`), zapis biegu (`tools/run_record.py`), rutyny (`tools/routine.py`).

<!-- section: tools -->
## 5. Narzędzia sesji — dostępność per miejsce (z `tools/routines/tools.json`)
| Narzędzie | Do czego | Dostępność (obserwacja) |
|---|---|---|
| `workflow_single_package` | jeden pakiet agenta z kartą (brief = args) | cloud: available (2026-10-23) · vm: unknown (2026-10-23); zamiennik: Agent (subagent) z tym samym briefem |
| `agent_subagent` | subagent z briefem w worktree | vm: available (2026-10-26) |
| `scheduled_tasks` | zadania zaplanowane (create_trigger, send_later) | cloud: available (2026-10-23) · vm: unknown (2026-10-23); zamiennik: cron + claude -p "…" w katalogu repo |
| `send_message` | wiadomość do innej sesji | cloud: refused (2026-10-23); zamiennik: docs/ops/VM_INBOX.md przez repo · vm: available (2026-10-23) |
| `github_mcp` | CI, APK, PR przez GitHub MCP | cloud: available (2026-10-23) · vm: unknown (2026-10-23); zamiennik: gh run list/view/watch, gh workflow run |
| `rate_limit_events` | próbki limitów (rate_limit_event) do LIMITS_LEDGER | cloud: available (2026-10-23) · vm: unknown (2026-10-23); zamiennik: procent z aplikacji Claude, wpis ręczny |
| `artifact_db` | odczyt/zapis bazy strony spec (spec_inbox, spec_runs) | cloud: available (2026-10-23) · vm: available (2026-10-26) |

<!-- section: reversal -->
## 6. Odwrót
Źródłem prawdy jest git: wszystko wypchnięte. Przy każdej niespodziance praca wraca do poprzedniej sesji; nic tam nie zostało usunięte.

<!-- section: takeover -->
## 7. Przejęcie przez S1-VM (R92)

> „w ogóle tą sesję powoli by trzeba było zakończyć i wszystko przenieść na gcp, nie? bo myślałem żeby administrator maszyny był standardowym punktem kontaktowym ze mną, jedyny w przypadku braku powodu, żeby inaczej było. wszystko co mamy wdrażamy ale dane, importy, to sam będę robił, najprędzej Zobaczę jak coś będzie nie tak.”

Sesja na VM (`session_01G2FdF1sSV9SQ9kbRg7tTdx`, „gcp”) to standardowy punkt kontaktu z właścicielem; S1 chmurowa przechodzi w czuwanie (zadania zaplanowane). Kanał S1 → VM: `docs/ops/VM_INBOX.md`. Wdrożenia: wszystko, co mamy, bez danych właściciela (`docs/ops/VM_DEPLOY_2026-10-23.md`). Odwrót: S1 chmurowa wraca do roli koordynatora jednym zdaniem właściciela.
