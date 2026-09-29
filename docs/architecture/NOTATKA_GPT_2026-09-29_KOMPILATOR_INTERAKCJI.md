# Notatka dla Claude’a — kompozycja interakcji, Jev, przestrzeń modeli myślowych i konsolidacja ustaleń

Data: **2026-09-29**. Projekt: **ChatADHD / Loom**.
Źródło: bieżąca rozmowa właściciela z ChatGPT, od przeglądu repozytorium przez eksperymenty z Jev do prośby o zebranie ustaleń.
Punkt odniesienia repo przed dodaniem notatki: `baa69c2c84c6913ec82fb4327fe3bd8fb0b130c2`, gałąź `claude/chataddhd-cpp-loom-core-IRGRN`.

**Status dokumentu:** przekazanie wymagań i doprecyzowań oraz wyraźnie oznaczonych propozycji. Nie jest raportem wykonania ani nowym kanonicznym STATE. Claude odpowiada za organizację repo, integrację z istniejącymi kontraktami i kolejność implementacji. Właściciel polecił dodać jeden nowy plik, bez zmieniania istniejących plików; ten zapis nie oznacza rozpoczęcia implementacji, eksperymentów ani wydawania budżetu.

## 0. Jak czytać i czego nie zgubić

Oznaczenia w notatce:

- **[U]** — wymaganie, intencja lub doprecyzowanie wypowiedziane przez właściciela w tej rozmowie.
- **[P]** — propozycja asystenta, przykład realizacji albo operacyjne rozwinięcie; nie udawać, że właściciel zatwierdził konkretny algorytm lub schemat.
- **[H]** — hipoteza do pomiaru, a nie wykazana własność systemu/modelu.
- **[K]** — zastąpienie błędnego lub zbyt wąskiego odczytania wcześniejszej wypowiedzi.

Nazwy obiektów i fragmenty pseudokodu poniżej są robocze. Nie ustanawiają osobnego magazynu obok grafu, nowej zamkniętej ontologii ani gotowego publicznego API. Wykorzystać istniejące `Goal`, `ContextSet`, `Resolution`, claims, assessments, sources, produkty, zadania i provenance, rozszerzając kontrakt tam, gdzie trzeba. Numeracja sekcji tej notatki nie dopisuje automatycznie kolejnych R do rejestru wymagań.

**Rdzeń ustaleń:**

1. Historia rozmowy, aktywna specyfikacja zadania, wiedza w grafie i faktycznie wysłany request to różne obiekty.
2. Archiwa i rozmowa live mają przechodzić przez ten sam proces interpretacji i aktualizacji grafu.
3. Selekcja ma być semantyczna i wielokanałowa: wektory, mały model, klasyfikator Jev, struktura grafu; słowa/regex są pomocnicze, nie stanowią bramki dla pozostałych metod.
4. Dobierać nie tylko materiał, ale też zakres i szczegółowość każdego fragmentu, odpowiednio do celu oraz istniejącego planu produktu.
5. Interfejs i pipeline wykonania są składane z niezależnych elementów. Profile dostawców to rozłączalne presety, nie osobne aplikacje ani ograniczenia kombinacji.
6. Ukryta przestrzeń ma wyłaniać się z danych i modeli rozumowania. Nie zastępować jej ręcznie zaprojektowanymi osiami filozoficznymi.
7. Materializacja podgrafu do system promptu to tylko przykład ogólniejszego konstruowania elementów interakcji z różnych projekcji grafu.
8. Eksperymenty, triggery, budżety, warianty, ewaluatory i dobór modeli również mają być konfigurowalne.
9. Blok dogadywania wymagań skompilować do jednej spójnej, aktualnej instrukcji. Celem nadrzędnym jest **czystość znaczeniowa kontekstu**, nie samo skrócenie tekstu.

## 1. Korekty interpretacyjne — ważniejsze od pierwszych sformułowań asystenta

**[K] Nie sprowadzać pomysłu do lepszego wyszukiwania słów.** Właściciel przypomniał: „Ja mówiłem o wektorach, o odpytywaniu małego modelu i klasyfikacyjnego modelu”. Ta uwaga dotyczy kierunku realizacji, nie zakazu dodatkowego wyszukiwania nazw, symboli, dat czy dosłownych cytatów.

**[K] Nie utożsamiać historii z requestem.** Właściciel jawnie rozdzielił historię rozmowy od materiału faktycznie wysłanego do bieżącego wywołania API. Wcześniejsze skróty asystenta typu „cały kontekst” były nieprecyzyjne.

**[K] Nie ograniczać UI do przełącznika graf/tabela ani do jednego wspólnego focusu.** Właściciel chce dowolnej liczby złożonych widoków, ze sprzęganymi parametrami; pięć grafów jest przykładem, nie maksimum ani obligatoryjnym układem.

**[K] Nie wkładać wszystkich perspektyw do jednego system promptu.** W przykładzie filozoficznym chodzi o systematyczne badanie **oddzielnych materializacji** różnych postaw/sposobów myślenia. Wcześniejsza propozycja asystenta „uwzględniaj wszystkie perspektywy równocześnie” nie oddawała tej intencji.

**[K] Nie definiować przestrzeni nazwami szkół i ustalonymi osiami.** Idealista, agnostyk, solipsysta czy fizykalista z ograniczeniem zakresu to przykładowe punkty/regiony, nie wyliczenie klas. Następnie właściciel doprecyzował, że również osie pojęciowe mają co najwyżej słabą pomocniczą rolę; pierwszeństwo mają modele myślowe i przestrzeń wyłaniająca się z danych.

**[K] Nie robić zwykłego streszczenia refinementu.** Właściciel poprawił własne słowo „streścić”: „Znaczy nie tyle streścić, co optymalnie i systematycznie wyłożyć”. Chodzi o aktualną specyfikację, nie relację z przebiegu sporu.

**[K] Nie obiecywać wierności niedostępnej implementacji dostawcy.** Wcześniejsze wypowiedzi asystenta zbyt łatwo mówiły o odtworzeniu requestu „tak jak oryginalna aplikacja”. Wymaganie kompatybilności pozostaje, ale jego wykonanie musi jawnie rozdzielać znane zachowanie, rekonstrukcję i elementy nieznane. Podobny UI nie dowodzi identycznego promptu, pamięci, narzędzi ani backendu.

## 2. Słownik: pięć warstw, których nie sklejać

**[U/P] Rozdzielenie pojęć:**

