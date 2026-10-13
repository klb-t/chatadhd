# B-MOD-04 — Operatory kompozycji portów

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-02, B-MOD-03. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Wspólne `chain`, `fallback`, `ensemble`, `mirror` jako dekoratory portów, wybierane z profilu `ports`. Wyprowadzić z dwóch istniejących wystąpień (kolejność ASR, `placement` magazynów, fuzja metod), nie projektować od zera.

## Pliki
`include/loom/port_compose.h` (nowy), `src/media/media_providers.cpp:372`, `src/storage/storage_service.cpp`, `src/context/method_channels.cpp:249`.

## Kroki
1. `Fallback<P>` i `Mirror<P>` zastępują ręczne pętle w media i storage (bez zmiany zachowania).
2. `Ensemble<P>` z fuzją (rrf/mean z istniejących) dla embedding i LLM-analizy; wynik niesie `produced_by` wszystkich członków.
3. Wybór z `ports`.

## Testy
Testy właściwości: fallback przy błędzie członka, mirror z kworum, ensemble z brakującym członkiem (degradacja, nie błąd), zachowanie tożsame z dotychczasowym.

## Kryterium akceptacji
Dwa różne modele LLM jako ensemble analizy jedną zmianą profilu.

## Ryzyko
Średnie: koszty ×N — ostrzeżenie ×10 wg zasady właściciela, nie blokada.
