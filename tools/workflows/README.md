# Skrypty workflow (wejścia pakietów pracy)

Kopie skryptów, którymi S1 uruchamia agentów przez narzędzie `Workflow`. Brief w skrypcie
to wejście pakietu pracy (zasada 2 z 2026-10-21: jasne wejście, rezultat, kryteria odbioru);
bez tych plików brief istnieje tylko w kontenerze sesji i ginie razem z nim.

- `m1-w0-packages.js` — M1-W0 jako 7 małych pakietów z kartą wznowienia między nimi
  (wzorzec „pętla pakietów”: jeden agent = jeden spójny kawałek, ~50 wywołań narzędzi,
  commit + karta; następny agent czyta karty i `git log`, nie cudzy kontekst).
- `fix-login-drop.js`, `p39-interaction-design.js` — poprzednie skrypty, bez zmian.

Uruchomienie: `Workflow({scriptPath: "<ścieżka do pliku>", args: {...}})`. Skrypty są zwykłym JS
(nie TS), kończą się `return`, więc nie przechodzą `node --check` bez opakowania w funkcję.
`m1-w0-packages.js` było sprawdzone składniowo i uruchomione raz; wzorzec pakietów jest tam
jeszcze niepotwierdzony na pełnym przebiegu.
