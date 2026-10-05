# Podwątek 7B — prompty, parametry i preferencje użytkownika

Stan 2026-10-05: **projekt przygotowany częściowo; wykonanie zablokowane brakiem opublikowanych źródeł**.
Gałąź: `gpt/research-prompt-preferences-2026-10-05`.
Baza pobrana przez GitHub API: `edcb32fb87116ac497f8eb3d59d53ecea0cfee3f`, drzewo `aa2793abd8dc9ce4b2a89659efbbfc87535df664`.
Commit danych: zostanie wskazany w następnym małym commicie raportowym po publikacji.
**Nie ma commitu gotowego do płatnego uruchomienia.**

Bezpośredni odczyt żądanego ZIP na tej bazie zwrócił 404. Pełne drzewo 2613 wpisów bez ucięcia nie zawiera ZIP ani rozpoczętego panelu. Niezależna kontrola archiwów, main i integratora także nie znalazła tych ścieżek. Zakres dowodu obejmuje wyłącznie sprawdzone publikacje, nie prywatne checkpointy. Szczegóły w [7b-source-audit.json](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-source-audit.json).

Przeczytano AGENTS, wymagania właściciela, raport wątku 7 oraz opis istniejącego manifestu, wykonawcy, normalizatora i profili. Nie odczytywano kluczy ani `eval/real-holdout-key`. Nie zmieniano STATE, głównego README, UI ani innych zakresów. Zmiany obejmują tylko dane w docs/research i ten wymagany raport.

## Co przygotowano

[Projekt](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-design.pending.json) zawiera macierz 2 prompty × 2 temperatury × 2 limity × 2 preferencje, cztery dokładne proponowane teksty systemowe i ich SHA-256, jeden proponowany schemat wyjścia oraz 16 konfiguracji z wersjami metod, parametrami i deklaracją produced_by. Plan obejmuje osiem oryginalnych pytań z wymaganych dwóch rodzin i 128 pierwszych wywołań. Źródła, gold, query_id, żądania i kolejność nie są pozornie zamrożone: ich hashe pozostają null.

[Rygor oceny](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-rubric.pending.json) rozdziela dostępność, ścisły JSON, schemat, etykietę, mechaniczne cytaty i ich adekwatność źródłową, przypisanie/czas, ucięcie, koszt i ręczną obserwowalną preferencję 0/1/2 z null dla nieocenialnych odpowiedzi. Brak odpowiedzi nie jest obserwowanym błędem etykiety; mianownik planu pozostaje 8. Krótszy tekst sam nie wygrywa. Pełny ślad oznacza sprawdzalne zdarzenia źródłowe. Kryteriów nie wolno zmieniać po zobaczeniu odpowiedzi.

**To częściowy projekt, nie kompletna prerejestracja ani manifest uruchomieniowy.** Trzeba kontynuować odzyskany panel, nie zastępować go nowym wykonawcą. Warianty są danymi badania, bez ograniczeń produktu.

## Koszt

