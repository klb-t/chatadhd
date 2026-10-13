# B-MOD-03 — Profil `ports`: rola -> adapter, zakresy R47

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-01. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Nowa domena `RuntimeProfile` `ports`: mapa ról (`chat.default`, `analysis.fast`, `embed.default`, `asr`, `ocr`, `format.*`) na adapter+parametry, z łańcuchem zakresów aplikacja -> użytkownik -> rozmowa i regułą konfliktu. `explain` zwraca skąd wybór.

## Pliki
`src/model/gen_runtime_profiles.py`, `runtime_profiles_embedded.inc`, `include/loom/runtime_profile.h`, miejsce rozstrzygania zakresów (`model_profile.h`), UI: `web/src/components/SettingsPanel.tsx`.

## Kroki
1. Definicja domeny + domyślne wartości = dzisiejsze zachowanie.
2. Resolver `resolve_role(role, scope)` używany przez port z B-MOD-01.
3. Jedna trasa/akcja `ports.explain`.
4. Panel w ustawieniach: lista ról, wybór adaptera (bez ukrytych limitów, CLAUDE.md).

## Testy
Rozmowa nadpisuje użytkownika; konflikt wg reguły z danych; `explain` zwraca łańcuch zakresów; domyślny profil = zachowanie sprzed zmiany (A/B).

## Kryterium akceptacji
Zmiana roli `analysis.fast` na inny model bez zmiany czatu; wpis w `explain`.

## Ryzyko
Średnie: schemat profilu (wersjonowanie).
