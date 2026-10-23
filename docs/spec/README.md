# Wymagania właściciela jako dane (`docs/spec`)

Pakiet SPEC1. Stan na 2026-10-23. Opis mówi, co kod robi DZIŚ.

## Po co to jest

Właściciel wielokrotnie prosił o „pełną specyfikację”. Prawie komplet jego słów już jest w trzech plikach
Markdown, ale to proza i nic jej nie czyta: briefy dla agentów pisze się ręcznie, więc każda poprawka
właściciela oznaczała restart. Teraz te trzy pliki są też DANYMI z numerami:

| Plik źródłowy | Co w nim jest | Co z niego powstaje |
|---|---|---|
| `docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md` | 403 pozycje z kodami (PH, CL, GR, CM, GL, MR, AG, UI, DIP, ON, RE, PC, OT), cytaty dosłowne, „Sens”, dodatki bez numeru | pozycje z cytatami, sensem, streszczeniem wymagania |
| `docs/reports/LUKI_2026-10-16.md` | stan kodu wobec każdej pozycji: JEST / CZĘŚCIOWO / BRAK / NIEAKTUALNE / SPRZECZNE, dowód, następny krok | status z datą, dowody, następny krok |
| `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` | sekcje R1…R63, D1…D3 z cytatem dosłownym | pozycje z cytatem i opisem |

Pliki w tym katalogu:

- `ids.json` — rejestr numerów (alias → numer). Zmienia się tylko przez dopisanie.
- `understanding.json` — zatwierdzenia i poprawki rozumienia (powstaje przy pierwszym `propose`).
- `spec.json` — WYLICZONE dane (schemat `loom.spec/1`): nie edytuj ręcznie.
- `SPEC.md` — WYLICZONY widok dla człowieka: tabela liczników i lista pozycji po tematach. Nie edytuj ręcznie.
- `config.json` — opcjonalny; nadpisuje presety z `tools/spec.py` (rodzaje pozycji, etykiety statusów, ścieżki plików).

Narzędzie: `python3 tools/spec.py` (Python 3.11, bez sieci, **bez żadnego wywołania modelu**). Testy:
`python3 -m unittest discover -s tools/tests`.

## Numer pozycji

Numer kanoniczny jest **nieprzezroczysty**: `s_` i 12 znaków szesnastkowych (np. `s_f23586b9dffb`).
Nie niesie tematu, rodzaju ani daty — to są atrybuty pozycji. Przydziela się go RAZ i zapisuje w `ids.json`;
poprawienie literówki w cytacie (dyktowanie się myli) numeru nie zmienia. Dawne kody (`UI12`, `GR38`,
`R56`, `MM01–MM10` …) zostają jako **aliasy**, bo odwołują się do nich dokumenty; `show`, `list` i `cite`
rozumieją oba. Pozycje bez dawnego kodu (dodatki z dopiskiem „Uzupełnienie…”, sekcje rejestru bez numeru R)
mają alias z nagłówka, np. `slowa:uzupelnienie-3-2026-10-16-16-30-utc-…` — **zmiana nagłówka takiej sekcji da
nowy alias, a stary numer zostanie w `ids.json` zarezerwowany** (`check` to zgłosi jako ostrzeżenie).

## Jak zapisać poprawkę albo nową wypowiedź właściciela

Poprawka to NOWA albo zmieniona pozycja, nie restart:

```
printf '%s\n' '"dosłowne słowa właściciela"' > /tmp/cytat.txt
python3 tools/spec.py add --id R64 --date 2026-10-24 --quote-file /tmp/cytat.txt \
        --title "krótki tytuł" --sense "jedno zdanie, co to znaczy"
```

`add` dopisuje sekcję `## R64 — …` w `OWNER_REQUIREMENTS_2026-09-26.md` (przed pierwszą sekcją
nienumerowaną), buduje pozycję i odświeża `spec.json`, `ids.json`, `SPEC.md`. Plik z cytatem trafia tam
dokładnie tak, jak jest — narzędzie niczego nie dodaje ani nie poprawia. **Odmawia, gdy numer już istnieje**
(`R58` → „już istnieje”) i nigdy nie zmienia istniejącego cytatu. `--dry-run` pokazuje, co by wstawiło.
Kod `R`/`D` + liczba jest jedyną postacią, jaką przyjmuje `add`.

Ręczna edycja trzech plików `.md` też jest dozwolona; potem `python3 tools/spec.py build`. `check` zgłosi błąd,
dopóki `spec.json` nie nadąża za plikami.

## Rozumienie pozycji (reguła R61)

Kosztowna praca zaczyna się dopiero po zatwierdzeniu przez właściciela parafrazy koordynatora. Parser **nie
tworzy żadnych parafraz** — wszystkie pozycje startują ze stanem `none`.

```
python3 tools/spec.py propose  <numer> --text "jak ja to rozumiem"      # koordynator; stan: proposed
python3 tools/spec.py approve  <numer>                                   # TYLKO na słowo właściciela; stan: approved
python3 tools/spec.py correct  <numer> --text "jak to ma być naprawdę"   # TYLKO na słowo właściciela; stan: corrected
python3 tools/spec.py list --understanding proposed                      # co jeszcze czeka na niego
```

Nic nie zatwierdza się samo; `approve` odmawia, gdy nie ma propozycji ani poprawki. Każda zmiana stanu zostaje
w `history` (kto, kiedy, jaki tekst) w `understanding.json`. Cytat dosłowny się przy tym nie zmienia.

