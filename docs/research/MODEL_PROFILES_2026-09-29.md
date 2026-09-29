# Model modeli — profile warunkowe z istniejących wyników (T4)

[U] R37 i T4. [P] Schemat/projekcja oraz testowalna instrukcja kompensująca.
**Nie wykonano nowej inferencji, nie zmieniono routingu ani wiedzy kanonicznej.**
Baza: `494b164c4515b875014b3cfd1c3760de23bda401`.

## Kontrakt i pliki

`docs/contracts/model_profile.schema.json` (draft 2020-12) opisuje kolekcję
ModelProfile; `loom/tests/fixtures/model_profiles_v1.json` zawiera **14 profili**:
12 osobnych pytań Jev oraz dwa profile natywnej generacji grafu.
`loom/tools/eval/model_profiles.py` je odtwarza i waliduje.

To materializowane projekcje istniejących źródeł i przyszłych obiektów Model/claim,
nie nowy produkcyjny magazyn wiedzy. Schemat nie dodaje EvidenceClass ani Role.
Klucz profilu: model/wersja × operacja × domena × kształt wejścia × receptura.
Źródła, okres zastosowania, mianowniki, failure modes i ograniczenia są oddzielne.
Zmiana modelu, providera lub receptury nie dziedziczy automatycznie ocen.

## Źródła i poziom weryfikacji

Wyciąg `loom/tests/fixtures/model_profiles_v1/source_extract.json` zawiera
transkrypcję wybranych pól z odczytanych źródeł, ze ścieżkami, git blob SHA,
punktami/sekcjami pochodzenia. **Nie jest kopią pełnego oryginalnego JSON ani
ponowną analizą surowych odpowiedzi.** Generator ma dodatkowy tryb weryfikacji
oryginałów dostępnych w pełnym repo. Nie wykonano go tutaj, bo lokalny checkout
jest częściowy; brak oryginału daje jawny błąd, nie pozorne potwierdzenie.

Źródła: [agregaty Jev](inputs/jev-structure-analysis-2026-09-28.json),
[wyniki Jev](JEV_RESULTS_2026-09-28.md),
[oryginalne pytania](../../loom/tests/fixtures/eval/jev_structure_pilot_v1/inputs.json),
[połączona kontynuacja natywna](OPENROUTER_NATIVE_REMAINDER_2026-09-28.md),
[historyczna weryfikacja natywna](LOCAL_NATIVE_VERIFICATION_2026-09-28.md),
[R37](../architecture/OWNER_REQUIREMENTS_2026-09-26.md).

## Jev: nie jedna liczba, tylko profil każdego pytania

Zwrócony identyfikator w raporcie: `typesafe/jev-1.13-20260917`, provider TypeSafe.
Każde pytanie ma 64 decyzje przy starej rubryce, nie 64 niezależne obserwacje:
teksty mają wspólne rodziny, tłumaczenia i warianty. Rozdzielić strukturę stwierdzoną
od sensownej abstrakcji (T3); nie reinterpretować wstecznie starych etykiet.

| Pytanie | Operacja (robocza nazwa) | Poprawne / dostępne | Precyzja TAK | Brier | Błędy / pozostawione, pasmo 0,2–0,8 |
|---|---|---:|---:|---:|---:|
| q01 | asserted_conditional_forward | 50/64 | 0.3000 | 0.11917 | 5/46 |
| q02 | asserted_conditional_reverse | 64/64 | 1.0000 | 0.02720 | 0/51 |
| q03 | endorsed_universal_claim | 63/64 | 0.9231 | 0.02844 | 0/50 |
| q04 | endorsed_existential_claim | 64/64 | 1.0000 | 0.00888 | 0/64 |
| q05 | sample_to_universal_generalization | 64/64 | 1.0000 | 0.02159 | 0/55 |
| q06 | negated_joint_truth | 64/64 | 1.0000 | 0.00971 | 0/62 |
| q07 | evidence_in_favor_of_target | 58/64 | 0.7000 | 0.07616 | 0/35 |
| q08 | universal_counterexample | 64/64 | 1.0000 | 0.00270 | 0/64 |
| q09 | distinct_system_analogy | 64/64 | 1.0000 | 0.00803 | 0/63 |
| q10 | asserted_identity | 64/64 | 1.0000 | 0.00497 | 0/62 |
| q11 | asserted_causation | 59/64 | 0.5455 | 0.04043 | 0/57 |
| q12 | positive_inclusive_disjunction | 64/64 | 1.0000 | 0.01113 | 0/64 |

Wskaźniki precyzji/recall/Brier są **raportowane**, nie przeliczone z surowych
probabilistycznych odpowiedzi. Dla q01 znamy 6/20 odpowiedzi dodatnich; dla innych
pytań nie załadowano liczebności dodatnich etykiet — pola mianowników pozostają
`null`, zamiast zgadywania. Accuracy i liczby pozostawionych ocen są odtwarzane
z liczników/udziałów z kontrolą zgodności liczb całkowitych.

26/768 niezgodności, w tym q01=14, q03=1, q07=6, q11=5. Przy pasmie pozostaje
673 ocen i pięć niezgodności q01 (p=0,81–0,86, development). Nie interpretować
post hoc kwarantanny q01 jako zwalidowanej poprawy. Wyjaśnienie o literalnych
rolach P/Q jest hipotezą dotyczącą rubryki i interpretacji, nie ogólnym osądem
kompetencji modelu. Zero zaobserwowanych błędów w innych pytaniach nie jest
certyfikatem bezbłędności.

