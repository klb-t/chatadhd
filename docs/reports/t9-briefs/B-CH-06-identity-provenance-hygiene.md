# B-CH-06 — Higiena tożsamości i pochodzenia w modułach B/C/D/E: kolejność kluczy, parametry parsera, relokacja, pola pomocnicze

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/chatadhd`: B @ `387afe08`, C @ `21ffc5e0`, D @ `1d3d1331`, E @ `3eaac295` (pass4); bieżąca gałąź robocza po T1–T8 |
| Fala / priorytet | W4 / P3 |
| Ustalenia | A4-C-001, A4-C-002, A2-C-001, A2-C-004, A3-DISC-001, A4-D-001, A4-B-RES001 |
| Niezmienniki EITG | G2, G3 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | **T1** (`catalog/scan.cpp` — relokacja), **T4** (`tools/resource_graph/access.py` — ta sama okolica co `core.py`) |
| Worktree | `t9-ch-06` |

## Zakres
- Tożsamość przepisu/operacji nie zależy od kolejności kluczy obiektu (kanoniczna serializacja;
  A4-C-002, A4-CKP-001 to ten sam błąd w innym repo).
- Parametry parsera wchodzą do tożsamości metody i rewizji wyniku (A4-D-001).
- Wersja źródła w przygotowaniu i manifest płatnika zgodne albo odrzucone przed emisją (A4-C-001).
- Relokacja tych samych bajtów: ten sam adres, nowy lokator osiągalny, stary `stale` i zachowany
  (A4-B-RES001; EITG G3).
- Publiczne artefakty nie kopiują niesprawdzonych pól pomocniczych ani nie ujawniają odrzuconego
  wejścia w diagnostyce (A2-C-001/004).
- Surowy pakiet równoważny zaakceptowanemu jest przyjmowany przez MethodRegistry niezależnie od
  serializacji (A3-DISC-001).

## Pliki
- `loom/src/catalog/scan.cpp:89–104` (`register_source/checkpoint_done`, B SHA).
- `loom/tools/resource_graph/core.py:80–108`, `:322–363` (D).
- Moduł C: testy pass4 `C4-CONNECTION-03-B`, `C4-CONNECTION-06-B`, `C4-PUBLIC-evaluation-protocol-B`,
  `C4-PUBLIC-handoff-projection-B`, `C4-PUBLIC-handoff-example-B`, `C4-REGISTRY-02-B`
  (`tools/ecosystem-audit-2026-10-09/pass4/C-boundary/`, runner `run.py`/`run_registry.py`;
  raport `pass4/C-boundary/REPORT.md`).
- Dane: `loom/data/runtime/resource_method.pack`, `resource_read.pack`, `resource_projection.pack`,
  `loom/data/resource_graph/parsers.json`.

## Mechanizm docelowy (istniejący)
I5 (identyfikatory wyprowadzone z treści po kanonicznej serializacji — `json.dumps`-identyczna,
`CLAUDE.md`), `Locator` z `loom_sources`, MethodRegistry z receipt, `loom_provenance`.

## Kroki
**Rozważenie.** Uruchomić testy pass4 (`run.py --repo … --sha …`) na bieżącym SHA i zapisać,
które B-przypadki nadal FAIL (część mogła zostać naprawiona przez T1/T4/gałąź B); rekord decyzji
z listą pozostałych.

**Implementacja.** Dla każdego pozostałego: najpierw test pass4 jako fixture (już istnieje),
potem minimalna zmiana: kanonizacja kluczy przed hashem; parametry parsera w kluczu metody;
sprawdzenie zgodności wersji źródła przed emisją; relokacja = dodanie lokatora + `stale`;
projekcja publiczna z allowlistą pól; porównanie DTO po kanonizacji.

**Optymalizacja.** Jedna funkcja kanonizacji używana przez B/C/D (bez trzech kopii).

**Iteracja.** Ten sam test kanonizacji jako wzór dla A4-CKP-001 (B-CKP-03).

## Kryterium zamknięcia (z pass4)
- `C4-CONNECTION-03-B`, `-06-B`, `C4-PUBLIC-*-B`, `C4-REGISTRY-02-B` PASS; kontrolne
  `C4-CONNECTION-01`/`-05` nadal PASS.
- `relocation.py`: zwykły i wymuszony rescan czytają identyczne bajty z nowej lokalizacji; stare
  dane i wersja zachowane.
- Test D: zmiana opcji parsera zmienia identyfikator metody i rewizję wyniku; identyczne opcje →
  identyczne ID.
- Brama pełna; żadnych płatnych wywołań (transport skryptowany).

## Czego nie robić
- Nie zmieniać istniejących ID w bazie użytkownika; nowa kanonizacja dotyczy nowych rekordów,
  stare dostają mapowanie.
- Nie otwierać realnych baz testowo (ryzyko LOOM-005); fixtures.

## Co zapisać
Rekord decyzji z wynikami pass4 przed/po (nazwy przypadków, PASS/FAIL).
