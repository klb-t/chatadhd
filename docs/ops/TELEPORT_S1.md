# Teleport S1 — karta startowa koordynatora na nowej maszynie (R72: „teleporta bardziej niż buttstrapa”)

Stan na: **2026-10-23 08:0x UTC** (aktualizowany tuż przed przełączeniem; jeśli data jest stara, prawdę ma `git log` i
głowa `docs/reports/QUEUE_S1.md`). Bootstrap stawia maszynę; TEN plik przenosi warsztat i stan. Czyta go nowa sesja S1
jako pierwsza rzecz po `CLAUDE.md`.

## 1. Kim jesteś
Koordynator S1 projektu ChatADHD/Loom (model Fable; właściciel ustawił). Zasady: `CLAUDE.md` (filozofia właściciela —
wszystko konfigurowalne, domyślne to presety, przykłady to przykłady, żadnych wymyślonych ograniczeń; styl wątku: tylko
linie statusu; droga praca po zatwierdzonej parafrazie, a gdy właściciel nieobecny — parafraza `proposed` i działanie).
Decyzje techniczne są Twoje, zapisywane w `docs/reports/INDEX.md` z odwrotem. Pytania do właściciela tylko: jego
pieniądze, jego prywatne dane u nowego dostawcy, sprzeczność z wymaganiami — jako tak/nie.

## 2. Czytaj w tej kolejności (≈ 15 min)
1. `CLAUDE.md` (dostajesz automatycznie) → 2. ten plik → 3. `docs/reports/QUEUE_S1.md` — pierwsze 10 sekcji (log od
najnowszego) → 4. `docs/reports/INDEX.md` — ostatnie wpisy `S1 / …` → 5. `python3 tools/spec.py list --understanding
proposed` (parafrazy czekające na właściciela) i `python3 tools/spec.py show R72` (ostatnie słowa) → 6. `docs/STATE.md`
(stan projektu, liczby) → 7. `tools/workflows/README.md` + `tools/workflows/briefs/` (jak wyglądają pakiety).

## 3. Stan w chwili teleportu
- `origin/main` = `94872898` (K2c-2 części „Ty”, K2c-2-fix, G1a/G1b słowa grafu + prawda importu, sterownik S2).
  CI loom.yml na tym sha: kontrola była zaplanowana na 08:55 UTC — sprawdź `actions_list`/`gh run list -w loom.yml`.
- Gałęzie robocze (wszystkie na `origin`): `claude/m1-w1` = W2b-1 ekran „Ty” (9db553b2; bramka `w2b1` biegła 07:50–),
  `claude/m1-w0` = scalone do main, `claude/r35-android` = 2 niescalone commity (import przez picker, prywatny storage),
  `claude/chataddhd-cpp-loom-core-IRGRN` = dokumenty, narzędzia, spec (ten plik). Gałęzie `gpt/*` = praca GPT (przegląd co 6 h).
- Worktree do odtworzenia (układ `/home/user/…`, bootstrap robi dowiązanie, gdy użytkownik jest inny):
  `git worktree add /home/user/wt/m1d1 claude/m1-w1` (jedyny z buildem; JEDEN katalog buildu naraz, ≥ 5 GB wolne).
- Czekało na właściciela (nie pytaj ponownie, sprawdź odpowiedzi w czacie/QUEUE): parafrazy R67, R68, R69, R72
  (`proposed`), R70 (`corrected` jego słowami); pieniądze na GCP; dane na dysku GCP (kształt B).
- Kolejka pakietów (zadania #): K2c-3 (dane, małe), R67-fix (atrapy), K3 (selektor kontekstu — projekt Fable po
  zatwierdzeniu), W2b-2, G2, G3, okna W0–W2, K2b-5, OPS2, P37, #24. Szczegóły: `QUEUE_S1.md`.
- Zadania zaplanowane: `docs/ops/ZADANIA_ZAPLANOWANE_2026-10-23.md` (cykliczne „Przegląd gałęzi GPT” do odtworzenia).
- Sesja S2 (`session_01Armc9PkDiTDzjFBgXAMCjR`, smoke na emulatorze w GitHub Actions) zostaje w chmurze; kontakt:
  `send_message`, a bez tego narzędzia — komentarz w QUEUE i wiadomość właściciela.

## 4. Pierwsze komendy na VM (po bootstrapie; mierz czasy i wpisz do QUEUE)
```
cd /home/user/chatadhd && git fetch origin && python3 tools/pace.py --used <procent z aplikacji>
git worktree add /home/user/wt/m1d1 claude/m1-w1 && cd /home/user/wt/m1d1/loom
cmake --preset dev && time cmake --build --preset dev            # zimny build: czas nieznany, zmierz
ctest --preset dev -j3                                             # ~17 min tutaj
cd web && npm ci && npm run build && node e2e/m1-me-native.mjs     # test w przeglądarce (Chromium z Playwright)
GATE_OUT=/tmp/gate-vm bash /home/user/chatadhd/tools/gate.sh /home/user/wt/m1d1 vm   # pełna bramka, 40–90 min, w tle
```
Warsztat, który ma działać tak jak w chmurze: bramka (`tools/gate.sh`), e2e w Chromium (`loom/web/e2e/*.mjs`), kontrola
clang (`tools/clang_inc_check.py`), strażnik ASan (`tools/lint_asan_range_for.py`), spec (`tools/spec.py`), tempo
(`tools/pace.py`), raport zużycia (`tools/usage_report.py`), emulator Androida tylko przy `/dev/kvm` (`--emulator`).

## 5. Narzędzia sesji — sprawdź w pierwszej godzinie, zanim przeniesiesz pracę
| Narzędzie w chmurze | Do czego | Zamiennik na VM, jeśli brak |
|---|---|---|
| `Workflow` + `tools/workflows/single-package.js` | pakiety agentów z kartą | `Agent` (subagent) z tym samym briefem; brief = `args` z `tools/workflows/briefs/*.json` |
| zadania zaplanowane (`create_trigger`, `send_later`) | kontrole CI, przegląd GPT | `cron` + `claude -p "…"` w katalogu repo |
| `send_message` do S2 | wymiana sha / wyników smoke | komentarz w `QUEUE_S1.md` + linia do właściciela |
| GitHub MCP (`actions_*`, `pull_request_*`) | CI, APK, PR | `gh run list/view/watch`, `gh workflow run package.yml` |
| `list_events` (rate_limit_event) | próbki do `LIMITS_LEDGER.md` | procent z aplikacji Claude, wpis ręczny |
Nic z tego nie było sprawdzone na VM (stan 2026-10-23).

## 6. Odwrót
Źródłem prawdy jest git: wszystko wypchnięte. Przy każdej niespodziance praca wraca do sesji w chmurze
(`session_015yMVYJqXDd8sNSZdYq3gu5`), nic tam nie zostało usunięte. Przełączenie tylko między pakietami, bez agenta i bez
bramki w toku (`PRZEPROWADZKA_GCP_2026-10-23.md` §5).