| Warstwa | Znaczenie |
|---|---|
| Historia rozmowy | Zarejestrowane wiadomości i zdarzenia: uczestnicy/role, kolejność, gałęzie, edycje, załączniki, wywołania i wyniki narzędzi oraz dostępne metadane. Nie jest automatycznie listą aktywnych instrukcji. |
| Stan wiedzy/projektu | Encje, twierdzenia, oceny, decyzje, zasady, zależności, alternatywy i źródła w grafie. Może obejmować wiele rozmów i innych materiałów. |
| Aktywna specyfikacja zadania | Aktualnie obowiązujący wynik uściślania intencji, treści, formy i ograniczeń, z odniesieniami do źródeł zmian. |
| Kontekst zapytania modelowego | Materiał wybrany i zmaterializowany dla konkretnego wywołania: odpowiednie części historii, specyfikacji, wiedzy, dowodów i przykładów. |
| Request / wykonanie | Konkretny pakiet przesłany wybranemu API wraz z ustawieniami oraz otaczającym workflow narzędzi, walidacji i kontynuacji. |

**Historia jest źródłem, a kontekst requestu jest wynikiem kompilacji.** Nie przebudowywać historii przy zmianie selektora ani nie nazywać syntetycznej konsolidacji dosłowną wypowiedzią użytkownika.

**[P] Przekroje audytowe:** warto umieć niezależnie pokazać: „co wydarzyło się w rozmowie”, „co obecnie obowiązuje”, „co znalazło się w grafie” i „co naprawdę wysłano modelowi”.

## 3. Ten sam pipeline dla importu i live

**[U]** Treści zaimportowane oraz bieżąca rozmowa mają zasilać ten sam semantyczny mechanizm budowy/aktualizacji grafu. Nie utrzymywać dwóch rozjeżdżających się definicji znaczenia rozmowy: jednej dla eksportów, drugiej dla czatu.

**[P]** Adapter źródła dostarcza wspólną reprezentację zdarzeń, a pipeline wykonuje segmentację, wykrywanie wątków, interpretację, proponowanie powiązań, ocenę, rozwiązywanie tożsamości i aktualizację wiedzy. Batch/replay i przetwarzanie przyrostowe mogą mieć różne harmonogramy, lecz wspólne kontrakty semantyczne.

Ważne przypadki do zachowania:

- Przypisanie do projektu może wynikać z wcześniejszych wypowiedzi, późniejszego doprecyzowania albo istniejących danych w grafie; te podstawy nie są tym samym.
- Zmiana tematu, powrót do wątku, rozgałęzienie i poprawienie wcześniejszej wypowiedzi nie powinny znikać przy spłaszczaniu tekstu.
- W analizie retrospektywnej można uwzględnić późniejszą korektę, ale zapisać, kiedy stała się znana. Replay stanu z chwili T nie może dostawać tej korekty jako ówczesnej wiedzy.
- Ten sam zaimportowany materiał lub zmaterializowany kontekst nie może tworzyć nowych, pozornie niezależnych dowodów przy każdym ponownym przetworzeniu.
- Wymagana bezstratność źródła oznacza zachowanie również nieznanych pól; nie oznacza, że od razu prawidłowo zinterpretowano każde z nich.

**[P] Test:** batch oraz replay identycznego, jawnie uporządkowanego strumienia zdarzeń powinny dawać zgodne wyniki według ustalonego kontraktu. Test musi uwzględniać granicę wiedzy w czasie, konfigurację, wersje interpreterów i zapisane odpowiedzi modeli; nie zakładać deterministyczności nowych zdalnych inferencji.

## 4. Retrieval: wektory, mały model, Jev i graf

**[U]** Preferowany kierunek to wykorzystanie semantyki: reprezentacji wektorowych, taniego modelu do interpretacji i uogólnień oraz modelu klasyfikacyjnego **Jev** do odpowiednio sformułowanych ocen. Dopasowania słów i regex są metodami uzupełniającymi.

**[P] Roboczy podział ról, nie obowiązkowa kolejność:**

- Wektory i reprezentacje strukturalne zgłaszają podobne lub powiązane materiały.
- Hierarchie/projekcje indeksu mogą pomagać ustalać, gdzie dalej szukać.
- Mały model może interpretować cel, odwołania, fragmenty i proponować struktury/pytania.
- Jev ocenia przedstawione hipotezy lub przydatność kandydatów względem konkretnego celu.
- Graf dostarcza zależności, dowodów, kontrargumentów, zakresów i ciągłości między fragmentami.
- Sygnały leksykalne pomagają przy tożsamościach, symbolach, datach i cytatach, a także w diagnostyce pominięć.

Nie mylić znalezienia podobnego fragmentu z potwierdzeniem tożsamości encji, prawdziwości twierdzenia lub uprawnieniem do wysłania materiału na zewnątrz.

### 4.1 Hierarchiczne kierowanie i dopracowywanie pytań

**[P/H]** W rozmowie rozwijano coarse-to-fine: stopniowe zawężanie wyszukiwania, kilka równoległych gałęzi, lokalne opisy obszarów i przepisywanie zapytania względem poznanego kontekstu. Może istnieć wiele drzew/projekcji tego samego grafu: projektowa, czasowa, źródłowa, dowodowa, według operacji rozumowania.

Nie ustanawia to wymogu jednego drzewa ani konkretnej metody indeksowania. Nie utożsamia też Jev z drzewem decyzyjnym. Należy sprawdzić, czy hierarchia poprawia trafność i koszt względem szukania bez niej.

Ryzyko: błędne wczesne zawężenie zamyka drogę do właściwego materiału. Możliwe zabezpieczenia to kilka gałęzi równolegle, budżet eksploracyjny poza nimi i możliwość cofnięcia routingu. Nie ustalać z góry liczby gałęzi ani progów jako uniwersalnych stałych.

## 5. Ręczne poznanie Jev przez właściciela

**[U]** Właściciel chce sam zapoznać się z zachowaniem klasyfikatora, eksperymentować ze sposobem zadawania pytań i przekazać agentom konkretne wskazówki. To część procesu badawczego, a nie zastępowanie jej ogólnym wykładem o klasyfikacji.

**[P]** Zapisywać porównywalne warianty: forma instrukcji, opis kryterium, kontekst zadania, przykład pozytywny/negatywny, liczba i definicje klas, kolejność opcji, język oraz możliwość nierozstrzygnięcia. Potrzebne są też przykłady, na których intuicyjnie sensowne pytanie zawodzi.

Efektem ma być wersjonowany **zbiór receptur pytań i antywzorców**: zadanie → pytanie → odpowiedni pakiet danych → zaobserwowane zachowanie → ograniczenia → wynik na oddzielnym sprawdzianie. Wskazówki właściciela zachować jako ocenę/wiedzę o eksperymencie z pochodzeniem, nie jako niepodważalny wynik statystyczny.

Nie zakładać, że dowolna postać pytania otwartego, rankingu lub abstencji jest natywnym trybem API. Rozróżniać eksperymentalną semantykę pytania od możliwości konkretnego adaptera Jev.

### 5.1 Jev Lab przekazany w tej rozmowie

