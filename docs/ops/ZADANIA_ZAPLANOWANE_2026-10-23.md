# Zadania zaplanowane sesji S1 (stan 2026-10-23 07:5x UTC)

Po co: przy przeprowadzce koordynatora na inną maszynę/sesję (R68, `PRZEPROWADZKA_GCP_2026-10-23.md` §5 pkt 7)
zadania zaplanowane NIE jadą same — są przypięte do tej sesji w chmurze. Tu są ich treści, żeby odtworzyć je
w nowej sesji jedną komendą narzędzia (`create_trigger`) bez szukania. Jednorazowe (kontrole CI, odczyty smoke)
wygasają same i nie są tu kopiowane.

## Cykliczne (do odtworzenia)

### „Przegląd gałęzi GPT” — cron `6 */6 * * *` (UTC), przypięte do sesji koordynatora, od 2026-10-14 (prośba właściciela)
Treść (dosłownie, po angielsku — tak było zapisane):
```
Periodic check (owner's request): look at the GPT branches (origin/gpt/*, newest by commit date). For each branch
with new commits since the last check: what the owner told GPT (owner quotes in its docs/reports), progress,
overlap or conflicts with main and with S1/S2 work (git merge-tree against origin/main; files both sides touched;
duplicated features). Report to the owner briefly in Polish, in his words; flag real conflicts or duplicated work.
If nothing changed, say nothing.
```
Uwaga: używa narzędzi GitHub (MCP) albo `git fetch origin` — na VM wystarczy `gh auth login` i zwykły git.
Ostatnia zmiana na gałęziach GPT widziana 2026-10-16; od tego czasu każdy bieg kończył się „bez zmian”.

### „Sekretarz spec (R88)” — cron `23 * * * *` (UTC), ŚWIEŻA sesja na Sonnecie (nie przypięta), od 2026-10-23 09:35
Treść: w `docs/reports/QUEUE_S1.md` (sekcja „R88 v1”) i w `docs/spec/README.md`; pełny prompt w zadaniu
`trig_01VyHbcN2o8DQhwKq6D9iNaU` (list_triggers → derived_state.prompt). Czyta `spec_inbox` artefaktu
https://claude.ai/artifact/MfbrGaubViGGhsp2Z4ZEm7, stosuje `spec.py approve/correct/add`, odświeża stronę, commit+push.

**Dopisek IW-1 (2026-10-26) — do wklejenia na koniec promptu przy najbliższym `update_trigger`/`create_trigger`**
(żywy prompt zadania NIE został zmieniony w pakiecie IW-1; do tego czasu biegi z `spec_runs` przenosi
`python3 tools/run_record.py import-spec-runs <katalog>` — idempotentne):
```
Run record (0 tokens, tools/run_record.py, schema loom.routine_run/1): at the start run
  RID=$(python3 tools/run_record.py start --routine secretary.run@R88-v1 --where cloud:scheduled --param title="Sekretarz spec")
for every spec.py command you apply: python3 tools/run_record.py step $RID --id cmdN --state done --field command="<command>"
for every escalation: python3 tools/run_record.py add $RID escalations '{"node":"<id>","why":"<reason>"}'
at the end: python3 tools/run_record.py finish $RID --summary "<pusta | what changed>"
  (empty run: add --quiet — it stays in the record and changes no counter, so it gives no status line)
Commit docs/reports/runs/ together with the spec changes (an empty run may be committed with the next non-empty one).
Keep writing the spec_runs row on the page as before (mirror for the phone).
```

**Dopisek IW-3 (2026-10-26) — do wklejenia na koniec promptu razem z dopiskiem IW-1** (żywy prompt zadania NIE został
zmieniony w pakiecie; wszystkie progi to presety w `tools/spec.py` → `DEFAULT_CONFIG["question"]`, nadpisywane przez
`docs/spec/config.json`; zmiana presetu nie zmienia tego tekstu):
```
Inbox with class (0 tokens, no judgement): export the page's spec_inbox entries with state "new" (ArtifactData list) to
  a JSON file and run: python3 tools/spec.py inbox <file>
  It applies class approval (and any entry whose text is an approve word, e.g. "Zatwierdzam") and class correction by itself,
  and returns one verdict per entry. Write each verdict back to its spec_inbox document: state = verdict.state,
  verdict = verdict.verdict, why = verdict.why. Classes are an open list: an entry of a class you do not know is escalated,
  never dropped. Record each applied command as a run step (IW-1 lines).
Questions that wait (if_silent without watching): run
  python3 tools/spec.py silent --run-id $RID
  It executes if_silent ONLY for questions whose ask_after (from first_shown) has passed AND whose cost_to_reverse is on the
  low list (preset), up to the hard per-run limit (preset); every one gets a decision record with its reversal command
  (python3 tools/spec.py reopen <id>) in understanding.json and in the run record. Everything in "escalated" goes to the
  coordinator as an escalation (run_record add $RID escalations ...) with its "why". Do not decide those yourself.
  For every question in "waiting" that has a "due" time, make sure a one-time task exists at that time
  (run_once_at = due, prompt: this secretary prompt), so silence resolves without waiting for the next hourly run.
  If create_trigger is unavailable here, the hourly run picks it up (preset; say so in the run summary).
```

## Jednorazowe żywe w chwili zapisu (wygasną same)
- `CI check (main 94872898)` — 08:55 UTC, sesja S1: czy loom.yml na 94872898 zielony → APK v4.
- `Read android-smoke 32c7a401` — 08:33 UTC, sesja S2 (nie moja): odczyt smoke sterownika.

## Jak odtworzyć w nowej sesji
Narzędzie `create_trigger` (serwer claude-code-remote) z `cron_expression` i `prompt` jak wyżej, przypięte do nowej
sesji (tryb domyślny). Jeśli sesja na VM nie ma tych narzędzi (nie sprawdzone — PRZEPROWADZKA §5 pkt 10), zamiennik:
`cron` na VM wołający `claude -p "<treść>"` w katalogu repo, co 6 godzin.
