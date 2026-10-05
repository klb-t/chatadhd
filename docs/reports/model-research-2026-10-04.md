# Wątek 7 — badania modelowe

Stan 2026-10-05: **badania w toku**, gałąź `gpt/model-research-2026-10-04`, baza
`main` `30ad7d3` sprawdzona ponownym fetch. Edycje wyłącznie w research/structure
oraz tym raporcie. `STATE.md`, główny README, UI i zakresy innych wątków zachowane.
Właściciel dostarczył klucz i autoryzował **5 EUR**. Faktyczny nieodnawialny limit
operatora wynosi **5 USD**, nie został podniesiony. Referencja ECB 2026-10-02:
1,1225 USD/EUR. Klucza i prywatnych danych nie ma w repo.

## Wyniki i rachunek

Wszystkie **708 pierwszych prób** mają unikalne generacje i potwierdzony rachunek:
**0,703661900 USD**, nieznane koszty/rezerwacje **0**, pozostały limit
**4,296338100 USD**. To suma zapisanych rachunków generacji, nie faktura operatora.

| Etap | Wywołania | Rzeczywisty USD | Wynik i granica interpretacji |
|---|---:|---:|---|
| 1: osiem ramion / 432 żądania | 432 | 0,052296100 | 12 autorskich rodzin DEV ×4 pytania; po złożeniu split 48 ocen/ramię. j_active40/48, j_directed/j_roles/j_split43/48; zgodność commitment źródła, nie prawda światowa |
| 2: native synthetic DEV | 60 | 0,163908000 | 15 fikcyjnych gałęzi ×4 przepisy. **0/60 przyjętych native**:41 odrzuconych przed native,19 przez native. Semantyczna jakość null; brak zwycięzcy |
| 3: frontier, dwa różne zadania | 12 | 0,317069000 | Mechanika0/12; ręcznie8/12 interpretowalnych. Cel5 pass/1 fail/6 unknown, pełna adekwatność3/3/6, grounding źródłowy6/2/4. Nie gotowe produkcyjnie |
| 4: graf kontra tekst+JSON | 12 | 0,163190000 | Mechanika12/12; cel10/12, pełna adekwatność9/12 i grounding źródłowy9/12. Wszystkie12 dostępne; znane autorskie DEV |
| 5a: dwie powtórki wybranych etykiet | 192 | 0,007198800 | j_active40/48+40/48=80/96; j_directed44/48+45/48=89/96. Dostępność192/192; te same12 rodzin, nie niezależna populacja |

Domyślny pięciowymiarowy protokół wyboru pozostał niezmieniony. Osobny jawny
preset eksploracyjny po obejrzeniu etapu1 wybierał j_active i j_directed na podstawie
etykiet, dostępności i pełnego kosztu. Etap2 niczego nie wybiera. W odrębnych
zadaniach etapu3 Pareto wskazało GPT pattern oraz Gemini completion; etap4 GPT graph.
To kandydaci badawczy na dwóch przypadkach, bez progu, kwoty lub automatycznej adopcji.

Przed autorstwem nowego zbioru zamrożono **wszystkie grupy selekcji** w
[all-group-selection-freeze-v1.json](../research/model_research_2026-10-04/stage5/all-group-selection-freeze-v1.json).
Nowe źródłowo ocenione dane:24 rozmowy/96 pytań,6 grafów frontier,6 kontekstów reply.
Niezależny peer przed collection wykrył i zamknął trzy niejednoznaczności źródłowe;
poprzednie drafty zachowano dokładnie. Jeszcze nie wykonano płatnego sprawdzianu
na tych danych ani12 powtórek silnych metod. Nowe dane są autorskie i widoczne dla
recenzentów; nie twierdzimy, że to zewnętrzny, statystycznie niezależny ślepy holdout.
Nigdy nie czytano `eval/real-holdout-key` ani prywatnych eksportów właściciela.

## Odtwarzalność i graf metod

[Etap1](../research/model_research_2026-10-04/programme-actual/stage1-final-20261005-v2/README.md)
ma zwarty replay pierwszych odpowiedzi, rachunków i grafu. [Etapy3/4](../research/model_research_2026-10-04/followup-results/source-measured-graph-v1/README.md)
mają dokładne12 konfiguracji metod, wersje, parametry, prompty i hashe,24 przebiegi,
144 metryki we wspólnym formacie profili oraz180 krawędzi produced_by. Pięć
nieznanych pomiarów pozostaje null. Twierdzenia są datowane i wskazują dowody;
nie oznaczają wykonania CAPI, zapisu native store lub przyjęcia do produkcji.