## Jak brief cytuje numery

W briefie zamiast wklejać tekst pisze się `spec:<numer>` (kanoniczny albo dawny kod), np. `spec:s_f23586b9dffb`
albo `spec:UI12`. Agent czyta pozycję poleceniem `python3 tools/spec.py show <numer>`: dostaje cytat dosłowny z
plikiem i linią, sens, status z datą, dowody i następny krok. Koordynator może sprawdzić brief:

```
python3 tools/spec.py cite brief.md           # OK / BRAK dla każdego spec:<numer>; kod wyjścia 1, gdy numeru nie ma
python3 tools/spec.py cite brief.md --full    # wypisuje pozycje z cytatami dosłownymi do wklejenia agentowi
```

## Co oznaczają pola (skrót)

- `quotes[]` — cytaty dosłowne: `text`, `src` (plik:linia), `ref` (źródło, które cytat podaje u siebie), `attr`
  (oznaczenia z pliku, np. `dosłownie`, `przekazane`), `lead` (słowa tuż przed cytatem), `secondary` = cytat stoi
  przy napisie typu „relacja”, „zapis asystenta”, `principles.json` (heurystyka z listy w presetach; to NIE jest
  rozstrzygnięcie, czy to słowa właściciela — o tym mówi plik źródłowy).
- `sense` — wiersz „Sens:” (albo „Sedno:” w temacie kosztów); `summary` — jednozdaniowe wymaganie z sekcji
  „Wymagania wyprowadzone”; `restatement` — opis pod cytatem w rejestrze R/D (zapis asystenta).
- `status` — `{value, checked_on, observation: true, src}` skopiowane z LUKI. **To dana obserwacja z dnia
  `checked_on` (2026-10-16, z tytułu pliku), nie ograniczenie** (R60). Pozycje bez statusu (104) po prostu nie
  występują w LUKI; nie znaczy to, że czegoś brakuje w aplikacji.
- `kind` — wynika z przedrostka kodu i pliku: `R` → requirement, `D` → decision, `PH` → philosophy, pozostałe
  kody ze SLOWA → remark (uwaga właściciela), dodatki bez numeru → statement. Lista jest presetem
  (`kind_by_prefix` w `tools/spec.py` albo `config.json`), nie regułą; „rule” i „question” z briefu nie mają
  jeszcze żadnej pozycji, bo żaden przedrostek ich nie wyznacza i nic nie zostało zgadnięte.
- `theme` — temat ze SLOWA; dla pozycji tylko z LUKI wywnioskowany z przedrostka, jeśli przedrostek ma w SLOWA
  jeden temat (`theme_inferred: true`); inaczej `null` („bez tematu”).
- `refs`, `supersedes`, `superseded_by`, `supersession_notes` — odwołania do innych pozycji znalezione w tekście
  pozycji (kody i zakresy, np. `GL05–GL08`) oraz zapisy typu „zastąpione przez PC02”. Notatka niesie zdanie
  źródłowe, bo zastąpienie bywa częściowe — to czytelnik decyduje, czy całkowite.

## Co `check` sprawdza

Numery unikalne i zgodne z `ids.json`; aliasy nie powtarzają się; **każdy cytat (i sens, streszczenie, dowód,
następny krok) jest dosłownym fragmentem swojego pliku pod podanym `plik:linia`** (zmieniony cytat w `spec.json`
= błąd; przesunięty o linie = ostrzeżenie); `spec.json` jest identyczny z wynikiem `build` (inaczej: „uruchom
build”); każdy kod z LUKI ma pozycję w SLOWA/rejestrze albo jest wypisany jako sierota z linią; liczniki per
status, per temat, per plik; lista kodów tylko w jednym pliku; porównanie z liczbami z nagłówków LUKI (nagłówek
mówi dziś `BRAK 12`, pozycji jest 16 — nagłówek jest nieaktualny). `--strict` robi z sierot błąd, `--gaps`
wypisuje niezaklasyfikowane fragmenty.

## Znane luki (stan faktyczny)

- 117 niepustych wierszy w 43 fragmentach plików źródłowych nie jest pozycją: wstępy do tematów („Zasady
  czytania”, „Zakres”), listy „Pominięte / Odrzucone” (celowo NIE słowa właściciela), „Relacje między
  punktami”, sekcja „Krytyk kompletności” w LUKI (wypowiedzi, których zbiór jeszcze nie ma), nagłówki z
  „źródło: przebieg 2”. Lista: `python3 tools/spec.py check --gaps` oraz `gaps` w `spec.json`. Nic z tego nie
  zostało odrzucone po cichu — to proza, nie wypowiedzi z numerem.
- Cytaty wtórne (relacja agenta, zasada z bazy, zapis asystenta) parser zachowuje i oznacza `secondary`; nie
  odrzuca ich i nie rozstrzyga, czy to słowa właściciela — przenosi oznaczenia z plików.
- Zbiór sięga stanu 2026-10-16 (SLOWA, LUKI) plus dopiski z 2026-10-21 i rejestr R do R63 (2026-10-23). Czego nie
  ma w tych trzech plikach, tego nie ma w danych; nowe słowa właściciela dopisuje `add`. Przeglądu transkryptów
  po 16.10 ten pakiet nie robił.
- Status w `spec.json` nie jest odświeżany: to obserwacja z 2026-10-16. Nowy przegląd luk = nowy plik LUKI i
  zmiana ścieżki w `config.json` (`sources.luki`) albo nowa wersja pliku, potem `build`.
