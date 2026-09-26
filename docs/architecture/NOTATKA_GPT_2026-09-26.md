# Notatka dla agenta ChatADHD / Loom — 2026-09-26

> Źródło: rozmowa właściciela z ChatGPT, przekazana agentowi 2026-09-26.
> Zapisana dosłownie. Podsumowanie wymagań: `OWNER_REQUIREMENTS_2026-09-26.md` (R13).

## 1. Najważniejsza korekta interpretacyjna

Ostatnia sesja Claude'a dotycząca nowych kierunków zakończyła się zasadniczo na zapisaniu wymagań i kierunku architektonicznego. Subagenci zostali uruchomieni, ale limit sesji najpewniej skończył się, zanim zdążyli dostarczyć istotne rezultaty implementacyjne.

Nie należy traktować obecnego stanu jako gotowej implementacji paradigm/self-discovery engine. Obecne "OWNER_REQUIREMENTS_2026-09-26.md" należy traktować jako wejście do kolejnego etapu.

Claude sam zwrócił wcześniej uwagę, że inteligentny, bardzo oszczędny dobór kontekstu nie eliminuje korzyści z prompt cache.

Prawidłowa zasada:

> Intelligent context selection and prompt caching are complementary.

Nawet przy bardzo małym, precyzyjnie dobieranym kontekście pewien początek będzie często niemal identyczny:

- system prompt,
- podstawowa konstytucja projektu,
- główne invarianty,
- najważniejsze zasady,
- podstawowe preferencje użytkownika,
- stabilna część modelu projektu.

Dlatego context assembler powinien preferować:

1. stabilny prefix,
2. wolniej zmieniający się project context,
3. dynamiczny goal-specific tail.

Graf ma przede wszystkim minimalizować ilość informacji potrzebnej do poprawnego rozumowania, natomiast caching ma zmniejszać koszt tej części, która mimo optymalizacji pozostaje stabilna.

---

## 2. Głębszy sens całego ekosystemu

ChatADHD, Loom, LEM, Watchdog, projekty naukowe i inne przedsięwzięcia nie powinny być traktowane jako luźny zbiór projektów.

Są manifestacjami jednego głębszego procesu:

> obserwacje → stan epistemiczny → uogólnienia → decyzje / działania / produkty.

Przybliżony model:

"raw observations / conversations / code / literature / events"

→ "epistemic state"

→ "generalizations / principles / structures / relations"

→ "actions / experiments / software / documents / strategies"

Kluczową własnością użytkownika jest silna tendencja do szukania reprezentacji, która pozwala skompresować wiele szczegółowych przypadków za pomocą niewielkiej liczby zasad generujących.

Nie należy więc ograniczać self-discovery do wydobywania:

- feature'ów,
- decyzji,
- komponentów,
- TODO.

Znacznie cenniejszym wynikiem jest znalezienie generatora decyzji.

---

## 3. Filozofia projektowania — poziom ogólny

Dotychczasowa „filozofia kodowania" jest tylko lokalną manifestacją szerszej filozofii rozumowania i działania.

Dobre syntetyczne sformułowanie:

> Maksymalizuj to, co można wyrazić jako niezmienną transformację, a różnice przenoś do danych, metadanych i konfiguracji.

Jeszcze bardziej fundamentalnie:

> Szukaj takiej reprezentacji problemu, żeby różnorodność świata była danymi, a kod opisywał tylko uniwersalne operacje.

Praktyczny test architektoniczny:

> Jeśli nowy przypadek wymaga nowej gałęzi kodu, najpierw sprawdź, czy naprawdę nie powinien być nową wartością danych.

To nie oznacza zakazu special-case'ów. Special-case jest dopuszczalny, jeśli różnica jest rzeczywiście semantyczna i nie daje się uczciwie przenieść do danych lub opisu capability.

---

## 4. Te same zasady obowiązują poza programowaniem

Nie tworzyć osobnych modeli:

- „jak użytkownik koduje",
- „jak użytkownik prowadzi research",
- „jak użytkownik analizuje sprawę prawną",
- „jak użytkownik organizuje projekt".

Należy poszukiwać wspólnego modelu wyższego rzędu.

Użyteczna hierarchia:

### Values

Co użytkownik uznaje za warte ochrony lub maksymalizacji.

Przykładowe klasy:

- prawda,
- autonomia,
- przejrzystość,
- uczciwość,
- zachowanie informacji,
- minimalizacja arbitralności,
- zachowanie opcjonalności.

### Epistemic principles

Jak należy konstruować model rzeczywistości.

Przykłady:

- provenance,
- oddzielanie faktu od inferencji,
- jawna niepewność,
- zachowanie alternatywnych hipotez,
- szukanie kontrprzykładów,
- unikanie nadmiernej generalizacji,
- aktualizowanie modelu po nowych danych.

