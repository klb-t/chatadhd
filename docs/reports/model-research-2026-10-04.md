# Wątek 7 — badania modelowe

Aktualizacja: 2026-10-05. Gałąź `gpt/model-research-2026-10-04`, baza po
czystym rebase: `main` `30ad7d3`. Zakres: `loom/tools/structure`,
`docs/research` i ten raport. Nie zmieniano produkcyjnych zakresów innych wątków,
`STATE.md`, głównego README ani interfejsu. Gotowość całego przyrostu: **w toku**;
pełna bramka 108/108 jest zielona; etapy 1–2 ukończone, etap 3 w toku.

Właściciel zatwierdził osobny klucz/budżet **5 €**. To zastępuje starsze
ograniczenie do przygotowania offline i klucza 2 USD. Zgoda na wywołania jest;
właściciel dostarczył zaszyfrowany klucz 2026-10-05. Nowych ukończonych płatnych
wywołań: **432/432 w etapie 1**, potwierdzony koszt **0,052296100 USD**, oraz
**60/60 w etapie 2**, koszt **0,1639080 USD**. Ukończone etapy 1–2 razem:
**492 pierwsze próby i 0,216204100 USD**; nie doliczamy tu nieukończonego etapu 3.
Na zamknięciu etapu 1 licznik operatora był równy sumie jego 432 rachunków generacji;
nie ma nieznanych kosztów ani pozostałych rezerwacji tego etapu. Limit operatora
to **5 USD**, mieszczący się w autoryzacji 5 € przy zapisanym kursie ECB
1,1225 USD/EUR. Prywatne materiały nie trafiają do publicznego repo.

Pierwszych pięć odpowiedzi zachowano po zatrzymaniach związanych z opóźnieniem
metadanych. Każdą rozliczono odczytami GET, bez ponownego POST. Pozostałe 427
zebrano z wybraną w danych polityką `stage_end` i `unknown_cost_policy: reserve`.
Niepotwierdzone rachunki miały pełne rezerwacje i koszt null; każda nowa para
model/dostawca/trasa wymagała pierwszego dowodu płatności kredytem. Przed kolejnym
POST obowiązywał ścisły przedział zużycia i świeża kontrola limitów. Na końcu
potwierdzono wszystkie unikalne generacje oraz pełną sumę kosztów. Publiczny
preset nadal używa rozliczania każdego zapytania i zatrzymania przy niepewności.

Etap 2 ukończono: **60** zapytań native synthetic DEV. Przed wysłaniem
zapisano [świeży plan](../research/model_research_2026-10-04/programme-actual/plans-20261005/stage2.plan.json):
prognoza komponentów i rezerwacja **0,443196 USD**. To górna projekcja, nie koszt
rzeczywisty; pełny rachunek wynosi **0,1639080 USD**. Zachowano 41 odpowiedzi
odrzuconych przed native i 19 alternatyw odrzuconych przez native: **0/60
mechanicznie przyjętych**, nie semantyczne 0. Ocena semantyczna pozostaje null,
bez zwycięzcy. Etap 3 trwa; jego świeża górna projekcja **1,965986350 USD**
nie jest wydatkiem rzeczywistym.

## Zrobione i liczby

| Obszar | Przed | Po / znaczenie |
|---|---|---|
| Badanie 432 | Zamrożenia nie przechodziły z powodu dryfu plików narzędzi | 432/432 pierwszych odpowiedzi i rachunków; 384 decyzje w 8 ramionach po kompozycji split; stare manifesty i błędy zachowane |
| Resume | Osierocona odpowiedź omijana przy istniejącym poprawnym ledgerze | Blokowanie z ledgerem i bez; brak adopcji lub automatycznej ponownej próby; integrator niezależnie potwierdził poprawkę |
| Rachunek historyczny | 992 wiersze, duplikaty i nieprzypisane około1,10 USD | 631 prób,361 kopii;629 hashy odpowiedzi sprawdzonych,628 kosztów zgodnych,0 rozbieżności; lukę1,098135722 USD pozostawiono nieprzypisaną |
| Jev | Profile historyczne42/48 i45/48 | Historyczny replay zachowany; nowe ramiona j_active 40/48, j_directed/j_roles/j_split 43/48 na oglądanym DEV |
| Ekstrakcja | Pierwotny wynik6/60 | Odtworzono6/60;38/60 to osobno nazwany historyczny replay dekodera, nie poprawa modelu |
| Analizy native synthetic DEV | Brak zamrożonego porównania promptów/temperatur | 15 rzeczywistych fikcyjnych gałęzi,4 metody,60 requestów; bez podmiany na inny korpus/gold |
| Mały panel frontier | Starszy panel36 requestów, historycznie8,180672 USD | Nowy etap3:12 requestów/6 par, historycznie1,227694 USD; jakość modelowa niezmierzona |
| Graf kontra tekst+JSON | Brak przyjętego porównania tej pary | Etap4:12 requestów/6 par, historycznie1,003626 USD; mechanika i straty adaptera jawne |
| Metody w grafie | Rozproszone raporty, bez wspólnego eksportu wyników | Eksport wersji/parametrów/promptów i datowanych twierdzeń; faktyczny producent oddzielony od nieuruchomionego modelu |
| Powtórki zwycięzców | Brak jawnej reguły wyboru | Konfigurowalna polityka Pareto i unseen; niepełny billing lub brak jakości nie daje zwycięzcy |

