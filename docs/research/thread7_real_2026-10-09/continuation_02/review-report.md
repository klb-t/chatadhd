# C / kontynuacja 02 — przegląd źródeł i granic wykonania

Przegląd wykonał drugi agent tej samej sesji. Jest to kontrola interpretacji i kodu,
nie niezależna adjudykacja ani zatwierdzenie sądów przez właściciela. Surowe
fragmenty, wskaźniki i szczegóły przypadków pozostają w prywatnym checkpointcie.
Powiązania i zakres zawiera `review-receipt.json`.

## Adnotacje

Przeczytano własne wypowiedzi użytkownika w dwóch powierzonych rodzinach
oraz wskazano 143 kandydatów. Jeden długi blok stanowił jawnie cytowany
transkrypt zewnętrzny: skontrolowano jego przypisanie i granice, po czym
wykluczono go jako źródło deklaracji właściciela. Nie udawano semantycznej
adjudykacji każdego zdania podcastu.

Następnie skontrolowano wszystkie 115 kandydatów wskazanych przez drugiego
autora: literalność, mówcę, kontekst, zakres i niepewność. Zaproponowano 12
zmian, głównie pełniejsze cytaty zachowujące warunek oraz status uncertain dla
nieprzyjętych propozycji. Drugi autor analogicznie sprawdził 143 kandydatów
tego recenzenta. To dwie passy pracy tej sesji, nie 258 niezależnych obserwacji.
Sprawdzono także 61 znormalizowanych relacji między dowodami: zachowują
rodziny i zakres historycznej interpretacji; nie ustanawiają aktywnego profilu.

Lokalne instrukcje redagowania dawnej wiadomości pozostają historycznym
materiałem o określonym zakresie. Nie stają się automatycznie aktualnymi
preferencjami dla dowolnego zadania ani profilem globalnym. Korekty faktów,
cytaty innych osób, hipotezy i niepewne propozycje są odróżnione od preferencji.

## Zadania retrieval

Przed uruchomieniem rankingu skontrolowano interpretacje 48 zadań, 67
literalnych fragmentów i 12 hashy źródeł. Pełny tekst dwóch rodzin sprawdzono
również pod kątem zadeklarowanych braków odpowiedzi; rzeczywisty przypadek
gałęzi ma wspólnego natywnego rodzica. Pozostałe zadania skontrolowano na
podstawie pytania, dopuszczalnej odpowiedzi i bezpośrednich dowodów; nie
przypisuje się temu drugiego wyczerpującego odczytu każdej wiadomości archiwum.

Wykryto i poprawiono zbyt wąski gold: wcześniejsza wypowiedź zawierała tę samą
wymaganą definicję checkpointu co późniejsza, lecz była oznaczona jako błędna
wersja. Zamrożony zbiór przyjmuje trzy równoważne zbiory dowodów. Odróżniono
też polecenie przeniesienia dygresji od obserwowanego przejścia po gałęzi.

Obecność wcześniejszej wersji razem z nową w kontekście nie dowodzi błędnego
wykonania instrukcji. Metryki dotyczą wyboru oznaczonych dystraktorów i
zachowania dowodów, nie jakości nieistniejącej odpowiedzi LLM. P@k dotyczy
rankingu przed budżetem; wybrany kontekst ma oddzielne metryki. Pusty wybór ma
nieokreślone precision, a alternatywne wystarczające dowody ocenia się osobno
od pokrycia ich pełnej sumy. Poprawkę analizy wykonano przez replay zapisanych
rankingów, bez ich ponownego strojenia ani nowych pomiarów czasu.
Rozbieżność pierwotnego opisu P@k zamknięto osobnym
`retrieval-task-protocol-erratum-v1.json`; dawnych bajtów protokołu nie zmieniono.

Wyniki metod zostały udostępnione recenzentowi dopiero po zamrożeniu ocen.
Nie zmieniano później gold w reakcji na te wyniki. Ekspozycja autorów na
rodziny źródłowe i ukierunkowany dobór pytań wykluczają niezależny holdout.

## Recovery

Dwa konkretne błędy odtworzono kontrolowanym transportem:

- niewiarygodny czas zewnętrznego capture nie blokował rozliczenia; obecnie
  wymagana jest kolejność start próby ≤ capture ≤ czas sprawdzenia;
- dowolny komunikat wyjątku zapisany małymi literami i podkreśleniami przechodził
  filtr diagnostyczny; obecnie dopuszczane są dokładne kody, pozostałe komunikaty
  zastępuje stała bez prywatnej treści.

Testy obejmują regresje obu ustaleń. Zachowano brak ponownego POST, pierwotne
bajty dowodów oraz unknown bez wystarczających danych. Hash wiąże bajty;
przypisanie niezależnego capture do próby pozostaje jawną granicą zaufania
w procedurze przeglądu, a nie podpisem dostawcy.

## Testy i granice

Uruchomiono 70/70 testów mechaniki trzech modułów: recovery 26, retrieval 23,
adnotacje 21. Pełny log: `review-focused-tests.log.gz`. Jest to powtórna kontrola
istniejących testów; nie należy dodawać 70 do liczby unikalnych testów projektu.
Późniejsze testy autorów i końcowa bramka projektu mają własne logi; zapisanych
70 przypadków nie przedstawia się jako testu każdej późniejszej zmiany.

Nowe wywołania modeli: 0. Koszt: 0 USD. Przegląd nie dowodzi jakości odpowiedzi
modeli, prawdziwości twierdzeń prywatnych rozmów, dokładnie jednego inference,
odporności dysku na utratę zasilania ani kompletności całego archiwum.
