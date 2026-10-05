# Negatywny wynik pełnego Runtime — izolacja fixture

Zachowano pełny `current-focused-final.log` czterech wpisów CTest oraz dokładny `test_runtime_recipe_consumers.original.cpp` sprzed poprawki. Runtime consumer wykonał 10 przypadków: 9 przeszło, 1 nie przeszło; 110 asercji: 107 przeszło, 3 nie przeszły. Pozostałe trzy wpisy GitHub/model creation/selector przeszły. Licznik 765 pominiętych oznacza pozostałe przypadki ogólnego runnera wykluczone filtrem tego wpisu CTest; nie przedstawia ich wykonania.

`prepare()` przekazywało niezmienny pack hash i `{}` do deterministycznego `begin_run`, więc cztery scenariusze korzystały z tego samego run. Produkty baseline pozostały w magazynie przy kolejnych podzbiorach. Produkcja KnowledgeEngine uwzględnia zmieniony effective-profile fingerprint w tożsamości run. Poprawka dotyczy wyłącznie fixture: cztery nazwane wejścia semantyczne i sześć dodatkowych asercji różnych ID. Wszystkie trzy pierwotne asercje pozostają bez zmian.

Odtworzenie oryginalnego błędu przy świeżej bieżącej bibliotece, bez podmieniania źródeł repo:

```sh
python3 docs/reports/data-in-code/evidence/semantics-runtime-fixture-negative/replay.py \
  --source-root /path/chatadhd --build /path/current-build \
  --output /tmp/semantics-negative-replay
```

Replay kompiluje tylko oryginalny test i runner, linkując z gotowymi pełnymi bieżącymi bibliotekami. Zapisuje polecenia, hashe, pełne logi i zachowuje niezerowy kod negatywnego wykonania. Izolowany runner ma tylko przypadki tego źródła, więc nie odtwarza licznika 765 pozostałych przypadków ogólnego CTest. Dowód ten nie zastępuje końcowego CTest poprawionej gałęzi. Poprawiony zestaw i pełną bramkę uruchamia rodzic.
