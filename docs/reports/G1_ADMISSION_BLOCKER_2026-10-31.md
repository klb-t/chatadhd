# G1 — odbiór odłożony przez rzeczywistą odmowę dostępu, 2026-10-31

## Decyzja i zachowana baza

**G1 nie przyjęto. Nie nałożono żadnego patcha, nie wykonano buildu ani testów.**
Bieżąca przyjęta baza produktu: `3319312f22e1d7920a051a2496337fb6ee5e9554`,
kod package/GUI `daf4f48412924737f260a42499818ea6bbeba513`.
Sprawdzono zachowany CHECKPOINT i poprzednie wyniki: **20/20 plików poprzedniego
przyrostu pozostało zgodnych bajtowo**, śledzone drzewo i indeks były czyste.
Nie powtarzano M1/M2/DOCX/package/GUI. Błędy prezentacji PNG i nienałożony
patch wspólnych hooków pozostają opisane w poprzednim raporcie.

`accepted-base.json` nadal opisuje historyczne `d0af04a2` z 22:12:53 UTC;
nie jest poleceniem resetu ani dowodem utraty późniejszych commitów.
Obecny HEAD zachowuje tę bazę jako przodka. Kontynuacja tej samej sesji
integratora `01a192f5-2814-7e52-bf0c-32f3428151ed`.

## Kandydat i dowody administratora

- Task: `chatadhd-graph-source-parts-20261101`, stan `completed`.
- Worktree: `/var/lib/codex-team/worktrees/chatadhd-graph-source-parts-d0af04a-20261101`.
- Base/HEAD autora według receipt i `git worktree list --porcelain`:
  `d0af04a2f5a24205618ec827bec02d0765887af9`.
- Snapshot: `1793485310488154795`; archive attestation:
  `a5e7a7ae5038fb49415bdd92f95bc858285b7de0950f2f52e5681ab121db1f09`.
- `integration-receipts.json`: schema `codex-team-integration-receipts/1`,
  data 22:24:16 UTC; audyt stabilności/zgodności potwierdzony przez administratora.
- `team-receipts.json`: schema `codex-team-receipts/2`, data 22:38:46 UTC.
- `g1-code-review.json`: ready_for_sole_integrator, brak blokujących ustaleń
  statycznych; native **0 wykonanych z 8 przygotowanych**, HTTP **0 z 2**.

Te dowody nie zastępują własnego dostępu do źródeł i testów integratora.
Archiwum root-only nie było odczytywane. Nie zmieniano praw ani cudzych plików.

## Konkretny bloker dostępu

Rzeczywisty proces integratora: **UID/EUID 990**.
Polecenie read-only `git -c safe.directory=<worktree> -C <worktree> status
--porcelain=v1` zakończyło się kodem **128**: error opening `.git`: Permission denied.
Bezpośredni odczyt `/var/lib/codex-team/worktrees/chatadhd-graph-source-parts-d0af04a-20261101/.git`
potwierdził **errno 13 / EACCES**. Nie uzyskano świeżego statusu, niezależnego
HEAD ani jawnego diffu/untracked przez Git autora. Odczyt wspólnej listy
worktree potwierdza rejestrację i bazę, ale nie zastępuje statusu delty.

Wszystkie poniższe ścieżki względem **`/var/lib/codex-team/worktrees/chatadhd-graph-source-parts-d0af04a-20261101/`**
zwróciły **errno 13 / Permission denied** przy zwykłym odczycie pliku:

- `docs/reports/GRAPH_SOURCE_PARTS_2026-10-31.md`
- `loom/data/runtime/actions.pack`
- `loom/data/text/segmentation.pack`
- `loom/include/loom/text_source_parts.h`
- `loom/server/tests/test_server_graph_source_parts.py`
- `loom/src/actions/runner.cpp`
- `loom/src/context/unified_context.cpp`
- `loom/src/context/unified_context.h`
- `loom/src/model/runtime_profiles_embedded.inc`
- `loom/src/text/source_parts.cpp`
- `loom/src/text/text_pack.inc`
- `loom/tests/test_text_source_parts.cpp`

Dotyczy to **12/12 plików produktu** wymienionych w audycie, w tym pełnej treści
nowych source_parts.cpp, nagłówka i testów. `g1-integrator-access.json` z22:24:25
opisuje named-read ACL i zgodność20plików, lecz bieżący wykonany odczyt UID990
tego dostępu nie potwierdza. Nie ustalano przyczyny na poziomie hosta ani nie
próbowano zmieniać ACL, profilu usługi czy metadanych Git.

