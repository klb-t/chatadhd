# Wątek 1 — przyrost 2, 2026-10-05

Status: przygotowany kod i niezależny przegląd statyczny; pełny build, natywne replaye i pomiary są w toku. Ten wpis nie zgłasza jeszcze gotowości do przyjęcia.

Gałąź: `gpt/knowledge-precision-2-2026-10-05`. Baza: aktualny `main` `e4109df7e4af22b461def5f7d62e268d9b9a8825`; poprzednia gałąź `gpt/knowledge-precision-2026-10-04` (`50e6bb9`) pozostaje niezmieniona w kolejce. Przeczytano aktualny INDEX i Do1. Inwentarz w bazie nie jest jeszcze lokalnym plikiem: użyto `docs/reports/data-in-code/thread-1.md` ze zdalnego przyrostu11. R42 pochodzi z dokumentacyjnego przyrostu Claude `1c990a9` / tip `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`, nie z bazowego main.

## Wybrany zakres i dane

| Grupa | Przeniesiono | Domyślny preset |
|---|---|---|
| DIC-0301 | Siedem klas PL/EN ze starego `generalize/common.cpp` do `lexicons/cues.json`; usunięty ręczny fallback | Dokładnie 120 uporządkowanych par fraza/waga: 42/18/21/8/10/7/14 |
| DIC-0322 | Mnożnik Jaccarda w dopasowaniu seed | `principles.seed_jaccard_multiplier = 2.0` |
| DIC-0323 | Współczynnik pozostałej niepewności kolejnych jednostek | `principles.repeat_confidence_residual_factor = 0.7` |
| DIC-0324 | Współczynniki noisy-OR dla odkrytych zasad | `discovered_confidence_cap = 0.8`, `discovered_confidence_base = 0.25`, `discovered_confidence_score_factor = 0.1` |

Kod zachowuje kolejność działań i rozstrzygania remisów. Nowe pięć liczb czyta wymagany, skończony parametr; brak/błędny typ/NaN/Inf daje jawny błąd, bez drugiego presetu w C++. Nie dodano zakresowych sufitów dla tych ustawień. Wygenerowane `pack_embedded.inc` pochodzi wyłącznie z danych. Niezależny przegląd statyczny potwierdził 7/7 klas, 120/120 par, pięć dokładnych wartości i brak innych zmian w istniejących danych. Wynik natywny wymaga osobnego pomiaru.

## Nakładka użytkownika i R40/R42

Istniejący `Pack::load_with_overlay` zastępuje cały dokument znanej ścieżki. Użytkownik kopiuje bieżący `loom/data/lexicons/cues.json` lub `loom/data/policy/thresholds.json` do odpowiedniej ścieżki w swoim katalogu pack overlay i zmienia dane. Po zmianie należy otworzyć pack ponownie; jego efektywny hash obejmuje nadpisane dokumenty.

Usunięcie pojedynczej klasy w replacement `cues.json` ma wyłączyć ten matcher bez odtworzenia w kodzie. Zamieniona klasa nie jest sumą z dawnym słownikiem. Puste `phrases: []` nadal odrzuca dotychczasowy, cudzy validator; nie zmieniono go. To bieżące wyłączenie słownika, nie jeszcze trwały grafowy marker wykluczenia po aktualizacjach packa.

Stara własna nakładka `policy/thresholds.json` musi dostać pięć nowych pól z bieżącego presetu albo zostać świadomie zastąpiona bieżącym dokumentem. Brak liczby oznacza niekompletną receptę. Nie wskrzeszamy usuniętego parametru przez ręczny fallback. Literalne nazwy pliku/kluczy pozostają kontraktem kod↔dane według R42; istniejące semantyczne wiązania klas i inne polityki spoza tych grup pozostają długiem, a nie zbiorczą kategorią „standard”.

## Weryfikacja — jeszcze w toku

Przygotowano source-pinned replays w `loom/src/generalize/tests/`:

```sh
python3 loom/src/generalize/tests/dic0301_replay.py --core-build <frozen-build> --output <new-directory>
python3 loom/src/generalize/tests/principle_parameters_replay.py --help
```

Fixture C++ i runner Python przeszły kontrolę składni. Replaye porównują rzeczywiste `discover_principles`/`type_principle` i Pack przed/po przeciw temu samemu niezmiennemu zestawowi archiwów. Osobne scenariusze obejmują faktyczny disk overlay/reload/delete/replace/bad-JSON oraz zmianę każdego z pięciu parametrów i brak/błędny typ/NaN/Inf. Przygotowanie tych kontroli nie jest ich wykonaniem. Pełny CTest bez PYTHONPATH/TMPDIR oraz `knowledge_eval.py synthetic` i `selfhost` pozostają wymagane przed gotowością.

Nie wykonano płatnych ani zewnętrznych wywołań modeli; nie czytano ślepego korpusu ani `eval/real-holdout-key`. Ten przyrost nie zgłasza nowego zysku precyzji: jego celem jest identyczny domyślny wynik i rzeczywista konfigurowalność.

