# C / Thread 7 — kontynuacja 02: bramki i rzeczywiste badania offline

Data: 2026-10-09. Gałąź: `gpt/thread7-real-2026-10-09`.
Ten raport dotyczy nowego pakietu wykonawczego. Wcześniejsze raporty, freeze,
rachunki i wyniki pozostają niezmienne. Baza C to
`5e346c7161028ded976c40a0251fe8f9547bd233`; przypięte B
`b20c0d8ac37934e1600c3b9216492fd524220941`, A
`e29109852e456dd66493b75193bc5882df65ffe2`, main
`9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Pełne pochodzenie i hashe instrukcji:
`../research/thread7_real_2026-10-09/continuation_02/START.json`.

## Wykonany kod i usunięta blokada B

Minimalny commit `a6481d11a05158977a87dd55f3a4d6896a190dad` usuwa jawne
`dir="/tmp"` z fixture HybridHandoffTests; test korzysta z konfiguracji tempfile.
Oddzielny katalog z własnym markerem Git służy wyłącznie testowi negatywnemu.
Ochrona sekretów pozostaje włączona; negatywny test nadal odrzuca zapis przed
utworzeniem prywatnego materiału. Nie zmieniano markerów platformy ani allowlist.

W bieżącym środowisku pierwotne osiem testów przechodziło, ponieważ `/tmp` nie
było oznaczone jako Git. Kontrolowana reprodukcja warunku opisanego przez B
wywołała dokładnie osiem błędów na starym fixture. Po poprawce: 10/10 PASS.
Następnie izolowana bramka `research.structure`: 1458/1458, zero pominięć,
pełny stdout i kontrola repozytoryjnego verifiera PASS. To zakres structure,
nie całe CTest B. Użyto istniejącego przypiętego native tool o zgodnych źródłach;
nie deklarujemy nowego buildu native. Natychmiast opublikowano osobny
`continuation_02/HANDOFF_B.md`, pełne logi oraz dokładne polecenie weryfikacji
(commit `4e46ae87c5cd31967269f5fb28d1bb9400429019`).

Dalszy kod obejmuje walidację adnotacji, preparację źródłowych dowodów,
rozszerzalne operatory retrieval, ewaluację kontekstu i strat reprezentacji oraz
procedurę recovery w istniejącym payerze. Nie zmieniano produkcyjnego runtime,
UI, wspólnych schematów, STATE/INDEX ani cudzych gałęzi.

## Źródła i preparacje

Wykorzystano odzyskany wcześniej panel 12 rodzin: 6 OpenAI i 6 Anthropic,
2079 wiadomości / 2085 węzłów, okres 23.01–09.09.2026. Źródłowy większy wycinek
ma 179 rozmów, ale nowe pomiary dotyczą wyłącznie wskazanych 12 rodzin.
SHA256 wycinka: `2bff52e83852efcd596bc5c717dd4e13840c4a6a36ef34daeb7356c2c4f17ec4`.
Hash zachowanego freeze panelu:
`324d33a17ac09cc3b1c1dd11fb875b10d2a7ccf1f4c7b2fb06c313f9ba7e209f`.
Dostępne są 11 binariów załączników; 75 referencji pozostaje nierozwiązanych.
Sześć referencji rodzica prowadzi poza dostępne węzły. Historyczny panel
3 rodzin / 44 wiadomości / 8 pytań pozostaje osobny.

Przejrzano 770 wiadomości użytkownika we wszystkich rodzinach. Zapisano 258
kandydatów: 174 przyjęte jako historyczne instrukcje o określonym zakresie,
59 odrzucone, 25 niepewne; 61 relacji, także korekt i sprzeczności. Dziesięć
rodzin ma przyjęty dowód, dwie nie mają przyjętego dowodu. To liczby adnotacji,
nie trwałych niezależnych preferencji. Prywatne rekordy zachowują dokładny
fragment, pointer, hash, rolę, czas, zakres i uzasadnienie obu passów.

Wskazanie dowodów i kontrolę interpretacji wykonali asystenci tej samej sesji.
Nie było niezależnej adjudykacji ani akceptacji właściciela. Nie odczytywano
zastrzeżonego historycznego blind. Wszystkie niecytowane wypowiedzi użytkownika
zostały przeczytane; bardzo długi zewnętrzny transkrypt został oznaczony jako
cytat po kontroli granic i autorstwa, bez adjudykacji wszystkich jego twierdzeń.
Sugestie asystenta i cytaty osób trzecich nie stają się preferencją właściciela.

Zmieniono tylko 324 kombinacje `source_explicit`; 648 preparacji bez tych zmian
odziedziczono bez powtórzenia. Kompozycja v3 ma 972 gotowe pierwsze requesty:
648 kompletnych preparacji i 324 z oddzielną ekstrakcją pending. Dawne 297
braków adnotacji zamknięto przez sprawdzony dowód lub jawny pusty wynik znanych
dowodów. Nie dopisano brakujących preferencji. Spośród 324 zmienionych widoków
126 ma pusty zbiór widocznych przyjętych dowodów, 72 widoczną niepewność,
162 pomija adnotacje spoza widoku; kategorie nakładają się.

Request zawiera literalną wypowiedź i jej kotwicę źródłową; nie zawiera później
poznanego rozstrzygnięcia, ukrytych relacji ani interpretacji spoza widoku.
Badany wariant oznacza włączenie dowodu historycznego. Nie deklaruje rozstrzygniętej
aktywnej personalizacji. Profil wybrany wcześniej pozostaje wersjonowanym
wariantem, nie odgadniętym profilem właściciela. Commit adnotacji:
`68921add97c41f12651d030ddcb6321892cd1651`.

## Wykonane badanie retrieval

Przed rankingami zamrożono 48 zadań po cztery na rodzinę i 67 fragmentów dowodów.
Zachowano podział 8 rodzin tuning / 4 prowizoryczna walidacja; 32 i 16 zadań.
W 46 zadaniach istnieje dodatni dowód; dwa dotyczą jawnego braku odpowiedzi
w ograniczonym źródle. Pytania to parafrazy badacza oparte na źródłach, często
z charakterystycznym słownictwem. Gold jest prowizoryczny. Cały panel został
wyeksponowany badaczom; nie jest niezależnym holdoutem. Task freeze:
`867526d7269646c561858473dd1aabac850d04c4f7a6fa0127b223b057f5b0ec`,
commit `21ffc5e0769d53e4dbc507a2c57d8385e638b3f1`.

Sześć metod: dopasowanie tokenów, BM25, n-gramy znakowe 3–5, przejście po
strukturze eksportu, BM25 z rozwinięciem strukturalnym, RRF czterech kanałów.
Wagi i parametry ustalono przed pomiarami, bez strojenia na wynikach. Operatory
można rozszerzać przez rejestr i składać jako dane. Nie było dostępnego lokalnego
modelu embeddingowego z wymaganym runtime, więc nie liczono embeddingów.

Metody dostają tę samą pulę rodziny dla danego zadania; dwa przypadki sprzeczności
mają jawnie zamrożone wcześniejsze granice. Kandydatem jest cała wiadomość.
Budżety wynoszą 8192 i 32768 bajtów serializacji JSON oraz najwyżej 10 rekordów.
Rekord niemieszczący się w limicie jest jawnie pomijany, bez obcinania.
Tokeny/tokenizer pozostają null. Licznik słów Unicode ma własną wersję i nie jest
tokenizerem modelu. Semantyki tool calls i załączników nie oceniano.

Wykonano 288 pierwszych rankingów i 576 ocen budżetowych. Następnie naprawiono
konkretny brak adaptera: puste `child_ids` w normalizacji OpenAI mimo dostępnych
`parent_ids`. Osobno zamrożona polityka dodaje odwrotne przejście po istniejącej
relacji rodzica; nie tworzy relacji semantycznej ani brakującego zewnętrznego
węzła. Trzy zmienione ramiona dały 144 nowe rankingi i 288 ocen budżetowych;
144 rankingi niezmienionych ramion zweryfikowano i odtworzono z zapisu.
Łącznie: **432 faktycznie wykonane rankingi**. Korekta po pierwszym pomiarze
jest jawna; nie zmieniono gold, budżetów, wag ani promieni przejścia.

Aktualny recall zachowanych dowodów, średnia najpierw wewnątrz rodziny,
następnie po rodzinach:

| Metoda | Tuning 8 KiB | Tuning 32 KiB | Prowizoryczna walidacja 8 KiB | Prowizoryczna walidacja 32 KiB |
|---|---:|---:|---:|---:|
| Token cosine | 0,4740 | 0,4349 | 0,4375 | 0,4375 |
| BM25 | 0,4375 | 0,4479 | 0,3958 | 0,4583 |
| Znaki 3–5 | 0,6432 | 0,6510 | 0,6042 | 0,6042 |
| Struktura eksportu | 0,1198 | 0,1198 | 0,2292 | 0,3125 |
| BM25 + struktura po korekcie | 0,4505 | 0,4896 | 0,5833 | 0,6042 |
| RRF po korekcie | 0,5052 | 0,4974 | 0,5208 | 0,4583 |

Znaki zachowały pełny komplet w 25/46 zadań przy 8 KiB; BM25 w 17/46.
To najwyższy zaobserwowany wynik tego panelu, nie uniwersalny zwycięzca.
Poprawka struktury poprawiła rozszerzenie BM25, lecz pogorszyła RRF na tuning.
Wszystkie negatywne i pierwsze wyniki pozostają dostępne. Przy większym budżecie
zachłanny selektor czasem dopuszcza duży wczesny rekord, który wypiera późniejszy
właściwy dowód; dlatego recall nie musi rosnąć monotonicznie.

Jeden wymagany rekord JSON ma 39 450 bajtów i nie mieści się w żadnym limicie.
Inne niepowodzenia są błędami rankingu: przykładowe uzasadnienia decyzji były
na pozycjach 27 i 76. Jedyny rzeczywisty przypadek rozróżniania gałęzi bywa
pobierany razem z dystraktorem. To kontaminacja kontekstu, nie dowód błędnej
odpowiedzi modelu. W obu zadaniach bez odpowiedzi każda metoda wybrała niepusty
kontekst; nie wykazano rozpoznawania answerability.

P@k/MRR/nDCG dotyczą rankingu przed budżetem. Osobne metryki dotyczą faktycznie
wybranego kontekstu; pusta precyzja jest null. Erratum protokołu i późniejsze
zapisanie dawnych progów ewaluacji jako danych są jawne i nie nadpisują freeze.
Sparowane różnice i rozbicie korpusów znajdują się w JSON wynikowym. Jednostką
zależności jest rodzina; 48 zadań nie daje 48 niezależnych rozmów.

Pomiary czasu retrieval były pojedyncze i instrumentowane tracemalloc, z jawnym
cache między zadaniami rodziny. Przykładowe mediany: token cosine 1,557 ms,
BM25 23,862 ms, znaki 22,435 ms; najwyższa dodatkowa alokacja operatora znakowego
114,857 MiB przy budowie cache. To alokacje Python, nie RSS, z mieszaniną zimnych
i ciepłych przebiegów. Czasy kompozycji po korekcie sumują stare pomiary zależności
i nowe pomiary zmienionych operatorów, nie udają nowego pomiaru całości.

## Pomiary reprezentacji i utrat

Zmierzono 108 istniejących widoków: 12 rodzin × trzy reprezentacje × trzy
rozdzielczości. Odtworzenie przypiętym producentem v2 dało **108/108 zgodnych
bajtowo widoków**. Pełny tekst: 2 608 478 B; jawne pola: 7 797 444 B;
graf: 15 618 348 B. Graf ma 5,99× rozmiaru tekstu i 2,00× rozmiaru pól.

Wszystkie warianty zachowują 100% literalnych pól tekstowych wybranych węzłów.
Tekst zachowuje tylko 5,98% wszystkich liści native-message przy pełnym widoku,
bo pomija metadane i dane nietekstowe; nie oznacza to utraty 94% tekstu.
Pola i graf zachowują pełne pola native-message oraz referencje rodzica;
graf dodatkowo pełne znormalizowane węzły i redundantną kopię native_node.
Zachowanie sześciu nierozwiązanych referencji nie dowodzi dostępności ich celów.

Widok granicy ma 1552/2085 węzłów, podgraf 36/2085. To widok źródła, nie
streszczenie ani nowy checkpoint użytkownika. Graf odtwarza wartości JSON
wybranych węzłów, pola — native_message; żadna forma nie odtwarza całego eksportu
z oryginalną serializacją i binariami. Pokrycie w obrębie wybranych węzłów jest
oddzielone od pokrycia całej rodziny.

Czysty narzut kluczy/składni pełnych reprezentacji: 295 577 / 857 001 /
1 693 972 B. Mediany przygotowania pełnego widoku: 29,88 / 37,93 / 37,20 ms;
trzy powtórzenia techniczne i osobny pomiar pamięci na komórkę. Nawet mały podgraf
powodował około 50 MB dodatkowych alokacji, bo producer waliduje i haszuje całą
rodzinę. Brak izolacji obciążenia nie pozwala wnioskować o przewadze kilku ms.
Testy metamorficzne obejmują serializację, kolejność kluczy, przeniesienie źródła,
ubytek fragmentu, zmianę wartości i korektę zmieniającą cel retrieval. Zmodyfikowane
fixtury służą mechanice, nie są autentycznymi nowymi wypowiedziami.

## Odzyskiwanie i nierozstrzygnięte koszty

Commit `e68101196d4fbc907dede8f98a2f3ca25566674f` dodaje procedurę, która konsumuje
zewnętrznie zachowany capture request/response, wiąże go z zarezerwowaną operacją,
manifestem i fingerprintem klucza, a następnie sprawdza GET generation oraz GET key.
W istniejącym PrivateLedger dopisuje dowód i projekcję rozstrzygnięcia w jednej
transakcji. Nie nadpisuje pierwszych rekordów, nie powtarza POST, nie dodaje
ledgera pieniędzy. Ponowne recovery rozstrzygniętego przypadku jest idempotentne.

Pełny pierwszy capture i jawny przegląd jego atrybucji są warunkiem rozstrzygnięcia.
Hash nie jest podpisem dostawcy; sama dowolna nazwa generacji nie wystarcza.
Nieznany koszt, niepełny capture, sprzeczne bajty lub niezgodne usage zachowują
unknown i pełną rezerwację. Bez credential można zapisać unknown z wcześniej
związanym fingerprintem, bez dostępu sieciowego. Rozstrzygnięcie nie usuwa
pozostałych stopów programu i nie zastępuje świeżego preflight.

Testy kontrolowanego transportu obejmują awarie starego payera i recovery,
restart, konkurencję, dowody opóźnione, uszkodzone i sprzeczne, błędną chronologię,
prywatność komunikatów oraz granicę kolejki. Nie odzyskiwano rzeczywistej opłaconej
próby właściciela i nie wykonano rzeczywistych GET do dostawcy. To nie jest
obietnica exactly-once inference ani odporności na awarię sprzętową.

## Koszty, status i granice dowodu

Nowe wywołania modeli/dostawcy: **0**. Nowy koszt API: **0 USD**.
Historyczna kampania pozostaje: **720 prób, 0,873216500 USD**;
4,126783500 USD to archiwalna pozostałość nieodnawialnego limitu 5 USD.
Aktualna tożsamość klucza, usage, rezerwacje i ceny pozostają unknown.
Nie ponawiano historycznych opłaconych prób ani poszukiwania klucza w znanych
archiwach bez credential. Brak nowej konkretnej referencji nie blokował pracy offline.
Koszt lokalnego CPU nie został wyceniony jako koszt API.

Nie dowiedziono jakości odpowiedzi LLM, korzyści personalizacji, semantycznego
rozumienia grafu, niezależnej walidacji ani przewagi w całym archiwum.
Wyniki pochodzą z tego panelu, a wszystkie oceny źródłowe są prowizoryczną pracą
tej sesji. Nie dopasowywano hiperparametrów do wyeksponowanej walidacji.
Dane kandydatów na presety mają dowody, zakres i status bez automatycznej adopcji.
Znana luka A `A3-DISC-001` na granicy native MethodRegistry pozostaje zadaniem
integracyjnym A/B; zgodność kodeka lub schematu C nie oznacza jej zamknięcia.

## Wykonane testy, publikacja i punkt wznowienia

Końcowy przypięty kod `c525d0e942be95c8b3a79b670e621d50e0b31a1a`:
**research.structure 1554/1554 PASS, zero pominięć**, 45,97 s, repozytoryjny
verifier PASS. Pełny stdout, JUnit, manifest odkrycia i receipt:
`continuation_02/final-gate-evidence.zip`, podsumowanie `FINAL_TESTS.json`.
Pierwszy przebieg tej integracji miał jeden błąd: test reprezentacji korzystał
ze starej sygnatury ewaluatora i starej nazwy metryki dystraktora. Zachowano
nieudany przebieg oraz pierwszą niepełną poprawkę. Test teraz przekazuje jawne
parametry i sprawdza równoważną metrykę `annotated_version_distractors_selected`;
aktualny cel nadal wymaga 0, stary cel 1. Test celowany 45/45, potem pełna bramka.
Nie zmieniono asercji jakości ani surowych wyników badań.

Osobne testy poza discovery structure: walidator zadań 10/10, projekcja Basic/Expert
7/7. Szczegółowe przebiegi 59 preferencji, 185 recovery, 32 retrieval, 13
reprezentacji, 70 pierwszego przeglądu i 9 przeglądu adaptera zachowano z pełnymi
logami; częściowo nakładają się na bramkę i nie są sumowane jako niezależne testy.
Nie powtarzano zakończonego testu wielkiej macierzy ani historycznych płatnych prób.

Basic/Expert: `continuation_02/delivery-example-v3.json`, lokalny wersjonowany
kontrakt `delivery-contract-v3.json`, transportowy graf/trace
`delivery-artifact-v3.json.gz`. Istniejące schematy `loom.method_graph/1` oraz
`loom.method_run_trace/1`, kodek Python i dokładny roundtrip przeszły testy.
Trace opisuje eksport publicznej projekcji, nie inference ani wykonanie benchmarku
w native. Nowy blok offline pozostaje oddzielony od metryk odpowiedzi LLM null.
Aktualne dane kandydatów: `retrieval-presets-v3.json` i
`representation-candidates.json`; każda kolekcja ma dowód i status bez adopcji.

| Przyrost | Commit |
|---|---|
| Minimalne odblokowanie B | `a6481d11a05158977a87dd55f3a4d6896a190dad` |
| Logi bramki i HANDOFF_B | `4e46ae87c5cd31967269f5fb28d1bb9400429019` |
| Freeze zadań i konfiguracji | `21ffc5e0769d53e4dbc507a2c57d8385e638b3f1` |
| Adnotacje i preparacje v3 | `68921add97c41f12651d030ddcb6321892cd1651` |
| Recovery istniejącego payera | `e68101196d4fbc907dede8f98a2f3ca25566674f` |
| Benchmark i jawna korekta adaptera | `8239ce5113b421c83436ab9af18476bc78851f24` |
| Pomiary reprezentacji | `f37750f33b3d64bf25df8c1b6649adf733b25f13` |
| Poprawka integracyjna testu | `c525d0e942be95c8b3a79b670e621d50e0b31a1a` |

Prywatny checkpoint preferencji został zapisany trwale przed końcową bramką:
`PRIVATE_Thread7_continuation02_preferences_2026-10-09.zip`, SHA256
`69375fd9e1b9f6805e5db61b789c014137f32b38579a69d0e3a2dccc54842481`.
Poprzedni checkpoint `fca71d…` i starszy `137c49…` pozostają nienaruszone.
Końcowy pakiet przechowuje ich dokładne bajty, wszystkie nowe prywatne surowe
pomiary, adnotacje, producentów, wcześniejsze błędy i manifesty. Jego zewnętrzny
hash, commit dostawy i potwierdzenie zapisu podaje osobne `continuation_02/PUBLICATION.json`
(dzięki temu nie ma cyklu własnego hasha). Publiczna projekcja zawiera tylko kod,
bezpieczne agregaty i dowody mechaniki. Kontrola obejmuje także metadane,
komunikaty i rozpakowane logi; wynik w `public-projection-check.json`.
Jest to ograniczona kontrola znanych fragmentów/identyfikatorów, uzupełniona
przeglądem treści, nie dowód nieobecności wszystkich możliwych danych osobowych.

Punkt wznowienia to kompozycja `preferences/COMPOSITION-v3.json` (SHA
`20ae0e08a3429fc5789040e007f4585a5880924f41f40fab6fdf56748e829f4a`),
`preferences/expanded-v3` oraz zachowane `expanded-v2` dla 648 niezmienionych
wierszy. Aktualne wyniki offline to `run-native-reciprocal-v2` oraz 108 rekordów
w `representation/`. Nie trzeba ponownie wykonywać rankingu ani preparacji,
aby odtworzyć analizę. Najpierw należy sprawdzić payloady manifestów.

Stare małe kolejki wiążą stare body. Nowy request wymaga nowego zamrożonego
spec, kolejki i manifestu payera; nie wolno przepinać starej rezerwacji na inne
bajty. Płatny etap pozostaje warunkowy: nowa konkretna autoryzowana referencja,
aktualne identity/usage/rezerwacje/ceny, oficjalne zasady cache i górny koszt
małej fazy w istniejącym limicie. Pozostałe niewykonane pomiary dotyczą jakości
LLM, 324 ekstrakcji zależnych od pierwszej odpowiedzi, aktywnej stosowalności
preferencji i niezależnej adjudykacji. Ukończony pakiet offline nie zależy od
odzyskania klucza; brak preflight nie jest zastępowany fikcyjnym wynikiem.
