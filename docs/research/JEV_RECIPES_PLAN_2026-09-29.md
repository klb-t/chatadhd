# T3 — Zamrożony plan receptur Jev (2026-09-29)

[U] Zakres: T3 z `docs/GPT_OFFLINE_TASKS_2026-09-29.md`, odczytanego na
`4ba481e1c8a39f547e3d7f1fb3d1f24891dd9d67`. [P] To projekt eksperymentu i
pakiet wejść, **nie pomiar Jev**. Wywołania modeli: 0. Aktywacja i budżet live
pozostają nieustawione. Nie zmieniać progów produkcyjnych ani STATE.

## Podstawa i granica wniosków

Audyt `JEV_OFFLINE_POLICY_AUDIT_2026-09-29.md` pokazał, że q01 mieszało wymóg
jawnych ról P/Q z wykrywaniem sensownej abstrakcji. Oddzielamy dwa cele bez
wymagania dosłownych symboli. Materiał Gemini „Wydajność i optymalizacja zapytań
dla modelu klasyfikacyjnego jev.docx” dostarczono w rozmowie, nie w tej gałęzi.
Jego mocne tezy są hipotezami; nie kopiujemy ich jako gwarancji API.

Kontrakt sprawdzono w statycznej dokumentacji 2026-09-29 (bez inferencji):
- https://docs.typesafe.ai/api — state; niezależne questions; Noul/Choice/Score;
  instructions i criteria mogą zawierać opisy strukturalne. **Identyfikatory pytań
  nie są przekazywane modelowi**. Nazwy zagnieżdżonych pól instrukcji to odrębna
  kwestia; dodano kontrolę ujemną zmiany samych identyfikatorów pytań.
- https://docs.typesafe.ai/confidence — confidence zależy od rozkładu; nie jest
  niezależnym dowodem empirycznej poprawności. Nie zakładamy formuły z Gemini.
- https://openrouter.ai/docs/api/api-reference/alphadecisions/submit-a-decisions-request
  — POST `/api/alpha/decisions`, model `typesafe/jev-1.13`, body state/questions.

[P] API może się zmienić: przyszły wykonawca potwierdza obsługę bez zmieniania
zamrożonego eksperymentu po zobaczeniu odpowiedzi. Zmiana modelu, endpointu lub
formatu tworzy nową wersję manifestu wykonawczego. Ta kontrola lokalna sprawdza
jedynie używany podzbiór kontraktu; nie zastępuje odpowiedzi rzeczywistego API.

## Korpus i autorstwo

32 nowo napisane fikcyjne przypadki, 16 PL / 16 EN: 12 relacji, 8 routingu,
12 kontekstu. Nie wykorzystano starych 64 tekstów jako nowych obserwacji.
To **ten sam autor ChatGPT co narzędzia**, nie niezależny człowiek ani ślepy
sprawdzian. Przypadki językowe i konstrukcyjne są podobne, więc nie zakładamy
32 niezależnych losowych obserwacji. Przed wnioskami produkcyjnymi potrzebny jest
niezależny autor, przegląd etykiet i nowy sprawdzian na naturalnych materiałach.

`cases.json` zawiera wyłącznie dane wejściowe i identyfikatory zarządcze.
`gold.json` osobno przechowuje ręczne etykiety oraz uzasadnienia. Generator
requestów **nie otwiera gold**; do body nie trafiają family, language, case_id,
arm, etykiety ani uzasadnienia. Nie są to prawdziwe eksporty właściciela.

## Projekt porównań — przed wynikami

### A. Relacja wyrażona kontra abstrakcja

Te same przypisane role/kierunek/zakres, dwa niezależne Noul w jednym requestcie.
`expressed`: źródło ustanawia relację, także zwykłą parafrazą; pytanie/cytat
odrzuconej zasady nie wystarcza. `abstraction`: dopuszczalna hipoteza robocza
wsparta materiałem albo relacja bezpośrednio wyrażona. Wynik drugiego nie promuje
pierwszego. Etykiety obejmują (1,1), (0,1), (0,0); nie twierdzimy, że wszystkie
możliwe zestawy są równoliczne. „Sensowność” hipotezy jest oceną autora, wymaga
zewnętrznego sprawdzenia, nie matematycznym dowodem.

### B/C. Klucze JSON i struktura rubryki

Każdy przypadek relacji/kontekstu ma pięć ramion:
1. string — te same trzy fragmenty instrukcji połączone nowymi liniami;
2. object_meaningful — klucze question/scope/decision_rule;
3. object_neutral — field_a/field_b/field_c;
4. object_nonsense — zxq/uvj/kpt;
5. structured_criteria — instrukcja jak 2, każde kryterium w obiekcie {rule: tekst}.

Kontrasty 2/3/4 zmieniają tylko nazwy kluczy, zachowując wartości i kolejność.
Kontrast 1/2 bada opakowanie wraz z semantyką nazw; nie dowodzi czystego efektu
JSON bez dodatkowych tokenów. Kontrast 2/5 izoluje sposób opisu kryterium.
Nie zmieniamy wymaganych pól API ani Noul true/false. Ich zmiana byłaby innym
kontraktem, nie eksperymentem z nazwami. Qid-control relacji zmienia wyłącznie
transportowe identyfikatory pytań; porównanie mapuje je z powrotem po pozycji.

### D. Choice płaski i hierarchiczny

Osiem jasno rozstrzygalnych zadań, osiem kategorii (po cztery repo/laboratorium)
oraz `other`. Zbiory 3/5/9 są zagnieżdżone i zawierają etykietę referencyjną
**z konstrukcji korpusu**. To badanie dystraktorów przy zadanym zbiorze,
nie pomiar jakości generowania kandydatów. Nie ma dodatnich przypadków `other`;
nie wolno wyprowadzać z tego skuteczności abstencji. Język rozłożono po obu
dziedzinach; próba jest za mała na ranking języków.

