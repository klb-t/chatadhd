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

## 7. Przejęcie przez S1-VM (R92, właściciel 2026-10-23 ≈ 19:00 UTC)

> „w ogóle tą sesję powoli by trzeba było zakończyć i wszystko przenieść na gcp, nie? bo myślałem żeby administrator
> maszyny był standardowym punktem kontaktowym ze mną, jedyny w przypadku braku powodu, żeby inaczej było. wszystko co
> mamy wdrażamy ale dane, importy, to sam będę robił, najprędzej Zobaczę jak coś będzie nie tak.”

**Kto przejmuje:** sesja Remote Control na VM `devbox-20260923` (w aplikacji „gcp”, `session_01G2FdF1sSV9SQ9kbRg7tTdx`,
Opus 5.5, tmux `rc`). Od R92 to standardowy i jedyny punkt kontaktu z właścicielem; S1 chmurowa (ta sesja, Fable, 93 %
limitu tygodnia 23.10 20:44) dokańcza, co ma w biegu, i przechodzi w czuwanie (zadania zaplanowane budzą ją tylko do
kontroli CI i sekretarza).

**Co S1 chmurowa dokańcza sama (nie dublować):** bramka `r67c` na `/home/user/wt/m1d1` (`f025ed34`: R67-fix + naprawa
asan) → FF `main` → kontrola CI (+2 h 20 min) → sha do S2 (`session_01Armc9PkDiTDzjFBgXAMCjR`) → APK v5 (workflow
`package.yml`, ref main, android/server/first_steps, arm64-v8a, Release) → kontrola Haiku syntezy panelu
(`trig_01L8AQxYyXTq4PdKWUWrK9u7`, 20:02 UTC).

**Co zostaje w chmurze (obserwacja 23.10; VM nie ma tych narzędzi albo nie sprawdzono):**
- zadania zaplanowane: sekretarz spec `trig_01VyHbcN2o8DQhwKq6D9iNaU` (`23 * * * *`, Sonnet, świeża sesja, pisze na
  gałąź dokumentów i publikuje stronę spec `https://claude.ai/artifact/MfbrGaubViGGhsp2Z4ZEm7`); przegląd gałęzi GPT
  `trig_01QjSRmsb9M7eo8NP6r31d75` (`6 */6 * * *`, budzi S1 chmurową); kontrole CI jako `send_later` (S1 chmurowa);
- strona spec (Artifact) i jej baza (`spec_inbox`, `spec_runs`) — publikacja z VM niesprawdzona (narzędzie Artifact w CLI?
  sprawdź `/help`); do czasu sprawdzenia publikuje sekretarz;
- S2 (chmura) i emulator Androida (VM bez /dev/kvm — dowody z emulatora zostają w chmurze);
- wiadomości chmura → VM NIE działają (platforma); VM → chmura `send_message` działa. Kanał S1 → VM: `docs/ops/VM_INBOX.md`.

**Protokół na VM (to samo co tu, inne ścieżki):** jeden katalog budowy, ≥ 5 GB wolnego (VM: 16 GB wolnego 23.10 — po
buildzie zgłoś resztę); bramka `tools/gate.sh <root> <etykieta>` przed każdym FF main (OPS2: skrypt ma na sztywno
`/home/user` — na VM link `/home/user` → `$HOME` robi bootstrap, albo zrób OPS2); CI po każdym pushu na main (2,5 h;
nie pushować main, gdy bieg trwa — anuluje go); `python3 tools/pace.py --used <tydzień %>` przed każdym agentem
(R91: Fable do 100 %, reszta do 90 %); stan zawsze w `docs/reports/QUEUE_S1.md` (jeden wpis na zdarzenie, na górze).

**Wdrożenia (R92):** wszystko, co mamy, bez danych właściciela — serwer chatadhd + web (8787), AGEDS (8090; 8080 ma
WatchDog), WatchDog chodzi; dane i importy wprowadza sam. Opis dla niego: `docs/ops/VM_DEPLOY_2026-10-23.md`.
Otwarte pytania tak/nie (jego pieniądze): Spot → Standard. Decyzja techniczna S1: `claude-rc.service` tak (odwrót:
`systemctl --user disable claude-rc`).

**Odwrót z R92:** S1 chmurowa wraca do roli koordynatora jednym zdaniem właściciela; nic nie jest skasowane.