## Czego nie włączono

Nie zmieniono poprzedniego przyrostu precyzji/promptów, legacy `semantic_llm.cpp`, rejestru3, magazynu4, CAPI, UI ani STATE/README. Nie przeniesiono całych 127 grup. Nie dublowano wspólnego formatu metod3/4. Dawny automatyczny zapis pełnego grafu przy każdym kandydacie ma pełne źródła/dowody wyłącznie na `archive/2026-10-04/knowledge-precision-inline-method-graph-negative` (tip `42a8ffa9466c6103b88daae1c89b69c91a5b188f`): zmierzony payload 2117→350630B, 165.63×, bez potwierdzenia właściciela. Nie przyjęto go w tym przyroście.

## Do wątku 1

Otwarte Do1 z INDEX: wersje metod/promptów w grafie, dalsze recipes/analysis callers i presety7. Dopiero po przyjęciu poprzedniego prompt registry podłączyć go do obecnego wspólnego MethodRegistry/GraphPacket, bez dublowania formatu i bez automatycznego zapisu 166×. Następne koherentne grupy skalarne: DIC0289–0291 resolver. DIC0286 wymaga uzgodnienia API claims-only; DIC0287 potrzebuje listy/bindings w danych, nie udawanego numeric preset. Standardowa gramatyka ISO-date z DIC0227 może zostać w kodzie według R42.

## Do wątku 3

Metoda korzystająca z tych słowników/parametrów powinna mieć wersję obejmującą hash efektywnych danych, rzeczywisty adapter i krawędzie wyników do wersji metody. Sama migracja danych nie tworzy jeszcze takich krawędzi. Niewykonywalne recipe descriptors potrzebują uzgodnionej reprezentacji niedostępności; obecne `execution_capability: null` powoduje błąd resolve typu string, nie poprawny unavailable leaf. Do1/3 pozostaje batch/legacy analysis zgodnie z INDEX.

## Do wątku 4

Potrzebny wspólny adapter efektywnych dokumentów `kb::Pack` z rozstrzygnięć DefaultLayers12: builtin dokumenty → podmiany/usunięcia wskazane wiązaniami w danych → `Pack::from_documents` → walidacja/hash. Nie tworzyć drugiej lokalnej listy tombstone. Brak wymaganej liczby ma być niekompletną receptą, nie przywróceniem wykluczonego domyślnego parametru. Nie zmieniono teraz niepustości `phrases` ani magazynu.

## Do wątku 7

Przekazać najlepsze zmierzone, typowane presety wraz z datowanymi twierdzeniami i dowodami o dokładnych wersjach metod. Historyczny Jev preset oraz przygotowane zapytania nie są nowym wynikiem jakości. W1 nie wykonuje płatnych badań.

## Do wątku 9

Odbierać przyrost2 osobno po pierwszym W1 i świeżym rebase/gates. Zachować własne wpisy STATE/INDEX i zmiany INTERFEJS. Zakres obejmuje cztery grupy, nie ukończenie R40/R41/R42. Fixture replays pozostają w naszym katalogu bez zmiany centralnego CMake/progów. Nagłówek `loom/include/loom/generalize.h:7–10` nadal opisuje usunięty fallback; poprosić właściciela nagłówka o korektę tego komentarza. Przenieść dokument R42 ze ścieżki Claude w swoim zakresie.

## Do wątku 10

Edytować efektywne klasy i pięć parametrów jako dane, pokazując hash, pochodzenie warstwy i jawne błędy braków. Istniejąca nakładka jest whole-file; nie przedstawiać jej jako gotowego scalania pojedynczego parametru ani trwałej grafowej blokady.

## Do wątku 11

W strażniku R42 rozróżnić przeniesione dane od dopuszczalnych kluczy/diagnostyki/generated pack. Pozostałe literalne wiązania semantycznych klas i inne progi mają nadal właściciela1, nie uzasadnienie zbiorcze. Granica legacy `semantic_llm.cpp` pozostaje według INDEX po stronie11; korzystać z pierwszego prompt registry po odbiorze.

## Do wątku 12

`DefaultLayers` z `3c0bc36` ma `effective`+value oraz `disabled|excluded|proposal|missing` bez value. `exclude` przechowuje persistent marker, `update_pack` nie omija go przez nowy key/id. Potrzebne data bindings do klas `/classes/<escaped-name>`: effective zastępuje klasę, stany wyłączające usuwają ją przed matcherem. Dla wymaganych parametrów wykluczenie wyłącza metodę albo pokazuje niekompletną receptę; nie wkleja domyślnej liczby. Obecne `runtime_profile_values` adaptuje deskryptor RuntimeProfile11, nie legacy `kb::Pack`. Most ten jest wspólnym zadaniem4/12 i nie jest deklarowany jako gotowy w tym przyroście.
