# C → B: projekcja wyników kontynuacji 02

`delivery-example-v3.json` jest gotową publiczną projekcją Basic/Expert.
`delivery-contract-v3.json` wersjonuje wcześniejszy lokalny kontrakt
`continuation_01/handoff-final-contract-v2.json`, który dopuszczał wyłącznie
niezmierzone wyniki. To snapshot badawczy, nie nowy wspólny schemat ani silnik
profili. Pola dotychczasowych metryk jakości odpowiedzi modeli pozostają `null`;
nowy blok `expert.offline` zawiera odrębne pomiary retrieval i zachowania źródeł.

Basic podaje 48 zadań, 12 rodzin, 2079 wiadomości, okres 23.01–09.09.2026,
eksploracyjny status, koszt API 0 USD i brak pomiaru jakości odpowiedzi modeli.
Expert zachowuje liczebność rodzin oraz mianowniki poszczególnych metryk,
24 podsumowania metoda × budżet × część korpusu, 9 podsumowań reprezentacji,
258 adnotacji kandydackich i ograniczenia procedury odzyskiwania. Wszystkie
rodziny były dostępne badaczowi; 4 rodziny prowizorycznej walidacji nie są
niezależnym holdoutem. Ocena mechaniczna stosuje prowizoryczne adnotacje badacza,
które nie są automatycznie prawdą referencyjną.

Aktualna tabela retrieval ma 576 ocen budżetowych. Wykonano 288 pierwotnych
rankingów i 144 rankingi po jawnej korekcie adaptera relacji; niezmienione ramiona
odtworzono z dowodów. Tych dwóch wersji nie należy sumować jako nowych
niezależnych zadań. Metryki pamięci oznaczają alokacje Python, nie RSS. Budżety
są bajtowe i rekordowe; tokenizer oraz liczba tokenów pozostają `null`.

Preferencje są literalnym, historycznym dowodem o jawnym zakresie. Nie są
aktywnym ani globalnym profilem. Kompozycja preparacji v3 ma 972 body pierwszej
fazy, z czego 648 wariantów jest kompletnych, a 324 wymagają odpowiedzi pierwszej
fazy przed przygotowaniem ekstrakcji. Poprzednie małe kolejki nadal wiążą
poprzednią wersję preparacji. Nie można podmienić body w zamrożonej kolejce;
wznowienie z nową preparacją wymaga nowego zamrożonego spec/kolejki/manifestu
payera i aktualnego preflight. Dane kandydatów są wskazane przez hash w
`expert.offline.candidate_collections`; żadna propozycja nie zmienia ustawień.

`delivery-artifact-v3.json.gz` powstał przez istniejący
`experiment_analysis_v1.build_handoff_artifact` i exporter `method_graph`.
Przeszedł schematy `loom.method_graph/1` i `loom.method_run_trace/1`, kodek Python
oraz odzyskanie dokładnych danych wynikowych. Trace opisuje eksport publicznej
projekcji, nie wykonanie inference ani całego benchmarku w runtime.

**Granica native pozostaje jawna:** A3-DISC-001 dotyczy rozbieżności akceptacji
równoważnego DTO przy zmianach kluczy/kolejności/liczb. Akceptacja przez
GraphPacketStore nie oznacza akceptacji przez MethodRegistry ani możliwości
wykonania. Ten pakiet nie wywołuje native/runtime/UI i nie ogłasza zamknięcia
luki A/B. Lokalny kontrakt wyników oraz transportowy graf są sprawdzone tylko
w opisanym zakresie.

Koszt nowych wywołań modeli wynosi 0 USD; nowych wywołań jest 0. Historia
kampanii to 720 prób, 0,873216500 USD i archiwalne 4,126783500 USD z limitu 5 USD.
Aktualne usage, rezerwacje i dostępny budżet pozostają `null`. Kontrolowany test
odzyskiwania nie jest odzyskaniem rzeczywistej próby właściciela. Nie daje też
kryptograficznego potwierdzenia zewnętrznej atrybucji request/response.

Pochodzenie zachowuje pierwszy odzyskany checkpoint pod rolą
`historical_initial_checkpoint`, a bezpośredni poprzedni checkpoint kontynuacji 01
pod `immediate_previous_checkpoint`. Hash `START.json` osobno wiąże bazę C oraz
SHA zależności A/B/main. Nie zmieniono pochodzenia źródłowych freeze.

## Odtwarzanie i weryfikacja

```sh
PYTHONPATH=/tmp/thread7-python-deps:. TMPDIR=/var/tmp python3 docs/research/thread7_real_2026-10-09/continuation_02/delivery-test.py
PYTHONPATH=/tmp/thread7-python-deps:. TMPDIR=/var/tmp python3 docs/research/thread7_real_2026-10-09/continuation_02/delivery-build.py --output /var/tmp/thread7-delivery-new-output
```

Builder sprawdza hashe wejściowych publicznych dowodów i zapisuje nowe pliki
wyłącznie przez exclusive create. Nie nadpisuje historycznych artefaktów.
Ścieżka zależności Python jest lokalna; nie ma znaczenia dla tożsamości danych.
7/7 testów sprawdza zgodność kontraktu, dokładny roundtrip/rebuild, odrzucenie
zmienionego wejścia, pozornej jakości/native, zerowania unknown oraz
automatycznej adopcji. To testy mechaniki, nie nowe badania jakości modeli.
