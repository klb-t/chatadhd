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

**Dopisek IW-5/IW-6 (2026-10-26) — do wklejenia po dopisku IW-3** (żywy prompt nie został zmieniony; tabela słów i progi to
presety w `tools/routines/words.json`, role i modele w `tools/routines/roles.json`):
```
Control words (0 tokens, no judgement): after spec.py inbox, run on the same file
  python3 tools/words.py inbox <file> --run-id $RID
  It recognises the owner's control words (table tools/routines/words.json, ASR variants) in entries of the classes listed
  in presets.inbox_classes and applies effects whose cost is under the threshold (mandate, stop request with the sunk cost of
  running runs, marker, preference, request). "needs_confirm", "weak" and "escalated" results go to the coordinator as
  escalations with their "why"; do not apply them yourself. Every use is logged with its origin; reversal:
  python3 tools/words.py reverse <use_id>.
Before starting any routine or agent: python3 tools/start_gate.py --role <role> --json --run-id <its run id>
  (role → model → place, estimate, window; dated observations such as "Fable unavailable until the weekly reset" give an
  advisory and a substitute, never a stop). Verdict "owner" = waits for the owner; "wait" = do not start now.
```

**Dopisek IW-10 (2026-10-26) — operator = rozszerzony mandat sekretarza; do wklejenia po dopisku IW-5/IW-6** (żywe
zadanie NIE zostało zmienione w pakiecie). Mandat to dane: rutyny z `"operator_ok": true` w `tools/routines/routines.json`
(`python3 tools/operator_run.py list`); kadencja i progi pustego biegu to presety rutyny `operator.run` (`presets.operator`,
z `_history`). Zadania zaplanowane jako dane: `tools/routines/scheduled_tasks.json` (operator ma tam stan `proposed`).
Dwie drogi wdrożenia (decyzja koordynatora, obie z odwrotem): (a) dopisek do zadania sekretarza `trig_01VyHbcN2o8DQhwKq6D9iNaU`
(`update_trigger`, ta sama kadencja; odwrót: usunąć dopisek), (b) osobne zadanie `create_trigger` z tym samym cronem i
modelem (odwrót: wyłączyć zadanie). Tekst (to samo co `tools/routines/operator_prompt.md`; `python3 tools/operator_run.py prompt`):
```
Operator (IW-10) — extended mandate of the secretary. Paste AFTER the secretary prompt and its IW-1/IW-3/IW-5/IW-6 additions.
The secretary part stays as it is; this adds routines that need no judgement. 0 tokens on the write paths: every write is a
script command; you only run them, read their JSON and pass on what they cannot do.

Mandate (data, not this text): the routines with "operator_ok": true in tools/routines/routines.json
(python3 tools/operator_run.py list). Examples today: start.gate, ci.read, ledger.sample, status.lines, teleport.refresh,
tasks.recreate. The list is the file, not this prompt — when the file changes, your mandate changes.

1. Start your own record:  RID=$(python3 tools/routine.py run operator.run --where cloud:scheduled | tail -1 | cut -d' ' -f1)
   It pauses at step "secretary": do the secretary part exactly as before (inbox, spec.py inbox/silent, words.py inbox),
   recording its commands as steps of $RID, then:  python3 tools/routine.py resume $RID --done secretary
   The runner then runs start.gate, ci.read, status.lines and teleport.refresh by itself (each as a child run with
   --as operator --parent-run $RID) and the cadence measurement, and pauses at "session_parts".
2. session_parts: run  python3 tools/routine.py run ledger.sample --as operator --parent-run $RID  and
   python3 tools/routine.py run tasks.recreate --as operator --parent-run $RID . Each pauses at a hand-off that needs a
   tool of this session (list_events, list_triggers); do exactly what the hand-off text says, then resume that child run.
   If the tool is not available here, record an advisory in the child run (its text says how) and resume it.
   Then:  python3 tools/routine.py resume $RID --done session_parts
3. Anything outside the mandate is a JUDGEMENT: never do it. Running
   python3 tools/routine.py run <routine> --as operator --parent-run $RID  for a routine without operator_ok exits with
   code 4 and writes the proposal into $RID as an escalation — that is the correct outcome, not an error. The same for
   differences found by tasks.recreate (proposals; you never create, update or delete a scheduled task) and for a
   cadence proposal from step "cadence" (you never change your own cron).
4. Confirm gates are not yours to answer: a child run that pauses on a "confirm" step stays paused; its question is
   already an escalation. An active mandate of the owner (docs/spec/gestures_state.json, words.py) passes a gate by
   itself — you do not interpret mandates.
5. End: the card is the last step's output (python3 tools/operator_run.py summary $RID): children, escalations,
   proposals, failed runs. Put the escalations and proposals into spec_runs on the page as before (one row per run),
   commit docs/reports/runs/ and docs/ops/TELEPORT_S1.generated.md together with the spec changes, push to the documents
   branch. An empty run (nothing applied, no escalation) may be committed with the next non-empty one.
6. Do not write status lines into the chat or into QUEUE by hand: status.lines renders them from the records.
```
Pomiar „po” (IW-10): tury koordynatora na pakiet przed/po (baza ≈ 20 × 0,28 mln, §4) i koszt pustego biegu —
`python3 tools/operator_run.py stats` (z zapisów biegów sekretarza i operatora).

## Jednorazowe żywe w chwili zapisu (wygasną same)
- `CI check (main 94872898)` — 08:55 UTC, sesja S1: czy loom.yml na 94872898 zielony → APK v4.
- `Read android-smoke 32c7a401` — 08:33 UTC, sesja S2 (nie moja): odczyt smoke sterownika.

## Jak odtworzyć w nowej sesji
Narzędzie `create_trigger` (serwer claude-code-remote) z `cron_expression` i `prompt` jak wyżej, przypięte do nowej
sesji (tryb domyślny). Jeśli sesja na VM nie ma tych narzędzi (nie sprawdzone — PRZEPROWADZKA §5 pkt 10), zamiennik:
`cron` na VM wołający `claude -p "<treść>"` w katalogu repo, co 6 godzin.