### Action/design strategies

Jak powyższe zasady przekładają się na konkretne działania.

Przykłady:

- modularizacja,
- capability interfaces,
- KOD ≠ DANE,
- experiment selection,
- analiza dowodów,
- śledzenie podstaw prawnych,
- odwołanie od decyzji,
- zachowanie immutable originals,
- graceful degradation,
- odraczanie niepotrzebnych decyzji.

W ten sposób software, research i działania administracyjno-prawne mogą być modelowane jako różne instancje tej samej polityki rozumowania.

---

## 5. LEM jako model rozumowania użytkownika

LEM nie powinien być traktowany jako osobny eksperyment reprezentacyjny.

Jest potencjalnie formalną warstwą opisującą sposób, w jaki użytkownik:

- organizuje wiedzę,
- określa pewność,
- zachowuje provenance,
- rozdziela opis od normatywności,
- lokalizuje wartości,
- tworzy uogólnienia,
- przechodzi od wiedzy do działania.

Centralna intuicja:

> myśl w odpowiednio zaprojektowanej przestrzeni może wymagać znacznie mniej informacji niż jej pełna werbalizacja.

Ważny mechanizm kompresji:

Nie przechowuj wszystkich rezultatów rozumowania, jeśli można przechować:

"state + generators + exceptions + provenance"

i zrekonstruować pozostałe konsekwencje.

Analogicznie do programu:
nie zapisuje się miliona wyników funkcji, jeśli można zapisać funkcję i parametry.

---

## 6. Synchronizacja reprezentacji z domeną

Model nie powinien narzucać wszystkim domenom identycznej ontologii.

Powinien zachować wspólną strukturę epistemiczną, ale lokalnie dostosowywać reprezentację do opisywanego systemu.

Przykłady:

### Software

- modules,
- interfaces,
- data,
- protocols,
- capabilities,
- invariants,
- deployment targets.

### Research

- hypotheses,
- observations,
- experiments,
- confounders,
- falsifiers,
- competing explanations.

### Legal / administrative case

- events,
- evidence,
- institutions,
- actors,
- legal norms,
- claims,
- deadlines,
- procedures.

### Film

- characters,
- motivations,
- scenes,
- plot relations,
- timeline,
- visual/audio artifacts.

Wspólny rdzeń pozostaje podobny:

> czym to jest → skąd to wiadomo → z jaką pewnością → od czego zależy → co temu przeczy → co z tego wynika → czego jeszcze brakuje.

---

## 7. Najważniejsze dane do wydobycia z archiwów

Nie ograniczać się do jawnych zasad.

Szczególnie poszukiwać:

**A. Invariants** — zasady prawie zawsze obowiązujące.

**B. Heuristics** — zasady skuteczne zazwyczaj, ale nie absolutne.

**C. Defaults** — preferowane ustawienia, gdy brak innych przesłanek.

**D. Meta-principles** — zasady mówiące, jak tworzyć nowe zasady.

**E. Conflict resolution rules** — jak użytkownik rozstrzyga konflikty między wartościami lub zasadami.

**F. Transformation operators** — powtarzalne wzorce przejścia od problemu do rozwiązania.

Przykłady kandydatów:

"nowe źródło danych"
→ nie twórz osobnego systemu
→ rozszerz provider/capability abstraction.

"nowy typ artefaktu"
→ znajdź wspólną strukturę
→ zachowaj specyficzne własności jako dane.

"powtarzalna ręczna czynność"
→ zapewnij obserwowalność/provenance
→ dopiero potem automatyzuj.

"niepewność"
→ nie wybieraj arbitralnie
→ zachowaj alternatywy wraz z evidence.

"różnica wyłącznie implementacyjna"
→ nie propaguj jej do wyższych warstw abstrakcji.

To właśnie te operatory pozwolą systemowi generować nowe rozwiązania, których nigdy explicite nie zapisano.

---

## 8. Self-discovery powinno odkrywać generator, nie tylko historię

Docelowy efekt self-discovery:

nie tylko:

> „użytkownik wcześniej zrobił X"

ale również:

> „z zasad A, B i C użytkownik w sytuacji typu D zwykle dochodzi do klasy rozwiązań E".

Należy więc reprezentować zasady jako obiekty z polami podobnymi do:

- statement,
- scope,
- provenance,
- evidence_for,
- counterexamples,
- confidence,
- exceptions,
- derived_from,
- predicts,
- conflicts_with,
- supersedes,
- validation_status.

Nie należy tworzyć jednej dogmatycznej „prawdziwej filozofii użytkownika".

Dla tych samych danych może istnieć kilka konkurencyjnych modeli.

Traktować je podobnie do hipotez.

---

## 9. Najważniejszy benchmark dla modelu użytkownika

Bardzo wartościowy test:

**Temporal holdout / predictive reconstruction**

