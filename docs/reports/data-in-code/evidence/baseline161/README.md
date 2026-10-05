# Dokładna baza 161cc22 — offline probes

Wszystkie trzy probe skompilowano z nagłówkami immutable checkout `161cc22dfb84fe863389d6b90323bd44516a68dc` i ukończonymi bibliotekami **tego samego** baseline-build. Użyto jednego źródła old-API na wariant; kopie źródeł zapisano obok. Wariant semantics jest pełny: Runtime oraz **cztery produkty materializacji**, bez `LOOM_PARITY_CORE_ONLY`.

| Probe | Bajty JSON | Zakres |
|---|---:|---|
| memory_selector | 59955 | 3 metody, 8 pytań × 4 top_k i fallback, korpus > historycznego limitu cech, pamięć/graf/context/search |
| semantics_graph_materialize | 11766 | 13 tekstów, 5 progów, reguły/graf/kontekst i 4 produkty |
| worker_media_github | 604681 | pełne fake żądania/wyniki ASR/OCR/GitHub, WorkerOptions i batch 501 wiadomości |

`receipt.json` zawiera polecenia, SHA-256 bibliotek, źródeł i rezultatów, statusy kompilacji/wykonania, katalogi i liczbę produktów. `compiler.txt`, `*.command.sh`, `*.compile.log`, `*.stderr.log` zachowują dokładne środowisko/polecenia i surowy przebieg. Wszystkie wyjścia zakończyły się 0, JSON parsuje się, dane są syntetyczne. Każdy program uruchomiono sekwencyjnie z osobnego katalogu scratch. Normalizacja losowych danych jest jawna w źródłach, bez dodatkowego postprocessingu JSON.

Ten katalog dowodzi **strony bazowej**. Porównanie z aktualną pełną biblioteką i pełny ctest nadal wymagają zakończenia wspólnego buildu. Wcześniejsze helper dowody ze starszego archiwum9d15 pozostają oddzielnie opisane i nie zastępują tej bazy.
