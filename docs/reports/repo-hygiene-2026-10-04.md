# Wątek 8 — porządki repo, 2026-10-04

Gałąź `gpt/repo-hygiene-2026-10-04`, po końcowym rebase na `main=ba6eaf6`.
[PR #11](https://github.com/klb-t/chatadhd/pull/11) pozostaje roboczy do naprawy
Clanga przez W10 i świeżej macierzy CI. W8 nie przesuwa `main`.

Odczytano indeks `7282437`, zadanie DIC-0692 i jawne wyjątki dostępności testów.
Końcowy fetch przyniósł R39–R41: zmienił tylko OWNER_REQUIREMENTS, bez kodu,
CMake, testów lub web. Poprzednie tipy zachowano pod
`archive/repo-hygiene-before-rebase-2026-10-04` (`2544cf3`) i
`archive/repo-hygiene-before-requirements-rebase-2026-10-04` (`d75eaa0`).

## Zrobione i liczby przed / po

| Obszar | Przed | Po |
|---|---|---|
| Aktywne workflow | 5, w tym 3 piloty OpenRouter | 2; stare definicje zachowane bajt w bajt z hashami |
| `context_engine` w dowodzie z 2026-10-03 | 0 przypadków / 0 asercji | świeży pełny CTest: 18 / 1364 |
| `knowledge` w tym dowodzie | 0 / 0 | 18 / 150 |
| `resolve_lineage` | 2 / 0 | 2 / 15; checkout zachowuje historię |
| Strażnik dowodu | kontrola statusu zewnętrznego nie wykrywała pustych zestawów | manifest + kompletne podsumowania + rzeczywiste wykonanie; 28/28 regresji |
| Polityka strażnika | nazwy presetów, klasyfikacja i wyjątki w Pythonie | wersjonowany JSON + schema, walidacja, niezmienna migawka i hash w pokwitowaniu; 44/44 dawnych sprawdzianów bez zmiany wyniku |
| DIC-0692 | 3 wymiary przewidywania + 1 wsparcia i projekcje w Pythonie | profil z nakładkami; dowolne wymiary, projekcje i wiązania przesłanek |
| Domyślne seeding | 5 grafów / 61 przypadków / 2135 rankingów / 648 słowników metryk | wszystkie dokładnie zgodne z zamrożonym V4; 50/50 testów, w tym 36 dawnych bez zmian |
| `seeding/results` | 26 plików / 13 800 361 B | bez zmian; są używane przez testy, więc nie przeniesiono |
| Nowe testy W10 w CI | pomijane nawet po przyszłej deklaracji | wykonywane, gdy package je deklaruje; na obecnym main jawnie 2 niedostępne / 0 uruchomień |
| Pokwitowanie builda | commit/tree i cztery pliki wykonywalne/biblioteka | także oba narzędzia kompatybilności i wybrane pola CMake z hashem cache |

Nowe przebiegi seeding zachowują wszystkie moduły, fixture, policy, protokół,
profil, uporządkowane nakładki i profil efektywny wraz z hashami. Mały przebieg
syntetyczny odtworzono samym zamrożonym CLI po usunięciu oryginalnych wejść.
Schemat nowych wyników to `candidate-profile-projection-v3`; stare pliki i
niekorzystne wyniki pozostają nienaruszone. Nie deklarujemy wzrostu jakości.

## Weryfikacja bieżącego przyrostu

[Pełny dowód tej sesji](../verification/repo-hygiene-followup-2026-10-04/RESULTS.md):
**108/108 CTest w 102,58 s**, strażnik: **107 wykonanych wpisów**, **659
przypadków natywnych / 24 465 asercji**, **1290 Python / 0 pominięć**.
`catalog_scale` pozostaje jawnie niewykonanym opt-in, nie dowodem przetworzenia
1 GB. `research.seeding` wykonał 50 przypadków. Narzędzia `.github`: **38/38**
(28 strażnika, 6 procesów npm, 4 pokwitowania konfiguracji). Produkcyjny build
web: **85 modułów**, powodzenie w 2,18 s.

Ten nowy build to **GCC 13.3 + vendored SQLite + shared library + WERROR=ON**,
Python 3.12.14; nie jest buildem Clanga. Lokalnie wyłączono tylko symbole
Debug (`-g0`), zachowując asercje i ostrzeżenia. Dla miejsca na współdzielonym
dysku lokalne archiwum obiektów przekształcono w thin archive z tymi samymi
126 obiektami, w tej samej kolejności; po pełnym buildzie zwolniono własne
intermediates. Pokwitowanie pinowało sześć końcowych binariów; ponowny odczyt
potwierdził ich hashe i 1164 hashe wejść. Oryginalny manifest, XML i LastTest
są zachowane. Nieudane próby konfiguracji/puste obiekty są w osobnym archiwum.

Wcześniejszy [CI 37211656572](https://github.com/klb-t/chatadhd/actions/runs/37211656572)
na `6523c1b`: dev i ASan każdy 108/108; JNI 1/1 i wszystkie ówczesne testy
web/przeglądarki zielone. Dev 1276 Python, ASan 1252 + 24 jawne FFI skips.
Te stare receipts zachowują datę/źródło; nie przedstawiamy ich jako nowego CI
po migracji. Replay obu kompletnych par XML/manifest przed i po migracji
strażnika jest dokładnie zgodny. Pierwsze brakujące XML nie zostały odtworzone.

## Niezrobione i zależności

- Macierz Clang/vendored nadal zablokowana przez dwa przechwycenia w serwerze
  W10. W tej sesji nie uruchomiono nowej macierzy: indeks poleca to po poprawce.
  Zachowano właściwy kompilator, dołączony SQLite i `WERROR=ON`.
- Zapis metod/przebiegów seeding do kanonicznego grafu czeka na uzgodniony
  kontrakt W3/W4. Manifest jawnie mówi `pending_agreed_contract`; profil
  eksperymentu nie zastępuje wspólnego grafu metod ani warstw/wykluczeń R40.
- Szkic prezentacyjnego README jest w [raporcie](repo-hygiene-readme-draft-2026-10-04.md);
  publikacja i aktualizacja funkcjonalności należą do integratora.

Zero nowych płatnych wywołań, dostępu do klucza holdout lub ślepego korpusu.
Nie zmieniono STATE, README głównego, docs/README.md, loom/web/README.md,
UI, serwera, CMake ani kontraktów aplikacyjnych. Cały przyrost mieści się w W8.

## Do wątku 10

Na `main=ba6eaf6` usuń zbędne `[this]` z lambd `/api/logs` i `/api/version`:
`loom/server/src/app.cpp:768,772`, najmniejsza poprawka `[]`.
Na tipie W10 `2f25145` to **835 i 839**. Są to rzeczywiście zaobserwowane błędy
Clanga w dwóch zachowanych CI; zmiany main do ba6eaf6 są tylko dokumentacją.
Pełne logi: `docs/archive/repo-hygiene-2026-10-04/ci-first/vendored-job.*`
oraz `ci-corrected-vendored/`.

Oddzielny wniosek ze źródła W10: lambda `/api/usage-policy` na **515** używa
`this` tylko pod `LOOM_SERVER_HAS_USAGE_POLICY`. Sprawdź Clang z nagłówkiem W2
i bez niego; ten trzeci punkt nie jest wykonanym wynikiem CI.
Po naprawie przekaż gotowy commit do W9, żeby W8 mógł sprawdzić pełną macierz.

## Do wątku 11

DIC-0692 wdrożony z dokładną zgodnością domyślnych wyników; szczegóły i hash
macierzy w [raporcie seeding](repo-hygiene-seeding-profiles-2026-10-04.md).
[Inwentarz i migracja strażnika](repo-hygiene-evidence-policy-inventory-2026-10-04.md)
rozdzielają formaty dowodów od presetów. Wyjątki ASan/opt-in są jawnie w danych,
z identycznym odliczaniem i wymaganymi powodami. Parsery doctest/unittest są
formatami dowodu, nie metodami analizy semantycznej.

## Do wątków 3 i 4

Po wspólnym golden fixture przekaż W8 uzgodniony kontrakt metod/przebiegów.
Przygotowane wejścia adaptera: wersja/hash profilu efektywnego, hash policy,
hashe modułów, wybrana metoda/parametry, czas przebiegu i identyfikatory wyników.
Datowane oceny mają stać się twierdzeniami o metodach z dowodem, a wyniki
krawędziami do konkretnej wersji. Nie wdrożono drugiego formatu grafowego.

## Do wątku 9

- Odbierz nowy przyrost DIC-0692 i polityki dowodów po poprawce W10, aktualnym
  rebase i wymaganych bramkach. Uaktualnij INDEX/STATE: 50 seeding, 28 guard,
  38 narzędzi, świeży CTest108/108 i 1290 Python; pełny Clang CI nadal pending.
- Wstaw szkic README po odbiorze, z numerami i zdolnościami przyjętego źródła.
  R39–R41 pozostają wymaganiami; nowe profile eksperymentu nie są ukończonym
  onboardingiem lub grafowym rejestrem metod.
- Koordynuj wspólny kontrakt W3/W4 oraz późniejszy adapter W8. Przed scaleniem
  sprawdź metody jako byty i pochodzenie wyników jako krawędzie.
- W2 opisuje 19 grup policy poza CTest, W4 nowe wpisy: wymagają osobnych
  pokwitowań lub uzgodnionej rejestracji. W8 nie edytuje CMake/eksportu ABI;
  klasyfikację nowych runnerów można teraz zadeklarować w JSON polityki.
