# Audyt LEM-Workbench — 2026-10-09

Baza: `main@1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456`. Repo publiczne. Bieżąca aplikacja jest małym instrumentem Gemini z Room i Compose. Najistotniejsza luka: istnienie encji ExperimentConfig nie oznacza wpływu konfiguracji na wykonywane pomiary.

## Zakres i relacje

Sklasyfikowano **66/66 śledzonych plików**. Semantycznie prześledzono **24/24 plików kodu produktu**. Rozkład: build_ci_metadata: 11, documentation_license: 6, tests_fixtures: 4, product_resources: 21, product_code: 24. Manifest z hashem i liczbą linii w `coverage.json`. Nie obejmuje to pełnej historii git ani wizualnego audytu rastrowych zasobów.

README i ECOSYSTEM.md definiują LEM-Workbench jako Androidowy instrument programu LEM, nie implementację całej teorii LEM.

ECOSYSTEM.md jest jawnie brainstormem. Nie znaleziono runtime dependency na inne repozytoria; nie utożsamiamy postulowanego przepływu z gotową integracją.

Nie ma AGENTS.md, CLAUDE.md ani centralnego STATE/INDEX w tym drzewie. Właściwe lokalne reguły i stan wymieniono w coverage.json. **Wszystkie wymienione tam pobrane gałęzie mają zero commitów nieobecnych w main**; nie utworzono backlogu ze starej kolejki.

## Ustalenia

| ID | Klasyfikacja | Priorytet | Ustalenie |
|---|---|---|---|
| LEM-001 | naruszenie | P1 | ExperimentConfig istnieje, ale nie steruje wykonaniem |
| LEM-002 | naruszenie | P1 | Recepta pomiaru i polityka transportu zaszyte w Kotlinie |
| LEM-003 | naruszenie | P1 | Wyniki tracą surową odpowiedź i częściowo wykonane kroki |
| LEM-004 | naruszenie | P1 | Odkrywanie modeli pomija kolejne strony i stosuje ukryty filtr |
| LEM-005 | naruszenie | P1 | Brak migracji Room wybiera automatyczne usunięcie danych |
| LEM-006 | naruszenie | P2 | UI, historyczne twierdzenia i wygląd są kodem; parametry motywu ignorowane |
| LEM-007 | naruszenie | P2 | Graf nie obejmuje konfiguracji, modeli, metod i wyników |
| LEM-008 | dopuszczalny mechanizm | P3 | Ręczne uruchomienie i jawna porażka są rzeczywistymi mechanizmami |

### LEM-001 — ExperimentConfig istnieje, ale nie steruje wykonaniem

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/data/model/ExperimentConfig.kt:9–41`; `app/src/main/java/com/example/viewmodel/ResearchViewModel.kt:70–95`; `app/src/main/java/com/example/data/repository/ResearchRepository.kt:16–18`; `app/src/main/java/com/example/api/ResearchAgent.kt:39–51`.

**Dowód:** Encja ma 29 pól konfigurujących modele/architekturę poza id/name. ConfigDao i ResearchRepository oferują zapis/odczyt; żaden bieżący ekran, ViewModel, runner ani agent nie czyta allConfigs ani nie wywołuje insertConfig. Runner przyjmuje tylko modelName i opcjonalny wymiar.

**Zachowanie / wpływ:** Kliknięcie embedding uruchamia runEncoderSmokeTest(modelName), czyli domyślny wymiar 768; embeddingTaskType z encji nie trafia do EmbedContentRequest. Żaden z zapisanych parametrów treningu nie uruchamia treningu. Zapisana konfiguracja nie jest dowodem podłączenia funkcji; zmiana jej danych nie zmienia uruchamianego pomiaru.

**Wymaganie:** R15, R41, R42, Zadanie A pkt 5. **Interpretacja audytora:** Potwierdzona luka podłączenia. Nie twierdzimy, że UI udaje działający trening: README jawnie ogranicza produkt do instrument checks. Naruszeniem kryterium audytu jest traktowanie samej encji jako konfiguracji używanej przez aplikację.

**Alternatywy i stan:** Pełny profil pomiaru endpointu — częściowo: model wybrany w UI, reszta w kodzie; Konfiguracje badawcze/treningowe — zachowane jako encja, bez konsumenta; Brak wsparcia jawny per pole — niezrealizowany.

**Dane/graf → konsument:** Węzeł preset/method_version z obsługiwanymi polami, referencją do snapshotu config i statusem capabilities; zgodnie z R15/R41 → ModelsScreen.ModelCard, ResearchViewModel.runEmbeddingInstrument, ExperimentRunner.runEncoderSmokeTest, ResearchAgent.getEmbedding.

**Rekomendacja:** Podłączyć wykonywalne pola do runnera i transportu, a nieobsługiwane zachować z jawnym statusem; doprecyzować twierdzenie README o konfigurowaniu eksperymentów.

**Test akceptacyjny:** Wprowadzić dwa presety różniące się wymiarem i taskType; fake transport w teście przechwytuje różne requesty i result zawiera ID/hash skutecznego presetu. Pole nieobsługiwane jest odrzucane/oznaczane, nigdy cicho ignorowane.

**Ryzyko migracji:** Zachować stare rekordy konfiguracji jako niewykonane; nie przypisywać im retrospektywnie pomiarów.. **Zależności:** Profil metody i capabilities; Migracja encji i wyniku.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-002 — Recepta pomiaru i polityka transportu zaszyte w Kotlinie

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/research/ExperimentRunner.kt:13–20`; `app/src/main/java/com/example/research/ExperimentRunner.kt:71–121`; `app/src/main/java/com/example/api/ResearchAgent.kt:23–36`; `app/src/main/java/com/example/api/GeminiApiService.kt:94–113`.