Szczegółowy [plan wydatków](../research/model_research_2026-10-04/BUDGET.md)
zachowuje kolejność:432→60→12→12→powtórki/unseen. Pierwsze cztery etapy to
516 przygotowanych wywołań, historycznie3,1804544 USD. To nie bieżący cennik ani
gwarancja zmieszczenia się w5 €. Przed każdym etapem zapisujemy aktualny plan,
wycenę i stan osobnego konta; po nim koszt rzeczywisty i niepewne rezerwacje.
Nie zakładamy EUR=USD. Tylko skonfigurowany oczekiwany wzrost×10 wymaga dodatkowej
decyzji właściciela.

## Zachowanie historii i granice wyników

Opublikowano pełne archiwum siedmiu kontrprzykładów na
[`archive/gpt/model-research-followup-review-2026-10-04`](https://github.com/klb-t/chatadhd/tree/f14a4035eaad896a9905439fc3a60e46c6b0f1ba).
V3 ma SHA-256 `ec7020cbdd32a69a2ab7c41e1874375882837541f534add8c6c3f7c67764639d`;
zawiera dokładne źródła, wejścia, pierwsze odpowiedzi, wyniki przed/po,
reproduktery i niezmienione poprzednie ZIP-y. Wczesne szkice frontier mają osobne
archiwum prefrezowania. Tip przed rebase5753d2c zachowany na
`archive/gpt/model-research-before-rebase-2026-10-05`.

Wykryte błędy obejmowały niepełny rachunek przy wyborze zwycięzcy, fałszywe
dziedziczenie pochodzenia, mutable Git ref, duplikację ramienia, wycenę innych
bajtów niż wysyłane, niezweryfikowaną transformację odpowiedzi i mianownik
oparty na etykiecie grupy zamiast ID zapytań. Poprawki zachowują oryginały;
nie zmieniają modelowych wyników jakości. DEV jest oglądany i jawnie oznaczony.
Nie czytano `eval/real-holdout-key` ani prywatnych eksportów właściciela.

Nie rozliczono luki historycznej przez zgadywanie: końcowy checkpoint klucza
nie ma potwierdzonej tożsamości, a brakujące generacje wymagają eksportu operatora.
Nierozstrzygnięte0,00738793 USD ze starego klucza to rezerwacje, nie koszt nowego
programu. Native override promptów, produkcyjny zapis metod i ekran ustawień
pozostają własnością wskazanych wątków. Brak bieżącej jakości nie jest oceną0.

## Weryfikacja

[Pełna bramka](../research/model_research_2026-10-04/verification/native-full-20261005/receipt.json):
**108/108 CTest**, 128,72 s, bez pominięć. Wykonano 659 przypadków native z
24 465 asercjami, 1582 przypadki Python i oba zestawy smoke CLI/server. Wszystkie
1041 plików funkcjonalnych i dwie polityki mają hashe zgodne z opublikowanym
kodem `165e307e`; sześć binariów pozostało niezmienionych. Nie zmieniano progów,
timeoutów ani przypadków. Pierwsze dwie pełne bramki 107/108 oraz dokładne źródła
błędów są zachowane w [archiwum](https://github.com/klb-t/chatadhd/tree/6910897eb0630be2946398291a69821ea669e949/docs/research/model_research_2026-10-05-runner-review).
135 testów wykonawcy, 27 wyceny oraz niezależne atrapy sprawdzają również utratę
części historii, błędne pokwitowania, BYOK i wznowienie bez powtarzania POST.

## Do wątku 1

Przyjmij przepisy i wyniki jako dane wersjonowanych metod. Historyczne Jev42/48
i45/48 dotyczą sprawdzania dostarczonych kandydatów, nie dowolnej ekstrakcji.
Nowe Jev mają zmierzony wynik źródłowego commitment. Przekazano osiem
[dokładnych przepisów jako dane](../research/model_research_2026-10-04/stage1-measured-recipes-for-w1-v1.json),
bez przyjęcia do produkcji lub native extraction. Stage2 jest ukończony
mechanicznie: 0/60 przyjętych, bez zmierzonego wyniku semantycznego lub zwycięzcy. Stage2
wymaga native override promptu/parametrów, hashów w fingerprint/provenance i
oddzielenia zgodności schematu od interpretacji; bieżące API/rejestr należy
sprawdzić na twoim aktualnym przyroście.

## Do wątku 3

Konsument rejestru musi zachować rozdział faktycznego instrumentu replay od
planowanej wersji modelu, wszystkie mianowniki i datowane twierdzenia.
Mechaniczne scripted wyniki nie są jakością modelu ani dowodem wykonania
produkcji. Stosuj jeden uzgodniony kontrakt3/4, z pełnymi wersjami i parametrami.

## Do wątku 4

Uzgodnij adapter eksportowanych twierdzeń badawczych do kanonicznego
`loom.method_graph/1` / `loom.method_run_trace/1`; native GraphPacket i klientowy
manifest metod nie są tym samym envelope. Badania używają istniejących pól
ModelProfile zamiast konkurencyjnej metryki. Sam pakiet nie dowodzi przyjęcia
do store lub wykonania rejestru. Pełne consumer/API bramki należą do3/4.

## Do wątku 9

Aktualny milestone: kompaktowy publiczny etap1 `43ed079` oraz dodatkowe
zamrożenie stage5 `cb647034`. Etap1 ma **12** autorskich przypadków DEV,
po cztery pytania: 48 zależnych ocen w każdym z ośmiu ramion. Domyślny protokół
pięciu kryteriów pozostał niezmieniony i wyklucza te wyniki z powodu brakujących
osobnych wymiarów. Jawny dodatkowy preset eksploracyjny po obejrzeniu etapu1
wykorzystuje wyłącznie poprawność etykiety, dostępność i pełny koszt pierwszych
prób; Pareto zachowuje `j_active` i `j_directed`, bez limitu liczby wybranych,
progu lub dopisanych
wyników semantycznych/grounding. **Nowego korpusu nie tworzymy przed zamrożeniem
wszystkich właściwych grup selekcji.** Nie wyciągamy z etykiet wniosków o prawdzie
światowej, mechanizmie rozumowania ani jakości native extraction.

Najnowsze publiczne źródło funkcjonalne `1259d475` (adapter SHA-256 `f92da5c4…`) przeszło **108/108 CTest**,
127,91 s, **2263 przypadki jednostkowe i dwa smoke**; końcowy niezmienny pakiet
dowodowy jest kompletowany. Wcześniejsze pokwitowanie 128,72 s pozostaje osobnym
historycznym zamrożeniem i nie otrzymuje hashów nowszego źródła. Oryginały
pierwszych odpowiedzi, zatrzymań i negatywnych kontrprzykładów pozostają zachowane.
Nieodzyskany stary generic SHA V1 nadal uniemożliwia pełne odtworzenie jego
historycznej orkiestracji; nowy wykonawca nie odzyskuje starego producenta.
Luka **1,098135722 USD** wciąż wymaga dowodu starego klucza, a nie świeżego konta.
Limit operatora pozostał **5 USD** przy zgodzie właściciela na **5 EUR**;
ECB 1,1225 daje równowartość 5,6125 USD, lecz limitu nie podniesiono.

Po końcowych bramkach przyjmij tylko aktualną gałąź po rebase, liniowo; archiwów
negatywnych i szkiców nie włączaj do main. Zaktualizuj INDEX/STATE na podstawie
tego raportu, zachowując wpisy innych wątków. Nie oznaczaj pierwszej odpowiedzi
jako ukończonego badania432 ani metod jako produkcyjnie przyjętych bez
uzgodnionego konsumenta3/4. Osobny klucz dostarczono; aktualny stan i koszt
są w planie wydatków powyżej.
