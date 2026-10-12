# B-CH-01 — Ustawienia startowe jako dane: okablowanie domen bootstrapu i lustro Python

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/chatadhd` @ `9e20f99a` (`main` 2026-10-09); **potwierdzić na gałęzi roboczej po T7** |
| Fala / priorytet | W1 / P1 |
| Ustalenia | CH-001; domeny `runtime_paths`, `config`, `usage_policy` „gotowe dane, okablowanie oczekuje” (`migration-status.md`, `runtime-bootstrap-profile-handoff.md` na `origin/gpt/data-profiles-2026-10-04`); DIC-0022 (preset vs jawny wybór) |
| Niezmienniki EITG | G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | T7 (poprawki aplikacji Python dotykają sąsiednich plików) |
| Worktree | `t9-ch-01` |

## Zakres
Domyślne wartości startowe (adres, model, prompt systemowy, temperatura, tokeny, ustawienia grafu)
przestają być ręcznym obiektem w `config.cpp` i `config.py`; źródłem jest `loom/data/runtime/config.pack`
(istnieje) przez RuntimeProfile/DefaultLayers; aplikacja Python czyta te same dane przez pack
osadzony generowany z danych. Format `config.json` w katalogu danych **bez zmian** (KOD ≠ DANE).

## Pliki
- `loom/src/core/config.cpp:197–224` (`config_defaults`), `:110–131` (JsonStore start po błędzie).
- `engine/config.py:15–28` (`DEFAULTS`), `:71–83` (auto-upgrade).
- Dane: `loom/data/runtime/config.pack`, `runtime_paths.pack`, `usage_policy.pack`;
  `loom/src/model/gen_runtime_profiles.py` (generator osadzenia, `--check`).
- Konsumenci: `Runtime::open → Config`, `Config::get → ChatEngine/SemanticLLM`,
  `main.py → engine.config.Config`.
- Testy: `loom/tests/compat/` (zgodność config/secrets Python↔C++ — pozostaje zielona jako
  sentinel), nowy test rozróżnienia „preset” vs „jawnie ustawione” (DIC-0022).

## Mechanizm docelowy (istniejący)
RuntimeProfile domena `config` + DefaultLayers (R40: override / disable / exclude); dla Python —
`engine/config_defaults_embedded.py` **generowany** przez `gen_runtime_profiles.py` z tego samego
packa (R42: pack osadzony z danych, nie ręczne domyślne). Jawnie ustawiona wartość w `config.json`
wygrywa (`most_specific_wins`), wykluczenie nie wraca po restarcie.

## Kroki
**Rozważenie.** Potwierdzić lokatory na bieżącym SHA; sprawdzić, czy `config.pack` zawiera
komplet pól z `config_defaults` (jeśli nie — uzupełnić dane, zachowując obecne wartości bajt
w bajt); przeczytać `runtime-bootstrap-profile-handoff.md`; rekord decyzji: Python czyta pack
osadzony, nie plik z drzewa kodu (aplikacja na Androidzie nie ma `loom/data`).

**Implementacja.** (a) fixture: nowy katalog danych + Runtime::open bez `config.json` →
przechwycić efektywne wartości; (b) `config_defaults` czyta profil efektywny; obecne wartości
jako preset `legacy` z hashem; (c) brak/uszkodzony pack → `load_error` jawny albo pack osadzony;
(d) Python: generator + `DEFAULTS` zastąpione importem z modułu generowanego; `--check` w CI;
(e) test A/B: dwa packi → dwa różne modele domyślne w nowym katalogu bez edycji kodu;
(f) test DIC-0022: wartość z `config.json` zachowuje pierwszeństwo, wykluczony default nie wraca;
(g) allowlista strażnika literałów: klucze kontraktu (kategoria 1) zostają, wartości znikają.

**Optymalizacja.** Usunąć zduplikowane domyślne między C++ a Python (jedno źródło); zmierzyć:
liczba literałów przed/po w obu plikach.

**Iteracja.** Pozostałe dwie domeny bootstrapu (`runtime_paths`, `usage_policy`) tym samym
wzorem, jeśli T2/T6 ich nie okablowały; wpis do rekordu decyzji, które pozostają.

## Kryterium zamknięcia (z audytu CH-001 + G10)
- Zmiana domyślnego modelu/promptu w packu działa w nowym katalogu bez edycji C++/Python.
- Błędny pack daje jawny błąd; pack osadzony jest generowany z tych samych danych (`--check`).
- Wykluczenie nie wraca przez fallback Config po restarcie.
- `explain()` wartości efektywnej wskazuje warstwę (`pack` / `config.json` / wykluczenie).
- Testy compat Python↔C++ zielone; pełna brama `chatadhd`.

## Czego nie robić
- Nie zmieniać formatu `config.json` ani `secrets.json`; nie kasować wartości użytkownika
  (ryzyko z audytu: „nie wykasować modeli użytkownika”).
- Nie wprowadzać drugiego pliku domyślnych w Python pisanego ręcznie.
- Nie dotykać `semantic_model` resetu (CH-012 już naprawione).

## Co zapisać
Rekord decyzji; hash presetu `legacy`; liczby literałów przed/po; wynik `--check`.