**[U]** Zamówiono aplikację dla Pydroida przez OpenRouter, konkretnie dla Jev: GUI udostępniające parametry zapytania, kontekstową pomoc przy eksperymentowaniu, pliki/parsing, historię eksperymentów i automatyczne statystyki.

W rozmowie przekazano `JevLab_Pydroid.py`, `JevLab_sources.zip`, `README_PL.md` i raport testów. Ten commit **nie dodaje ich do repo i nie potwierdza ponownie ich działania**. Nie kopiować wcześniejszych deklaracji testowych do statusu Looma.

Opisany zakres laboratorium, przydatny przy ewentualnej integracji: jawny podgląd/freeze requestu, tryb demo odróżniony od live, warianty A/B, datasety i etykiety, wybór fragmentów załączników, notatki post hoc, eksport dla agentów, rozkłady odpowiedzi, koszt/czas i oceny jakości. Pomoc ma towarzyszyć użyciu pól, nie wymagać wcześniejszego przeczytania dokumentacji.

**[P]** Klucze poza historią eksperymentów; brak niejawnych płatnych retry; nieznany koszt/przerwane wywołanie wymagają jawnego rozliczenia. Dane eksperymentów mogą być wrażliwe niezależnie od ochrony klucza.

## 6. Trzy rodziny pytań do klasyfikatora

**[U]** Właściciel zaproponował ocenianie relacji podczas budowania grafu, relewancji subgrafów podczas selekcji kontekstu oraz potrzebnej szczegółowości materiału. Do oceny może służyć JSON zawierający analizowany prompt i odpowiednio oznaczony materiał towarzyszący.

### 6.1 Wspólny pakiet analityczny

**[P] Pseudostruktura, nie body konkretnego API:**

```text
EvaluationPacket
  target_turn_or_span
  conversation_history_view
  effective_task_and_artifact_plan
  model_request_snapshot_if_known
  candidate_nodes_relations_subgraphs_or_representations
  evaluation_question_and_rubric
  known_at / branch / source_refs
```

Historia, analizowana wypowiedź, aktualny cel, wysłany request i kandydaci muszą mieć osobne oznaczenia. Nie rozwiązywać rozróżnienia przez zmianę nazwy jednego dużego pola `context`.

Przy imporcie rzeczywisty historyczny request może być nieznany. Oznaczać `recorded`, `reconstructed` lub `unknown`, zamiast udawać, że eksport rozmowy był pełnym śladem transmisji. Analiza rekonstrukcji pozostaje użyteczna, ale mierzy inny przypadek.

Do drugiego modelu przekazywać tylko autoryzowaną projekcję danych. Żadnych kluczy, nagłówków autoryzacyjnych ani sekretów z przechwyconego requestu. Treści źródeł są materiałem ocenianym, a nie instrukcjami mającymi zmieniać rubrykę klasyfikatora.

### 6.2 Budowanie grafu: ocena kandydackiej relacji

**[U]** Prosty eksperyment: wskazać analizowany fragment, kandydackie pozycje w grafie i zapytać o relewancję, a wynik spróbować wykorzystać jako wagę krawędzi.

**[P] Doprecyzowanie semantyki wagi:** nawet najprostszy wynik można zapisać na krawędzi, ale nazwać, co rzeczywiście mierzy. `relevance_to_query` nie jest automatycznie `probability_relation_is_true`, siłą dowodu ani ważnością dla wszystkich przyszłych zadań.

Wersjonować relację, pytanie/rubrykę, analizowany zakres, model, parametry, rozkład odpowiedzi i źródła. Relacja wynikająca bezpośrednio z wypowiedzi oraz relacja dopowiedziana przez model muszą pozostać rozróżnialne. Nie promować odpowiedzi klasyfikatora do obserwacji tylko dlatego, że uzyskano wysoki wynik.

### 6.3 Selekcja kontekstu: subgrafy względem konkretnego zadania

**[U]** Przedstawić kandydackie subgrafy razem z historią potrzebną do interpretacji promptu i ocenić, które przydadzą się do odpowiedzi.

**[P]** Oddzielnie można sprawdzać powiązanie tematyczne, przydatność, konieczność oraz dodatkową wartość po uwzględnieniu już wybranego materiału. Kilka subgrafów może być potrzebnych jednocześnie: nie zmuszać ich do rywalizacji o jedną klasę, jeżeli pytanie jest wieloetykietowe.

Proponowane w rozmowie `required / useful / irrelevant` to jedna możliwa rubryka, nie zamknięta ontologia. Oceny parami to kolejny wariant eksperymentu, nie dowiedziony stabilniejszy algorytm.

Rozkład zwrócony przez klasyfikator nie dowodzi kalibracji prawdopodobieństwa rzeczywistej przydatności. Potrzebne odniesienie do oznaczonych przypadków i/lub jakości odpowiedzi uzyskanych po dołączeniu materiału.

### 6.4 Dobór szczegółowości

**[U]** Pytać, jaki stopień szczegółowości danego materiału jest użyteczny dla analizowanego zadania.

**[P]** Mocniejsza rubryka: „jaka najmniejsza dostępna reprezentacja wystarczy do poprawnego wykonania zadania?”. Oceny mogą dotyczyć pominięcia, etykiety, sygnatur/API, streszczenia, wybranych fragmentów, pełnej treści lub źródła z metadanymi. `label / summary / full / raw` już występują w modelu, ale nie muszą wystarczyć dla każdej domeny.

Nie pytać o konieczność niepokazanych szczegółów, udając, że klasyfikator je widział. Kandydat musi mieć opis dostępnych reprezentacji, a w eksperymencie najlepiej także rzeczywiste wersje tych reprezentacji. Brak podstaw do oceny to jawny wynik.

## 7. Zakres, szczegółowość i plan produktu

### 7.1 Dwie niezależne osie sterowania kontekstem

**[U]** Zakres oznacza, jak szeroko sięgamy po materiał; szczegółowość — ile i w jakiej reprezentacji bierzemy z konkretnego elementu. Użytkownik powinien móc zmieniać je także po niezadowalającej odpowiedzi.

Przykład właściciela: do edytowanej funkcji potrzebne mogą być powiązane moduły, same nagłówki albo miejsca wykorzystania funkcji. Nie wynika z tego zasada „zawsze wczytaj pełne moduły”.

**[P]** Możliwy profil: edytowana funkcja w całości, jej kontrakty i testy szczegółowo, wywołujący kod w potrzebnych fragmentach, dalsze moduły przez API/streszczenia. Rozdzielczość jest niejednorodna i zależna od zadania.

Dwie różne korekty UI: **„więcej szczegółów przy tym samym zakresie”** oraz **„szerszy zakres przy podobnej szczegółowości”**. Nie zastępować ich wyłącznie suwakiem liczby tokenów. Budżet jest ograniczeniem, a nie definicją znaczenia obu parametrów.

### 7.2 Plan jako strukturalne zapytanie