ECE i kalibracja na nowej populacji są **niedostępne**. Znany Brier nie uprawnia
do zaznaczenia `empirically_certified=true`. Łączny koszt USD 0,005341182 i latencje
zachowano na poziomie wspólnych 64 requestów; nie dzielimy ich przez 12,
żeby wymyślić koszt/latencję niezależnego pytania.

## Natywna generacja grafu: inny rodzaj zadania

Użyto połączonego raportu dwóch rozłącznych przebiegów jednego planu:
**32 zaplanowane, 22 rozpoczęte, 20 odpowiedzi, 2 niepewne, 10 nietkniętych.**
Nie dodajemy mianowników 32+26. Pełnego kontraktu nie spełnił żaden z 20
zwróconych kandydatów. Nie ma zatem podstaw do estymowania semantycznej trafności
warunkowej na poprawnym kontrakcie — `null`, nie „0% rozumienia”.

| Instrument/requested identifier | Provider | Odpowiedzi / plan | Powtórzone klucze JSON | Inne odrzucone kontrakty | Niepewne / nietknięte | Zwrócony koszt USD |
|---|---|---:|---:|---:|---:|---:|
| `openai/gpt-4.1-mini` | `openai` | 11/16 | 6 | 5 | 0 / 5 | 0,0222088 |
| `qwen/qwen3-30b-a3b-instruct-2507` | `siliconflow/fp8` | 9/16 | 0 | 9 | 2 / 5 | 0,00396339 |

Identyfikatory modeli z tabeli nie są dowodem dokładnego snapshotu wersji
serwera; `version=null` i `requested_identifier_only` zachowują to ograniczenie.
Zebrane pierwsze błędy kontraktu: quote_mismatch9, handle2, subject1,
scope_membership1, utf8_boundary1. Nie przypisujemy tych połączonych liczników
poszczególnym modelom bez danych na poziomie odpowiedzi.

USD 0,02617219 to suma znanych usage, nie pełne rozliczenie konta. Rezerwacja
USD 0,00602193 za dwie niepewne próby nie jest rzeczywistym kosztem ani
potwierdzeniem, że koszt był zerowy. Native_v1 stawia wymogi uchwytów, zakresów
i UTF-8, których nie ma Jev Noul: tych wyników nie używać do rankingu modeli.

Historyczne 189/189 celów kompilacji, 70/72 początkowo + jeden poprawiony rerun
→ 71/72 zostały zapisane jako **odrębne dowody działania mechanizmów**. To nie
nowy build w tej sesji. Raport zaznacza zero wykonanych testów dużej skali oraz
wcześnie zakończone testy lineage. Nie przenosić tych liczb na model-reliability.

## Instrukcja kompensująca i plan ablacji (#17)

[U] Właściciel ocenia uogólnianie swojej filozofii jako średnie/średnio słabe.
Zapisano to jako feedback o klasie zadania; nie przypisano fikcyjnej liczby ani
listy konkretnych modeli. [P/H] Kolekcja zawiera dokładny polski tekst instrukcji
kompensującej: źródła vs interpretacje, zakres/wyjątki, konkurencyjne modele,
kontrprzykłady, nienarzucanie pojęciowych osi i brak promocji za elegancję opisu.

Plan: A bez instrukcji; B z nią; C z długościowo dopasowanym neutralnym
przypomnieniem. Tekst C, dane, model/provider, rubryka i budżet muszą zostać
zamrożone przed wykonaniem. Oceniać wierność źródłom, zachowanie wyjątków i
nieekwiwalentnych alternatyw, nadmierną abstencję, transfer na kod/ekstrakcję,
pełny koszt. Ocena zaślepiona względem ramienia, analiza sparowana po przypadku.
Predykcje na niewidzianych źródłach tylko wtedy, gdy takie źródła faktycznie są.

**To wersjonowalny szkic produktu i plan, nie gotowy pomiar ani wykazana ablacją
poprawa.** Konsensus sędziów i zgodność z preferencją nie zastępują prawdziwości.

## Odtworzenie i sprawdzenie przez Claude’a

```sh
python -m unittest discover -s loom/tools/eval -p test_model_profiles.py -v
python loom/tools/eval/model_profiles.py
python loom/tools/eval/model_profiles.py --verify-repo-sources
```

Ostatnia komenda wymaga pełnego repo i oryginałów z podanych blobów. Sprawdza
tożsamość plików git SHA-1 oraz wybrane pola oryginalnego JSON/q01–q12 względem
wyciągu. Wariant `--emit NOWY_PLIK.json` odtwarza projekcję, odmawia nadpisania.

**35/35 lokalnych testów przeszło**, w tym mianowniki, q01, koszt niepewny,
rozłączność dowodów natywnych, nieznane liczby dodatnich, brak gwarancji
kalibracji, zerowe automatyczne promocje, źródła, replikacja generatora i CLI.
To testy kontraktu/obliczeń. Żadnych nowych wywołań modeli, CTest ani integracji
natywnej; schemat jest propozycją do przeglądu, nie nową kanoniczną ontologią.
