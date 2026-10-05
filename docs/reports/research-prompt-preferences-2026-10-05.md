# Podwątek 7B — prompty, parametry i preferencje użytkownika

Stan 2026-10-05: **oryginalny panel odzyskany i sprawdzony; scorer w przeglądzie przed collection**.
Gałąź `gpt/research-prompt-preferences-2026-10-05`, baza `edcb32fb87116ac497f8eb3d59d53ecea0cfee3f`.

Oryginalna paczka i panel zostały następnie opublikowane przez 7A na `gpt/research-jev-validation-2026-10-05`, commit `37a65efab8927f5fa3901b1db4237a7bbb69e141`. Zachowano ich dokładne bytes i hashe; nie używamy wcześniejszego projektu. Ten wcześniejszy stan odkrywania źródeł jest zachowany na `archive/gpt/research-prompt-preferences-source-discovery-2026-10-05`, commit `7b35923e1e74809bdc81353ba7dd833ffd65f72e`.

CRC wszystkich 568 wpisów ZIP i SHA/rozmiary 566 payloadów PASS. SHA-256 ZIP: `3885798cdc1769a8de1b29e7a1ef8b1633edc8cf33502baa947497c19c91ec06`. Rozpakowano do nowego pustego katalogu. Wszystkie 137 wpisów historycznego SOURCE_FREEZE zweryfikowano na oryginalnych wersjach źródłowych.

Oryginalne **16 konfiguracji × 8 pytań = 128 pierwszych żądań** zachowane byte-for-byte: 2 prompty × 0/0,7 × 256/1024 × zwięźle/pełny ślad. Całe źródła i aktualny gold zachowane; gold nie ma w promptach. Dokładny wspólny schemat pozostaje `label,evidence[{turn_id,quote}],explanation,counterarguments[string]`. Oryginalna kolejność i wszystkie hashe bez zmian.

Siedem istniejących testów przygotowania PASS; istniejący loader wykonawcy przyjął manifest 128 operacji. Trwa dodatkowy przegląd kontrolowanych testów strict first-response scorera w istniejącym prepare.py. Jego dodatkowy freeze musi poprzedzać pierwsze odpowiedzi; ten checkpoint jeszcze nie daje sygnału do startu.

Planowana górna wycena przy publicznej referencji 0,40 USD/M input i 1,60 USD/M output: **0,3580992 USD** = 567568 górnych jednostek wejściowych ×0,0000004 +81920 limitowanych tokenów wyjścia ×0,0000016. Jednostki wejścia są konserwatywnym allowance (bytes+1088), nie wynikiem tokenizacji. To wycena referencyjna; 7A odświeża ofertę wszystkich komponentów i dopuszcza całość w jednym prywatnym rejestrze.

Rzeczywiste płatne wywołania/koszt/rezerwacje 7B: **0 / 0 USD / 0 USD**. Brak opublikowanych pierwszych odpowiedzi tego panelu; wszystkie wyniki modelu i wnioski zależne od preferencji pozostają niezmierzone. Nie wykonano drugiego płatnego wykonawcy, nie odczytywano kluczy ani eval/real-holdout-key, nie zmieniano STATE/README/UI. Build/CTest dla tego przyrostu danych nie uruchamiano.

## Do wątku N

- **7A:** źródła już odebrane z `37a65efa`; przygotuj przyjęcie manifestu, lecz poczekaj na dodatkowy SCORING_FREEZE/raport 7B przed dispatch. Wspólny klucz ma limit 5 USD i jeden Twój prywatny ledger.
- **7B:** dokończ aktualny przegląd scorera (braki kosztu tylko dla podjętych prób, zgodne metryki profili), przepuść jego testy i opublikuj freeze wszystkich aktywnych źródeł/kryteriów bez zmiany promptów/żądań.
- **9:** checkpoint nie jest zakończonym empirycznym badaniem. Zachowaj archiwalny stan sprzed publikacji źródeł.