**Dowód:** Dwa teksty embedding, domyślny wymiar 768, instrukcja generacji i token kontraktu są zapisane w runnerze; ResearchAgent zawsze ustawia temperature=0f, bierze tylko pierwszego kandydata; RetrofitClient ma stały BASE_URL i 60 s timeouty.

**Zachowanie / wpływ:** UI pozwala wybrać model z rejestru, ale nie korpus pomiaru, system prompt, temperaturę, liczbę kandydatów, wymiar, endpoint lub timeout. Wynik nie identyfikuje wersji promptu/korpusu. Zmiana recepty bez zmiany algorytmu wymaga rekompilacji; porównanie pomiarów między wersjami APK nie ma pełnego śladu konfiguracji.

**Wymaganie:** R42, R40, R41, R33. **Interpretacja audytora:** Adres, prompt i parametry przechodzą dokładny test R42 jako dane. Nazwy pól API i format JSON są odrębnymi kontraktami, nie wymagają usuwania.

**Alternatywy i stan:** Obecny smoke test — aktywny, stała recepta; Inny korpus/prompt/wymiar/temperatura — nieedytowalne w danych; Inny endpoint lub timeout tego samego adaptera — nieobsługiwane.

**Dane/graf → konsument:** Wersjonowany instrument preset + provider/transport profile + response_selection policy → ExperimentRunner, ResearchAgent.generateText, ResearchAgent.getEmbedding, RetrofitClient.

**Rekomendacja:** Wyprowadzić recepty, parametry i profil transportu do danych; pozostawić implementację Gemini jako adapter mechanizmu.

**Test akceptacyjny:** Bez zmiany kodu wczytać wariant promptu, wymiaru i timeoutu; przechwycone requesty i run snapshot odzwierciedlają dane; brak konfiguracji daje jawny błąd albo pack wygenerowany z danych.