- Planowany całkowity koszt / górna rezerwa: **null**, brak dokładnych wejść i aktualnej oferty przypiętego endpointu.
- Oczekiwana przez właściciela rezerwa około **0,4–0,5 USD**: niezweryfikowana, nie jest samodzielnym przydziałem.
- Dokładny planowany limit wyjścia: `64×256 + 64×1024 = 81920` tokenów.
- Publiczna referencja OpenRouter odczytana 2026-10-05: **0,40 USD/M input**, **1,60 USD/M output**; [strona modelu](https://openrouter.ai/openai/gpt-4.1-mini/activity). Sam komponent wyjściowy przy tej referencji: **0,131072 USD**. Cała referencja: `0,0000004×suma_tokenów_wejścia + 0,131072`; nie zakłada cache discount. To nie jest pełna oferta/rezerwacja runnera.
- Płatne wywołania 7B: **0**; rzeczywisty koszt wywołań modeli 7B: **0,000000000 USD**; rezerwacje 7B: **0**. Nie jest to pomiar bieżącego salda wspólnego klucza.
- Wspólny limit **5 USD**, jeden prywatny rejestr prowadzącego **7A**. Nie uruchomiono drugiego płatnego wykonawcy.

[Wyliczenie komponentów dla każdego wariantu](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-cost-plan.pending.json).

## Wyniki każdej konfiguracji

| Konfiguracja | Prompt | Temperatura | max_tokens | Preferencja | Próby / plan | Wyniki | USD rzeczywisty |
|---|---|---:|---:|---|---:|---|---:|
| literal-evidence-v1.t0.n256.concise | dowód/czas/mówiący | 0 | 256 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t0.n256.full-trace-counterarguments | dowód/czas/mówiący | 0 | 256 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t0.n1024.concise | dowód/czas/mówiący | 0 | 1024 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t0.n1024.full-trace-counterarguments | dowód/czas/mówiący | 0 | 1024 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t07.n256.concise | dowód/czas/mówiący | 0.7 | 256 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t07.n256.full-trace-counterarguments | dowód/czas/mówiący | 0.7 | 256 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t07.n1024.concise | dowód/czas/mówiący | 0.7 | 1024 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| literal-evidence-v1.t07.n1024.full-trace-counterarguments | dowód/czas/mówiący | 0.7 | 1024 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t0.n256.concise | rejestr zmian | 0 | 256 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t0.n256.full-trace-counterarguments | rejestr zmian | 0 | 256 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t0.n1024.concise | rejestr zmian | 0 | 1024 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t0.n1024.full-trace-counterarguments | rejestr zmian | 0 | 1024 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t07.n256.concise | rejestr zmian | 0.7 | 256 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t07.n256.full-trace-counterarguments | rejestr zmian | 0.7 | 256 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t07.n1024.concise | rejestr zmian | 0.7 | 1024 | zwięźle | 0 / 8 | null — brak odpowiedzi | 0 |
| commitment-ledger-v1.t07.n1024.full-trace-counterarguments | rejestr zmian | 0.7 | 1024 | pełny ślad + kontrargumenty | 0 / 8 | null — brak odpowiedzi | 0 |

Nie otrzymano pierwszych odpowiedzi tego panelu. Wszystkie metryki modelu są niezmierzone; nie przypisano zer jako wyników jakości. Nie ma negatywnych odpowiedzi do archiwizacji ani porównania metod. Wnioski zależne od preferencji: **brak wyników empirycznych**. Zamrożony projekt analizy przewiduje porównania tych samych pytań przy pozostałych czynnikach stałych; dwa autorskie źródła nie dają ośmiu niezależnych rodzin.

## Bramki i wznowienie

[Kontrola danych](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-verification.json): PASS ścisłego JSON, pełnej i unikalnej macierzy 16 konfiguracji, 128 planowanych slotów, hashów proponowanych promptów/wspólnego schematu, sum kosztów części wyjściowej i jawnego zachowania null. [Hashe projektu](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-artifact-hashes.json) zachowują pliki w tej wersji.

Bramka wykonania: **BLOCKED**. CRC/SHA archiwum, źródłowy gold, prawdziwy prepared manifest i dokładna wycena: niewykonane z powodu nieopublikowanych wejść. Nie zmieniono runnera/scorera; ich testów nie deklarujemy. Nie uruchamiano build/CTest dla tego przyrostu danych; nie ma deklaracji pełnej bramki odziedziczonej z bazy. Nie rozluźniano progów.

Dokładne miejsce wznowienia: [7b-HANDOFF.md](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7b-HANDOFF.md), **punkt 1: publikacja oryginalnej paczki i rozpoczętego panelu przez 7A**. Dopiero potem można dostarczyć commit gotowy do uruchomienia i oceniać opublikowane pierwsze odpowiedzi.

## Do wątku N

- **7A:** Publikuj brakujący oryginalny ZIP, SHA/manifest/instrukcję i istniejący panel; podaj commit. Ten przyrost jest projektem oczekującym na dane, nie żądaniem płatnego startu. Po bindingu 128 żądań odśwież pełną wycenę i dopuść panel wyłącznie w swoim jednym prywatnym rejestrze.
- **7B:** Zacznij od punktu 1 handoffu. Kontynuuj odzyskane przygotowanie i scorer; zamroź pełny panel przed collection. Nie używaj starych rodzin zamiast new_label_001/new_label_013. Oceń pierwsze odpowiedzi po publikacji 7A.
- **3/4:** Dopiero empiryczne wyniki wiąż z wersjami metod i produced_by w istniejącym formacie profili. Projekt i brak pomiaru nie są jakością modelu ani natywnym zapisem grafu.
- **9:** Nie oznaczaj 7B jako zakończonego badania ani gotowego do płatnego wykonania. Przyrost nie modyfikuje STATE/README/UI.