1. Daj systemowi archiwum tylko do daty "T".
2. Każ mu wyindukować:
   - filozofię,
   - zasady,
   - model projektu,
   - przewidywane następne decyzje.
3. Ukryj późniejszą historię.
4. Porównaj przewidywania z rzeczywistymi decyzjami po "T".

Nie mierzyć wyłącznie trafności nazw feature'ów.

Sprawdzać, czy system przewidział:

- strukturę abstrakcji,
- rodzaj generalizacji,
- zachowanie opcjonalności,
- provenance,
- provider abstractions,
- context selection,
- data-driven project kinds,
- kolejne eksperymenty badawcze.

Jeżeli model potrafi przewidywać późniejsze decyzje projektowe, oznacza to, że nauczył się generatora, a nie tylko wykonał dobre streszczenie historii.

---

## 10. ChatADHD jako learned personal design calculus

Docelowy ChatADHD nie powinien być tylko:

> chatem z bardzo dobrą pamięcią.

Lepsze określenie:

> learned personal design / reasoning calculus.

System uczy się:

- nie tylko co użytkownik wie,
- nie tylko co użytkownik chce,
- ale przede wszystkim jak z przesłanek tworzy nowe struktury, decyzje i działania.

Conversation jest jednym z wejść do tego procesu.

Produkty są materializacją aktualnego stanu modelu.

---

## 11. Relacja ChatADHD / Loom / LEM

Robocze rozdzielenie odpowiedzialności:

**LEM** — reprezentacja epistemiczna i operacje na stanach wiedzy / wartości / provenance.

**Loom** — runtime i mechanizm transformacji:

- storage,
- graph,
- execution,
- materialization,
- project compiler,
- context engine,
- agent execution,
- artifact pipelines.

**ChatADHD** — warstwa interakcji i synchronizacji modelu:

- conversation,
- preference learning,
- corrections,
- intent,
- project navigation,
- contextual interaction,
- steering.

Granice te nie muszą być sztywne — ważniejsze jest zachowanie semantycznego podziału odpowiedzialności.

---

## 12. Wartości jako warstwa modelu

Projekty nie wynikają wyłącznie z potrzeb funkcjonalnych.

Często są konsekwencją ochrony lub realizacji wartości.

Dlatego model użytkownika powinien przechowywać również zależność:

"world state / claim"

→ "epistemic interpretation"

→ "affected values"

→ "possible actions"

→ "trade-offs"

To nie powinien być prosty scalar utility score.

Wartości mogą:

- działać lokalnie,
- pozostawać w konflikcie,
- mieć różne zakresy,
- zmieniać priorytet zależnie od sytuacji.

System powinien móc wyjaśnić:

> ta decyzja wynika z zasady X, która chroni wartość Y, przy ograniczeniu Z.

---

## 13. Ważna zasada implementacyjna: inferencja ≠ fakt

Wszystko, co system rekonstruuje z filozofii, musi być jawnie oznaczone jako inferencja.

Każdy inferowany element powinien mieć przynajmniej:

- provenance,
- inference method,
- confidence,
- expected property,
- supporting principles,
- competing alternatives.

Dotyczy to:

- brakujących komponentów,
- potencjalnych feature'ów,
- modeli projektów,
- zasad użytkownika,
- nowych hipotez naukowych,
- interpretacji dokumentów,
- przewidywanych działań.

---

## 14. Co z tego powinien zrobić agent

Nie traktować powyższego jako listy feature'ów do implementowania 1:1.

Najpierw należy zbudować spójny model pojęciowy, który pozwoli reprezentować:

- values,
- epistemic principles,
- design principles,
- transformation rules,
- project paradigms,
- artifacts,
- morphisms,
- provenance,
- inference,
- confidence,
- conflicts,
- scopes,
- exceptions,
- preferences,
- context relevance.

Dopiero później rozdzielać implementację między agentów.

Kluczowy warunek:

> wszyscy agenci muszą operować na tej samej semantyce pojęć.

Unikać sytuacji, w której każdy agent tworzy własne znaczenie:
"paradigm", "artifact", "role", "principle", "morphism", "inference", "context".

---

## 15. Kierunek nadrzędny

Najkrótsze podsumowanie całej rozmowy:

> Ekosystem ma nauczyć się nie tylko historii projektów użytkownika, lecz zasad generujących jego sposób rozumowania i działania; następnie wykorzystać te zasady do kompresji wiedzy, rekonstrukcji brakujących struktur, inteligentnego doboru kontekstu i samodzielnego rozwijania projektów zgodnie z wartościami, epistemologią i preferencjami użytkownika.

Ostateczny cel nie jest „automatyzacją kodowania".

Celem jest:

> automatyzacja przejścia od danych i doświadczeń do uogólnień, a od uogólnień do spójnych działań i produktów.