**Ryzyko migracji:** Zachować obecną receptę jako nazwany preset kompatybilności; hash powinien obejmować teksty oraz wszystkie parametry.. **Zależności:** LEM-001; Źródło danych domyślnych.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-003 — Wyniki tracą surową odpowiedź i częściowo wykonane kroki

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/research/ExperimentRunner.kt:22–58`; `app/src/main/java/com/example/research/ExperimentRunner.kt:77–121`; `app/src/main/java/com/example/research/ExperimentRunner.kt:134–170`; `app/src/main/java/com/example/api/ResearchAgent.kt:28–36`.

**Dowód:** Embeddingi służą do obliczenia cosine, po czym do rekordu trafiają metryki i pusty filesArtifactsProduced. Generacja zapisuje długość/hash response.trim(), a nie odpowiedź. Failure zapisuje fullHyperparameters={} oraz numberOfExamples=1 także po możliwym pierwszym udanym embeddingu.

**Zachowanie / wpływ:** Po zakończeniu runnera nie da się odtworzyć wektorów, pierwszej odpowiedzi kandydata, pozostałych kandydatów ani surowego envelope API. Awaria drugiego kroku nadpisuje opis całej próby pojedynczym failed result. costTokenCounts pozostaje pusty. Ograniczona odtwarzalność i brak dowodu dla ponownej oceny; wynik nie ma dokładnej metody/promptu ani śladu częściowego kosztu.

**Wymaganie:** R15, R41, Zadanie A: wyniki i dowody, README: persisting raw outcomes. **Interpretacja audytora:** Nie wykryto fabrykowania wyników: metryki liczone są z prawdziwych odpowiedzi. Krytyka dotyczy utraty użytych danych i provenance, nie tego, że pomiar jest syntetyczny.

**Alternatywy i stan:** Zachować wyłącznie agregaty/hash — obecne, nieopisane jako polityka retencji; Raw artifacts + agregaty — brak; Ślad kroku i częściowych odpowiedzi — brak.

**Dane/graf → konsument:** Run/step/evidence nodes z raw artifact reference, request/config hashes, retention policy i kosztami oznaczonymi unknown gdy API ich nie dostarcza → ResearchAgent response decoder, ExperimentRunner, ResearchViewModel→ResearchRepository.insertExperiment, ExperimentsScreen.

**Rekomendacja:** Dodać utrwalenie requestu i outputu każdego kroku przed agregacją; utratę lub pominięcie przechowywania uczynić jawną polityką.

**Test akceptacyjny:** Transport testowy zwraca znane wektory/odpowiedź; wynik wskazuje ich zachowany hash/artifact i dokładny request. Drugi embedding rzuca: pierwszy krok i jego output pozostają, drugi jest failed, koszt nie jest zmyślany.

**Ryzyko migracji:** Odzyskanie starych raw danych jest niemożliwe; oznaczyć missing, nie rekonstruować ich z interpretacji.. **Zależności:** LEM-001; Polityka retencji; Repozytorium artefaktów.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-004 — Odkrywanie modeli pomija kolejne strony i stosuje ukryty filtr

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/api/ResearchAgent.kt:56–58`; `app/src/main/java/com/example/api/GeminiApiService.kt:67–77`; `app/src/main/java/com/example/ui/screens/ModelsScreen.kt:26–31`; `app/src/main/java/com/example/ui/screens/ModelsScreen.kt:106–107`; `app/src/main/java/com/example/viewmodel/ResearchViewModel.kt:58–60`.

**Dowód:** ListModelsResponse zawiera nextPageToken, ale listModels() zwraca wyłącznie pierwszą listę models. Interfejs API nie ma argumentu pageToken. UI dzieli dostępne modele przez substring embed/generate, a ViewModel sortuje nazwą.

**Zachowanie / wpływ:** Przy odpowiedzi z nextPageToken aplikacja nie prosi o dalsze strony; modele o innych capability nie są pokazywane w żadnej sekcji. Rejestr i wybór modeli nie obejmuje wszystkich modeli konta nawet gdy API dostarcza jawny sygnał kontynuacji. Nie dowiedziono, że bieżące konto faktycznie wymaga więcej niż strony.

**Wymaganie:** Zadanie A: filtrowanie i sortowanie, R15, R41. **Interpretacja audytora:** Kod potwierdza warunkową utratę paginacji. Skala na rzeczywistym API pozostaje niezmierzona, bo nie wykonano wywołań. Filtr capabilities jest uzasadniony zakresem instrumentów, ale powinien ujawniać modele niewspierane i regułę doboru.

**Alternatywy i stan:** Pełne pobranie stron — brak; Jawny limit i incomplete status — brak; Wszystkie modele z capability status — brak; Aktualne dwa rodzaje pomiarów — obecne.

**Dane/graf → konsument:** Model registry snapshot z pagination completeness oraz filtrem widoku/method-capability binding → GeminiApiService.listModels, ResearchAgent.listModels, ResearchViewModel.refreshModels, ModelsScreen.

