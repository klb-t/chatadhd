# Rzeczywisty benchmark retrieval — continuation 02

Wykonano 48 zamrożonych zadań w 12 rodzinach (6 OpenAI, 6 Anthropic), 6 metod i 2 budżety: **288 rankingów i 576 ocen wyboru kontekstu**. Nowych wywołań API: 0; koszt API: 0 USD. Badano dobór literalnych dowodów, nie jakość odpowiedzi LLM. Wszystkie profile i parametry ustalono przed rankingami; nie strojono ich na wynikach.

Źródła i protokół

- Rodziny zachowują podział 8 tuning / 4 provisional_validation. Badacz i recenzent tej sesji widzieli źródła oraz adnotacje; to nie jest niezależny holdout. Każda rodzina ma 4 zadania. 46 zadań ma oczekiwane dowody (32 tuning + 14 validation), 2 dotyczą braku odpowiedzi w dostępnym źródle.
- Pytania są parafrazami badacza, utworzonymi na podstawie źródeł; często dzielą charakterystyczne słownictwo z dowodami. Nie stanowią naturalnego rozkładu nowych pytań użytkownika. Gold jest prowizoryczny, bez akceptacji właściciela lub niezależnej adjudykacji.
- Domyślną pulą jest cała rodzina. Dwa zadania o nierozwiązanej sprzeczności mają zamrożoną granicę przed późniejszym rozstrzygnięciem. Dla każdego zadania wszystkie metody otrzymały dokładnie tę samą pulę; hash puli zapisano przy każdej obserwacji.
- Zachowano pełne źródła. Kandydatem retrieval jest literalny tekst całej wiadomości, odczytany istniejącym ekstraktorem v2, z aliasami i alternatywnym tekstem Anthropic. Załączniki, obrazy, argumenty tool calls i nietekstowe pola nie są przeszukiwane semantycznie. Pula ma 2079 rekordów, 1 955 878 bajtów UTF-8 literalnego tekstu, w tym 540 rekordów bez literalnego tekstu (515 OpenAI, 25 Anthropic).
- Budżety 8192 i 32768 bajtów obejmują dokładną serializację JSON kontekstu; dodatkowy limit to 10 całych wiadomości. Za duży rekord jest pomijany z zapisanym powodem, nigdy przycinany. Modelowe tokeny/tokenizer pozostają null. Osobny licznik Unicode `\w+`, wersja `python_unicode_regex_word/1`, nie jest tokenizerem modelu.
- Użyto istniejących `local_baselines.tokens/chars/cosine` i `retrieval_exploration_v1.explore.bm25`. Identyfikatory i SHA implementacji są w prywatnym START. Lokalnych bajtów wcześniej opisanego modelu embeddingowego i wymaganego runtime nie było; nie pobierano modelu ani nie liczono embeddingów.

Zachowanie dowodów

Poniżej średnia recall dowodów w faktycznie wybranym kontekście. Najpierw uśredniono zadania w rodzinie, potem rodziny z równą wagą. Dla alternatywnych kompletów wystarcza jeden dopuszczalny komplet. Zadania bez pozytywnego dowodu nie dostają fikcyjnego recall.

| Metoda | Tuning 8 KiB | Tuning 32 KiB | Validation 8 KiB | Validation 32 KiB |
|---|---:|---:|---:|---:|
| Token cosine | 0.4740 | 0.4349 | 0.4375 | 0.4375 |
| BM25 | 0.4375 | 0.4479 | 0.3958 | 0.4583 |
| Znaki 3–5 | 0.6432 | 0.6510 | 0.6042 | 0.6042 |
| Odległość w grafie eksportu | 0.1198 | 0.1198 | 0.2292 | 0.3125 |
| BM25 + przejście po grafie | 0.4010 | 0.4323 | 0.5000 | 0.6042 |
| RRF czterech kanałów | 0.5885 | 0.5495 | 0.5208 | 0.4583 |

Znaki 3–5 miały najwyższy zaobserwowany średni recall tego panelu. Przy 8 KiB zachowały cały wymagany komplet w 25/46 zadań z dowodami; BM25 w 17/46, token cosine w 19/46. To opis próbki, nie uniwersalny zwycięzca ani dowód jakości końcowej odpowiedzi. Profile pozostają kandydatami bez automatycznej adopcji.

Większy budżet nie gwarantuje tutaj lepszego recall: selektor zachłanny może przy mniejszym budżecie pominąć dużą wiadomość wysoko w rankingu i dopuścić późniejszy potrzebny rekord. Przykładowo RRF na części validation osiąga 0,5208 przy 8 KiB i 0,4583 przy 32 KiB. Surowe rankingi są w obu porównaniach identyczne; zmienia się selekcja.

Koszt lokalny i pamięć

Jednorazowe pomiary z włączonym tracemalloc, bez stabilizowania obciążenia maszyny. Mediana czasu obejmuje unikalne zależności danej kompozycji, a nie tylko końcową fuzję. Indeksowanie/caching w obrębie rodziny jest jawne: liczniki słów są współdzielone, liczniki znaków powstają przy pierwszym zapytaniu danej puli i zostają w pamięci. To miesza zimne i ciepłe wywołania i nie jest docelowym testem opóźnienia produkcji.

| Metoda | Mediana czasu [ms] | Maksimum alokacji Python w dowolnej zależności [MiB] |
|---|---:|---:|
| Token cosine | 1.557 | 0.016 |
| BM25 | 23.862 | 2.970 |
| Znaki 3–5 | 22.435 | 114.857 |
| Odległość w grafie eksportu | 0.619 | 0.158 |
| BM25 + przejście po grafie | 24.806 | 2.970 |
| RRF czterech kanałów | 66.601 | 114.857 |

