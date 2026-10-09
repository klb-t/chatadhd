# Kolejność pracy po audycie

To rekomendacja audytora, bez zmian produktu i bez przypisywania właścicielowi niewypowiedzianych decyzji. Dokładne testy, migracje, zależności oraz statusy wszystkich 52 otwartych lub niepewnych ustaleń są w `backlog.json`. 5 już naprawionych i 8 mechanizmów pozostaje w findings jako dowód, nie nowe zadania.

| Kolejność | Grupa | Ustalenia | Bramka akceptacyjna |
|---|---|---|---|
| 1 | Granice wykonania, zgody i utraty danych | CH-004/005/006/011; LEM-003/005; WD-011; CKP-A-012 (najpierw test hipotezy) | Zablokowany send nie wykonuje transportu; retry nie udaje sukcesu; zmiana fokusu nie edytuje innego pola; wynik i migracja zachowują źródła. |
| 2 | Dane naprawdę zmieniają działanie | CH-003; WD-001/002/003; CKP-A-003; EA-AGEDS-003; LEM-001 | Dwa presety dają różne, przewidywane parametry w rzeczywistym konsumencie, poprawne provenance i jawny błąd nieobsługiwanego pola. |
| 3 | Wersjonowane receptury i wybór alternatyw | CH-001/002/007/010; WD-005/009/010; CKP-A-001/002/005/007; EA-AGEDS-001/002/004/005/009; LEM-002/004 | Profil legacy pozostaje reprezentowalny, alternatywa działa bez rekompilacji; snapshot nie zmienia znaczenia dawnych wyników. |
| 4 | Graf i wszystkie poziomy UI | CH-008/009/015; WD-004/006; CKP-A-004/006/011; EA-AGEDS-006/007/008; LEM-006/007 | Roundtrip Basic→Advanced/Expert→restart→runtime zachowuje effective values, exclusion i wersję; test checkpoint opt-in obejmuje zakres, częstotliwość, wolumen, budżet. |
| 5 | Zachowany standalone baseline | LOOM-001…006 | Najpierw porównać z aktywnym następcą na jego SHA; nie wdrażać historycznej zmiany automatycznie do chatadhd. |

Zależności: najpierw fixture realnego konsumenta i test granicy, potem istniejący model danych/profil oraz jawna migracja, następnie konsumpcja i roundtrip UI/graf. Nie dodawać JSON tylko po to, by odhaczyć pole. Priorytet hipotezy określa pilność jej sprawdzenia, nie potwierdza naruszenia.