**Rekomendacja:** Obsłużyć nextPageToken, oddzielić kompletność rejestru od filtra i sortowania widoku.

**Test akceptacyjny:** Dwustronicowy fake registry: pobrane obie strony, zachowany snapshot; pusty/unknown capability nie znika bez informacji. Porządek/filtrowanie w danych nie wpływa na samą kompletność rejestru.

**Ryzyko migracji:** Unikać duplikatów nazw pomiędzy stronami; nie wybierać automatycznie nowego modelu po rozszerzeniu rejestru.. **Zależności:** Adapter paginacji; Profil filtrowania UI.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-005 — Brak migracji Room wybiera automatyczne usunięcie danych

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/data/local/AppDatabase.kt:11–29`.

**Dowód:** Baza ma version=2, exportSchema=false; builder wywołuje fallbackToDestructiveMigration(), bez addMigrations. Nie ma innej implementacji migracji w śledzonym drzewie.

**Zachowanie / wpływ:** Gdy Room wymaga migracji i nie ma do niej ścieżki, skonfigurowany fallback pozwala odtworzyć bazę destrukcyjnie zamiast zatrzymać upgrade. Ryzyko utraty konfiguracji, wyników i ledger przy zmianie schematu; użytkownik nie wybiera polityki migracji.

**Wymaganie:** Zadanie A: zachowanie przy brakach i niejawne decyzje, R15, ENGINEERING_RULES.md: audytowalny wynik. **Interpretacja audytora:** Potwierdzono konfigurację destrukcyjnego fallbacku, nie wykonano realnej migracji Android ani nie twierdzimy, że dane konkretnego użytkownika już zniknęły.

**Alternatywy i stan:** Destrukcyjne odtworzenie — ustawione bez pytania; Jawna migracja z zachowaniem danych — brak; Zatrzymanie i eksport/backup — brak.

**Dane/graf → konsument:** Migration/retention policy z planem wersji, backupem i potwierdzonym wynikiem; sekrety poza payloadem grafu, referencje do chronionego store → AppDatabase.getDatabase, Room DAO dla configs/results/ledger.

**Rekomendacja:** Zastąpić niejawną destrukcję jawną migracją lub bezpiecznym błędem, a reset przechowywać jako oddzielną świadomą operację.

**Test akceptacyjny:** Przygotować bazę wersji 1 z rekordami i przejść do 2: dane zachowane; dla nieznanej wersji otwarcie odmawia zamiast kasować, a jawny reset testowany osobno.

**Ryzyko migracji:** Wymaga prawdziwych fixture starych schematów; brak exportSchema utrudnia pełne odtworzenie poprzednich wersji.. **Zależności:** Schematy historyczne Room; LEM-003.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-006 — UI, historyczne twierdzenia i wygląd są kodem; parametry motywu ignorowane

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/ui/theme/Theme.kt:30–36`; `app/src/main/java/com/example/ui/screens/DashboardScreen.kt:74–94`; `app/src/main/java/com/example/ui/screens/ExperimentsScreen.kt:65–79`; `app/src/main/java/com/example/ui/navigation/Screen.kt:3–7`.

**Dowód:** MyApplicationTheme przyjmuje darkTheme oraz dynamicColor, ale zawsze wybiera DarkColorScheme. Teksty ekranów i nawigacji są literałami Kotlin. Dashboard zawiera ACCEPTED HISTORICAL CHECKPOINT i dwa twierdzenia badawcze bez odsyłacza do rekordu ledger.

**Zachowanie / wpływ:** Zmiana parametrów motywu nie wpływa na renderowanie. Zmiana języka, nawigacji albo checkpointu badawczego wymaga zmiany kodu; raw machine status INSTRUMENT_* jest pokazywany bez tłumaczenia. Interfejs nie jest profilem danych. Twierdzenie historyczne jest oznaczone jako historyczne, ale nie ma wykonującej się ścieżki do dowodu ani wersji akceptacji.

**Wymaganie:** R42, R15, R40, Zadanie A: Basic/Advanced te same dane. **Interpretacja audytora:** Zmienna darkTheme sama w sobie nie stanowi działającej alternatywy. Nie kwestionujemy treści hipotez geometrycznych — audyt dotyczy ich przechowania i provenance.