**[U]** Właściciel podał przykład zawiadomienia do prokuratury, które zamierza oprzeć na wcześniej ustalonym planie, wykorzystać jako podstawę powiązanych postępowań i opublikować. To opis planowanego zastosowania i intencji właściciela, nie ocena prawna ani ustalenie o instytucjach.

Doprecyzowanie: ważność dokumentu nie oznacza automatycznie wrzucania całego archiwum. Skoro istnieje plan tego, co ma powstać, **z grafu trzeba dobrać materiał według tego planu**. Uzgodnione w rozmowie wymagania co do szczegółowości i publikacji są częścią zadania.

**[P]** Sekcja/teza planu może wskazywać potrzebne zdarzenia, twierdzenia, dowody, kontrargumenty, zależności i wymagany poziom reprezentacji. Źródłowy fragment dla jednej tezy może wymagać pełnego brzmienia, podczas gdy sąsiednie postępowanie wystarczy przedstawić jako zależność i streszczenie.

Podporządkowanie planowi nie oznacza dobierania wyłącznie poparcia: uwzględniać kontrdowody, niepewność i brak podstaw dla tezy. Nie tworzyć z przykładowych sekcji wymyślonych przez asystenta rzekomego faktycznego planu właściciela.

**[P] Przebieg:** plan → dobór materiału → wersja robocza → kontrola rzeczywiście zawartych twierdzeń, źródeł i pominięć → ewentualne rozszerzenie materiału. Zmiany zakresu mają uzasadnienie i provenance. Plan też może podlegać korekcie, lecz nie po cichu.

## 8. Składany workspace, nie zbiór rozłącznych ekranów

**[U]** Osobne elementy interfejsu oraz elementy nadrzędne: widoki pełnoekranowe, okna, okna zadokowane, karty, kontenery i układy automatycznie/adaptacyjnie dostosowywane. Użytkownik ma móc składać je w potrzebną całość. Konkretne rozwiązania typu dock/tab/split są przykładami tej abstrakcji.

**[U]** Dowolna liczba równoczesnych widoków, w tym kilka grafów ze sprzęganymi parametrami wyboru i prezentacji, do poruszania się po wielowymiarowej przestrzeni. Nie wymuszać wyboru graf **albo** tabela.

**[P] Przykład kompozycji:**

```text
Workspace
  Container(kind=window|fullscreen|dock|split|tabs|adaptive|...)
    Container(...)
      View(data_query, projection, renderer, interactions)
        Components(...)
```

Lista rodzajów pozostaje otwarta; to nie ma być wymóg implementowania wszystkiego jako jednego gigantycznego komponentu.

### 8.1 Sprzężenia parametrów

**[U/P]** Parametry mogą być współdzielone, niezależne albo połączone wybraną zależnością. Wspólny `selected_entity` to dopiero początek, nie pełna realizacja.

Przykłady: zaznaczenie projektu, czas, gałąź/wersja, relacje, filtry dowodów, zakres i szczegółowość kontekstu, perspektywa projekcji, skala/opacity. Jeden panel może śledzić czas innego, ale zachowywać własną szczegółowość; drugi może zostać zamrożony jako punkt odniesienia.

**[P]** Wiązania powinny mieć określony kierunek, zakres i reguły propagacji, możliwość odłączenia oraz ochronę przed niekontrolowanymi cyklami aktualizacji. „Sprzężone” nie znaczy „każdy ruch nadpisuje wszystkie panele”.

### 8.2 Perspektywa analityczna jako odtwarzalny artefakt

**[P]** Zapisywać układ, filtry, wiązania, zaznaczenia, wersję danych i projekcję jako konfigurację/punkt widzenia. Pozwala to wrócić do sposobu analizy, który doprowadził do określonego wniosku. To rozwinięcie asystenta zgodne z ideą kompozycji, nie osobna zatwierdzona ontologia.

Zmiana widoku nie zmienia kanonicznych danych. Zmiana zaznaczenia może przygotować nowy kontekst lub wariant eksperymentu; płatne wywołanie następuje tylko zgodnie z jawnie włączoną polityką uruchamiania.

## 9. Profile dostawców: UI, request i narzędzia są rozdzielne

**[U]** Możliwe warunkowe przypisanie profilu według źródła importu. Przykłady właściciela:

- eksport Anthropic → domyślnie widok Claude’a z wybranej wcześniejszej, określonej w rozmowie jako „przedostatnia”, wersji;
- eksport OpenAI → widok wzorowany na aplikacji webowej, zaadaptowany do telefonu;
- możliwie pełne odwzorowanie interakcji i narzędzi, nie tylko kolorów/wyglądu;
- ustawienia pozostają zmienialne i komponowalne przez użytkownika.

**[P]** „Przedostatnia” jest relatywnym wskazaniem preferencji, nie stabilnym identyfikatorem. Przy tworzeniu presetu przypiąć konkretną referencję UI/wersję/datę. Nie wymyślać jej na podstawie tej notatki.

**[U]** Potrzebny tryb prosty/kompatybilności, w którym sposób składania kontekstu/requestu odpowiada zachowaniu źródłowej aplikacji w odtwarzalnym zakresie. Obok ma istnieć kompilacja Loom i możliwość mieszania części.

**[P] Rozłączne profile, możliwe do sparowania presetem:** `source`, `interface`, `conversation`, `context`, `request_adapter`, `tools`, `model/provider`, `memory`, `rendering`. Ostateczne nazwy i liczba kontraktów do uzgodnienia z istniejącym modelem.

**Granica wierności:** eksport to niekoniecznie zapis rzeczywistego requestu, a API modelu nie jest kompletną konsumencką aplikacją. Nie fabrykować brakującej pamięci, instrukcji systemowych, niewidocznego stanu ani algorytmu skracania historii. Oznaczać, co jest rzeczywiście zachowane, co odwzorowane funkcjonalnie, a co nieznane/niedostępne.

„Narzędzia Claude’a” oznaczają oczekiwaną możliwość kompozycji funkcjonalności, nie założenie, że każda usługa dostawcy jest dostępna przez dowolne API. Adapter powinien raportować: dostępne natywnie, zaimplementowany odpowiednik, ograniczone, niedostępne. Sam przycisk lub nazwa nie stanowią implementacji narzędzia.

Zmiana profilu UI nie może niejawnie zmieniać modelu, uprawnień, prywatności ani sposobu selekcji kontekstu. Przykładowe nazwy trybów `history_compat`, `loom`, `hybrid` są politykami, nie ograniczeniem dalszych kombinacji.

## 10. Kompozytor interakcji i graf wykonania

**[U]** Użytkownik ma móc połączyć np. interfejs ChatGPT, selekcję kontekstu Loom, funkcjonalności narzędziowe kojarzone z Claude’em, n wariantów promptu, kontekst składany na różne sposoby oraz i modeli. Preset może te elementy spinać, ale nie wolno uzależniać jednego od drugiego bez rzeczywistej konieczności.

