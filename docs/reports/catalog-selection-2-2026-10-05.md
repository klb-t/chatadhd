# Wątek 6 — katalog, drugi przyrost — 2026-10-05

Gałąź `gpt/catalog-selection-2-2026-10-05`, baza kodu integratora
`66da570d3b5379492e128d940ad474467082c59f` (o jeden commit przed ówczesnym
`main` `e4109df`), następnie rebase na dokumentacyjny checkpoint
`0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b` — kod identyczny. Poprzednia gałąź `gpt/catalog-selection-2026-10-04`
pozostaje bez zmian. Ten przyrost jest niezależny od jej odbioru.
Fetch przed publikacją: `main=e4109df`, checkpoint integratora nadal `0a81480`.
Ponowny `rebase origin/main` po bramkach: gałąź aktualna, bez zmiany kodu.

## Zakres

Inwentarz6 nie jest jeszcze na bazie. Odczytano jego zachowaną wersję z
`2eb65d475cb6671dbe6388c53bb63eec74ea92a8`, blob
`d1d8bb10580ede6f7a60e6a8bc550c2a5b07110c`:
[thread-6.md](https://github.com/klb-t/chatadhd/blob/2eb65d475cb6671dbe6388c53bb63eec74ea92a8/docs/reports/data-in-code/thread-6.md).
R40 jest na bazie; R42 odczytano z gałęzi Claude
`3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`. Nie edytowano dokumentu wymagań.

| Grupa | Przed | Po |
|---|---|---|
| DIC-0462 | Wagi aliasów, kotwic, zasad, terminów użytkownika i ścieżek repo powtarzane w `profile.cpp` | Jedna mapa `policy/relevance.json#/term_class_weights` z efektywnego Pack, bez ręcznego fallbacku |
| DIC-0464 | Nieużywane `AliasTerm::weight`, z dodatkowym domyślnym polem i fallbackiem w dekoderze | Pole i jego fallback usunięte; dopasowanie korzysta z dotychczasowych danych aliasów |

Wagi już istniały w packu: `alias=3`, `principle=1.5`, `path=2`.
Nie dodano drugiego presetu, nowego pliku polityki, loadera ani magazynu ustawień.
Checked API propaguje brak wymaganej klasy i nieskończone liczby przed
`ensure_schema` oraz zapisem profilu/skanera. Usunięto także zbędne wczesne
DDL w `catalog::run_stage`; błąd walidacji polityki skanera nie tworzy tabel
katalogu w pipeline. Nie deklarujemy atomowości całego pipeline: brak `path`
przy repo może zostać wykryty dopiero przez budowanie profilu po poprawnym
skanie. Rekord przebiegu i błąd zadania nadal zapisuje istniejący silnik zadań. `path` wymagane tylko przy
rzeczywiście emitowanym terminie repo. Zero, wartości ujemne i dowolne skończone
liczby są dopuszczone; nie dodano progów ani ograniczeń zakresu.
Stare prywatne adaptery zachowano dla istniejących testów, z jawnym wyjątkiem
przy błędnej polityce. Produkcja korzysta z `Result`.

Nowe napisy kodu to klucze istniejącego kontraktu packa, escapowanie RFC6901
i identyfikatory diagnostyczne z parametrem pointer (R42). Pozostałe grupy
inwentarza nie są deklarowane jako ukończone.

## Weryfikacja

Status: **gotowy do niezależnego odbioru przez wątek9**. Pełny build, CTest,
niezmieniony guard rzeczywiście wykonanych przypadków i web: PASS.
Commity kodu/testów `701bcd3` oraz narzędzi replay `5b3ac6d`; każdy od razu
wypchnięty na nową gałąź z `[skip ci]`, bez uruchamiania płatnego Actions.

| Pomiar | Przed | Po |
|---|---|---|
| Powielone wartości wag w wybranych miejscach | 8 wystąpień wartości liczbowych | 0; trzy istniejące wartości w packu |
| Domyślny profil bez opcji / z repo | 197 / 200 terminów | 197 / 200; pełny JSON, id, hash, projects identyczne |
| Nakładka alias/principle/path | Nadal 3 / 1.5 / 2 w materializowanym profilu | −4.25 / 0 / 6.75 we wszystkich odpowiednich terminach |
| DEV | 31 TP / 45, 0 FP, 14 FN; 3/3 auxiliary wybrane | Identycznie; całe 68 rekordów, profile, cechy, sketches, native unit/preview/decision, raport score i deterministyczna część przygotowania |
| Natywne regresje owned | Brak tego zestawu | 12/12 przypadków, 579/579 asercji |
| Replay | Niezmieniona baza | 40/40 sprawdzeń; oba odrębne ELF i prywatne nagłówki przypięte hashem |
| Web | Bieżący kod bez zmian UI | TypeScript + Vite PASS; 85 modułów |

Pełny CTest po zmianie: **117/117 wpisów**, 276.48 s. Wewnątrz faktycznie
wykonano **710 przypadków C++ / 26399 asercji** oraz **1322 przypadki Python,
0 skipów**. Istniejący opt-in `unit.test_catalog_scale` ma jawne 0/0 i nie
stanowi pokrycia skali. Owned 12/579 powyżej wykonano osobno, nie dodawano ich
do tych liczb. Bazowy pełny CTest nie był wykonywany w tej sesji; porównanie
przed/po dotyczy natywnych probe i kompletnego DEV.

Debug bez `NDEBUG`, GCC13, Werror, dołączony SQLite, shared i serwer.
`-g0` ogranicza tylko symbole debug; `-j1` build i `-j2` CTest ograniczają
współbieżność na współdzielonej maszynie. Nie zmieniono timeoutów, progów,
listy testów ani presetów. Brak zewnętrznego `PYTHONPATH`/`TMPDIR`; CTest
ustawia własne, repozytoryjne środowisko. 445 źródeł i 259 artefaktów
natywnych, cache i compile_commands identyczne przed/po CTest. Całe stdout
117 wpisów JUnit zgodne z `LastTest.log`, bez obcięcia ani odtwarzania XML.

Dowód: [evidence.zip](../../loom/src/catalog/tests/profile_policy/results/2026-10-05/evidence.zip),
manifest plików i polecenia w archiwum. [Instrukcja replay](../../loom/src/catalog/tests/profile_policy/README.md).
Archiwum: 60 surowych plików + manifest, 530391 B;
SHA256 `44c0478ebdeb84f5ab9ea5b1b049a995c228cb493404e0d122d4b14998f20db7`.
Guard skopiowany bez zmian z przyjętego dowodu integratora; jego SHA256
`39e7748da96c3fb5a58e788b5a751bf8c80501b86ed17204e3f62057e2351c3a`.

Owned narzędzia i niezależne testy:
`loom/src/catalog/tests/profile_policy/`. Probe kompilowany osobno przed i po
zmianie prywatnego layoutu; porównywane całe profile wraz z id/hash, natywny
scan/mentions, realna nakładka wag i komplet 68 rekordów DEV. Runner DEV
wywodzi się z zachowanego narzędzia poprzedniego przyrostu; jego pochodzenie
jest w `dev_runner_source.json`. Nie przeniesiono wariantów strojenia ani
wejść semantycznych z poprzedniej gałęzi.

## Czego nie zrobiono

Nie stroimy jakości i nie zmieniamy domyślnej selekcji. Nie czytano ponownie
ślepego korpusu ani `eval/real-holdout-key`. Zero płatnych/provider calls.
Nakładka Pack działa już teraz, ale nie jest pełnym wykonaniem R40:
trwałe wykluczenia i objaśnienia warstw mają przyjść ze wspólnego mechanizmu,
bez drugiego lokalnego store. Hash packa/profilu nie jest grafową krawędzią
„wytworzony przez”. Wspólny kontrakt3/4 jest dostępny; powiązanie wyników
katalogu z natywnymi rekordami grafu pozostaje otwarte.

Istniejący cache etapów w `KnowledgeEngine` jest poza zakresem6. Dla dawnych
przebiegów z niestandardowymi wagami, które poprzedni kod ignorował, użyć
publicznego `force:true` albo bezpośredniego `build_profile`, aby odbudować
pochodny profil. Zmiana packa sama zmienia klucz; nie zmieniono globalnej
wersji pipeline ani historii istniejących wyników.

DIC-0465 (języki fleksji) wymaga również uogólnienia dispatchu normalizatora
poza zakresem6. Nie edytowano KB, publicznych nagłówków, CMake, centralnych
testów, UI, STATE ani README. Nie regenerowano packa: dane bez zmian.

## Do wątku N

- **9:** odebrać drugi przyrost niezależnie od pierwszej gałęzi; pierwszy
  mechanizm i jego odrzucone warianty pozostają na dotychczasowej gałęzi.
  Aktualizować INDEX według rzeczywistego odbioru i zachować cały INTERFEJS.
  Ten raport przenosi otwarte zadanie6 z INDEX: dalsze dane archive/alias/sketch/
  score i nowe badania tylko DEV. Nie deklaruje zamknięcia wszystkich 31 grup
  ani poprawy14 pomijanych jednostek.
- **3/4/12:** dostarczyć katalogowi efektywny Pack/profil dla konkretnego
  użytkownika oraz wyjaśnienia warstw i trwałe wykluczenia. Dla R41 uzgodnić
  mapowanie `CatalogUnit`/wyników do rzeczywistych rekordów i przebiegów
  `loom.method_graph/1` / `loom.method_run_trace/1`; sam hash nie wystarczy.
- **4/11:** kolejne pola polityki wymagające walidacji KB i regeneracji packa
  pozostają poza zakresem6; nie kopiować ukrytych fallbacków jako domyślnych
  danych bez sprawdzenia efektywnego packa. Dotyczy też dispatchu języków0465. Powiązać cache etapów z wersją
  faktycznego producenta/metody (R41), również przy poprawkach kodu bez
  zmiany packa; obecny cache może oddać stary profil z ignorowanymi wagami.
  W publicznym `ProfileConfig::extra_terms` komentarz „weight 3” wymaga
  doprecyzowania: to preset z packa, a nie stała wagi.
- **8/9:** rejestracja nowych owned testów w centralnym CTest wymaga pliku
  poza zakresem6. Do tego czasu wykonywać także owned runner, obok pełnego
  CTest; nie przedstawiać samego CTest jako wykonania tych nowych przypadków.
