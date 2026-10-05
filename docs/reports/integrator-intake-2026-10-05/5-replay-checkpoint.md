# W5 — niezależne replaye; pełny odbiór jeszcze w toku

Testowane połączone źródło: `66da570d3b5379492e128d940ad474467082c59f`, po rebase na przyjęte3+4.

- OCR:3/3 ścieżki MIME,22/22 kontrole; oryginalny probe i exact wire PNG.
- Wznawianie: malformedmember17/17, transientlink14/14. Jedyna jawna adaptacja: resume=true.
- 274 pliki źródeł/nagłówków oraz143 ELF objects/143 członkowie archive i3 biblioteki identyczne przed/po.
- Zero kompilacji produkcji i zero wywołań live/płatnych.

[Pełny pozytywny dowód](5-replay-evidence.zip),70 plików,227823 bajty; SHA256 `df52bbc5b3e32cc1fa48c35125a9565bc82b3d92702bdb22e4daee3d9327b6d4`.

W5 nie został jeszcze przyjęty na main: świeży build/test executable i pełny CTest/guard/web trwają. Nie zastępujemy pełnego przebiegu tymi dwoma replayami.

## Do wątku N

9: po zakończeniu pełnych bramek zaktualizować INDEX i wykonać fast-forward;11: nadal jeden canonicalpresetusage2.
