# W2 — Wyszukiwanie, zakres i szczegółowość

Start: `README.md`, aktualny `../STATE.md`, R12/R25/R27/R28. Sprawdź, czy nowy
ContextRequest jest już wpięty w czat. Nie myl budżetu tokenów z zakresem.

Własność: `loom/src/context/`, dedykowane testy kontekstu, własne receipts.
`context_engine.h` oraz konieczne zmiany katalogu/danych rezerwuj u ROOT.
Nie edytuj `src/chat`, modelowego ledgeru ani interfejsu web.

Już zaimplementowane w bieżącym przyroście: `ContextRequest.relation_hops`
(domyślnie 1, również 0) oraz `detail_resolution`
(`null`/`label`/`summary`/`full`/`raw`). Działają niezależnie od budżetu;
rozszerzanie grafu nie wyłącza domknięcia wymaganych przesłanek. Kod ma
dedykowany `test_context_controls.cpp`; stan wykonania testów sprawdź w
bieżącym receipt i STATE, nie wywnioskuj go z obecności pliku. Kontrakt:
`../research/CONTEXT_SCOPE_DETAIL_2026-09-30.md`. Nie implementuj tych pól ponownie.

Następny cel: wybór materiału według planu i jego poszczególnych tez,
uzupełniające kanały wyszukiwania i pomiar rzeczywistej jakości selekcji.

1. Sprawdź aktualny konsument czatu, controls oraz trace i potwierdź ich testy.
2. Rozszerz istniejący model zapytania o plan/tezy z osobnymi zakresami i
   szczegółowością, wymaganym kontrdowodem i jawnymi brakami. Uzgodnij kontrakt
   z W1; nie twórz drugiego ActiveTaskSpec ani nowej równoległej ontologii.
3. Zapisuj dlaczego źródło weszło/odpadło, brakujące przesłanki i wpływ budżetu.
   Rozważ stronicowanie zapytań sąsiedztwa oraz jawny ślad obcięcia/błędu;
   obecny limit 10 000 wyników na endpoint nie dowodzi pełnego pokrycia grafu.
4. W osobnym przyroście podłącz istniejący kanał semantyczny, wektorowy lub
   ocenę modelu przez jawny interfejs; brak zdolności nie może udawać oceny zero.
5. Porównaj kanały i ich połączenia na jawnych DEV, a po uzgodnieniu źródeł
   również na rzeczywistych danych: pokrycie, ranking i precyzja osobno.
   Lexical shadow wskazuje pominięcia, nie blokuje innych metod. Utrzymuj
   osobne miary odzyskania dowodu, jego trafności i poprawności odpowiedzi.

Kryteria: działający konsument native; zależności zachowane/oznaczone incomplete;
rozłączne działanie obu osi, również per teza; istniejące testy i progi bez
osłabienia. Sukces deterministycznych testów controls nie jest pomiarem jakości
semantycznego wyszukiwania ani dowodem pełnej realizacji R28.
Nie odpieczętowuj walidacji katalogu/grafu. Nie stroimy po wyniku holdoutu.