**Alternatywy i stan:** Obecny ciemny motyw — aktywny; Jasny/systemowy/dynamiczny — parametry istnieją, ignorowane; Lokalizacja i profile UI — brak; Checkpoint z ledger/evidence — brak.

**Dane/graf → konsument:** UI profile + locale text catalog + referenced research checkpoint/claim with provenance → MyApplicationTheme, LemLabApp, Screen enum, DashboardScreen, ExperimentsScreen.

**Rekomendacja:** Przenieść teksty, paletę i checkpoint do danych; podłączyć parametry do renderera albo usunąć pozorną obsługę ze stabilnego API.

**Test akceptacyjny:** Zmiana danych profilu rzeczywiście zmienia motyw i etykiety; status maszynowy renderowany poprzez katalog; klik checkpointu prowadzi do źródła i daty, nie tekstu w kodzie.

**Ryzyko migracji:** Zachować obecny wygląd jako domyślny preset; nie nadać istniejącym twierdzeniom fikcyjnego źródła akceptacji.. **Zależności:** Katalog i profile UI; Graf/ledger claim refs.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-007 — Graf nie obejmuje konfiguracji, modeli, metod i wyników

Klasyfikacja: **naruszenie**. Lokalizacja: `app/src/main/java/com/example/data/local/AppDatabase.kt:11–15`; `app/src/main/java/com/example/data/model/ExperimentResult.kt:9–36`; `app/src/main/java/com/example/data/model/LedgerEntry.kt:9–18`; `app/src/main/java/com/example/api/ApiKeyStore.kt:16–38`; `app/src/main/java/com/example/viewmodel/ResearchViewModel.kt:22–35`.

**Dowód:** Jedynymi encjami Room są ExperimentResult, LedgerEntry i ExperimentConfig; profile/API key są w SharedPreferences, modele i stan wykonania w StateFlow; nie znaleziono grafowego adaptera, node/edge ID, produced_by lub resolution warstw.

**Zachowanie / wpływ:** Wynik przechowuje nazwę modelu i JSON metryk, bez połączenia do dokładnej wersji config/metody. Brak wspólnego resolvera warstw, wykluczeń i ścieżki Basic/Advanced do tych samych danych. Pełność pokrycia R15/R40/R41 nie jest spełniona; sam ledger eksperymentów nie zapewnia grafowego opisu wszystkich używanych danych.

**Wymaganie:** R15, R40, R41, Zadanie A pkt 4. **Interpretacja audytora:** Jest to luka względem bieżącego zadania A, nie dowód złamania deklaracji README: repo jawnie jest instrumentem, nie pełnym LEM. Nie wymagamy zapisania wartości sekretu w grafie; wystarcza kontrolowana referencja i polityka.

**Alternatywy i stan:** Room jako instrument store — aktywne; Projekcja/adaptacja Room → graf — brak; Natywne obiekty grafu z persystencją Room — brak.

**Dane/graf → konsument:** Config/profile/method/model/run/evidence/provider/trigger/queue graph descriptions, z referencjami do zewnętrznych artifact/secret stores → ResearchRepository, ResearchViewModel, ExperimentRunner, Compose screens.

**Rekomendacja:** Opisać istniejące obiekty i powiązania w grafie przy zachowaniu Room; dopiero po kontrakcie podłączyć resolver danych i UI.

**Test akceptacyjny:** Od wyniku przejść do config, modelu, promptu i raw dowodu; zmiana warstwy presetu zmienia request i ma wyjaśnienie; permanent exclusion nie wraca po aktualizacji packa.

**Ryzyko migracji:** Nie narzucać ontologii ChatADHD na semantykę badawczą; mapować jawnie typy i straty, zachować nieznane pola.. **Zależności:** LEM-001; LEM-003; Kontrakt interoperacyjności ustalony przez aktywny silnik.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

### LEM-008 — Ręczne uruchomienie i jawna porażka są rzeczywistymi mechanizmami

Klasyfikacja: **dopuszczalny mechanizm**. Lokalizacja: `app/src/main/java/com/example/research/ExperimentRunner.kt:60–68`; `app/src/main/java/com/example/ui/screens/ModelsScreen.kt:178–194`; `app/src/main/java/com/example/api/ResearchAgent.kt:10–12`; `app/src/main/java/com/example/viewmodel/ResearchViewModel.kt:70–99`.