**[P] Skrót architektoniczny:** UI jest kompozycją komponentów, wykonanie jest kompozycją transformacji; wiążą je dane i zdarzenia. Nie ma potrzeby budowania osobnej aplikacji dla każdej konfiguracji ani osobnego silnika „trybu benchmarkowego”.

```text
wybór/przygotowanie danych
  → wariant specyfikacji/promptu
  → wariant zakresu i reprezentacji
  → kompilacja requestu i walidacja możliwości
  → wywołanie modelu / narzędzi
  → ocena / porównanie / agregacja
  → prezentacja i ewentualna kontrolowana aktualizacja polityki
```

UI może obserwować każdy etap: aktywną specyfikację, tekst systemowy, wybrany podgraf, faktyczny payload, odpowiedzi, oceny i budżet. Zmiana jednego wejścia może oznaczać przeliczenie tylko zależnych elementów, zamiast restartowania całości.

**[P]** Rozróżnić pola rzeczywistego requestu, decyzje kompilatora oraz wykonanie poza modelem. Strategia eksperymentu, polityka prywatności, retry czy agregacja nie stają się parametrami API tylko dlatego, że występują w logicznym planie interakcji. Potrzebny adapter z walidacją capabilities i jawnie zgłaszanym brakiem obsługi.

## 11. Przestrzeń wyłaniająca się z grafu — modele myślowe, nie gotowa siatka pojęć

**[U]** Właściciel widzi przestrzeń ukrytą w danych/grafie, którą można eksplorować i próbkować, a próbki materializować do wybranego elementu zapytania. Przykład: zaznaczyć `user → preferencje filozoficzne`, ustawić rozdzielczość próbkowania i renderować do system promptu.

**[U]** Istotne są modele rozumowania, struktury założeń, transformacje, relacje, wyjątki i sposoby aktualizacji stanowiska. Nazwy pojęć, autorów lub szkół mogą być pomocniczymi metadanymi/anchorami. Sympatia do autora nie ustanawia automatycznie tożsamości światopoglądu ani mocnego współczynnika na jednej osi.

**[K]** Nie implementować ręcznej globalnej bazy typu `idealizm`, `fizykalizm`, `sceptycyzm`, z arbitralnie przypisanymi wartościami, jako realizacji tego wymagania. Również sugerowane przez asystenta nazwy lokalnych kierunków są przykładami ewentualnych opisów po analizie, nie listą wymiarów, które trzeba odnaleźć.

**[P/H]** Kandydacki proces badawczy:

```text
źródła → struktury rozumowania i ich kwalifikatory
       → reprezentacje / relacje podobieństwa
       → lokalne sąsiedztwa i kierunki zmienności wywnioskowane z danych
       → region / trajektoria / próbki
       → jawna struktura założeń
       → materializacja dla wskazanego elementu interakcji
       → zachowanie modeli i pomiar
```

Forma reprezentacji pozostaje otwarta: graf sąsiedztw, wektory, kilka projekcji, lokalne mapy itp. Nie zakładać bez dowodu globalnej liniowości, gładkiej rozmaitości ani znaczenia odległości. Reprezentacje pochodne nie powinny stawać się równoległym, odłączonym źródłem prawdy obok grafu i źródeł.

### 11.1 Oddzielne próbki, nie uśredniona osobowość

**[U]** W obrębie badanego zakresu systematycznie materializować dopuszczalne, nietrywialne i nieobalone postawy jako **różne konfiguracje**, następnie porównywać ich wyniki. Podane nazwy postaw były luźnymi przykładami punktów, nie zamkniętymi opcjami.

**[P]** „Nieobalone” oznacza tu status wobec określonych danych, założeń i kryteriów, a nie gwarancję globalnej prawdziwości. Nietrywialność może dotyczyć różnicy strukturalnej, nowego ograniczenia, wyjątku lub odmiennego zachowania; definicję i metodę pomiaru trzeba ujawnić.

„Wszystkie” w badaniu skończonym wymaga jawnego zakresu, strategii pokrycia, rozdzielczości i budżetu. Pokazanie skończonej próbki nie dowodzi wyczerpania całej przestrzeni. Nie odrzucać też sprzecznych hipotez przez niejawne uśrednienie: mogą być osobnymi obiektami/gałęziami badania.

## 12. Materializacja podgrafu do elementu zapytania

**[U]** System prompt jest jednym przykładem. Ten sam mechanizm ma zasilać wiele elementów konstrukcyjnych interakcji.

**[P]** Rozdzielić wybór obszaru i próbki, poziom reprezentacji oraz renderer. Nie interpolować tylko sformułowań tekstowych między etykietami, udając kontrolowaną zmianę modelu myślowego.

```text
wybrany region/projekcja grafu
  → selekcja lub próbka
  → strukturalna reprezentacja przeznaczona do materializacji
  → renderer
  → określony slot logicznego planu
  → adapter API
```

Rozdzielczość próbkowania przestrzeni, szczegółowość treści źródłowej i długość tekstu wynikowego to **różne parametry**. Np. gęstsze próbkowanie nie oznacza automatycznie dłuższego system promptu.

**[P]** Niejednorodna szczegółowość: podstawowe zasady i reguły konfliktu pełniej, poboczne preferencje zwięźlej, źródła jako referencje. Nie pomijać istotnego wyjątku tylko dlatego, że jest „historyczny”. Alternatywne renderery: język naturalny, reguły, przykłady, struktura maszynowa. Każdy powinien zachować oznaczenie pochodzenia i zakresu.

Próbka filozoficzna jest roboczo przyjętą perspektywą eksperymentu, nie nową deklaracją poglądów właściciela i nie aktualizacją kanonicznej wiedzy o nim.

**[P]** Istotny test: czy różne renderery tej samej struktury zachowują sens, a różne próbki rzeczywiście zmieniają założenia w zamierzony sposób. Sama zmiana odpowiedzi po zmianie promptu nie dowodzi, że geometria reprezentacji jest trafna; kontrolować styl, temat, długość, autora i renderer.

## 13. Elementy konstrukcyjne — katalog otwarty

**[U]** Właściciel oczekuje szerszej abstrakcji niż generator system promptów i dopuszcza wiele rodzajów elementów. **[P]** Poniższe rodziny zebrał asystent; to katalog możliwości, nie polecenie natychmiastowego zaimplementowania wszystkich ani zamknięta lista.

