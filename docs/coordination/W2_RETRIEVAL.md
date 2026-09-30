# W2 — Wyszukiwanie, zakres i szczegółowość

Start: `README.md`, aktualny `../STATE.md`, R12/R25/R27/R28. Sprawdź, czy nowy
ContextRequest jest już wpięty w czat. Nie myl budżetu tokenów z zakresem.

Własność: `loom/src/context/`, dedykowane testy kontekstu, własne receipts.
`context_engine.h` oraz konieczne zmiany katalogu/danych rezerwuj u ROOT.
Nie edytuj `src/chat`, modelowego ledgeru ani interfejsu web.

Cel: niezależne regulowanie zasięgu wyszukania materiału i reprezentacji każdej
wybranej informacji w faktycznym ContextSet konsumowanym przez czat.

1. Ustal obecną reprezentację celu, targetów, role map i resolutions.
2. Dodaj minimalne rozszerzenie pozwalające przy tym samym zakresie zmieniać
   szczegółowość oraz przy tej samej szczegółowości rozszerzać zakres.
3. Zapisuj dlaczego źródło weszło/odpadło, brakujące przesłanki i wpływ budżetu.
4. W osobnym przyroście podłącz istniejący kanał semantyczny, wektorowy lub
   ocenę modelu przez jawny interfejs; brak zdolności nie może udawać oceny zero.
5. Porównaj kanały na jawnych DEV: coverage, ranking i precyzja osobno;
   lexical shadow wskazuje pominięcia, nie blokuje innych metod.

Kryteria: działający konsument native; zależności zachowane/oznaczone incomplete;
rozłączne działanie obu osi; istniejące testy i progi bez osłabienia.
Nie odpieczętowuj walidacji katalogu/grafu. Nie stroimy po wyniku holdoutu.