**Dowód:** Pomiar startuje z onClick w ModelsScreen; błędy requestu są zamieniane na INSTRUMENT_FAILED i utrwalane przez ViewModel. Brak klucza rzuca jawny wyjątek. Brak algorytmu samoczynnych eksperymentów.

**Zachowanie / wpływ:** Wejście na ekran modeli odświeża wyłącznie registry; embedding/generation wymaga kliknięcia. Błąd nie jest zastępowany wiarygodnym wektorem lub wynikiem. Nie znaleziono automatycznego eksperymentu opt-out. Brak budżetu/trigger/volume jako danych ogranicza przyszłe rozszerzenia, lecz nie dowodzi dzisiejszego automatycznego wydatkowania.

**Wymaganie:** R33, R42 wyjątki 1/2/3/6, ENGINEERING_RULES.md punkty 1/6. **Interpretacja audytora:** Wysłanie rejestru po zapisie klucza jest jawnie komunikowane, ale to nie ten sam rodzaj akcji co eksperyment. String schema/JSON/API field i machine status to kontrakt/mechanizm; prezentacja statusu użytkownikowi wymaga katalogu (LEM-006).

**Alternatywy i stan:** Pomiar ręczny — aktywny; Automatyka z opt-in, budget, scope i częstotliwością — niezaimplementowana, nie odrzucona; Fałszywy wynik zastępczy — nieobecny.

**Dane/graf → konsument:** Obecny manual trigger jako jawny preset; przy automatyce osobny opt-in trigger/budget policy zgodny z R33 → ModelsScreen.ModelCard onClick, ResearchViewModel.runEmbeddingInstrument/runGenerationInstrument, ExperimentRunner.failedInstrumentResult.

**Rekomendacja:** Zachować ręczne starty i jawne błędy; nie otwierać nowego backlogu na rzekome niejawne automatyczne eksperymenty.

**Test akceptacyjny:** Test bez klucza i z błędem transportu: brak wygenerowanych metryk sukcesu, zapis failed; samo uruchomienie aplikacji/ekranu nie uruchamia embedding ani generation.

**Ryzyko migracji:** Nie zmieniać ręcznych pomiarów w automat bez opt-in.. **Zależności:** LEM-003 dla dowodów częściowych.

**Weryfikacja:** Analiza statyczna wszystkich plików Kotlin i pełnego przepływu UI → ViewModel → runner → agent → DAO. Brak uruchomienia Android/Gradle.

## Granice i wznowienie

- Brak gradle, kotlinc i wrapper executable w repo/środowisku; testów Android/Room/Compose nie uruchomiono.
- Nie wykonywano requestów Gemini ani płatnych wywołań. Realne zachowanie dostawcy/paginacja na koncie niezmierzone.
- Przebadano 24 pliki Kotlin (dokładny mianownik w coverage.json), manifest i konfigurację zasobów; rastrowe ikony nie były renderowane.
- Domyślna konfiguracja backup obejmuje Android allowBackup=true bez aktywnych exclusions; nie oceniano bezpieczeństwa backupu ani zachowania urządzenia, więc nie tworzymy potwierdzonego findingu o ujawnieniu klucza.

Najpierw LEM-001/002/003: fixture transportu, rzeczywisty preset instrumentu, pełny ślad request/output. Potem polityka migracji i projekcja do grafu. Nie wznawiać dawnych gałęzi: są już w main.

Wymagania właściciela z zadania A stosujemy jako aktualne kryteria. Cytat właściciela jest null w rekordach: treści kodu/README nie są cytatami jego intencji. Interpretacje i rekomendacje są oddzielone od obserwacji. Nie edytowano produktu, schematów ani STATE/INDEX. Przegląd B/C jest zadaniem końcowej integracji audytu w chatadhd, nie twierdzeniem o ukończeniu w tej części.

Skan automatyczny: 27 plików, 1636 linii, 832 kandydatów; patrz `scan/summary.json`. To sygnały do weryfikacji, nie liczba naruszeń. ExperimentRunner jest produktem, mimo heurystycznej etykiety research skanera.