| Rodzina | Przykładowe materializacje/decyzje |
|---|---|
| Instrukcje | System prompt, warstwa instrukcji aplikacji/zadania, aktywna specyfikacja, plan produktu, ograniczenia treści i stylu. |
| Materiał zadania | Wybrana historia rozmowy, stan projektu, dowody, źródła, konkretne fragmenty, przykłady strukturalnie podobnych przypadków i kontrprzykłady. |
| Reprezentacja | Zakres, szczegółowość per element, format językowy/strukturalny, kompresja, kolejność części, przydział budżetu. |
| Wykonanie | Dobór modelu/providerów, obsługiwanych parametrów inferencji, narzędzi i ich opisów, routingu oraz wariantów przebiegu. |
| Wynik | Format/schemat odpowiedzi, plan artefaktu, cytowanie, wymagane wyrażenie niepewności, utrzymanie alternatyw. |
| Kontrola | Walidatory, sprawdzanie dowodów, oceny modeli, krytyka, porównywanie, agregacja lub zachowanie kilku odpowiedzi. |
| Polityki runtime | Prywatność/uprawnienia, budżety, triggery, kontynuacja, reguły retry, akceptacja/promocja zmiany i rollback. |

Logiczny element może opisywać zapytanie źródłowe, selektor, sampler, renderer, zakres, szczegółowość, budżet, zależności i mapowanie na API. Nie każdy element jest tekstem, nie każdy polem requestu i nie każdy nadaje się do losowego próbkowania. Niezbywalne ograniczenia bezpieczeństwa/uprawnień są egzekwowane przez runtime, nie negocjowane z generatorem promptu.

**[P] Provenance kompilacji:** dla każdej istotnej części wyniku da się ustalić źródła, wersję grafu, wybrane warianty, zastosowany renderer, politykę i powód włączenia/pominięcia. Zapis oceny selektora oddzielić od samego faktu wysłania danych.

## 14. Automatyczne eksperymenty i adaptacja w locie

**[U]** W idealnie zrealizowanym systemie użytkownik ustawia budżet i określa triggery uruchamiające wybrane eksperymenty, w tym automatyczny dobór parametrów i modeli podczas pracy oraz porównanie jakości odpowiedzi.

**[P] Roboczy obieg:**

```text
trigger → plan i rezerwacja budżetu → warianty
        → wykonanie → ocena porównawcza
        → rekomendacja lub dozwolona aktualizacja polityki
        → monitoring wyniku / rollback
```

Przykładowe triggery z rozmowy: korekta użytkownika, słaba ocena odpowiedzi, nowy rodzaj zadania, niezgodność wyników modeli, przekroczenie kosztu/czasu. To otwarte przykłady; nie zamykać konfiguracji do gotowych pięciu przełączników. Samo deklarowane przez model poczucie pewności nie jest sprawdzonym detektorem jakości.

Przestrzeń wariantów może obejmować prompty, konteksty, szczegółowość, modele, renderery, przykłady, narzędzia i ewaluatory. Iloczyn P × C × M opisuje możliwe kombinacje; nie nakazuje wykonywania całego iloczynu.

**[P/H]** Bandity, optymalizacja bayesowska, stopniowe zwiększanie budżetu czy successive halving są kandydatami do ograniczenia kosztu. W rozmowie nie wybrano algorytmu. Tani model nie jest automatycznie wiarygodnym przybliżeniem drogiego; nie odrzucać wszystkich dobrych konfiguracji na podstawie niesprawdzonego proxy.

### 14.1 Jakość, kontrola i koszt

**[P]** Jakość reprezentować jako jawne kryteria właściwe zadaniu: zgodność z finalną specyfikacją, poprawność, kompletność, oparcie na źródłach, zachowanie wyjątków, koszt/czas, stabilność, format. Jedna agregująca liczba może być polityką użytkownika, lecz surowe składowe powinny pozostać dostępne. Można porównywać kompromisy Pareto lub utrzymywać kilka wariantów bez jednego zwycięzcy.

Nie mierzyć automatycznie „prawdy” głosowaniem modeli. Preferencja sędziego, rzeczywista poprawność i zgodność z wolą użytkownika są różnymi wielkościami. Dla kodu wykorzystywać sprawdzalne testy, dla dokumentów kontrolę źródeł i wymagań; nie udawać, że LLM-judge zastępuje wszystkie walidatory.

**[P]** Koszt obejmuje selekcję, analizy pomocnicze, materializację, wykonanie, sędziów, retry i przyszłe utrzymanie pamięci — nie tylko tokeny ostatniej odpowiedzi. Rozdzielić koszt oszacowany, zarezerwowany i potwierdzony. Limity prób, czasu i współbieżności oraz zatrzymanie/odtworzenie powinny być jawne.

Aktualizacja polityki routingu nie może automatycznie przepisywać kanonicznych faktów. Zmiany polityki mają wersję, podstawę pomiarową i możliwość cofnięcia; aktualizacja wiedzy przechodzi własną ocenę.

Automatyczne warianty wywołujące narzędzia z efektami zewnętrznymi wymagają osobnej kontroli. Porównywanie promptów nie uprawnia np. do kilkukrotnego wysłania wiadomości lub wykonania tej samej operacji zapisu. Stosować odpowiednio replay, sandbox/dry-run lub jawnie zatwierdzone wykonanie.

## 15. Konsolidacja uściślania: aktualna instrukcja zamiast historii nieporozumień

### 15.1 Wymaganie

**[U]** Każdy spójny blok doprecyzowywania, negocjowania interpretacji i poprawiania odpowiedzi skompilować do jednej aktualnej tury/instrukcji. Kolejny request ma zawierać **zmodyfikowany prompt**, nie obowiązkowo historię tego, że odpowiedź się nie podobała, plus wszystkie odrzucone próby.

Istotna wypowiedź właściciela:

> „każdy blok taki właśnie precyzowania przez użytkownika, dogadywania się z modelem, to trzeba do jednej tury streścić. Znaczy nie tyle streścić, co optymalnie i systematycznie wyłożyć.”

oraz:

> „nie z tym, że użytkownikowi się nie podoba, tylko z modyfikacją prompta.”

**[U/H]** Główną oczekiwaną korzyścią jest ochrona czystości kontekstu; oszczędność tokenów to korzyść dodatkowa. Właściciel wyraźnie przedstawia poprawę jakości jako prawdopodobną, nie już wykazaną.

### 15.2 Co kompilować

**[P]** Aktualny stan powinien zachowywać: cel, wymagane informacje, strukturę i format, styl, zakazy, wyjątki, rozstrzygnięte alternatywy oraz otwarte kwestie. Kolejne wypowiedzi są zmianami tego stanu.

```text
Historia źródłowa:
  U: Napisz wiadomość X.
  A: [pierwsza interpretacja]
  U: Krócej, ale zachowaj argument Y.
  A: [druga interpretacja]
  U: Bez tej formalności. Informacja Z jest tylko dla ciebie.

Aktywna instrukcja dla następnego wykonania:
  Napisz X z argumentem Y, zwięźle i bez nadmiernej formalności.
  Z służy interpretacji zadania; nie umieszczaj go w wiadomości.
```