Płaski Choice-9 dodatkowo w odwróconej kolejności. Pozycje opcji są rotowane
między przypadkami. Hierarchia: rzeczywista odpowiedź root wybiera repo/lab/other;
**dopiero wtedy** właściwy przygotowany child. Child nie otrzymuje złotej trasy.
Błędny root liczy się do wyniku całego procesu. Nie podawać wyłącznie celności
child na poprawnie wybranych gałęziach. `other` kończy ścieżkę bez child;
niepoprawna odpowiedź/remis wymaga review i nie uruchamia gałęzi.

### E. Przydatność i minimalna reprezentacja

Relacja do celu: jeden Noul. Subgrafy: trzy niezależne Noul; wiele/żaden może
być trafny. Identyfikatory kandydatów rotowane między przypadkami. Detail:
Score z pięcioma uporządkowanymi poziomami 0 omit, 1 label, 2 summary,
3 implementation/content, 4 raw+locator. W state faktycznie znajdują się
reprezentacje kumulatywne. Badamy minimalny wystarczający pakiet dla zadanego
celu, nie ilość szczegółów generowanej odpowiedzi. Miniaturowy kod i lokalizatory
są fikcyjne, nie stanowią benchmarku naprawy prawdziwej aplikacji.

## Pakiet i harmonogram

188 gotowych body: 72 relacje (12 × 6), 60 kontekst (12 × 5), 56 routing
(8 × 7, w tym 16 alternatywnych children). Przy wykonaniu wszystkich ramion
**maksymalnie 180 wywołań**, bo z każdej pary children wybierane jest najwyżej
jedno; dla `other`/błędu mniej. To liczby planu, nie wykonane inferencje.

Eksport zawiera osobne pliki body JSON i INDEX z zależnościami i deterministycznie
przetasowaną kolejnością (seed `jev_recipes_v1-fixed-20260929`). Wykonawca
odracza children do zakończenia ich root. **Nie wysyła całego INDEX ani koperty
trial jako body.** `examples.json` zawiera gotowe reprezentatywne body w repo;
pełny zestaw odtwarza generator, jego hash jest zamrożony.

Przed live zamrozić wybrany podzbiór trial IDs, model rzeczywiście zwrócony,
provider/routing, limit wydatków, limit prób/czasu i wersję wykonawcy. Przegląd
nie oznacza zgody na wydatki. Wygenerowanie wszystkich plików nie uprawnia do
wysłania wszystkich children. First responses zachować; błąd/timeout i koszt
nieznany nie mogą znikać z raportu. Retry to jawny nowy eksperyment.

## Plan analizy

[P] Jednostka podstawowa: przypadek, kontrasty sparowane na tym samym wejściu.
Raportować osobno rodziny, pytania, PL/EN, ramiona i liczbę faktycznie poprawnych
odpowiedzi API. Brak odpowiedzi ≠ NIE. Replikacje techniczne nie dodają nowych
przypadków. Brak nowych danych oznacza brak wyniku, nie zero błędów.

Noul: macierz pomyłek i precision/recall/F1 z mianownikami przy z góry ustalonym
progu 0,5, Brier, różnica p i zmiana decyzji między ramionami. Dodatkowo opisowo
pasmo review (0,2;0,8), bez strojenia go po wynikach. Główne pytanie A to
odróżnianie wyrażenia od dopuszczalnej hipotezy, nie pooled accuracy dominowana
przez łatwe NIE. Podawać każdą niezgodność etykiety wraz z przypadkiem; rewizja
etykiety tworzy wersję v2, oryginalny wynik pozostaje.

Subgrafy: wyniki per kandydat i dokładny zbiór trafnych kandydatów per zadanie;
liczba pominiętych niezbędnych informacji. Nie sumować prawdopodobieństw Noul
jak rozkładu wykluczających się klas. Detail: MAE oczekiwanego score oraz
rozkład/modalna klasa, osobno niedoszacowanie i nadmiar szczegółów. Żądanie
najwyższego poziomu nie jest domyślnie poprawne.

Choice: końcowa trafność liścia, pomyłki root, niepoprawne/missing odpowiedzi,
wywołania na przypadek, sumaryczny koszt i czas całej ścieżki. Przegrana na root
nie może być pominięta. Prawdopodobieństw warunkowego child nie porównywać
bezpośrednio z globalnym flat. Nie zakładać, że ich iloczyn jest skalibrowanym
prawdopodobieństwem. Liczba opcji i długość payloadu są zapisane jako czynniki.

Mała autorska próba służy diagnostyce, nie istotności statystycznej i nie rankingowi
modeli. Nie wybierać zwycięzcy na podstawie minimalnego p-value spośród ramion.
Przed promocją receptury: nowy niezależny korpus, zamrożona receptura, taki sam
kontrakt i ocena downstream; wzrost modelowego confidence sam nie jest sukcesem.

## Freeze / odtwarzanie

`manifest.json` wiąże SHA-256 przypadków, gold, tego planu, kodu generatora,
przykładów i całego deterministycznego zestawu triali. Manifest nie hashuje siebie.
Zmiana któregokolwiek wejścia wymaga nowej wersji, a nie cichej aktualizacji
po odpowiedziach. Testy są testami kontraktów/preparacji, nie wynikami modelu.

```sh
python -B loom/tools/eval/jev_recipes.py
python -B -m unittest discover -s loom/tools/eval -p test_jev_recipes.py -v
python -B loom/tools/eval/jev_recipes.py --export /tmp/jev_recipes_v1_requests
```

Katalog eksportu musi być nowy. Nie ma opcji wykonania ani kodu sieciowego.
