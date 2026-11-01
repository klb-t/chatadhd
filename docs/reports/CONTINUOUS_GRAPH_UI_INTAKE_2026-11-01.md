> Aktualizacja 2026-11-01: konkretny ACL naprawiono i zweryfikowano przy wznowieniu.
> Poniżej historyczny wynik przed naprawą; bieżący odbiór: [continuous UI](CONTINUOUS_GRAPH_UI_ACCEPTANCE_2026-11-01.md).

# Continuous graph UI — odbiór zablokowany przed aplikacją, 2026-11-01

Baza i przyjęty wynik: `5912012c5d700e3c3ed4f89d7fbb17bfdc4c9fc9`.
Śledzone źródła były czyste. Runtime/readery/reprojekcja/media niezmienione.
Nie zastosowano żadnego patcha UI, nie uruchomiono typecheck/build/browsera ani
pakowania. Bramka continuous graph UI pozostaje **NOT RUN** i obowiązkowa przed
wydaniem; completed autora nie oznacza akceptacji UI.

## Konkretny bloker odbioru

Task `chatadhd-continuous-graph-ui-20261101`: świeży `team-receipts.json` podaje
`completed`, updated `2026-11-01T09:03:26.419983+00:00`, snapshot
`1793523805897232157`, archive SHA256
`5dcf7bca8c3cf0b86e2059ef2560bca716345c2f2ed4247949671ae38dd6be31`.
Archiwum prywatne nie było otwierane. Manifest integration-receipts odczytany
w tej turze ma datę `2026-11-01T08:53:15.589289+00:00` i nie zawiera jeszcze tego odbioru.

W swoim sandboxie uzyskano **EACCES, errno 13** dla katalogu
`/var/lib/codex-team/worktrees/chatadhd-continuous-graph-ui-5912012c-20261101`
oraz jawnych odczytów:

- `.git`, `CHECKPOINT.md`, `CONTINUOUS_GRAPH_TASK.md`;
- `GRAPH_VISUALIZATION_EXISTING_RESEARCH.json`, `GRAPH_VISUALIZATION_CODE_GAPS.json`;
- `EXISTING_VIEW_ENGINE_DESIGN.md`, `VIEW_ENGINE_GITHUB_PROVENANCE.json`.

`git -c safe.directory=WORKTREE -C WORKTREE status --porcelain=v1 -z
--untracked-files=all -- . ':(exclude)docs/exports/**'
':(exclude)eval/real-holdout-key/**'` zakończył się kodem **128**:
`fatal: cannot change to ...: Permission denied`.
Rejestr `git worktree list --porcelain` wskazuje bazę 5912012c, detached;
komunikat `prunable gitdir file points to non-existent location` z tego rejestru
nie dowodzi braku pliku przy odmowie dostępu. Nie wykonywano prune/repair/chmod.

Nie można odczytać closed manifest/product patch ani sprawdzić HEAD/status/hashów
przed/po. Nie odtwarzano delty z innych kanałów ani nie pisano zastępczego renderer/researchu.
Następny krok: dostęp read-only UID990 do ścieżki i allowlisted plików, matching
końcowy receipt/manifest; wznowienie tego samego integratora od rzeczywistego HEAD.
Wtedy review/apply raz, jedna wspólna bramka web/typecheck/real browser zgodnie
z bieżącym zleceniem. Nie trzeba ponawiać backendu ani author cheap checks.

Wolne miejsce podczas wejścia: **1 150 820 352 B**, późniejszy pomiar
**1 147 731 968 B**; próg 1 GiB spełniony, ale zapas mały. Nie tworzono kopii cache,
node_modules ani nowego stagingu payloadów. Przed przyszłym build/stage wymagany
ponowny pomiar. To nie jest zgłoszony ENOSPC; bieżącym blokerem jest dostęp do delty.

## Obowiązkowa bramka projekcji — oddzielnie od opcjonalnych mediów