Pełne negatywne odpowiedzi/modelowe treści, oceny, receptury, źródła i odtwarzanie
pozostają na [archive/gpt/model-research-runner-review-2026-10-05](https://github.com/klb-t/chatadhd/tree/754450ebe6e402d1868f31c26b70dc5131e0d5d4/docs/research/model_research_2026-10-05-runner-review).
Etap2 ZIP ma419 plików/SHA `8b99e25ee349b8d9b09576a82e30ce06a85cf9dcca0ad28e8a16aed39bc92d4d`;
etapy3/4 ZIP200/SHA `71dd854dde0f86623b045c6aaad1d90e251e5bc923860eaf23a5ff10e67e6dd6`.
Niezależny clean replay potwierdził oba oryginalne grafy i mechanikę0/12 oraz12/12.
Zwarte dane na przyjmowanej gałęzi referują te archiwa. Nie zastępują pełnych
prywatnych envelope HTTP; publiczne projekcje jawnie zapisują utratę i oryginalne hashe.

Po192 wywołaniach, **przed jakąkolwiek oceną**, wykryto rozbieżność kształtu gold:
zamrożony plik ma listę w `cases`, stary adapter wymagał listy na korzeniu.
[Jawna korekta kontenera](../research/model_research_2026-10-04/stage5/label-repetition-v1/SCORING_CONTAINER_FIX.md)
wprowadza uniwersalny JSON Pointer `/cases`. Hash całego gold jest sprawdzany najpierw.
Nie zmieniono etykiet, odpowiedzi, progów, przygotowanych żądań ani zamrożonej
polityki; modeli nie powtarzano. Dokładny poprzedni kod, błąd na atrapie i oba peer
reviews są osobnym archiwum. Formatowa poprawka nie jest udawana jako prerejestracja.

## Weryfikacja i nierozstrzygnięte sprawy

[Najnowsza pełna bramka](../research/model_research_2026-10-04/verification/repetition-gold-pointer-full-20261005/receipt.json):
**108/108 CTest**,134,63s,0 pominięć; **2347 przypadków jednostkowych i dwa smoke**.
81 zestawów native:659 przypadków/24465 asercji;25 Python:1688 przypadków.
Wszystkie1053 pliki funkcjonalne/polityki wiążą się z opublikowanym `9812ef64`;
sześć natywnych binariów niezmienionych. Pełne stdout/JUnit/LastTest zachowano;
progów ani testów nie usuwano. To ponowne użycie build o niezmienionych źródłach native,
nie deklaracja nowego clean build. Dodatkowe52 testy adaptera i dwa niezależne peer
reviews używały wyłącznie atrap. Zmiany dokumentacyjne/danych nie zmieniają tej bramki.

Historyczny rachunek:992 wiersze→631 prób/361 kopii,629 hashów odpowiedzi,
628 zgodnych kosztów,0 rozbieżności. **1,098135722 USD pozostaje nieprzypisane**:
brak tożsamości ostatniego checkpointu/starych rachunków generacji; świeży klucz
nie dostarcza tego dowodu. Rezerwacja0,00738793 USD nie jest nowym wydatkiem.
Historyczne6/60 ekstrakcji odtworzono;38/60 to replay dekodera, nie nowa jakość modelu.
Nieodzyskany SHA starego generic adaptera V1 uniemożliwia pełne odtworzenie tamtej
orkiestracji; nowy wykonawca nie jest jego rekonstrukcją. Historia jest zachowana.

## Do wątku N

- **1:** Przyjmij wersjonowane [osiem przepisów](../research/model_research_2026-10-04/stage1-measured-recipes-for-w1-v1.json) i wyniki jako dane metod. Jev dotyczy dostarczonych kandydatów, nie native extraction. Stage2 nie daje zwycięzcy; potrzebne osobne prompt/parametry override, provenance i oddzielenie schematu od semantyki. Wyniki stage5 nowych danych jeszcze w toku.
- **3/4:** Zachowaj exact requested/observed model, parametry/prompt hash, daty, mianowniki, null i produced_by. Uzgodnij adapter tych istniejących ModelProfile twierdzeń do kanonicznego `loom.method_graph/1`/`loom.method_run_trace/1`; eksport Python nie udaje native consumer/store. Historyczny próg etykiety0,5 jest zamrożonym instrumentem badania, nie nowym wymaganiem silnika.
- **9:** Nie przyjmuj jeszcze całego wątku jako zakończonego; stage5 trwa. Rebase na aktualny main i pełne bramki/mixed web przed liniowym odbiorem. Archiwów negatywnych nie włączaj do main. Zaktualizuj INDEX/STATE według aktualnego raportu, zachowując cudze wpisy; zgoda/key są już dostarczone.