Maksimum alokacji dotyczy pojedynczego operatora/zależności i nie obejmuje już istniejącej pamięci cache. Nie jest pomiarem RSS ani szczytem całej kompozycji. Pełny koszt lokalny CPU nie jest wyceniony w USD. Pomiary czasu przygotowania indeksu, selekcji i każdego operatora zapisano oddzielnie.

Błędy i ograniczenia

- `retrieval-04-01`: wymagany literalny tekst ma 37 993 bajty; rekord JSON 39 450. Cała wiadomość nie mieści się w żadnym badanym budżecie. 31/32 zadań tuning i 14/14 answerable validation mają możliwość zmieszczenia kompletnego gold przy obu limitach. Osobna metryka oracle określa ten sufit; nie jest wejściem do rankingu.
- `retrieval-00-02`: metoda znakowa nie zachowała uzasadnienia mimo wykonalnego budżetu; pierwszy właściwy rekord był na pozycji 27. `retrieval-07-04`: analogiczny dowód uzasadnienia był na pozycji 76. To błędy rankingu, nie brak danych.
- `retrieval-06-03`: jedyny przypadek rozróżnienia faktycznej gałęzi eksportu. Przy 8 KiB BM25, rozwinięcie BM25 i RRF zachowały dowód bez oznaczonego dystraktora innej gałęzi; token cosine i znaki zachowały oba. Zawartość dystraktora w kontekście nie dowodzi, że model wybrał błędną gałąź: model nie odpowiadał.
- `retrieval-08-03` i `retrieval-11-02`: nierozstrzygnięte sprzeczności wyłącznie w zdefiniowanych prefiksach. Metody różnią się zachowaniem obu stron sporu; np. graf w drugim przypadku zachował cały komplet, a BM25 1/3. Nie deklarujemy automatycznego rozwiązania sprzeczności.
- W obu zadaniach bez odpowiedzi każda metoda wybrała niepusty kontekst. Żadna badana metoda nie wykazała zdolności stwierdzania braku odpowiedzi; samo odzyskanie pasujących słów nie rozstrzyga answerability.
- W OpenAI znormalizowane `child_ids` są puste, choć `parent_ids` istnieją. Obecna konfiguracja przechodzi po dokładnie dostępnych polach, więc faktyczna nawigacja jest tam głównie ku przodkom; Anthropic ma też zadeklarowane dzieci. Nie wyprowadzano nowych krawędzi w trakcie oceny i nie zmieniono konfiguracji po obejrzeniu wyników. To ograniczenie testowanego adaptera, nie dowód słabości grafów semantycznych.
- Stratyfikacja OpenAI/Anthropic w danych wynikowych jest opisowa, po 6 rodzin, miesza tuning i wyeksponowaną validation. Jednostką zależności jest rodzina; 48 zadań nie oznacza 48 niezależnych rozmów. Przedziały ufności nie są wyliczone.

Wersje i granice wnioskowania

`run-v1` zachowuje pierwsze rankingi, czasy, konteksty i metryki. `analysis-v2` odtwarza wyłącznie zapisane rankingi: 576 hashy kontekstu zgadza się. Korekta po pomiarze nadaje pustej precyzji kontekstu null oraz dodaje sufit wykonalności całych dowodów. Dokładne bajty obu producentów i ich hashe są zachowane. Nie uruchomiono ponownie metod ani pomiarów czasu.

P@1/5/10, recall@k, MRR i nDCG są metrykami rankingu **przed budżetową selekcją**. P@k ma stały mianownik k. `context_precision` i `context_evidence_recall` dotyczą rzeczywiście zserializowanego kontekstu. Przy pustym wyborze precision jest null, a recall dodatniego gold wynosi 0. Erratum protokołu jest osobnym dopiskiem; zamrożony protokół pozostał bez zmian.

Komponenty można składać jako dane i rozszerzać rejestrem operatorów. Dane 12 kandydatów na presety obejmują metodę, limit, wersję i dowody, bez automatycznej zmiany profilu użytkownika. Nie są kompletnymi presetami odpowiedzi LLM. Integracja produkcyjnego MethodRegistry/native nie została wykonana; pozostaje przypięta luka A3-DISC-001 raportowana przez A.

Pliki: `retrieval-results-v2.json` (oddzielne metryki i sparowane różnice rodzinne względem BM25), `retrieval-results-errors-v2.json` (klasy i pseudonimy przypadków), `retrieval-results-performance-v2.json` (czas, pamięć, ograniczenia i stratyfikacja źródeł), `retrieval-presets-v1.json`, `retrieval-results-receipt.json`. Prywatne zadania, fragmenty dowodów, source pointers, rankingi i dokładne konteksty pozostają poza Git.

Aktualny artefakt analityczny: **analysis-v3**, `retrieval-results-v3.json` i `retrieval-results-receipt-v3.json`; kandydaci: `retrieval-presets-v2.json`. Progi ewaluacji i metoda referencyjna zostały zapisane w `retrieval-evaluation-v3.json` jako dane opisujące poprzednie niezmienione wartości. Jawnie oznaczono, że zapisano je po pierwszym pomiarze. Wszystkie 576 wierszy v2 i v3 są identyczne; 0 nowych rankingów i 0 nowych pomiarów czasu. Dodano odrzucanie nieobsługiwanej polityki niezakotwiczonego przejścia po grafie. Ostateczny zestaw mechaniki: **25/25 testów**, pełny prywatny log i jego hash w receipt. Wersje v1 i v2 pozostają zachowane.