Administrator przyjął **zakresowe dowody etapowe**, nie pełną ponowioną macierz
na końcowej bibliotece. W tej turze sprawdzono statycznie archiwum i **108/108**
payload hashy; nie wykonano ponownie testów.

Aktualny kandydat zawierający fix:

- code `e27ce9ff0076c287963a588d942d68e7bebaca3f`;
- runtime tree `688466985dd9440563b0ec51eb30457d9312117a`;
- lib `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a`;
- server `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90`;
- artifact `0caab73321bc7d9a565d1ec6f69e48be9108af6c4e885b2f36dfda2b4f62c63e`;
- manifest `ddd3c4a40b90c09082d622fdae472117d9d8387394792076f611cb6b7880a345`.

| Obowiązkowy zakres / historyczny wynik | Dokładna biblioteka wykonania |
|---|---|
| PDF: stare replay odmawia, integralność snapshotu, stale confirmation/tamper refusal, jawna reprojekcja do osobnej wersji i propozycje mapowania — PASS zakresowy | `b7329f37ccd959b7cb770fd1c0f873544e775f66c3bb48f12bd01992a4fa0f5c` |
| Wspólny reopen zawierający nowy PDF — PASS zakresowy | `c41b3e364a8b90e297946015b051d3c1291f35382a7d7f7d5af825a9d867cf0e` |
| Pusta projekcja zgłasza `not_exercised`, nie vacuous compatibility — PASS | `029e18c21b8460ac72f9ec2072c10b6d450ee770242cbaceda5b5432991e5a01` |
| Poprzednik na osobnej kopii odmawia nowego wybranego occurrence, czyta zachowane stare; następnie rzeczywisty start z odtworzonej pre-copy przywraca stary wybór/original — PASS ograniczonego cold restore, **nie** in-place compatibility | `0c2052d8bd31214876a5690f05394db0e4f4ca862c7d882dfe7e6f6f2202e4c5` |

Poprzednik tej próby: własny pakiet `36e945fd`, SHA256
`f9767a0c20e628f942f8abda9b24daebab5fe9fb23666ae70324f132840754fb`.

**RPO: cold rollback nie zachowuje zapisów po kopii.** Selekcje migrują wyłącznie
jawnie: reprojection zwraca propozycje, `selection_changed=false`; osobny wyraźny
wybór nowych ID zmienia globalny selector konta. Stare ID/receipts nie są podpisywane
nowym narzędziem. ZIP tree migration nadal unsupported.

Brakujące fakty: pośrednie biblioteki b732/029 powstały na bazie eae7ece4 z roboczą
deltą, bez osobnego immutable Git commita/pakietu każdej fazy. Nie istnieje dowód,
że pełną fazę old→new ponowiono na lib7004. Końcowy kod/source binding oraz
późniejsze dotknięte sprawdzenia są udokumentowane i przyjęte zakresowo przez
administratora; nie dopisujemy brakującego execution PASS. Fresh coherent backup,
RPO przy cutover, operacyjny preflight i decyzja właściciela pozostają osobne.

Dawny zamrożony `ed1392310b87aca76be7571dbc90e447e7c35a71` / pakiet
`ee2117e8644bed34bc2b1772df017e001b6c713842df56edbc8fee8cddf947e8`
**nie zawiera nowego projection_upgrade** i nie otrzymuje jego PASS przez
skojarzenie. Nie cofnięto kodu do tego pakietu. Legacy XLS/video/bundle/extent to
oddzielny zakres; nie są warunkiem ponowienia obowiązkowej bramki projekcji.

Nowy immutable kandydat UI **nie powstał**. Real vision/mobile/deploy NOT RUN.
Stare ETA 11–12 PL nie jest obietnicą. Brak zmian produkcji/usług/VM/billing,
poświadczeń, modeli/API, nowych agentów, push/CI/publikacji. Deadline nieodnowiony.
Dowody: `.integration-continuous-graph-ui-20261101/intake-blocker.json`,
`release-critical-binding.json`, `resources.json`, `manifest.json`, `final-result.json`.
