# B-CH-02 — Legacy SemanticLLM jako metoda z wersją: prompt, limity, jeden efektywny analizator

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/chatadhd` @ `9e20f99a`; inwentarz wątku 1 @ `161cc22d` (`origin/gpt/data-profiles-2026-10-04`) |
| Fala / priorytet | W2 / P1 |
| Ustalenia | CH-002, CH-003, DIC-0009, 0010, 0012, 0013, 0014, 0204 i pozostałe grupy wątku 1 ze statusem innym niż `migrated` (127 grup w inwentarzu; agent liczy aktualne); DIC-0004/0008/0011 (`partial`/`deferred` z wątku 11) |
| Niezmienniki EITG | G2, G6, G9 |
| Rozmiar / model | L / Sonnet 5.5 high |
| Zależności | T2 (worker), T6 (strażnik ×10 na ścieżce workera — nie dublować) |
| Worktree | `t9-ch-02` |

## Zakres
Prompt analizy (`kAnalysisPromptText`), prefiks 3000 znaków, `temperature 0.1`, `max_tokens 800`,
timeout, próg 20 znaków, zatrzask 5 błędów, mnożnik scalania 0,8, nagłówki `HTTP-Referer`/`X-Title`
stają się **wersją metody** w MethodRegistry z hashem promptu i parametrów; wynik analizy niesie
`produced_by` z tym hashem. Nakładka `semantic_analyzer` dociera do **jednego efektywnego
snapshotu** per zadanie: real-time graf, worker i fallback LLM używają tego samego (CH-003).

## Pliki
- `loom/src/semantic/semantic_llm.cpp:23–43` (prompt), `:114–136` (żądanie), `:175` (próg 20),
  `:197` (scalanie), `:130` (nagłówki); `loom/include/loom/semantic_llm.h:48–64`.
- `loom/src/runtime.cpp:181–202`; `loom/src/semantic/analyzer.cpp:53`, `:166–195`;
  `loom/src/worker/semantic_worker.cpp:31`, `:363–379`; `loom/src/graph/graph_engine.cpp:118–141`;
  `loom/src/graph/graph_memory.cpp:31–64`.
- Lustro Python: `engine/semantic_llm.py:142–171`.
- Dane: `loom/data/prompts/` (5 plików — sprawdzić, czy `semantic_analysis` już tam jest),
  `loom/data/runtime/semantic_analyzer.pack`, `loom/data/policy/calibration.json` (DIC-0204),
  MethodRegistry.
- Konsumenci: `Runtime → SemanticLLM::analyse/call_llm`, `SemanticWorker → SemanticLLM`,
  `engine.semantic_llm._call_llm`.

## Mechanizm docelowy (istniejący)
MethodRegistry (metoda → wersja → preset, R41/R48) + pack promptów z hashem + `produced_by`
(`loom_provenance`). Strategia wejścia `prefix` / `chunks` / `full` jako parametr presetu ze
śladem pominiętych zakresów (CH-002: „prefix jawnie raportuje utratę”). Zatrzask błędów jako
ustawienie z widocznym stanem (`GRAPH_ENTRY` §1: `invariant:external`, próg jako ustawienie).

## Kroki
**Rozważenie.** Policzyć aktualne grupy `open` wątku 1 na bieżącym SHA (część mogła zniknąć po
integracji 2026-10-05 i gałęzi B); potwierdzić, że MethodRegistry przyjmuje metodę „legacy
semantic analysis” bez nowego typu; rekord decyzji: strategia domyślna = `prefix` 3000 (bajt
w bajt z dziś) z jawnym raportem utraty.

**Implementacja.** (a) fixture z atrapą transportu przechwytującą dokładne żądanie;
(b) prompt + parametry do packa jako wersja `legacy` z hashem; (c) `SemanticLLM` czyta efektywny
preset; `produced_by` z hashem w wyniku; (d) jeden snapshot analizatora per zadanie przekazany
trzem konsumentom (CH-003); (e) test A/B: dwie wersje promptu/limitów → dwa różne żądania i dwa
hashe; (f) test utraty: treść po znaku 3000 — `full`/`chunks` zachowuje, `prefix` raportuje;
(g) Python: ten sam prompt z tego samego packa (osadzony generowany) albo jawny status
„legacy Python używa wersji X” — nie dwa różne prompty bez wiedzy.

**Optymalizacja.** Prefiks promptu stabilny (R49) — prompt analizy jako blok S0; usunąć
duplikaty prompt/parametry między C++ i Python.

**Iteracja.** Presety z badań wątku 7 (n-gramy, BM25, Jev) jako kolejne wersje tej samej metody
(`GRAPH_ENTRY` §7), bez płatnych wywołań w tym briefie.

## Kryterium zamknięcia (CH-002, CH-003, DIC-0009…0014)
- Dwie wersje promptu i limitów zmieniają dokładne żądanie atrapy i hash `produced_by`.
- Pack dodaje unikalny wzorzec regex: real-time, worker i fallback LLM wykrywają go z tym samym
  hashem; uszkodzona nakładka ma ten sam jawny status w każdej ścieżce, nie podstawia builtinu.
- `max_tokens` czatu nie steruje analizą; analiza ma własny budżet w presecie.
- Próg 20 znaków, zatrzask 5 i mnożnik 0,8 są ustawieniami z obecnymi wartościami jako preset.
- Brama pełna; generatory `--check`; 0 płatnych wywołań.

## Czego nie robić
- Nie zmieniać domyślnej ekstrakcji (ryzyko audytu: „zmiana ekstrakcji może zmienić cały graf”);
  nowa strategia to nowa wersja, stara zostaje.
- Nie przeładowywać profilu w połowie analizy (zamrożenie per zadanie).
- Nie czynić pochodzenia `regex` vs `llm` podrabialnym przez dane (DIC-0008).

## Co zapisać
Rekord decyzji; hashe wersji `legacy`; liczba grup wątku 1 zamkniętych / pozostałych z lokatorami.