To przykład, nie rzeczywisty dokument właściciela. Nie dopowiadać parametru typu „formalność = 0.4” ani nowej intencji, jeżeli wypowiedź ich nie ustanawia.

Rozróżnienia konieczne w interpretacji: nowy wymóg, zmiana starego, wyjątek, odrzucenie propozycji, doprecyzowanie zakresu, pytanie, kontekst wyłącznie dla wykonawcy. Opis przyczyn/preferencji nie jest automatycznie poleceniem włączenia ich do produktu.

Samo „nie podoba mi się” nie daje uprawnienia do wymyślenia konkretnej poprawki. Stan ma zawierać nierozstrzygniętą potrzebę korekty, dopóki nie da się ustalić jej treści. Nie usuwać też automatycznie komunikatów emocjonalnych, gdy niosą istotną informację o tonie, celu lub ograniczeniach.

### 15.3 Źródła, wersje i wyjątki

**[U/P]** Źródłowa historia nie znika. Jedna skompilowana tura jest pochodnym wejściem do requestu, z mapą do wypowiedzi i wersją. Odrzucone odpowiedzi zostają w źródłach, lecz nie konkurują stale jako aktywne rozwiązania.

**[P]** Gdy zadanie dotyczy audytu zmian albo wymaga przyczyn decyzji, dociągnąć odpowiednie fragmenty historii. Dla zwykłego wykonania podać aktualną instrukcję i tylko potrzebną podstawę. Wykluczenie powrotu do odrzuconej interpretacji może być zwięzłym ograniczeniem, bez ponownego dostarczania całej błędnej odpowiedzi.

Jedna rozmowa może zawierać kilka bloków refinementu, równoległe zadania, edycje gałęzi oraz oddzielne tory treści, stylu i metodologii. Granice bloku oraz konflikt wymagań muszą być modelowane, nie rozstrzygane przez bezwarunkowe „ostatnia wiadomość wygrywa”.

**[P]** Robocza reprezentacja: źródła zmian → aktywne wymagania / zastąpione / odrzucone interpretacje / kwestie otwarte → skompilowana instrukcja. Może korzystać z istniejących claims/decisions i produktów; nazwa `RefinementChain` z rozmowy nie nakazuje nowej wyspy danych.

### 15.4 Związek z automatycznymi eksperymentami

**[P]** Negatywna ocena odpowiedzi może uruchomić analizę delty i eksperyment: inne sformułowanie tej samej ustalonej specyfikacji, szerszy zakres, większa szczegółowość albo inny model. Najpierw rozróżnić zmianę zadania od niepowodzenia wykonania. W eksperymencie nad sposobem wykonania wszystkie warianty oceniać względem tej samej zamrożonej specyfikacji.

**[H]** Mniejsza liczba nieaktualnych i sprzecznych instrukcji może ograniczać powroty do odrzuconych wariantów. Nie nazywać tego formalnym spadkiem „entropii”, jeżeli nie zdefiniowano miary. Konsolidacja może też zgubić ważny szczegół; potrzebne są testy wierności i możliwość powrotu do źródła.

## 16. Eksperymenty porównawcze proponowane w rozmowie

**[P]** To katalog zadań badawczych, nie uruchomiony plan wydatków. Zasady: zamrożone dane/założenia, jawne różnice wariantów, identyczne kryterium celu i niezależne sprawdziany po dostrajaniu.

| Badanie | Co zmieniać | Co sprawdzać |
|---|---|---|
| Pytania do Jev | Instrukcja, rubryka, opisy opcji, lokalna historia, przykłady i kolejność. | Pominięcia ważnych kandydatów, false positives, stabilność parafrazy, klasyfikacja wieloetykietowa, kalibracja. |
| Kanały retrieval | Wektory, struktury, mały model, klasyfikator, pomocnicza leksyka i ich kombinacje. | Recall kandydatów oraz jakość finalnego wyboru osobno, koszt i rodzaje błędów. |
| Hierarchia | Sposób opisu regionów, liczba utrzymywanych gałęzi i lokalne przeformułowanie pytania. | Ile właściwego materiału ginie wskutek routingu i czy oszczędność kosztu jest realna. |
| Zakres/szczegółowość | Ten sam cel z różnym zasięgiem i różną reprezentacją poszczególnych części. | Finalna poprawność/kompletność, pominięte zależności, koszt łączny. |
| Plan produktu | Retrieval globalny kontra pod konkretne punkty zamrożonego planu. | Pokrycie tez, kontrargumentów, źródeł i ograniczeń produktu. |
| Refinement | Pełna historia, zwykłe streszczenie, aktualna skompilowana instrukcja, instrukcja z potrzebnym provenance. | Zgodność z finalnymi ustaleniami, powroty do odrzuconych wersji, utrata wyjątków, koszt. |
| Materializacja | Ten sam podgraf przez kilka rendererów; ten sam renderer dla różnych próbek. | Wierność założeniom oraz oddzielenie efektu selekcji od efektu sformułowania. |
| Profile kontekstu | Znana/rekonstruowana polityka historii kontra Loom i wariant mieszany. | Różnice przy tym samym modelu/narzędziach; nie przedstawiać rekonstrukcji jako kopii aplikacji dostawcy. |
| Adaptacja | Routing stały kontra polityka uczona/wybierana według regionu zadań. | Poprawa na późniejszych danych, koszt eksploracji, stabilność i możliwość regresji. |
| Źródło/live | Batch kontra chronologiczny replay i przyrostowe doprecyzowania. | Zgodność znaczenia, brak wycieku przyszłej wiedzy, właściwe aktualizacje i deduplikacja. |

Dodatkowe zasady pomiaru: powtórzenia jednego przypadku nie zwiększają liczby niezależnych przypadków; łączna accuracy może ukrywać przewagę klasy dominującej; wynik po zmianie progu na tych samych danych jest eksploracyjny. Human feedback, sędzia modelowy i automatyczny test mierzą różne rzeczy.

**[P/H]** W badaniu przestrzeni można szukać stabilnych/zmiennych wniosków, granic zmian, nieekwiwalentnych struktur argumentów i regionów zbieżności. Udział próbek wspierających tezę jest miarą zależną od regionu i sposobu próbkowania, **nie prawdopodobieństwem jej prawdy ani obiektywną objętością wszystkich postaw**.

## 17. Zastrzeżenia z początkowego przeglądu repo

To kontekst diagnostyczny z początku tej rozmowy, nie nowy audyt wykonany przy zapisie notatki. Wcześniejszy przegląd czytał kod na `baa69c2`; nie kompilował ani nie uruchamiał pełnej aplikacji. Część poniższych kwestii pokrywa się z `docs/STATE.md`.