Checkpoint autora i **7 plików handoff** są czytelne: **8/8 hashy zgodnych
z attestation przed i po odbiorze**. Zgodny `tracked.patch` ma hash
`ebb2bccd7cdcd5bdc776a0380e6c7551cfd464c77899a1a5db209f3296aaf902`.
Pozostałe jawne patche:

| Patch | SHA256 |
|---|---|
| tracked-core.patch | 341a60ae0d1ecc21936302bf5f64d386ac729b113fd0d024cb4201e782f076d1 |
| shared-hooks.patch | 40cfc608196497c4323a07f119497391b183c26784367c71c11ac3f740de588d |
| new-files.patch | 0d9e908a238da921953e9ff88b1589a806027dd216f6a6ff82c6aaa787a99dbd |

Nie rekonstruowano z nich plików objętych odmową. Zgodność transportu nie
została przedstawiona jako zgodność świeżych bajtów produktu. Manifesty team,
integration i g1-admin-receipt były niezmienione między odczytami.
Własny dowód `.integration-g1-20261031/access-final.json`, SHA256
`cb07fc13ca53bf08ac505dfb79713e2761b89d34a48c9db4f88ff888455e89a9`;
szczegóły pierwszego odczytu w `access-before.json`. To lokalne ignorowane
dowody; checkpoint, handoff i administracyjne pliki nie są commitowane.

## Przeczytane wymagania i następny odbiór

Odczytano wszystkie cztery dokumenty pod absolutnymi ścieżkami
`/var/lib/codex-team/context/`: `philosophy-section-0.md` (§0 rewizja2),
`shared-structure-graph.md`, `DOCUMENT_GRAPH_CODE_MAP_20261031.md` oraz
`CONTINUOUS_GRAPH_SOURCE_CHECK_20261031.md`. Nie są nieobecne; wcześniejszy
błąd autora dotyczył względnego `context/`. Nie odczytywano prywatnych źródeł
wymienianych w tych dokumentach. Szersze wymagania grafu są kierunkiem,
nie wynikiem tego przyrostu. Dane nie zastępują inwariantów, a hash tekstu
nie ustala tożsamości źródła ani wystąpienia.

Po zapewnieniu zwykłego odczytu wskazanych plików i metadanych Git przez UID990,
administrator może wznowić ten sam integrator. Nie ma nałożonej delty G1
do ponawiania. Następne kroki pozostają konkretne:

1. Świeże receipts/status/HEAD oraz hash wszystkich plików przed i po odczycie;
   przeczytać pełną deltę i testy. Nie obchodzić root-only snapshotu.
2. Minimalnie scalić source .pack, akcje i unified context na zachowanym HEAD;
   zregenerować runtime_profiles_embedded.inc i text_pack.inc, oba `--check`.
   Nie nadpisać nowych defaultów DOCX generatem z bazy autora.
3. Sprawdzić efektywny `seg.language()` zamiast surowego błędnego request.lang
   oraz dodać/wykonać przypadek fallbacku. Zweryfikować granice cache:
   zwykła idempotencja nie dowodzi integralności ani naprawy całego podgrafu.
4. Własny przyrostowy build `-j2`, CTest `-j1` po aktualnym potwierdzeniu
   rezerwacji. Native8/HTTP2 i uzupełnienia mają sprawdzić Source/Unit/genesis,
   odrębne wystąpienia identycznego tekstu, wersję bez utraty poprzedniej,
   reopen, oryginał, dokładny zakres oraz treść w faktycznym żądaniu providera.
   Sprawdzić autoryzację trasy i współrzędne UTF-8; nie mylić ich z PDF/OCR/DOCX.
5. Regresje dobrane do rzeczywiście zmienionych warstw; bez powtórki
   zaakceptowanych bramek tylko z powodu wznowienia.

Automatyczny upload → materializacja, GUI wyboru fragmentu → kontekst,
pełne hierarchie części, hiperkrawędzie i control → binding → setting nie są
odebrane. G1 ma na razie deklarowany zakres jawnych akcji backendowych;
nie wykonano niezależnej walidacji produktu. Brak nowego wyniku implemented/
tested/deployed dla G1, brak kompilacji i procesów w tle w tym przyroście.
Bez publikacji, push/CI/deploy, zmian hosta, modeli, wydatków i nowego cyklu.