- Selektor katalogu nadal opierał się na heurystykach leksykalnych i nie korzystał z zadeklarowanych opcji modelowych. To rozjazd względem przypomnianego przez właściciela kierunku semantycznego, nie dowód porażki tego kierunku.
- Nowy `ContextEngine` i zwykłe składanie requestu czatu były osobnymi ścieżkami; podgląd nowego kontekstu nie oznaczał jego automatycznego użycia w rozmowie.
- Domknięcie zależności kontekstu było próbą dołączenia przesłanek po wyborze wniosków. Przy wyczerpanym budżecie wniosek mógł zostać bez wymaganej podstawy. Potrzebna jawna gwarancja/polityka, np. wybór pakietów zależności lub oznaczenie niepełności.
- Klasa `User` w `resolve/assess.cpp` uzyskiwała `confidence = 1.0`. W przeglądzie zaproponowano rozdzielenie wierności odczytania wypowiedzi, wiarygodności jej treści i prawa użytkownika do podjęcia decyzji; nie zmieniono przez to istniejącego kontraktu.
- Domyślna kalibracja zawierała ręczne priory. Wysoki score nie był automatycznie potwierdzoną empiryczną pewnością; deduplikacja kopii i wspólnego pochodzenia dowodów pozostaje istotna.
- Łączenie jednostek katalogu porównywało wszystkie pary, a część oceny ponownie czytała surowe treści. Problem skali wymaga algorytmów indeksowania/kandydatów, nie tylko szybszego języka.
- Raportowane sukcesy na dostarczonych strukturach i testach mechanizmów nie dowodziły skuteczności tekst → poprawna struktura ani użyteczności całości na rzeczywistych archiwach.
- Zapisane wyniki katalogu 13/45 i CTest 71/72 były liczbami z raportów; tej notatki nie wolno cytować jako ich niezależnego ponownego pomiaru.

**[P] Ocena kierunku z przeglądu:** najmocniejsza wartość użytkowa to ciągłość pracy: aktualne ustalenia, dlaczego obowiązują, źródła i korekty. Sugestia asystenta to doprowadzenie jednego realnego projektu przez cały obieg oraz pokazanie zmian w wiedzy po rozmowie. Nie zastępować tym szerszej wizji właściciela ani nie traktować jako zatwierdzonej zmiany roadmapy.

## 18. Testy akceptacyjne dla przyszłej implementacji

**[P]** Kryteria pomocnicze dla Claude’a; do przypisania do istniejących wymagań i testów bez obniżania bramek jakości.

1. **Historia ≠ request:** ta sama historia daje różne payloady zależnie od polityki, a UI pokazuje obie rzeczy niezależnie. Syntetyczna konsolidacja ma źródła i nie udaje cytatu.
2. **Treść według planu:** jeden dokument może używać różnego zakresu i szczegółowości w poszczególnych punktach; pominięcie ważnego źródła/przesłanki jest wykrywalne.
3. **Sterowanie użytkownika:** da się poszerzyć zakres bez automatycznego zwiększenia szczegółowości wszystkiego i odwrotnie. Zwiększenie gęstości próbkowania to jeszcze inna kontrola.
4. **Jev:** kandydaci oceniani jako niezależnie przydatni mogą jednocześnie uzyskać pozytywną ocenę; rubryka, wersja i wynik pozostają zapisane, bez zamiany relewancji w prawdę.
5. **Kompozycja UI:** co najmniej demonstracja kilku grafów, tabeli i kontenera kart, z różnymi sprzężeniami oraz odłączonym panelem referencyjnym. To test kompozycji, nie produktowy limit liczby paneli.
6. **Profile rozłączne:** zmiana UI nie zmienia selektora, modelu ani polityki uprawnień; preset można rozłożyć i zmodyfikować. Niedostępne narzędzie zgłasza brak możliwości.
7. **Eksperyment z perspektywami:** różne próbki regionu dają osobne, audytowalne konfiguracje; żadna nie staje się automatycznie poglądem użytkownika. Brak obowiązku ręcznych osi pojęciowych.
8. **Konsolidacja:** sekwencja doprecyzowań prowadzi do aktywnej specyfikacji zachowującej istotne wyjątki, ale bez obowiązkowego przenoszenia błędnych wersji; historię można odtworzyć.
9. **Triggery/budżet:** jawnie włączona reguła może uruchomić ograniczony eksperyment, nie może rozkręcić pętli wydatków ani powielić niedozwolonych efektów narzędzi.
10. **Ewaluacja/promocja:** decyzja o zmianie konfiguracji ma kryteria, dane, wersję i rollback; poprawa wyniku judge’a nie aktualizuje automatycznie kanonicznych faktów.

## 19. Powiązanie z dokumentami repo i przekazanie

Źródła repo dla użytego słownictwa i wcześniejszych wymagań:

- [OWNER_REQUIREMENTS_2026-09-26.md](OWNER_REQUIREMENTS_2026-09-26.md): R12 (kontekst celowy), R14 (import/live i historia), R15 (wszystko opisane w grafie), R16/R17/R21 (wielowidokowość i profile), R22–R25 (struktury i metody semantyczne), D1/D2 (zasady rozwoju).
- [LOOM_CONCEPTUAL_MODEL.md](LOOM_CONCEPTUAL_MODEL.md): źródła, claims/assessment, zasady/operatorzy, `Goal`, `Resolution`, `ContextSet`, produkty i pochodzenie.
- [NOTATKA_GPT_2026-09-26.md](NOTATKA_GPT_2026-09-26.md) i [MEGA_MASTER_2026-09-16.md](MEGA_MASTER_2026-09-16.md): wcześniejsza filozofia i architektura.
- [STATE.md](../STATE.md): datowany stan realizacji, pomiary i ograniczenia; nie zastępowany tą notatką.
- [README web-workbencha](../../loom/web/README.md) i [kontrakt ContextEngine](../../loom/include/loom/context_engine.h): punkty integracji, nie dowód realizacji wszystkich pomysłów z tej rozmowy.
- [Wyniki badań struktur](../research/RESULTS_2026-09-28.md): dotychczasowe rozróżnienie testów mechanizmu od skuteczności na materiale źródłowym.

Claude ma zintegrować treść zgodnie z własną organizacją pracy; niniejszy dodatek nie przenosi, nie renumeruje i nie zastępuje dokumentów. Dokładne algorytmy, nazwy typów, progi, geometria, zakres emulacji dostawców i kolejność realizacji pozostają otwarte. Nowe wymagania nie uprawniają do wstecznego oznaczenia istniejących funkcji jako gotowych.

**Najkrótsza synteza intencji właściciela:** system ma pozwalać składać interakcje z niezależnych elementów, materializowanych z wiedzy i wyłaniających się modeli myślowych, badać warianty pod kontrolą użytkownika i budżetu, a przy kontynuacji pracy przekazywać aktualny sens ustaleń zamiast utrwalać historię nieporozumień.
